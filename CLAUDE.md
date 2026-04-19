# CLAUDE.md — State Machine Framework

## Project Summary
Production-grade, modular state machine framework for embedded C systems. Handle-based, multi-instance, state-agnostic, zero-heap, ISR-safe. Platform-agnostic with weak-symbol HAL abstraction. Version 3.0.0, v3 rewrite in progress (Phases 0-2, 4-5 complete; Phase 3, 6-8 remaining).

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
│   ├── basic_example.c     # 3-state FSM (INIT→RUNNING→STOPPED)
│   └── simulation_example.c
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
├── Quick-Guide.md
└── README.md
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

# Use as library in another project
add_subdirectory(path/to/state-machine-template)
target_link_libraries(your_target sm_framework)
```

## Key Architecture (v3.0)
- **State-agnostic:** User defines all states/events via enums + SM_STATE_COUNT/SM_EVENT_COUNT
- **Handle-based:** SM_Handle_t = SM_Context_t*, no extern globals, multi-instance
- **ISR-safe event queue:** Ring buffer with frontEvt optimization (QP/C pattern), configurable depth
- **Const flash transitions:** SM_Transition_t[] in ROM with guard conditions + actions
- **3-tier errors:** MINOR (auto-recover), NORMAL (managed recovery), CRITICAL (system lock with DIS)
- **Time events:** Intrusive linked-list, arm/disarm, one-shot/periodic (SM_FEATURE_TIME_EVENTS)
- **Deferred events:** Defer/recall with LIFO recall to front (SM_FEATURE_DEFER)
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
- Do not call SM_Process from ISR context (documented as non-ISR-safe)
- Do not call SM_DeferEvent/SM_RecallEvent from ISR (state callback context only)
- Do not leave all debug messages enabled in production (use SM_DEBUG_LEVEL and SM_Debug_EnableLevel)

## TODO
- [x] **Phase 3: Error Handler Rewrite** — DIS on critical_lock, SM_REQUIRE assertions (700-799), SM_Error_GetStats, SM_DEFINE_MODULE("sm_error"), error stats tracking
- [x] **Phase 6: Test Infrastructure** — Unity v2.6.0 via FetchContent, 9 test suites (102 tests), ctest integration, test platform with longjmp assert capture
- [ ] **Phase 7: Examples & Documentation** — blinky w/ timer events, sensor pipeline w/ guards, error recovery demo, multi-FSM, STM32 platform stub, README rewrite, Quick-Guide rewrite, MIGRATION.md (v2→v3)
- [ ] **Phase 8: Validation & Release** — cppcheck, clang-tidy, arm-none-eabi-size audit (RAM < 2KB, Flash < 10KB), zero heap verification (nm), MISRA C:2012 checklist, _Static_assert validation, tag v3.0.0
- [x] Phase 0: Cleanup — legacy files removed (commit ed92613)
- [x] Phase 1: Architecture Redesign — 8 headers, handle-based API (commit 7823e69)
- [x] Phase 2: Core Rewrite — full RTC engine, frontEvt, DIS, time/deferred events, guards, HSM (commit 4238969)
- [x] Phase 4: HAL Expansion — nested critsec, SimTick, platform detection, capabilities (commit 6b7fac6)
- [x] Phase 5: Debug Rewrite — runtime levels, 16 module tags, periodic interval, ASCII hexdump (commit 4eb4e03)

## Session Continuity

**Last session:** `e3e00e41` (full: see `.claude/projects/` for transcript JSONL)

## Conversation History Archive

Past AI conversations (217 total) are archived at the workspace root: `.claude/conversation-history/`. Search `index.json` by keyword or browse `index.md` for topic-grouped context on prior decisions, approaches, and project history.


## Auto-Commit & Push

After completing each task, automatically commit all relevant changes with a descriptive message and push to `origin main`. Report what was committed. This is standing authorization — no confirmation needed.

## graphify

This project has a graphify knowledge graph at graphify-out/.

Rules:
- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- If graphify-out/wiki/index.md exists, navigate it instead of reading raw files
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` to keep the graph current
