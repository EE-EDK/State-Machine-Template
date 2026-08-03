# CLAUDE.md — State Machine Framework

## Project Summary
Production-grade, modular state machine framework for embedded C systems. Handle-based, multi-instance, state-agnostic, zero-heap, ISR-safe. Platform-agnostic with weak-symbol HAL abstraction. Version 4.0.0 — the v4 semantic-correction release (strict-FIFO delivery, atomic transitions, bounded event drain, ms deadline-based timers) on top of the completed v3 rewrite.

## Directory Structure
```
state-machine-template/
├── include/sm_framework/   # Public API headers (v3.0)
│   ├── sm_framework.h      # Umbrella header (version 3.0.0)
│   ├── sm_config.h         # Config defaults (SM_STATE_COUNT/SM_EVENT_COUNT required)
│   ├── sm_types.h          # All types: SM_Context, SM_Handle_t, events, transitions, time events
│   ├── sm_safety.h         # Safety macros: SM_DEFINE_MODULE, SM_REQUIRE, DIS, bounded loops
│   ├── sm_platform.h       # HAL interface: timing, critsec, watchdog, sleep, NVS, reset, capabilities
│   ├── sm_engine.h         # Core API: SM_Init, SM_Process, SM_PostEvent, time/deferred events
│   ├── sm_error.h          # Error API: 3-tier report/recover/history/stats, DIS on critical_lock
│   └── sm_debug.h          # Debug API: SM_LOG_*, per-module tags, runtime level control
├── src/
│   ├── core/
│   │   ├── sm_engine.c     # Full RTC dispatch engine with DIS on critical_lock
│   │   ├── sm_error.c      # Error handler: DIS, SM_REQUIRE, stats (Phase 3 complete)
│   │   └── sm_debug.c      # Debug system with tag filtering, periodic interval
│   ├── platform/
│   │   └── sm_platform_weak.c  # Weak HAL defaults (nested critsec, SimTick, capabilities)
│   └── app/
│       └── app_main.c      # Minimal app glue
├── examples/
│   ├── basic_example.c           # 3-state FSM (INIT→RUNNING→STOPPED)
│   ├── simulation_example.c      # Real timing + error reporting
│   ├── blinky_example.c          # Timer events (periodic blink)
│   ├── sensor_pipeline_example.c # Guard conditions (data quality gate)
│   ├── error_recovery_example.c  # 3-tier error handling demo
│   ├── multi_fsm_example.c       # Two independent state machine instances
│   └── platform/
│       └── stm32_platform_stub.c # Reference STM32 HAL implementation
├── config/
│   └── sm_config_template.h
├── docs_dev/               # Development planning docs
│   ├── task_plan.md        # Master 9-phase rewrite plan with decisions D1-D11
│   ├── findings.md         # Bug inventory, architecture gaps, QP/C review
│   └── progress.md         # Session log
├── tests/
│   ├── CMakeLists.txt          # Test build system (Unity FetchContent, sm_framework_test lib)
│   ├── test_common.h           # Shared test enums, assert-capture macros
│   ├── test_platform.c         # Test platform (longjmp assert, resettable sim time)
│   ├── test_event_queue.c      # 10 tests: frontEvt, ring, watermark, delivery order
│   ├── test_engine.c           # 21 tests: init, process, guards, timeout, dwell, history
│   ├── test_time_events.c      # 11 tests: arm, disarm, one-shot, periodic, multi-timer
│   ├── test_deferred.c         # 9 tests: defer, recall LIFO-to-front, flush, capacity
│   ├── test_error.c            # 18 tests: 3-tier errors, DIS, stats, recovery
│   ├── test_debug.c            # 14 tests: levels, tags, periodic, hexdump
│   ├── test_safety.c           # 11 tests: DIS corruption, bounded loops, SM_REQUIRE
│   ├── test_hal.c              # 18 tests: critsec nesting, timeout wrap, capabilities
│   └── test_integration.c      # 6 tests: full lifecycle, cross-subsystem scenarios
├── CMakeLists.txt          # Build system (cmake 3.15+, C99)
├── Quick-Guide.md          # v3.0 quick reference
├── MIGRATION.md            # v2→v3 migration guide
└── README.md               # v3.0 project documentation
```

## Build Commands
```bash
# Standard build
rm -rf build && mkdir build && cd build && cmake .. -DBUILD_EXAMPLES=ON && cmake --build .

# With tests
cmake .. -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build . && ctest

# Run examples
./examples/basic_example
./examples/simulation_example
./examples/blinky_example
./examples/sensor_pipeline_example
./examples/error_recovery_example
./examples/multi_fsm_example

# Use as library in another project
add_subdirectory(path/to/state-machine-template)
target_link_libraries(your_target sm_framework)
```

## Key Architecture (v4.0)
- **State-agnostic:** User defines all states/events via enums + SM_STATE_COUNT/SM_EVENT_COUNT (1..65534; SM_EVT_TIMEOUT occupies SM_EVENT_COUNT)
- **Handle-based:** SM_Handle_t = SM_Context_t*, no extern globals, multi-instance
- **ISR-safe event queue:** Strict FIFO in post order for ALL sources (user/ISR/timeout/timer); frontEvt slot is a fast path used only when the queue is completely empty (QP/C D6 revised); SM_EventQueueIsFull mirrors SM_PostEvent exactly
- **Const flash transitions:** SM_Transition_t[] in ROM with guard conditions + actions
- **Atomic transitions:** exit → action → state update → entry within one SM_Process call; deferred entry only for the initial state after SM_Init/SM_Reset
- **Bounded drain:** SM_Process handles up to SM_MAX_EVENTS_PER_PROCESS events per call (default = SM_EVENT_QUEUE_SIZE); min_dwell re-checked per event against the then-current state
- **3-tier errors:** MINOR (auto-recover), NORMAL (managed recovery), CRITICAL (system lock with DIS)
- **Time events:** ms deadline-based against SM_Platform_GetTimeMs (wrap-safe, < 2^31 ms), drift-free periodic with coalescing on stall, capacity enforced at Arm (returns bool), ticked BEFORE the drain so fires deliver same-cycle (SM_FEATURE_TIME_EVENTS)
- **State timeout:** public SM_EVT_TIMEOUT event; latch set only on successful post (full queue retries next cycle); valid in SM_AddTransition
- **Deferred events:** FIFO recall (oldest first) to the TRUE front of the main queue (displaces occupied front QP-postLIFO-style); on full main queue the event stays deferred (SM_FEATURE_DEFER)
- **Safety:** DIS verification on state + critical_lock, hard-bounded loops, numeric assertion IDs (SM_DEFINE_MODULE + SM_REQUIRE)
- **Debug:** Per-module tags (16 max), runtime level enable/disable, compile-time stripping
- **HAL:** Weak-symbol overrides for timing, critsec, watchdog, sleep, NVS, reset reason, capabilities
- **HSM:** Optional hierarchical states (SM_FEATURE_HSM) with parent fallback
- **Memory:** ~544 bytes RAM baseline, ~580 with defer queue

## Architecture Decisions (D1-D11)
See `docs_dev/task_plan.md` for full rationale. Key: D6 frontEvt, D7 DIS, D8 bounded loops, D9 time events, D10 deferred events — all inspired by QP/C 8.1.4 review.

## Conventions
- C99 standard (SM_STATIC_ASSERT with negative-array fallback for C99)
- No heap allocations in framework core
- All config via `#define` in app_config.h (copy from sm_config_template.h)
- SM_STATE_COUNT and SM_EVENT_COUNT are mandatory `#error` if undefined
- `volatile` on ISR-shared data (current_state, event queue head/tail/count, critical_lock)
- `extern "C"` guards for C++ compatibility
- SM_WEAK disabled on PE/COFF (Windows/MinGW) — override via build system exclusion
- Numeric assertion IDs: 100-199 init, 200-299 process, 300-399 time events, 400-499 deferred events, 500-599 event posting, 600-699 reset/lifecycle, 700-799 error handler

## What NOT to Do
- Do not block in state callbacks (no delay/infinite loops)
- Do not modify SM_Context fields directly (use SM_* API exclusively)
- Do not call SM_Process from ISR context (documented as non-ISR-safe); do not call SM_Process recursively from callbacks (corrupts the same instance)
- Do not call SM_DeferEvent/SM_RecallEvent from ISR (state callback context only)
- Treat SM_EventQueueIsFull/Depth/IsEmpty as diagnostic-only (TOCTOU with concurrent SM_PostEvent from ISR); use SM_PostEvent’s return value for decisions
- Do not leave all debug messages enabled in production (use SM_DEBUG_LEVEL and SM_Debug_EnableLevel)

## TODO
All phases complete. Maintenance items only:
- [ ] Fix cppcheck installation (std.cfg path hardcoded to non-existent R: drive)
- [ ] GitHub Actions CI (deferred from Phase 6)
- [ ] gcov/lcov coverage reporting (deferred from Phase 6)
- [ ] **Replace graphify's regex extraction with a clang-based (libclang AST) analyzer.** The current repo-local package is regex-based — honest for navigation/god nodes but not a real parser. Goal is the absolute best state machine: the graph must be precise enough to feed the Phase B/C host tooling (transition coverage maps, trace symbol tables, generated-table cross-checks). Keep the same report format (GRAPH_REPORT.md + wiki/index.md) and the same `graphify.watch._rebuild_code` contract so nothing downstream changes.

## Completed Phases
- [x] Phase 0: Cleanup — legacy files removed (commit ed92613)
- [x] Phase 1: Architecture Redesign — 8 headers, handle-based API (commit 7823e69)
- [x] Phase 2: Core Rewrite — full RTC engine, frontEvt, DIS, time/deferred events, guards, HSM (commit 4238969)
- [x] Phase 3: Error Handler Rewrite — DIS on critical_lock, SM_REQUIRE assertions (700-799), SM_Error_GetStats, SM_DEFINE_MODULE("sm_error"), error stats tracking
- [x] Phase 4: HAL Expansion — nested critsec, SimTick, platform detection, capabilities (commit 6b7fac6)
- [x] Phase 5: Debug Rewrite — runtime levels, 16 module tags, periodic interval, ASCII hexdump (commit 4eb4e03)
- [x] Phase 6: Test Infrastructure — Unity v2.6.0 via FetchContent, 9 test suites (118 tests), ctest integration, test platform with longjmp assert capture
- [x] Phase 7: Examples & Documentation — 4 new examples (blinky, sensor pipeline, error recovery, multi-FSM), STM32 platform stub, README/Quick-Guide/MIGRATION.md rewritten for v3.0
- [x] Phase 8: Validation & Release — ARM Cortex-M4 size audit (4.3KB flash, 84B BSS), zero-heap verification (nm), _Static_assert validation, GCC -Wall/-Wextra/-Wpedantic/-Wshadow/-Wconversion clean; cppcheck deferred (broken install), clang-tidy not available

## Session Continuity

**Last session:** v4.0.0 semantic release + graphify package + findings verification (this session's commits)

**2026-08-03 (later) — graphify + findings closure:** Repo-local `graphify/` package added (stdlib-only; satisfies the rebuild contract above — the previous workspace-level tool wasn't available in remote containers). Full verification pass over docs_dev/findings.md Bug Inventory: all B1–B13 confirmed closed with a status table added to the file; B12 had a real v3 residue (dequeue outside critical section) that v4 closed; string-table bounds in sm_debug.c now derive from the array (`SM_LEVEL_TAG_COUNT`) per the "Improve" list. Pushed to main per explicit authorization.

**2026-08-03 — v4.0.0 semantic correction release:** Engine execution semantics fixed after deep review. (1) Time events rewritten from SM_Process-call counting to ms deadlines (wrap-safe, drift-free periodic with stall coalescing; Arm returns bool + enforces SM_FEATURE_MAX_TIME_EVENTS; expired one-shots unlink; two-phase tick keeps critsec short). (2) SM_Process drains up to SM_MAX_EVENTS_PER_PROCESS events per call. (3) Transitions atomic (exit→action→entry same call). (4) Strict FIFO for internal posts (no timeout queue-jumping). (5) SM_EVT_TIMEOUT public, accepted by SM_AddTransition, timeout latch only on successful post. (6) SM_EventQueueIsFull matches SM_PostEvent. (7) SM_RecallEvent true front-insert, event preserved on full queue; recall is FIFO (docs previously claimed LIFO — code always was FIFO). Tests: 19 suites green, timer suite rewritten (15 cases incl. call-count-independence, coalescing, capacity). Blinky now advances SimTick (it silently relied on tick-counted timers). MIGRATION.md has the v3→v4 section.

**2026-04-27 — Deep review + documentation pass (commits `264a623+`):** Bounded-loop macro end condition (`var <= bound`); `SM_Process` returns if state descriptor missing, skips transition if `to_state` out of range (warn); headers document internal event priority vs user FIFO, queue query TOCTOU, `SM_AddTransition` context, global debug state, boot `timestamp==0` for errors; `docs_dev/findings.md` banner clarifies legacy audit vs v3; README/Quick-Guide integration notes added.

## Conversation History Archive

Past AI conversations (217 total) are archived at the workspace root: `.claude/conversation-history/`. Search `index.json` by keyword or browse `index.md` for topic-grouped context on prior decisions, approaches, and project history.


## Auto-Commit & Push

After completing each task, automatically commit all relevant changes with a descriptive message and push to `origin main`. Report what was committed. This is standing authorization — no confirmation needed.

## graphify

This project has a graphify knowledge graph at graphify-out/, generated by
the repo-local `graphify/` Python package (stdlib-only, committed with the
repo — works in any clone, no install needed).

Rules:
- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- For questions about a specific state machine (states, transitions, timeouts), read graphify-out/MACHINES.md — per-machine Mermaid diagrams, timing tables, and validator findings (V1 unreachable states, V2 timeout without SM_EVT_TIMEOUT route, V3 dwell>timeout, V4 terminal states, V5 fully-guarded events). Application machines and test fixtures are listed separately; only application WARNs matter
- If graphify-out/wiki/index.md exists, navigate it instead of reading raw files
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` (or `python3 -m graphify.watch`) from the repo root to keep the graph current
- graphify-out/ stays gitignored (ephemeral, regenerable output)
- Planned: swap the regex extraction layer for a clang/libclang AST analyzer (see TODO) — same output format and rebuild contract, higher precision
