# CLAUDE.md — State Machine Framework

## Project Summary
Production-grade, modular state machine framework for embedded C systems. Handle-based, multi-instance, state-agnostic, zero-heap, ISR-safe. Platform-agnostic with weak-symbol HAL abstraction. Version 4.1.0 — the build-consistency release (fixed reserved `SM_EVT_TIMEOUT` id, build-wide FSM dimensions verified at `SM_Init`, atomic DIS pairs, timer-schedule lifecycle) on top of v4.0's semantic-correction release (strict-FIFO delivery, atomic transitions, bounded event drain, ms deadline-based timers).

## Directory Structure
```
state-machine-template/
├── include/sm_framework/   # Public API headers (v4.1)
│   ├── sm_framework.h      # Umbrella header (version 4.1.0)
│   ├── sm_config.h         # Config defaults (SM_STATE_COUNT/SM_EVENT_COUNT required)
│   ├── sm_types.h          # All types: SM_Context, SM_Handle_t, events, transitions, time events
│   ├── sm_safety.h         # Safety macros: SM_DEFINE_MODULE, SM_REQUIRE, DIS, bounded loops
│   ├── sm_platform.h       # HAL interface: timing, critsec, watchdog, sleep, NVS, reset, capabilities
│   ├── sm_engine.h         # Core API: SM_Init, SM_Process, SM_PostEvent, time/deferred events
│   ├── sm_error.h          # Error API: report/recover/history/stats, MINOR accessors (D18), DIS on critical_lock
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
│   ├── phase_b_model_plan.md # Phase B (smgen) plan, decisions D12-D17
│   ├── findings.md         # Bug inventory, architecture gaps, QP/C review
│   └── progress.md         # Session log
├── smgen/                  # Model compiler (B1: schema+validator+CLI, stdlib-only)
├── models/                 # TOML machine models (blinky, basic, sensor_pipeline)
├── graphify/               # v2 typed knowledge-graph generator (stdlib-only)
│   ├── cparse.py           # Length-preserving C text utils + preprocessor gate map
│   ├── analyze.py          # Functions/decls/macros(variants)/types(fields)/config/assertions; scoped call resolution
│   ├── machines.py         # SM_Config_t/SM_Transition_t/SM_StateDesc_t -> state graphs + V1-V5
│   ├── link.py             # machine<->code bindings, models<->examples round-trip, docs<->API, test inventory
│   ├── pytools.py          # smgen/graphify Python module graph (ast)
│   ├── metrics.py          # betweenness, PageRank, SCC, articulation, label propagation, layers, bow-tie
│   ├── render.py           # GRAPH_REPORT.md (G1-G14 validators), wiki, graph.json, graph.html
│   ├── watch.py            # Orchestration -- the public contract
│   └── tests/              # 27 unittest cases (synthetic fixtures + real-repo invariants), in ctest
├── tests/
│   ├── CMakeLists.txt          # Test build system (Unity FetchContent, sm_framework_test lib)
│   ├── test_common.h           # Shared test enums, assert-capture macros
│   ├── test_platform.c         # Test platform (longjmp assert, resettable sim time)
│   ├── test_event_queue.c      # 10 tests: frontEvt, ring, watermark, delivery order
│   ├── test_engine.c           # 21 tests: init, process, guards, timeout, dwell, history
│   ├── test_time_events.c      # 15 tests: arm, disarm, one-shot, periodic, multi-timer
│   ├── test_deferred.c         # 10 tests: defer, recall FIFO-to-front, flush, capacity
│   ├── test_error.c            # 26 tests: error tiers, MINOR accessors, DIS, stats, recovery
│   ├── test_debug.c            # 14 tests: levels, tags, periodic, hexdump
│   ├── test_safety.c           # 11 tests: DIS corruption, bounded loops, SM_REQUIRE
│   ├── test_hal.c              # 18 tests: critsec nesting, timeout wrap, capabilities
│   ├── test_integration.c      # 6 tests: full lifecycle, cross-subsystem scenarios
│   ├── test_lifecycle_hardening.c # 11 tests: v4.1 timer/reset, re-init, defer range, DIS pairs
│   ├── test_rt_transition.c    # 12 tests: SM_AddTransition bounds, table capacity
│   ├── test_post_event_guards.c # 12 tests: post accept range, reserved timeout id
│   ├── test_stats_bounds.c     # 11 tests: statistics contract and bounds
│   └── test_*_null*.c, test_recovery_edges.c, test_reset_extras.c
│                               # 10 tests each: NULL/edge contracts per subsystem
│   ├── test_abi_guard.c        # 13 tests: SM_Init 105/106/107 + ABI mismatch reproduction
│   ├── test_isr_interleave.c   # 12 tests: injected ISR at critical-section seams (W2a)
│                               # (272 RUN_TEST cases across 22 suites; 26 ctest targets)
├── CMakeLists.txt          # Build system (cmake 3.15+, C99)
├── Quick-Guide.md          # v4.1 quick reference
├── MIGRATION.md            # v2→v3 migration guide
└── README.md               # v4.1 project documentation
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

## Key Architecture (v4.1)
- **State-agnostic:** User defines all states/events via enums; SM_STATE_COUNT (1..255) and SM_EVENT_COUNT (1..65535) are set once per BUILD (CMake cache vars, PUBLIC on sm_framework) because they are compiled into the framework itself; `SM_Init` rejects an application compiled with different values (assertion 105/106). SM_EVT_TIMEOUT is the fixed reserved id 0xFFFF, outside the user range
- **Handle-based:** SM_Handle_t = SM_Context_t*, no extern globals, multi-instance
- **ISR-safe event queue:** Strict FIFO in post order for ALL sources (user/ISR/timeout/timer); frontEvt slot is a fast path used only when the queue is completely empty (QP/C D6 revised); SM_EventQueueIsFull mirrors SM_PostEvent exactly
- **Const flash transitions:** SM_Transition_t[] in ROM with guard conditions + actions
- **Atomic transitions:** exit → action → state update → entry within one SM_Process call; deferred entry only for the initial state after SM_Init/SM_Reset
- **Bounded drain:** SM_Process handles up to SM_MAX_EVENTS_PER_PROCESS events per call (default = SM_EVENT_QUEUE_SIZE); min_dwell re-checked per event against the then-current state
- **Error tiers:** two enforced, one informational — MINOR (recorded and queryable via `SM_Error_IsMinorActive` / `GetMinorTimestamp` / `ClearMinor`; the framework takes no action of its own), NORMAL (managed recovery), CRITICAL (system lock with DIS)
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
- Numeric assertion IDs: 100-199 init, 200-299 process, 300-399 time events, 400-499 deferred events, 500-599 event posting, 600-699 reset/lifecycle, 700-749 error handler, 750-759 MINOR accessors, 800-899 debug

## What NOT to Do
- Do not block in state callbacks (no delay/infinite loops)
- Do not modify SM_Context fields directly (use SM_* API exclusively)
- Do not call SM_Process from ISR context (documented as non-ISR-safe); do not call SM_Process recursively from callbacks (corrupts the same instance)
- Do not call SM_DeferEvent/SM_RecallEvent from ISR (state callback context only)
- Treat SM_EventQueueIsFull/Depth/IsEmpty as diagnostic-only (TOCTOU with concurrent SM_PostEvent from ISR); use SM_PostEvent’s return value for decisions
- Do not leave all debug messages enabled in production (use SM_DEBUG_LEVEL and SM_Debug_EnableLevel)

## TODO

### v4.2.0 work order — remaining (opened 2026-08-23)

Directive brief: `docs_dev/state-machine-template_execution-brief_v1.0.md`
(decisions D18–D24, work order W1–W8). Its **§0a EXECUTION STATUS** block records
what has run. **W1, W2a, W2b, W3 are DONE** (commits `77a1a11`, `609b93f`,
`ffdc2f7`, `d437af0`). Framework is at **4.2.0**. One commit per item; do not
batch. §5's verification protocol is mandatory.

- [ ] **W4 — parent-fallback rename (D19).** `SM_FEATURE_HSM` →
      `SM_FEATURE_PARENT_FALLBACK`, `SM_HSM_MAX_DEPTH` → `SM_PARENT_MAX_DEPTH`,
      deprecated alias for one minor version. Compile it under test — that is
      what clears the G10 WARN honestly — with a 3-level chain asserting that
      intermediate entry/exit actions do **not** run, commented as the
      documented limit rather than a bug. Fix the dangling
      `sm_find_transition_hsm` reference at `sm_config.h:259`. **⚠ The brief's
      §4 list is now one site short:** `SM_ABI_FINGERPRINT` in `sm_types.h`
      references `SM_FEATURE_HSM` and must be renamed with it.
- [ ] **W5 — HAL disposition (D20).** `SM_FEATURE_WATCHDOG` +
      `SM_GetNextDeadline` (planned) + a tickless-idle example; document the four
      application-facing calls; demote `IsTimeout`; `SimTick` behind
      `SM_PLATFORM_SIM`; delete `src/app/app_main.c`. Record in CLAUDE.md why
      any surviving G7 WARN stays — do **not** add stub tests to silence them.
- [ ] **W6 — doc and measurement truth (F-D).** `README.md:364-367` and
      CLAUDE.md still claim **~544 B RAM baseline**; measured is **316 B**
      (Cortex-M4, `-Os`). Add a generated size report so it cannot drift again.
      Restructure `config/sm_config_template.h`'s USAGE step 2, which still
      tells the reader to define the dimensions in that header — the exact
      thing that does not work.
- [ ] **W7 — test-build divergence (F-B).** `tests/CMakeLists.txt` hardcodes
      `SM_STATE_COUNT=4U` / `SM_EVENT_COUNT=8U` into a separate
      `sm_framework_test` target, so the `PUBLIC` propagation that **is** the
      v4.1 fix is never exercised by `ctest`. Derive the defs from the root
      cache vars and add one test that links the real `sm_framework` target.
      Prerequisite for W8 meaning anything.
- [ ] **W8 — feature matrix + CI (D22).** 8-config compile sweep + full `ctest`
      on 3, as the first `.github/workflows/`. **Named cost:** the `ASSERT=0`
      config breaks every `TEST_EXPECT_ASSERT` case and must instead assert the
      return-value path — which is finding 3.4's untested branch and the whole
      reason that config matters. Budget for it.

Still open from the 2026-08-22 dossier, unchanged: 1.5 internal transitions,
1.13 multicore, 3.4 assert-then-return, 3.6 unshadowed DIS fields, 5.5 binary
trace, 6.3 linear transition search, 11.1 c-bone frozen v3.0.0 fork.

**Blocked (2026-08-22, unchanged):** the Opus/Sonnet review fleet never ran —
monthly spend limit. Workflow resumable with `resumeFromRunId:
wf_c3683e9d-143`. **All findings to date remain single-reviewer.**

All phases complete. Active roadmap:
- [ ] **Phase B — model as source of truth (`smgen`):** TOML machine models → generated C tables with build-failing validation, committed artifacts, round-trip verification against graphify's extractor, model hash in firmware. Full plan with decisions D12–D17 and phase gates B1–B4: `docs_dev/phase_b_model_plan.md`. **B1 complete** (smgen/ package: schema parser + V1–V10 validator + validate/hash CLI; pilot models in models/ validate clean; 39 Python tests in ctest). Next: B2 (C emitters, migrate blinky then basic).

Maintenance items:
- [ ] Fix cppcheck installation (std.cfg path hardcoded to non-existent R: drive)
- [ ] GitHub Actions CI (deferred from Phase 6; natural home for `smgen check` once B3 lands)
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

**Last session:** 2026-08-23 — v4.2.0 execution brief (D18–D24, W1–W8); W1 and W2b landed

**2026-08-23 — W1 ABI fingerprint + W2b graphify G16:** Working the directive
brief at `docs_dev/state-machine-template_execution-brief_v1.0.md`.

**W1 (D23).** v4.1's guard checked two macros; the application allocates
`SM_Context_t` and its layout depends on eight, plus several more that change
engine semantics without moving a field. Finding F-A — the same defect class
v4.1 shipped to fix, with a worse residue. `SM_ABI_FINGERPRINT` folds
`sizeof(SM_Context_t)` with every layout- and semantics-affecting macro;
`SM_Init_` carries it; assertion **107** compares it, placed *above* the memset
so nothing dereferences `sm` until the layout is known to agree. **Reproduced,
not argued:** an app at `SM_EVENT_QUEUE_SIZE=4` against the library at 8 —
v4.1 returned true and clobbered **64 canary bytes past the end of the caller's
object**; v4.2 fires 107 with the canary intact. `tests/test_abi_guard.c` is
that reproduction in ctest (one source, two targets), and it also covers 105
and 106, which v4.1 had shipped **with no test at all**.

**W2b (F-C).** DIS write-atomicity is a *structural* property, so it is proven
statically rather than by a race test — a hook on the critical-section boundary
has no seam to fire in between two adjacent non-critical stores, so a runtime
harness would report "no tear" against precisely the buggy code. **G16** checks
that both stores of every DIS pair fall inside one critical section.
**Detector demonstrated:** clean on HEAD (3 INFO, all `SM_Init_` construction,
exemption stated in-source via a `DIS-ATOMIC-EXEMPT:` marker that the report
still prints), and **6 ERROR against `9427166~1`** — including the three
live-machine sites (`sm_execute_transition`, `SM_Reset`, `SM_Error_Report`)
that v4.1 fixed. This is what retires the prior session's "the DIS race fix is
unproven by test" caveat: it is now proven by static invariant with the
detector shown to fire on the known-bad revision. The runtime harness (W2a)
remains outstanding and covers the *dynamic* claims only — queue index races,
watermark, timer-list integrity during tick, recall-vs-post.

**W2a (D21).** Runtime ISR-interleaving harness in `tests/test_platform.c` +
`test_common.h` + `tests/test_isr_interleave.c` — **zero production source
edits**, verified by diff. A hook fires at critical-section boundaries; by
default only at `nesting == 0`, because on one core an interrupt cannot run
inside a critical section. An opt-in in-critsec mode models an NMI or a second
core and is labelled as a different contract. 12 cases: what an ISR observes
during a drain, queue index/watermark consistency under injected posts, timer
list integrity across an injected arm and disarm mid-tick, recall-vs-post.

**The harness's own limits are the important part**, and are written into the
file header rather than only the commit message: it fires at instrumented
points, not arbitrary instruction boundaries; it models one core with no
reordering; and **it cannot observe a torn DIS pair**. That last was
[DERIVED] in the brief and is now **measured**: the harness was run against
`9427166~1`, the pre-DIS-fix engine that G16 flags with 6 ERRORs, and **all 12
cases pass**. It gives genuinely torn code a clean bill of health. Never cite a
green run here as evidence about DIS atomicity — that is G16's job.

Two things the harness found on its own: `SM_PostEvent`'s documented TOCTOU
hazard, demonstrated rather than asserted (an ISR that floods the queue at the
seam inside `SM_PostEvent` makes the task-context post correctly return false);
and a **graphify precision bug** — `_isr_contract` matched the phrase
"ISR-safe" anywhere in a doc comment, so a test comment naming *another*
function's contract made the test itself ISR-safe and raised a false G5. Three
prose mentions in the shipped headers had the same shape, so it was latent, not
novel. Rewording the test would have laundered it; the rule is now that a
contract leads a line or is parenthetical, pinned by 4 tests, with the declared
contract table verified byte-identical before and after.

**W3 (D18).** MINOR was documented as "auto-recovery" and implemented as two
fields nothing read — `minor_active` had **no reader anywhere in the framework
and no public accessor**, so an application could not have implemented the
advertised behaviour even if it wanted to. The defect was the false promise,
not the 8 bytes. Added `SM_Error_IsMinorActive` / `SM_Error_GetMinorTimestamp`
/ `SM_Error_ClearMinor` (assertions 750–753); `ClearMinor` retires the flag
*without* wiping the current error record, which is why it exists alongside
`SM_Error_Clear`. **No framework auto-recovery** — every other recovery path
here is already application-driven, and the framework cannot know what
recovering from your minor error means. Re-documented at all six sites,
including `error_recovery_example.c`, whose Phase 1 was **titled**
"Auto-Recovery" while its body honestly cleared the error by hand. That example
is the one whose stdout deliberately changed; the other five are still
banner-only against the golden baseline.

Detector proof: new API cannot fail against pre-W3 HEAD except by link error,
which proves dependency, not detection — so the accessors were **mutated**
(return false / return 0 / no-op) and 4 cases failed. The timestamp assertion
was strengthened after noticing it passed at t=0 even if the field were never
written; a weak assertion is worse than none, because it reads as coverage.

**G8 earned its keep this item:** it flagged CLAUDE.md claiming `test_error.c`
had 18 tests when the file had 26. Inventory corrected — 272 RUN_TEST cases
across 22 suites, 26 ctest targets.

**Correction carried forward** (from the brief, verified this session): the 8
remaining G-check WARNs are **7 × G7-untested-api + 1 × G10**, not "seven HAL
stubs". `App_Main_GetVersion` is not HAL, and `SM_Platform_OutputSend` *is*
exercised (via `sm_debug.c`) but has no direct test caller. `IsTimeout`,
`NVS_Read`, `NVS_Write`, `GetResetReason`, `HasCapability` and
`GetCriticalNesting` are called by `tests/test_hal.c` and are not warned.

**Environment note:** this shell's heredocs eat backslash-newline pairs, which
silently collapses multi-line C macros onto one line and turns regex `\b` into
a backspace. Write patch scripts to a file and run them; never heredoc anything
containing a backslash.

**2026-08-22 — graphify v2 typed graph, full "perfect state machine" review, and the v4.1.0 fixes it justified

**2026-08-22 — v4.1.0 build-consistency + lifecycle release (commits `9427166`, `6ee7768`):** Implemented the review's root-cause findings, in dependency order so nothing was rewritten twice. **(1) ABI.** `SM_EVT_TIMEOUT` was `SM_EVENT_COUNT`, so it differed between the library and every application: the engine posted its value while app tables matched on theirs, and **every `SM_EVT_TIMEOUT` route in the repo was dead code** (reproduced by linking a 6-state/2-event program against the shipped library: `SM_Init` asserted on a valid initial state, `SM_PostEvent` accepted out-of-range ids, the timeout never fired). Fixed by making it a fixed reserved id `0xFFFF`; the FSM dimensions became CMake cache vars applied **PUBLIC** to `sm_framework`; examples stopped re-`#define`-ing them; `SM_Init` is now a macro over `SM_Init_` carrying the app's dimensions, rejecting a mismatch with assertion 105/106. **(2) Lifecycle.** `SM_Reset` disarms the timer schedule (armed timers used to keep firing into the reset machine); `SM_TimeEvt_Init` unlinks a scheduled timer instead of orphaning the list behind it — by searching the owner's list, **not** by reading `te->armed`, because timers are often stack-allocated (the first attempt read it and segfaulted `test_time_events`); new `SM_DIS_ASSIGN` writes each DIS field/shadow pair inside one critical section (they were separately observable, so an ISR calling a documented ISR-safe reader between them asserted on healthy data); `SM_DeferEvent` validates ids; `sm_debug.c` gained a module + 800-899 assertion block. **Verification pattern worth repeating:** example stdout captured as a golden baseline before changes (all six stayed byte-identical) and new tests compiled against the pre-fix engine via `git archive HEAD` — 6 of 11 fail there, proving they detect rather than decorate. **graphify had to learn two things this exposed:** writes through a macro's lvalue argument (routing writes through `SM_DIS_ASSIGN` silently deleted every DIS write edge) and API coverage through macro expansion. ctest **23/23**, zero warnings, G-checks **1 ERROR + 17 WARN → 0 ERROR + 8 WARN**. Still open (need design decisions): MINOR tier unimplemented, HSM half-feature, dead HAL surface, no ISR-interleaving harness, no feature-matrix build — see the status table in `docs_dev/review_findings_2026-08-22.md`.

**2026-08-22 — graphify v2 + review findings:** graphify rewritten as a typed knowledge graph (scoped call resolution, macro variants, struct-field read/write edges, critical-section spans, ISR contracts, machine↔code bindings, model round-trip, docs xref, metrics, G1–G15 validators, `graph.html` viewer; 29 unit tests in ctest). Review produced `docs_dev/review_findings_2026-08-22.md` — ~90 tagged findings/ideas across engine, timers, error/safety, API/ABI, debug, memory, tests, docs, build, smgen/graphify, workspace consumers, topology. Headline verified defects: the prebuilt library bakes `SM_STATE_COUNT=4U/SM_EVENT_COUNT=8U` so apps with more states assert in `SM_Init` and every example's `SM_EVT_TIMEOUT` route is dead (G15); field+DIS-shadow two-store races vs ISR-safe readers (G14); armed timers survive `SM_Reset`; `SM_TimeEvt_Init` on an armed timer truncates the list; measured Cortex-M4 `sizeof(SM_Context_t)`=316 B vs documented ~544. The Opus/Sonnet review fleet was blocked by the account spend limit — its workflow script is saved and resumable (see memory). **Next:** turn the findings list into a plan (user's call on ordering).

**2026-08-03 (later still) — machine graphs + Phase B plan:** graphify gained machine-level extraction (`graphify/machines.py`): per-machine Mermaid diagrams + V1–V5 validator in graphify-out/MACHINES.md; first run caught basic_example's dead 5s timeout (fixed: SM_EVT_TIMEOUT failsafe route + SimTick). Phase B planned in `docs_dev/phase_b_model_plan.md` — smgen (TOML → generated C), decisions D12–D17 (committed artifacts, tomllib zero-dep, linker-enforced callbacks, schema-impossible unrouted timeouts, model hash, project-level shared counts), phases B1–B4 with exit criteria. Next concrete step: B1.

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
- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md. v2 (2026-08-22) sections: G-check validator findings (G1 duplicate assertion IDs, G3 no SM_DEFINE_MODULE, G4 unbalanced critsec, G5 ISR-contract violations, G6 call cycles, G7 untested API, G8 undocumented/stale docs + CLAUDE.md test-count claims, G9 model↔example round-trip, G10 feature flags never compiled under test, G11 unreached callbacks, G12 never-overridden weak HAL, G13 articulation points, G14 volatile writes outside critical sections, **G16 DIS pair write atomicity**), topology (bow-tie, layers, SCCs, articulation points, degree signature), god nodes with betweenness/PageRank, interface layer (decl → weak/override implementations), feature gates, config macros, **state access matrix** (writers/readers per `SM_Context` field), critical sections + documented ISR contracts, assertion map, macro expansion map, machine↔code callback bindings, test inventory / API coverage, docs cross-reference, models↔examples round-trip, Python tooling, directory + structural communities
- For questions about a specific state machine (states, transitions, timeouts), read graphify-out/MACHINES.md — per-machine Mermaid diagrams, timing tables, and validator findings (V1 unreachable states, V2 timeout without SM_EVT_TIMEOUT route, V3 dwell>timeout, V4 terminal states, V5 fully-guarded events). Application machines and test fixtures are listed separately; only application WARNs matter
- If graphify-out/wiki/index.md exists, navigate it instead of reading raw files
- graphify-out/graph.json is the typed node/edge/metrics/findings export for downstream tools; graphify-out/graph.html is a self-contained interactive viewer (open in a browser; filter by node/edge kind, color by community/layer)
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` (or `python3 -m graphify.watch`) from the repo root to keep the graph current. Run from the repo root: a workspace-level `graphify` package in site-packages shadows this one otherwise
- graphify-out/ stays gitignored (ephemeral, regenerable output)
- `ctest` runs `test_graphify_unit` (graphify/tests: extractor fixtures + real-repo invariants such as the single-writer set for `SM_Context.current_state`, ISR contracts, model round-trip MATCH, no call cycles, **G16 DIS write-atomicity**) — a checker nobody has tested is a checker nobody should trust. G16's own detector was demonstrated by mutation (forcing `protected = True` fails 2 cases) and against the known-bad revision, below
- Planned: swap the regex extraction layer (cparse.py + analyze.py only) for a clang/libclang AST analyzer (see TODO) — the dataclasses, graph.json and report format are the stable contract
