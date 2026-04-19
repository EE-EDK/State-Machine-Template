# Task: State Machine Framework v3.0 Rewrite

## Goal
Rewrite the embedded C state machine framework from a basic template into a production-grade, MISRA-informed, ISR-safe, power-aware framework suitable for real deployed firmware. Preserve zero-heap-allocation guarantee and weak-symbol HAL portability. Fix all identified bugs, architecture gaps, and missing production features.

## Success Criteria
- Event queue (configurable depth, ISR-safe ring buffer with payload)
- Guard conditions on transitions
- Hierarchical state support (optional, compile-time)
- Full thread safety (atomic event queue, nested critical sections)
- Watchdog + power management HAL hooks
- State persistence interface
- Unit test suite with 90%+ branch coverage
- Zero MISRA C:2012 Required rule violations in core
- Compiles clean on GCC ARM, GCC x86, Clang, IAR (CI matrix)
- RAM < 2 KB baseline (no debug, no statistics, 8-event queue)
- Flash < 10 KB baseline

## Architecture Principles
1. **Zero heap** -- all static/stack, compile-time sized
2. **ISR-safe API** -- any function callable from ISR is documented and proven safe
3. **Opaque handles** -- no extern globals; accessor API only
4. **Compile-time stripping** -- every subsystem (debug, stats, history, hierarchical) removable via `#define` to zero cost
5. **Single event queue** -- replaces single-event bottleneck
6. **Data-driven transitions** -- const table in flash, not runtime-built
7. **Weak HAL** -- same override pattern, expanded to watchdog + sleep + NVS

---

## Phases

- [x] **Phase 0: Cleanup & Legacy Removal** (prep) -- completed 2026-04-18
- [x] **Phase 1: Architecture Redesign** (headers, types, memory layout) -- completed 2026-04-18, commit 7823e69
- [x] **Phase 2: Core Rewrite** (state machine engine, event queue, transitions) -- completed 2026-04-18, commit 4238969
- [x] **Phase 3: Error Handler Rewrite** (DIS, SM_REQUIRE, stats) -- completed 2026-04-19
- [x] **Phase 4: HAL Expansion** (watchdog, power, NVS, critical section nesting) -- completed 2026-04-18, commit 6b7fac6
- [x] **Phase 5: Debug System Rewrite** (per-module filtering, compile-time stripping) -- completed 2026-04-18, commit 4eb4e03
- [x] **Phase 6: Test Infrastructure** (9 suites, 102 tests, ctest) -- completed 2026-04-19
- [ ] **Phase 7: Examples & Documentation** (real-world examples, migration guide)
- [ ] **Phase 8: Validation & Release** (static analysis, size audit, final review)

---

## Phase 0: Cleanup & Legacy Removal
**Goal:** Remove dead code, resolve dual-generation confusion, establish clean starting point.

### Tasks
- [x] 0.1 Delete legacy root files: `app_main.c`, `app_main.h`, `main.c`, `App_Config_Template.h` (1,329 lines removed)
- [x] 0.2 Renamed `config/app_config_template.h` -> `config/sm_config_template.h`, updated 6 files
- [x] 0.3 Removed unused `COMM_PACKET_SIZE`, `COMM_RETRY_COUNT` from sm_config.h and template
- [x] 0.4 Broken macro was in deleted legacy file only -- no action needed
- [x] 0.5 Removed duplicate `#include <unistd.h>` in `basic_example.c`
- [x] 0.6 Committed `ed92613`, pushed to origin main

### Exit Criteria
- Only v2 modular files remain
- `cmake .. && make` still builds clean
- Examples still compile and run

---

## Phase 1: Architecture Redesign
**Goal:** Redesign headers, types, and memory layout for the production feature set.

### Tasks
- [ ] 1.1 Design new `sm_types.h`: opaque handle (`SM_Handle_t`), event queue struct, event with payload, guard function typedef, hierarchical state node
- [ ] 1.2 Design event payload: `typedef struct { SM_Event_t event; uint32_t data; } SM_EventItem_t` -- 8 bytes per slot
- [ ] 1.3 Design ring buffer: `SM_EventQueue_t` with `head`, `tail`, `count`, `SM_EventItem_t items[SM_EVENT_QUEUE_SIZE]` -- ISR-safe via critical section
- [ ] 1.4 Design transition table: `typedef struct { SM_State_t from; SM_Event_t event; SM_State_t to; SM_Guard_t guard; } SM_Transition_t` -- `const` in flash
- [ ] 1.5 Design state descriptor: `typedef struct { SM_StateCallback_t on_entry; SM_StateCallback_t on_execute; SM_StateCallback_t on_exit; uint32_t timeout_ms; uint32_t min_dwell_ms; SM_State_t parent; } SM_StateDesc_t`
- [ ] 1.6 Design opaque context: all internal state behind `SM_Handle_t` pointer to static struct, no extern globals
- [ ] 1.7 Design config hierarchy: `sm_config_defaults.h` (framework) -> user `sm_config.h` (overrides) -> `_Static_assert` validation block
- [ ] 1.8 Write `sm_types.h`, `sm_config_defaults.h` headers
- [ ] 1.9 Write `sm_defs.h` -- state/event/error enums as **example** enums with `SM_USER_STATES`/`SM_USER_EVENTS` extension point, or fully user-defined
- [ ] 1.10 Decide: should states/events be framework-defined (current) or fully user-defined (more flexible)? **Decision needed.**

### Key Design Decisions

| # | Decision | Options | Recommendation | Rationale |
|---|----------|---------|----------------|-----------|
| D1 | State/event enum ownership | A: Framework-defined (current 10 states) / B: User-defined (framework provides INIT+ERROR only) / C: Hybrid (framework reserves 0-3, user defines rest) | **B: User-defined** | A template shouldn't dictate application states. Framework needs only INIT (index 0) and a way to know max. User `#define SM_STATE_COUNT N`. |
| D2 | Event payload size | A: None (current) / B: `uint32_t` / C: `uintptr_t` / D: `void*` + size | **B: `uint32_t`** | Fits sensor readings, error codes, GPIO masks. No pointer chasing. Fixed size for ring buffer. |
| D3 | Transition table location | A: RAM, runtime-built (current) / B: Flash, const array / C: Both (const default + runtime override) | **C: Both** | Const table covers 95% of cases (zero RAM). Runtime `SM_AddTransition()` for dynamic behavior. |
| D4 | Hierarchical states | A: Not supported / B: Always supported / C: Compile-time optional (`SM_FEATURE_HSM`) | **C: Compile-time optional** | Most projects don't need it. When disabled, zero overhead. Parent field stripped from state descriptor. |
| D5 | Handle vs global | A: Single global (current) / B: Opaque handle, multiple instances / C: Handle but singleton enforced | **B: Opaque handle** | Enables unit testing (create/destroy per test), multiple FSMs (comm + control), no extern leakage. |

### Memory Budget (Phase 1 Target)

| Component | Size (32-bit, baseline config) |
|-----------|-------------------------------|
| SM context (opaque) | ~64 bytes |
| Event queue (8 slots x 8 bytes) | 64 bytes |
| Transition table (const, flash) | 0 bytes RAM |
| State descriptors (const, flash) | 0 bytes RAM |
| Error handler | ~340 bytes |
| Debug (if enabled) | ~16 bytes static + 256 stack |
| **Total RAM** | **~484 bytes** (down from ~1,084) |

### Exit Criteria
- All new headers compile (types, config, platform stubs)
- `_Static_assert` validates all config constraints
- Design doc in `docs_dev/findings.md` captures all decisions

---

## Phase 2: Core Rewrite
**Goal:** Implement state machine engine with event queue, guard conditions, const transition table, timeouts with dwell time. Incorporate QP/C-inspired safety patterns: DIS verification, hard-bounded loops, frontEvt optimization, time events, deferred events, numeric assertion IDs.

### QP/C Patterns Adopted in This Phase

| Pattern | QP/C Reference | Our Implementation |
|---------|---------------|-------------------|
| **frontEvt optimization** | `QEQueue.frontEvt` — direct-delivery slot | `SM_EventQueue_t.front` — bypass ring buffer when queue empty |
| **Hard-bounded loops** | `Q_INVARIANT(id, i < MAX)` on every loop | `SM_ASSERT_ID(id, i < SM_MAX_TRANSITIONS)` on transition search |
| **Duplicate Inverse Storage** | `QP_DIS_UPDATE/VERIFY` on state + temp | `sm->state_dis = ~sm->current_state` verified before dispatch |
| **Time events** | `QTimeEvt` linked-list, arm/disarm | `SM_TimeEvt_t` linked-list per SM instance, tick handler |
| **Deferred events** | `QActive_defer/recall` with LIFO recall | `SM_DeferEvent/SM_RecallEvent` with separate ring buffer |
| **Numeric assertion IDs** | `Q_DEFINE_THIS_MODULE + Q_REQUIRE_LOCAL(id, ...)` | `SM_DEFINE_MODULE(id) + SM_REQUIRE(local_id, expr)` |

### Tasks — Event Queue (with frontEvt)
- [ ] 2.1 Implement `SM_EventQueue_t` with **frontEvt optimization**: when queue is empty, `SM_PostEvent` places event directly in `front` slot (O(1), no ring buffer touch). When `front` is occupied, overflow into ring buffer. `SM_Process` checks `front` first, then ring.
- [ ] 2.2 Ring buffer: `head`/`tail`/`count` with ISR-safe critical section wrapping. `nMin` watermark tracking (QP/C `QEQueue.nMin` pattern) for sizing validation.
- [ ] 2.3 Implement `SM_EventQueueIsFull`, `SM_EventQueueIsEmpty`, `SM_EventQueueDepth`, `SM_EventQueueFlush`, `SM_EventQueueGetMin` (watermark).

### Tasks — Core Engine
- [ ] 2.4 Implement `SM_Init(SM_Handle_t sm, const SM_Config_t *config)`: store config, init queue, init error handler, set initial state, **set DIS for initial state** (`sm->state_dis = (uint16_t)~sm->current_state`), mark initialized.
- [ ] 2.5 Implement `SM_Process(SM_Handle_t sm)` — full RTC (run-to-completion) step:
  1. **Verify DIS** — `SM_REQUIRE(200, sm->current_state == (uint16_t)~sm->state_dis)`
  2. Check critical lock → force error state if active
  3. Execute `on_entry` on first cycle after transition (`state_entered` flag)
  4. Execute `on_execute` callback
  5. Check state timeout (fire-once with `timeout_fired` flag, cleared on state entry)
  6. Check min dwell time — suppress transitions if `elapsed < min_dwell_ms`
  7. Dequeue event from front/ring buffer
  8. **Bounded transition search** — iterate const table with `SM_ASSERT_ID(210, i < config->transition_count)` loop bound
  9. Evaluate guard condition if present
  10. If transition found: call `on_exit`, call transition action, **update DIS**, call `on_entry` of new state
  11. Process time events via `SM_TimeEvt_Tick_`
  12. Update statistics if enabled
- [ ] 2.6 Implement guard condition evaluation: `bool (*SM_Guard_t)(SM_Handle_t sm, uint16_t event, uint32_t data)` — called before transition; if returns false, try next matching transition (QP/C `Q_UNHANDLED` analog — multiple transitions from same state+event with different guards).
- [ ] 2.7 Implement state timeout: fire-once flag per state entry, `SM_PostEvent(sm, SM_EVT_TIMEOUT, 0)` internally. Cleared on every state transition.
- [ ] 2.8 Implement minimum dwell time: if `min_dwell_ms > 0` and `elapsed < min_dwell_ms`, skip event processing entirely.

### Tasks — Time Events (QP/C-inspired)
- [ ] 2.9 Define `SM_TimeEvt_t`:
  ```c
  typedef struct SM_TimeEvt {
      struct SM_TimeEvt *next;  // intrusive linked list
      SM_Handle_t sm;           // owning state machine
      uint16_t sig;             // signal to post on expiry
      uint32_t data;            // event data payload
      uint32_t ctr;             // down-counter (0 = disarmed)
      uint32_t interval;        // auto-reload (0 = one-shot)
  } SM_TimeEvt_t;
  ```
- [ ] 2.10 Implement `SM_TimeEvt_Init(SM_TimeEvt_t *te, SM_Handle_t sm, uint16_t sig, uint32_t data)` — initialize but do not arm.
- [ ] 2.11 Implement `SM_TimeEvt_Arm(SM_TimeEvt_t *te, uint32_t ticks, uint32_t interval)` — insert into SM's time event list. `ticks` = initial delay, `interval` = auto-reload (0 = one-shot). ISR-safe (critical section).
- [ ] 2.12 Implement `SM_TimeEvt_Disarm(SM_TimeEvt_t *te)` — remove from list, return true if was armed. ISR-safe.
- [ ] 2.13 Implement `SM_TimeEvt_Tick_(SM_Handle_t sm)` — called from `SM_Process`. Walk linked list, decrement counters, post event when counter reaches 1 (not 0 — QP/C convention avoids off-by-one). Reload if `interval > 0`. **Hard-bounded** with `SM_FEATURE_MAX_TIME_EVENTS` (default 8).
- [ ] 2.14 Add `SM_FEATURE_TIME_EVENTS` config flag (default 1). When 0, all time event code compiles out.

### Tasks — Deferred Events (QP/C-inspired)
- [ ] 2.15 Define deferred event queue: separate `SM_EventQueue_t` instance per SM (`SM_Context_t.defer_queue`), sized by `SM_DEFER_QUEUE_SIZE` (default 4).
- [ ] 2.16 Implement `SM_DeferEvent(SM_Handle_t sm, uint16_t event, uint32_t data)` — post to defer queue instead of main queue. Returns false if defer queue full.
- [ ] 2.17 Implement `SM_RecallEvent(SM_Handle_t sm)` — pop from defer queue and post to **front** of main queue (LIFO recall — QP/C `QActive_recall` pattern). Returns true if an event was recalled.
- [ ] 2.18 Implement `SM_FlushDeferred(SM_Handle_t sm)` — discard all deferred events.
- [ ] 2.19 Add `SM_FEATURE_DEFER` config flag (default 0). When 0, no defer queue allocated, functions compile out.

### Tasks — Safety (DIS + Bounded Loops + Numeric Assertions)
- [ ] 2.20 Define assertion macros in `sm_platform.h` or new `sm_safety.h`:
  ```c
  #define SM_DEFINE_MODULE(name_)  static char const sm_this_module_[] = name_;
  #define SM_REQUIRE(id_, expr_)   ((expr_) ? (void)0 : SM_Platform_Assert(sm_this_module_, (id_)))
  #define SM_ASSERT_ID(id_, expr_) SM_REQUIRE(id_, expr_)
  #define SM_INVARIANT(id_, expr_) SM_REQUIRE(id_, expr_)
  ```
  Platform assert signature changes: `void SM_Platform_Assert(const char *module, int id)` — module string + numeric ID, not file+line. Smaller strings, deterministic.
- [ ] 2.21 Add DIS fields to `SM_Context_t`: `uint16_t state_dis` (inverse of `current_state`), `uint8_t init_dis` (inverse of `initialized`). Verify before every dispatch and after every transition.
- [ ] 2.22 Add `SM_DIS_UPDATE(val)` and `SM_DIS_VERIFY(orig, dis)` macros (QP/C `QP_DIS_UPDATE/VERIFY` pattern).
- [ ] 2.23 Bound every loop: transition search (`i < transition_count`), time event tick (`i < SM_FEATURE_MAX_TIME_EVENTS`), HSM ancestor walk (`depth < SM_HSM_MAX_DEPTH`).

### Tasks — State Queries and History
- [ ] 2.24 Implement `SM_GetState`, `SM_GetPreviousState`, `SM_GetStateTime`, `SM_GetExecCount`.
- [ ] 2.25 Implement state history ring (last N states, `SM_STATE_HISTORY_DEPTH` default 4). `SM_GetStateHistory(sm, buf, len, &count)`.
- [ ] 2.26 Implement `SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data)` — public ISR-safe API wrapping frontEvt + ring buffer. Returns false + increments `stats.total_events_dropped` on overflow instead of asserting (QP/C `QF_NO_MARGIN` vs margin-based pattern — we use margin=0 as default, no assertion on overflow).

### Tasks — Runtime Transitions and HSM
- [ ] 2.27 If `SM_FEATURE_RUNTIME_TRANSITIONS`: implement `SM_AddTransition(sm, &transition)` — append to runtime table, bounded by `SM_MAX_TRANSITIONS`.
- [ ] 2.28 If `SM_FEATURE_HSM`: implement parent-state fallback — if no transition found in current state's const table, check parent's transitions (walk up ancestry). Bounded by `SM_HSM_MAX_DEPTH` (default 6, matching QP/C `QHSM_MAX_NEST_DEPTH_`). Entry/exit ordering: exit inner-to-LCA, enter LCA-to-inner (UML statechart semantics).

### Tasks — Statistics
- [ ] 2.29 If `SM_FEATURE_STATISTICS`: track `total_transitions`, `total_events_posted`, `total_events_dropped`, `total_timeouts`, `state_entry_counts[]`, `queue_min_free` (watermark).

### Thread Safety Contract
- `SM_PostEvent` — ISR-safe (critical section around frontEvt + queue)
- `SM_TimeEvt_Arm`, `SM_TimeEvt_Disarm` — ISR-safe (critical section around list ops)
- `SM_GetState` — ISR-safe (single volatile read + DIS verify)
- `SM_Process` — NOT ISR-safe, call from main loop / RTOS task only
- `SM_DeferEvent`, `SM_RecallEvent` — NOT ISR-safe (called from state callbacks only)
- All other accessors — NOT ISR-safe unless documented

### Not Adopted from QP/C (reference only)
- **Active Object pattern** (`QActive` = HSM + queue + thread + priority): Our framework provides the engine; users wrap it in their own RTOS task or bare-metal loop. See QP/C `qp.h:452` for the pattern if needed.
- **Dynamic event pools + GC** (`QF_newX_/QF_gc`): Our value-copy queue avoids lifecycle complexity. For systems needing shared events, reference QP/C `src/qf/qf_dyn.c` pool allocator.
- **Publish-subscribe** (`QActive_publish_`): Beyond single-FSM scope. For multi-FSM pub-sub, reference QP/C `src/qf/qf_ps.c` bitmap subscriber set.
- **States-as-functions** (`QState handler(me, e)` + `Q_SUPER()`): We keep table-driven transitions for inspectability/auto-generation. For complex HSMs where function-pointer states are superior, reference QP/C `src/qf/qep_hsm.c`.

### Exit Criteria
- Event queue with frontEvt posts/dequeues correctly
- DIS verification catches simulated corruption
- All loops bounded with assertion IDs
- Guard conditions block/allow transitions correctly
- Timeout fires exactly once per state entry
- Dwell time prevents premature exit
- Time events arm/disarm/fire correctly (one-shot + periodic)
- Deferred events park and recall in correct order
- State history tracks last N transitions
- Build passes clean

---

## Phase 3: Error Handler Rewrite
**Goal:** Fix all bugs, support custom recovery, integrate with new handle-based API. Add DIS on critical_lock field.

### Tasks
- [x] 3.1 Rewrite `SM_ErrorHandler_t` as embedded struct inside SM context (no extern) -- done in Phase 1
- [x] 3.2 Fix `GetHistoryCount` to track actual count (not always return max) -- done in Phase 1
- [x] 3.3 Fix time-zero sentinel bug (use `bool minor_active` flag instead of timestamp==0) -- done in Phase 1
- [x] 3.4 Fix recovery logic: user-registered callback is sole recovery mechanism -- done in Phase 1
- [x] 3.5 Add `SM_Error_RegisterRecoveryCallback` -- done in Phase 1
- [x] 3.6 Add `SM_Error_RegisterNotifyCallback` -- done in Phase 1
- [x] 3.7 Add bounds checking on error code/level before array access. SM_REQUIRE(700, level < SM_ERROR_LEVEL_COUNT) + history bounds (730-731, 742)
- [x] 3.8 DIS on `critical_lock`: `critical_lock_dis` in SM_ErrorHandler_t, SM_DIS_UPDATE on write (sm_error.c + SM_Init), SM_DIS_VERIFY on read (SM_Error_IsCriticalLock:710, SM_Process:205, SM_Reset:600)
- [x] 3.9 `SM_Error_GetStats` — SM_ErrorStats_t with errors_by_level[], recovery_success, recovery_fail, last_error_time
- [x] 3.10 ISR safety documented in sm_error.h file header
- [x] 3.11 `SM_DEFINE_MODULE("sm_error")` + assertion IDs 700-749 throughout

### Exit Criteria
- History count matches actual logged errors
- Time-zero edge case handled
- DIS on critical_lock verified in tests
- Custom recovery callback invoked for all error codes
- All array accesses bounds-checked with assertion IDs

---

## Phase 4: HAL Expansion
**Goal:** Expand platform abstraction to cover watchdog, power management, NVS, nested critical sections.

### Tasks
- [ ] 4.1 Add `SM_Platform_WatchdogKick(void)` -- weak, default no-op
- [ ] 4.2 Add `SM_Platform_WatchdogStart(uint32_t timeout_ms)` -- weak, default no-op
- [ ] 4.3 Add `SM_Platform_WatchdogStop(void)` -- weak, default no-op
- [ ] 4.4 Add `SM_Platform_EnterSleep(SM_SleepMode_t mode)` -- weak, default no-op. Modes: LIGHT, DEEP, STANDBY
- [ ] 4.5 Add `SM_Platform_ExitSleep(void)` -- weak, default no-op
- [ ] 4.6 Add `SM_Platform_NVS_Write(uint16_t key, const void *data, uint16_t len)` -- weak, returns false
- [ ] 4.7 Add `SM_Platform_NVS_Read(uint16_t key, void *data, uint16_t len)` -- weak, returns false
- [ ] 4.8 Implement nested critical section tracking: `SM_Platform_EnterCritical` increments nesting counter, `SM_Platform_ExitCritical` decrements and only re-enables interrupts at 0. Provide default implementation in weak file.
- [ ] 4.9 Make `SM_Platform_IsTimeout` weak (currently non-weak -- user can't override)
- [ ] 4.10 Fix simulation `Platform_GetTimeMs` double-increment issue (separate tick counter from time source)
- [ ] 4.11 Add `SM_Platform_GetResetReason(void)` -- weak, returns UNKNOWN. Enum: POR, WATCHDOG, SOFTWARE, EXTERNAL, BROWNOUT
- [ ] 4.12 Add compile-time platform detection: auto-define `SM_PLATFORM_ARM`, `SM_PLATFORM_POSIX`, `SM_PLATFORM_SIM` from compiler predefined macros
- [ ] 4.13 Add runtime platform capability check: `SM_Platform_HasCapability(SM_PlatformCap_t cap)` -- WATCHDOG, NVS, SLEEP, UART, SPI, etc.

### Exit Criteria
- All new HAL functions have weak defaults that compile and run on host
- Nested critical sections track depth correctly
- `Platform_IsTimeout` is overridable
- Simulation time doesn't double-increment

---

## Phase 5: Debug System Rewrite
**Goal:** Fix no-op functions, add per-module filtering, compile-time stripping, binary-safe output. Align assertion output with QP/C numeric ID pattern.

### Tasks
- [ ] 5.1 Fix `Debug_EnableErrorMessages`, `Debug_EnableWarningMessages`, `Debug_EnableInfoMessages` — store values in `SM_DebugConfig_t` flags array indexed by level
- [ ] 5.2 Implement per-module debug tags: `SM_DEBUG_TAG("MOD_NAME")` macro, filter by tag bitmask
- [ ] 5.3 Fix `Debug_SetPeriodicInterval` — store in config, use in periodic check
- [ ] 5.4 Fix `snprintf` negative return handling in formatter (check < 0 before cast to uint32_t)
- [ ] 5.5 `SM_DEBUG_LEVEL` compile-time gate: 0=off (zero code), 1=error only, 2=+warning, 3=+info, 4=+verbose. When 0, all debug functions and `SM_LOG_*` macros compile to `((void)0)`.
- [ ] 5.6 Add timestamp + level + module prefix: `[12345] [ERR] [sm_engine] message\n`
- [ ] 5.7 Add `SM_Debug_HexDump(const void *data, uint32_t len)` for protocol/buffer debugging
- [ ] 5.8 Ensure `SM_DEBUG_BUFFER_SIZE >= SM_DEBUG_MSG_MAX_LEN` via `_Static_assert`
- [ ] 5.9 Remove `vsnprintf`/`stdio.h` dependency when `SM_FEATURE_DEBUG == 0` (no stdio pulled in)
- [ ] 5.10 **Assertion output format** — `SM_Platform_Assert(module, id)` outputs `ASSERT: sm_engine:210` (module string + numeric ID). No `__FILE__`/`__LINE__` strings — saves flash on small MCUs. QP/C `Q_DEFINE_THIS_MODULE` pattern.
- [ ] 5.11 Use `SM_DEFINE_MODULE("sm_debug")` + numeric IDs throughout debug module

### Exit Criteria
- All enable/disable functions actually work
- `SM_FEATURE_DEBUG=0` produces zero debug code in binary (verify with `nm` / `size`)
- Per-module tags filter correctly
- No stdio dependency when debug disabled
- Assertion output uses module+ID format (no file paths)

---

## Phase 6: Test Infrastructure
**Goal:** Create comprehensive unit test suite using Unity framework, runnable on host. Cover all QP/C-inspired features.

### Tasks
- [x] 6.1 Unity v2.6.0 via FetchContent (URL download, no vendored files)
- [x] 6.2 tests/CMakeLists.txt with sm_framework_test lib (all features enabled) + add_sm_test helper
- [x] 6.3 test_event_queue.c — 10 tests: frontEvt bypass, ring overflow, watermark, FIFO delivery order
- [x] 6.4 test_engine.c — 21 tests: init validation, on_entry/execute/exit, guards (single + multi-fallthrough), timeout fire-once, dwell time, state history, reset
- [x] 6.5 test_time_events.c — 11 tests: init fields, arm one-shot/periodic, disarm, tick countdown, multi-timer, re-arm, sig+data verification
- [x] 6.6 test_deferred.c — 9 tests: defer/recall FIFO from defer queue, LIFO to main front, flush, capacity=4
- [x] 6.7 test_error.c — 18 tests: 3-tier report, DIS corruption on critical_lock (assertion 710), history ring wrap, recovery callback + max retries, notify callback, SM_Error_GetStats
- [x] 6.8 test_debug.c — 14 tests: level enable/disable, tag registration (16 max), periodic interval, HexDump null/zero safety
- [x] 6.9 test_safety.c — 11 tests: DIS update/verify, state_dis/init_dis/critical_lock_dis corruption, SM_REQUIRE module+id, bounded loop exhaust
- [x] 6.10 test_hal.c — 18 tests: SimTick, IsTimeout + uint32 wraparound, critsec nesting 3-deep, capabilities, NVS/reset stubs
- [x] 6.11 test_integration.c — 6 tests: full lifecycle, time event + transition, error injection + recovery, deferred across states, guard multipath, statistics
- [x] 6.12 ctest integration — `cmake --build . && ctest` runs all 9 suites, 102 tests, 0.35s
- [ ] 6.13 Code coverage target (gcov/lcov) — deferred to Phase 8
- [ ] 6.14 GitHub Actions CI — deferred to Phase 8

### Exit Criteria
- `ctest` passes all tests
- Branch coverage >= 90%
- DIS and bounded-loop assertions verified in safety tests
- CI green on all matrix targets

---

## Phase 7: Examples & Documentation
**Goal:** Provide real-world examples and migration guide from v2.

### Tasks
- [ ] 7.1 Example: blinky state machine (3 states, timer events) -- minimal
- [ ] 7.2 Example: sensor sampling pipeline (IDLE->SAMPLE->PROCESS->TRANSMIT->IDLE with guard on buffer full)
- [ ] 7.3 Example: error recovery demo (inject errors, watch 3-tier escalation)
- [ ] 7.4 Example: multi-FSM (two independent state machines in one app)
- [ ] 7.5 Example: STM32 platform override (show real HAL implementation stub)
- [ ] 7.6 Update `README.md` with new API, architecture diagram, memory budget
- [ ] 7.7 Update `Quick-Guide.md` with new quick-start
- [ ] 7.8 Write `MIGRATION.md`: v2 -> v3 API mapping table
- [ ] 7.9 Update `CLAUDE.md` project documentation

### Exit Criteria
- All examples compile and run in simulation
- README accurately describes v3 API
- Migration guide covers every v2 API function

---

## Phase 8: Validation & Release
**Goal:** Static analysis, size audit, final quality gate.

### Tasks
- [ ] 8.1 Run `cppcheck --enable=all` -- zero warnings
- [ ] 8.2 Run `clang-tidy` with embedded checks -- zero warnings
- [ ] 8.3 Verify RAM/Flash budget: `arm-none-eabi-size` on minimal config
- [ ] 8.4 Verify zero heap: `nm` output has no malloc/free/calloc/realloc references
- [ ] 8.5 Verify MISRA C:2012 Required rules (manual checklist for top-20 rules)
- [ ] 8.6 Verify all `_Static_assert` fire correctly on bad config
- [ ] 8.7 Tag release `v3.0.0`
- [ ] 8.8 Update root workspace CLAUDE.md status: Template 100% -> v3.0.0

### Exit Criteria
- Static analysis clean
- RAM < 2 KB, Flash < 10 KB baseline
- Zero heap usage confirmed
- Release tagged

---

## Decisions Log

| # | Decision | Rationale | Date |
|---|----------|-----------|------|
| D1 | User-defined states/events (framework provides init mechanism only) | Template shouldn't dictate app states; current 10 states are arbitrary | 2026-04-18 |
| D2 | `uint32_t` event payload | Fits sensor/error/GPIO data, no pointer chasing, fixed ring buffer slot size | 2026-04-18 |
| D3 | Const flash table + optional runtime override | Const covers 95% use cases at zero RAM; runtime for dynamic behavior | 2026-04-18 |
| D4 | Hierarchical states compile-time optional | Most projects don't need HSM; when disabled, zero overhead | 2026-04-18 |
| D5 | Opaque handle, multiple instance support | Enables unit testing, multiple FSMs, no extern globals | 2026-04-18 |
| D6 | Adopt QP/C frontEvt optimization | Bypass ring buffer for single-event case — common in low-traffic states. QP/C `QEQueue.frontEvt` proven pattern | 2026-04-18 |
| D7 | Adopt QP/C DIS (Duplicate Inverse Storage) on critical fields | IEC-61508/MISRA safety pattern. Catches memory corruption on `current_state` and `critical_lock` before acting on corrupted data | 2026-04-18 |
| D8 | Adopt QP/C hard-bounded loops + numeric assertion IDs | Every loop gets `SM_INVARIANT(id, i < MAX)`. Assertions use `module:id` not `file:line` — smaller flash, deterministic. QP/C `Q_DEFINE_THIS_MODULE` pattern | 2026-04-18 |
| D9 | Adopt QP/C time events (linked-list, arm/disarm) | Essential for embedded — replaces ad-hoc timer management. QP/C `QTimeEvt` pattern with intrusive linked list, one-shot + periodic | 2026-04-18 |
| D10 | Adopt QP/C deferred events (defer/recall with LIFO recall) | Critical for state machines that need to postpone events. QP/C `QActive_defer/recall` pattern. Behind `SM_FEATURE_DEFER` flag | 2026-04-18 |
| D11 | NOT adopting: Active Objects, dynamic event pools, pub-sub, states-as-functions | Active Objects = too heavy for template. Dynamic pools = heap-like complexity. Pub-sub = multi-FSM scope. States-as-functions = our table-driven approach is deliberate for inspectability. Reference QP/C source for users who need these | 2026-04-18 |

## Errors Encountered

| Error | Attempt | Resolution |
|-------|---------|------------|
| (none yet) | | |

## Risk Register

| Risk | Mitigation |
|------|------------|
| Scope creep (HSM becomes complex) | HSM is Phase 2 optional feature; ship without it if timeline slips |
| Breaking change pain for existing users | MIGRATION.md + v2 branch preserved |
| Test infrastructure overhead | Unity is single-header, minimal footprint |
| ARM cross-compile CI complexity | Start with host-only tests, add ARM cross in Phase 8 |
