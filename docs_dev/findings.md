# Findings: State Machine Framework v3.0 Rewrite

> **Historical — pre-rewrite audit (legacy v2 file names).** The **Bug Inventory** and line references below target **v2 sources** (`sm_state_machine.c`, `sm_error_handler.c`, etc.). Many items were **fixed in the v3 rewrite** (ring queue, DIS, timeout once-flag, crit sections, debug storage, weak `IsTimeout`). **Do not use this file as the current defect backlog.** For the shipping API and behavior, use `README.md`, `Quick-Guide.md`, and the public headers in `include/sm_framework/`.
>
> **2026-04-27 maintenance:** Deep review + doc pass (`264a623` and following): `SM_BOUNDED_LOOP_*` postcondition corrected (exhaustive bounded loops valid); `SM_Process` runtime guards when `SM_FEATURE_ASSERT` is off; invalid `to_state` dropped with warning; API docs for queue ordering (internal vs user posts), ISR queue snapshot TOCTOU, no `SM_Process` reentrancy; time-event countdown semantics aligned with `SM_TimeEvt_Tick_`; debug subsystem documented as process-global.

## Review Findings (Pre-Rewrite Audit)

### Bug Inventory (Must Fix)

| # | File | Line | Bug | Severity |
|---|------|------|-----|----------|
| B1 | `src/core/sm_debug.c` | 97-111 | `Debug_EnableErrorMessages`, `EnableWarningMessages`, `EnableInfoMessages` are no-ops -- never store the bool | Medium |
| B2 | `src/core/sm_debug.c` | 143-147 | `Debug_SetPeriodicInterval` is a no-op -- never stores the interval | Medium |
| B3 | `src/core/sm_debug.c` | 211 | `snprintf` negative return cast to `uint32_t` -- sends garbage length | High |
| B4 | `src/core/sm_error_handler.c` | 73 | Minor error time-zero sentinel: `timestamp == 0` is valid at boot | Low |
| B5 | `src/core/sm_error_handler.c` | 210-213 | `GetHistoryCount` always returns 16 regardless of actual count | Medium |
| B6 | `src/core/sm_state_machine.c` | 149 | No bounds check on `current_state` before array index | High |
| B7 | `src/core/sm_error_handler.c` | 150 | No bounds check on `error.code` before array index | High |
| B8 | `include/sm_framework/sm_config.h` | 252 | `#if SM_MAX_STATES < STATE_MAX` references enum before defined (evaluates to 0) | Medium |
| B9 | `App_Config_Template.h` | 128-141 | `PRINT_CONFIG_SUMMARY` macro inside `#if` block, syntax errors | Low (legacy) |
| B10 | `src/platform/sm_platform_weak.c` | 16-34 | Simulation time double-increments (IsTimeout calls GetTimeMs) | Medium |
| B11 | `src/core/sm_state_machine.c` | 168-178 | Timeout event fires every cycle after expiry (no once-flag) | Medium |
| B12 | `src/core/sm_state_machine.c` | 181-189 | Event read/clear not in critical section (race with ISR post) | High |
| B13 | `examples/basic_example.c` | 13,15 | Duplicate `#include <unistd.h>` | Trivial |

### Architecture Gaps (Must Address in Redesign)

| # | Gap | Impact | v3 Solution |
|---|-----|--------|-------------|
| A1 | Single pending event (drop rest) | Events lost under load | Ring buffer event queue |
| A2 | No event payload | Can't pass context with events | `uint32_t data` per event |
| A3 | No guard conditions | Can't do conditional transitions | `SM_Guard_t` function pointer in transition |
| A4 | Flat state machine only | Can't model complex subsystems | Optional HSM (`SM_FEATURE_HSM`) |
| A5 | Framework-dictated states | Template forces 10 specific states | User-defined states (D1) |
| A6 | `extern` global context | Tight coupling, no multi-instance | Opaque handle (D5) |
| A7 | Runtime-built transition table | Wastes RAM, init complexity | Const flash table (D3) |
| A8 | No state history depth | Only `previous_state` | Ring of last N states |
| A9 | No min dwell time | States can exit immediately | `min_dwell_ms` per state |
| A10 | Legacy v1 files coexist | User confusion | Remove in Phase 0 |

### Missing Production Features

| # | Feature | v3 Plan |
|---|---------|---------|
| F1 | Watchdog integration | HAL weak functions (Phase 4) |
| F2 | Power/sleep management | HAL weak functions + sleep mode enum (Phase 4) |
| F3 | State persistence (NVS) | HAL weak NVS read/write (Phase 4) |
| F4 | Nested critical sections | Nesting counter in weak default (Phase 4) |
| F5 | Unit tests | Unity framework (Phase 6) |
| F6 | Per-module debug filtering | Tag bitmask system (Phase 5) |
| F7 | Reset reason detection | HAL weak function (Phase 4) |
| F8 | Multiple FSM instances | Handle-based API (Phase 2) |

### Code Quality Notes

- **Good:** Zero heap, `_Static_assert` on critical sizes, `extern "C"` guards, clean `#ifndef` config pattern, comprehensive compiler warnings in CMake
- **Improve:** `const` correctness on read-only pointers, `volatile` on ISR-shared fields beyond `pending_event`, bounds checking before all array accesses, `_Static_assert` on string table sizes vs enum counts

## Reference: Comparable Frameworks

| Framework | Key Feature We Should Adopt |
|-----------|----------------------------|
| QP/C (Quantum Leaps) | Hierarchical state machines, const transition tables, event queue with payload |
| SMF (Zephyr RTOS) | Simple flat+hierarchical, user-defined states, handle-based |
| Boost.SML (C++) | Compile-time transition table, guard/action separation |
| UML Statecharts | Entry/exit/do actions, guard conditions, composite states, history |

## Memory Model Reference

### Current v2 RAM Breakdown
| Component | Bytes |
|-----------|-------|
| g_sm_context (extern global) | ~368 |
| g_state_table[10] (static, RAM) | ~640 |
| g_state_data (static) | ~8 |
| g_debug_config (static) | ~12 |
| g_comm_verification (static) | ~8 |
| g_recovery_handlers[11] (static) | ~44 |
| g_custom_formatter (static) | ~4 |
| **Total static RAM** | **~1,084** |
| Stack (debug call chain) | ~400 |

### Target v3 RAM Breakdown
| Component | Bytes | Notes |
|-----------|-------|-------|
| SM context (opaque handle) | ~64 | State, timing, flags |
| Event queue (8 x 8B) | 64 | Configurable depth |
| Transition table | 0 | Const in flash |
| State descriptors | 0 | Const in flash |
| Error handler | ~340 | History buffer dominates |
| State history (4 entries) | 16 | Configurable depth |
| Debug config (if enabled) | ~16 | |
| Time event list head | 4 | Pointer to first time event |
| Deferred queue (if enabled, 4 slots) | 36 | SM_FEATURE_DEFER only |
| DIS fields (state_dis, init_dis) | 4 | Safety verification |
| **Total static RAM** | **~544** | ~50% reduction from v2 |
| **With defer queue** | **~580** | Still well under 1 KB |

## QP/C Reference Review (2026-04-18)

### Key Architecture Patterns Studied

**Source:** `qpc-master/` (QP/C 8.1.4 by Quantum Leaps)

| Pattern | QP/C Implementation | v3.0 Adoption |
|---------|---------------------|---------------|
| State-as-function-pointer | `QState handler(me, e)` + `Q_SUPER(parent)` | NOT adopted — table-driven approach kept for inspectability |
| frontEvt optimization | `QEQueue.frontEvt` direct-delivery slot | **ADOPTED** — bypass ring buffer when queue empty |
| Duplicate Inverse Storage | `QP_DIS_UPDATE/VERIFY` on state, pool blocks | **ADOPTED** — on `current_state`, `critical_lock`, `initialized` |
| Hard-bounded loops | `Q_INVARIANT(id, i < MAX)` on every loop | **ADOPTED** — all transition/time-event/HSM loops bounded |
| Numeric assertion IDs | `Q_DEFINE_THIS_MODULE("name") + Q_REQUIRE_LOCAL(200, ...)` | **ADOPTED** — `SM_DEFINE_MODULE + SM_REQUIRE(id, expr)` |
| Time events | `QTimeEvt` intrusive linked-list, arm/disarm, one-shot/periodic | **ADOPTED** — `SM_TimeEvt_t` per SM instance |
| Deferred events | `QActive_defer/recall` with LIFO recall to front | **ADOPTED** — behind `SM_FEATURE_DEFER` flag |
| Active Objects | `QActive` = HSM + queue + thread + priority | NOT adopted — too heavy for template |
| Dynamic event pools + GC | `QF_newX_/QF_gc` with reference counting | NOT adopted — value-copy queue is safer |
| Publish-subscribe | `QPSet` bitmap + `QActive_publish_` | NOT adopted — beyond single-FSM scope |
| Cooperative kernel (QV) | Single-stack scheduler with `QPSet_findMax` | NOT adopted — users bring their own scheduler |
| Preemptive kernel (QK) | Priority-based preemption with threshold | NOT adopted — RTOS territory |

### QP/C Key File References

| File | Purpose | Lines |
|------|---------|-------|
| `include/qp.h` | Master types, macros, API | 791 |
| `src/qf/qep_hsm.c` | HSM dispatch algorithm | 647 |
| `src/qf/qf_actq.c` | Active Object event queue | 507 |
| `src/qf/qf_dyn.c` | Dynamic event pools + GC | 393 |
| `src/qf/qf_time.c` | Time events (linked-list timers) | 447 |
| `src/qf/qf_defer.c` | Deferred events | 155 |
| `src/qf/qf_ps.c` | Publish-subscribe | 297 |
| `src/qf/qf_mem.c` | Memory pool allocator | 268 |
| `include/qsafe.h` | Functional Safety assertions (DIS, module IDs) | 152 |

### Memory Impact of QP/C Adoptions

| Feature | RAM Cost | Flash Cost | Notes |
|---------|----------|------------|-------|
| frontEvt field | +8 bytes | +~50 bytes | `SM_EventItem_t` in context + branch logic |
| DIS fields | +4 bytes | +~100 bytes | `state_dis` + `init_dis` + verify macros |
| Time event linked-list head | +4 bytes | +~300 bytes | Pointer + tick handler code |
| Per `SM_TimeEvt_t` instance | +24 bytes each | 0 | User-allocated, not framework RAM |
| Deferred queue (optional) | +36 bytes | +~150 bytes | Only if `SM_FEATURE_DEFER=1` |
| Numeric assertion strings | 0 | -~200 bytes | Shorter than `__FILE__` strings |
| Bounded loop invariants | 0 | +~80 bytes | Extra branch per loop |
| **Net change** | **+16 bytes** (no defer) | **+~280 bytes** | Trivial cost for safety gains |
