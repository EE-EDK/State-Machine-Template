# State Machine Framework v4.1

Production-grade, handle-based state machine framework for embedded C systems. State-agnostic, multi-instance, zero-heap, ISR-safe. Designed for bare-metal microcontrollers from Cortex-M0 to application processors, with a weak-symbol HAL that ports in minutes.

Built on patterns from QP/C 8.x: frontEvt fast path, Duplicate Inverse Storage, numeric assertion IDs, and intrusive time-event linked lists.

v4.0 corrected the engine's execution semantics: strict-FIFO event delivery for all sources, atomic exit→action→entry transitions, a bounded multi-event drain per `SM_Process`, and millisecond deadline-based (drift-free) time events. v4.1 closes a build-consistency defect: `SM_EVT_TIMEOUT` is now a fixed reserved id and the FSM dimensions are set once per build, so a library and its application can no longer disagree about them. See `MIGRATION.md` for both upgrades.

## Features

- **Handle-based multi-instance** -- `SM_Handle_t = SM_Context_t*`, no extern globals, run N machines in one binary
- **State-agnostic** -- user defines all states and events as enums; framework imposes no application semantics
- **Zero heap** -- user statically allocates `SM_Context_t`; all buffers sized at compile time
- **ISR-safe event queue** -- strict FIFO in post order for all sources, with a QP/C frontEvt fast path for the common single-event case
- **Const flash transitions** -- `SM_Transition_t[]` in ROM with optional guard conditions and transition actions
- **Atomic transitions** -- exit → action → entry complete within one `SM_Process` call; observers never see a state before its entry ran
- **Bounded event drain** -- up to `SM_MAX_EVENTS_PER_PROCESS` events per `SM_Process`, so chained sequences complete in one call with a hard WCET bound
- **Error handling: two enforced tiers and one informational** -- MINOR (recorded and queryable; the application defines the policy), NORMAL (managed recovery), CRITICAL (system lock with DIS protection)
- **Time events** -- millisecond deadline-based timers (wrap-safe, drift-free), one-shot or periodic, arm/disarm from any context, capacity enforced at arm
- **State timeout** -- public `SM_EVT_TIMEOUT` event usable directly in transition tables
- **Deferred events** -- defer/recall pattern; FIFO among deferred events, recalled to the true front of the main queue
- **Safety** -- DIS verification on state and critical_lock, hard-bounded loops, numeric assertion IDs via `SM_DEFINE_MODULE` + `SM_REQUIRE`
- **Debug** -- per-module tags (16 max), runtime level enable/disable, compile-time stripping to zero overhead, HexDump
- **HAL** -- weak-symbol overrides for timing, critical sections, watchdog, sleep, NVS, reset reason, platform capabilities
- **HSM** -- optional hierarchical states (`SM_FEATURE_HSM`) with parent fallback dispatch
- **C99 standard** -- `_Static_assert` on C11+, negative-array fallback on C99

## Quick Start

```c
/* SM_STATE_COUNT / SM_EVENT_COUNT come from the BUILD, not from this file:
 *   cmake -DSM_STATE_COUNT=3 -DSM_EVENT_COUNT=2 ..
 * They are compiled into the framework itself, so the library and your
 * application must see identical values -- see "Configuration" below.
 * SM_Init rejects a mismatched build rather than misbehaving later (v4.1). */
#include "sm_framework/sm_framework.h"
#include <stdio.h>

enum { STATE_IDLE = 0, STATE_ACTIVE, STATE_DONE };
enum { EVT_GO = 0, EVT_FINISH };

static void idle_entry(SM_Handle_t sm)  { SM_PostEvent(sm, EVT_GO, 0); }
static void active_entry(SM_Handle_t sm){ printf("Active!\n"); (void)sm; }
static void done_entry(SM_Handle_t sm)  { printf("Done.\n"); (void)sm; }

static const SM_StateDesc_t states[SM_STATE_COUNT] = {
    [STATE_IDLE]   = { .on_entry = idle_entry   },
    [STATE_ACTIVE] = { .on_entry = active_entry },
    [STATE_DONE]   = { .on_entry = done_entry   },
};

static const SM_Transition_t transitions[] = {
    { .from_state = STATE_IDLE,   .event = EVT_GO,     .to_state = STATE_ACTIVE },
    { .from_state = STATE_ACTIVE, .event = EVT_FINISH, .to_state = STATE_DONE   },
};

int main(void) {
    SM_Context_t ctx;
    SM_Config_t cfg = {
        .states           = states,
        .transitions      = transitions,
        .transition_count = sizeof(transitions) / sizeof(transitions[0]),
        .initial_state    = STATE_IDLE,
    };
    if (!SM_Init(&ctx, &cfg)) return -1;
    for (int i = 0; i < 10; i++) SM_Process(&ctx);
    return 0;
}
```

## Installation

### As a CMake subdirectory

```cmake
add_subdirectory(path/to/state-machine-template)
target_link_libraries(your_target sm_framework)
```

The framework's `CMakeLists.txt` exports the `sm_framework` library target with the correct include paths.

### Standalone build

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build .
ctest --output-on-failure
```

Run examples directly:

```bash
./examples/basic_example
./examples/simulation_example
```

## Architecture

### Handle-based multi-instance

Every API function takes `SM_Handle_t` (a pointer to `SM_Context_t`) as its first parameter. There are no file-scope globals -- you can run multiple independent state machines by allocating multiple contexts:

```c
SM_Context_t motor_ctx, comms_ctx;
SM_Init(&motor_ctx, &motor_cfg);
SM_Init(&comms_ctx, &comms_cfg);
```

### Event queue with frontEvt

The event queue uses a ring buffer with a "front event" bypass slot. When the queue is **completely empty**, a post places the event directly in the front slot, skipping the ring buffer entirely; `SM_Process` dequeues the front slot first, then the ring FIFO. Because the front slot is only claimed when nothing is pending, delivery is strict FIFO in post order for every source -- user, ISR, timeout, and time events (QP/C pattern D6, revised in v4.0). The one deliberate exception is `SM_RecallEvent`, which inserts at the true front by design.

### Const flash transition tables

Transitions are declared as `const SM_Transition_t[]` arrays that live in flash. Each entry specifies source state, event, destination state, an optional guard function, and an optional action function:

```c
static const SM_Transition_t transitions[] = {
    { .from_state = S_A, .event = E_GO, .to_state = S_B,
      .guard = is_ready, .action = log_transition },
};
```

Guards return `bool` -- if false, the transition is skipped. Actions execute between the source state's `on_exit` and the destination state's `on_entry`; the full exit → action → entry sequence completes atomically within the same `SM_Process` call (v4.0).

### State descriptors with callbacks

Each state is described by an `SM_StateDesc_t` with three callbacks (`on_entry`, `on_execute`, `on_exit`), an optional `timeout_ms`, and an optional `min_dwell_ms`. These arrays are also `const` (flash-resident).

## Configuration

Configuration uses the `#ifndef` override pattern. Copy `config/sm_config_template.h` as a starting point.

### Mandatory dimensions -- set once per build (v4.1)

```bash
cmake -DSM_STATE_COUNT=4 -DSM_EVENT_COUNT=6 ..
```

`SM_STATE_COUNT` and `SM_EVENT_COUNT` are enforced with `#error` -- the framework will not compile without them. They are **not per-file settings**: they are compiled into the framework's own translation units, where they drive `SM_Init`'s initial-state check, `SM_PostEvent`'s accept range, and (with statistics enabled) `SM_Context_t`'s layout.

So the library and every application linked against it must be compiled with the same values. Two supported ways:

- **Set them in your build** (what this repository does). The `sm_framework` target propagates them `PUBLIC`, so linking it is enough for your own targets to inherit them.
- **Compile the framework as part of your application** (`add_subdirectory`) and force-include a shared config header into *every* target: `-include app_config.h`.

What does **not** work is `#define SM_STATE_COUNT` in an application source: the framework's `.c` files never see it, so the two sides diverge silently. Before v4.1 that is exactly what the examples in this repository did, and it made every `SM_EVT_TIMEOUT` transition dead code. `SM_Init` now rejects a mismatch (assertion 105/106) instead of letting it through.

The same applies to any macro that changes `SM_Context_t`'s layout -- `SM_EVENT_QUEUE_SIZE`, `SM_ERROR_HISTORY_SIZE`, `SM_STATE_HISTORY_DEPTH`, `SM_MAX_TRANSITIONS`, `SM_DEFER_QUEUE_SIZE` and the `SM_FEATURE_*` flags.

### Optional overrides (with defaults)

```c
#define SM_EVENT_QUEUE_SIZE    (8U)    /* Ring buffer depth (1-64, default 8) */
#define SM_ERROR_HISTORY_SIZE  (8U)    /* Error history entries (1-255, default 8) */
#define SM_ERROR_MAX_RECOVERY  (3U)    /* Max recovery attempts before escalation */
#define SM_STATE_HISTORY_DEPTH (4U)    /* State transition history depth */
#define SM_TASK_PERIOD_MS      (10U)   /* Expected SM_Process() call period */
#define SM_DEBUG_LEVEL         (4U)    /* Compile-time: 0=off, 1=error, 2=+warn, 3=+info, 4=+verbose */
#define SM_DEBUG_BUFFER_SIZE   (256U)  /* Debug output buffer */
#define SM_DEBUG_MSG_MAX_LEN   (128U)  /* Single message max length */
#define SM_MAX_TRANSITIONS     (32U)   /* Runtime transition table size */
```

### Feature flags

| Flag | Default | Description |
|------|---------|-------------|
| `SM_FEATURE_HSM` | 0 | Hierarchical states with parent fallback |
| `SM_FEATURE_RUNTIME_TRANSITIONS` | 0 | `SM_AddTransition()` API for runtime table modification |
| `SM_FEATURE_STATISTICS` | 0 | Transition counts, event counts, per-state entry counts |
| `SM_FEATURE_DEBUG` | 1 | Debug output subsystem (0 = all debug compiles to zero) |
| `SM_FEATURE_ASSERT` | 1 | Runtime assertions via `SM_REQUIRE` / `SM_ASSERT` |
| `SM_FEATURE_TIME_EVENTS` | 1 | Timer event subsystem (linked-list, arm/disarm) |
| `SM_FEATURE_DEFER` | 0 | Deferred event queue (defer/recall/flush) |

## API Reference

### Lifecycle

```c
bool SM_Init(SM_Handle_t sm, const SM_Config_t *config);
void SM_Process(SM_Handle_t sm);
void SM_Reset(SM_Handle_t sm);
```

- `SM_Init` -- initialize instance with config (state descriptors, transitions, initial state). Returns false on invalid params.
- `SM_Process` -- run one cycle: on_execute, timeout check, time-event tick, then drain up to `SM_MAX_EVENTS_PER_PROCESS` events with atomic exit→action→entry per transition. Call periodically.
- `SM_Reset` -- flush queue, clear errors, return to initial state (entry runs on the next `SM_Process`). Blocked if critical lock is active.

### Event Posting

```c
bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data);
```

ISR-safe. Returns false if the queue is full. Uses critical sections internally.

```c
bool SM_EventQueueIsFull(SM_Handle_t sm);
bool SM_EventQueueIsEmpty(SM_Handle_t sm);
uint8_t SM_EventQueueDepth(SM_Handle_t sm);
void SM_EventQueueFlush(SM_Handle_t sm);
uint8_t SM_EventQueueGetMin(SM_Handle_t sm);  /* High-water mark for queue sizing */
```

**Integration notes**

- Prefer **`SM_PostEvent`’s return value** over “check `SM_EventQueueIsFull` then post” — another context (e.g. ISR) can fill the queue between the check and the post (TOCTOU). `SM_EventQueueIsFull` mirrors `SM_PostEvent`'s accept logic exactly (v4.0).
- **Delivery is strict FIFO in post order for all sources** — user, ISR, state timeout, and time events share one ordering rule (v4.0). The only exception is `SM_RecallEvent`, which inserts at the true front by design.
- **`SM_Process`** must run from task/main context only — not from ISR, and **not recursively** from entry/execute/exit, guards, or transition actions on the same handle.
- **`SM_AddTransition`** (when enabled) is not synchronized with ISR posting; use only from init or the same runtime context as `SM_Process`.
- **Invalid rows in the transition table** (e.g. `to_state >= SM_STATE_COUNT`) are ignored at runtime with a warning when debug is enabled; validate tables at boot if you need a hard fault.

### State Queries

```c
uint16_t SM_GetState(SM_Handle_t sm);             /* ISR-safe (volatile read) */
uint16_t SM_GetPreviousState(SM_Handle_t sm);
uint32_t SM_GetStateTime(SM_Handle_t sm);          /* ms since entering current state */
uint32_t SM_GetExecCount(SM_Handle_t sm);           /* SM_Process calls in current state */
bool SM_GetStateHistory(SM_Handle_t sm, uint16_t *buf, uint8_t buf_len, uint8_t *count);
```

### Time Events

Requires `SM_FEATURE_TIME_EVENTS == 1` (default on).

```c
void SM_TimeEvt_Init(SM_TimeEvt_t *te, SM_Handle_t sm, uint16_t sig, uint32_t data);
bool SM_TimeEvt_Arm(SM_TimeEvt_t *te, uint32_t delay_ms, uint32_t interval_ms);  /* interval_ms=0 for one-shot */
bool SM_TimeEvt_Disarm(SM_TimeEvt_t *te);
```

Allocate `SM_TimeEvt_t` statically. `SM_TimeEvt_Arm` schedules a **millisecond deadline** against `SM_Platform_GetTimeMs()` (v4.0): the timer fires when the deadline passes, checked once per `SM_Process`, so late checks fire immediately instead of stretching with call cadence. `interval_ms` reloads periodically — deadlines advance by whole intervals (drift-free; periods missed during a stall coalesce into one event). Delay/interval must be < 2^31 ms (~24.8 days, wrap-safe). Returns `false` on bad arguments or when `SM_FEATURE_MAX_TIME_EVENTS` timers are already scheduled.

### Deferred Events

Requires `SM_FEATURE_DEFER == 1`.

```c
bool SM_DeferEvent(SM_Handle_t sm, uint16_t event, uint32_t data);
bool SM_RecallEvent(SM_Handle_t sm);   /* oldest deferred event -> true front of main queue */
void SM_FlushDeferred(SM_Handle_t sm);
```

Not ISR-safe -- call from state callbacks or `SM_Process` context only. Recall pops deferred events **oldest first (FIFO)** and inserts each at the true front of the main queue, ahead of queued backlog. If the main queue is full the event stays deferred and recall returns false.

### Error Handling

```c
bool SM_Error_Report(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code);
void SM_Error_Clear(SM_Handle_t sm);
bool SM_Error_IsCriticalLock(SM_Handle_t sm);       /* ISR-safe (volatile + DIS verify) */
bool SM_Error_AttemptRecovery(SM_Handle_t sm);
bool SM_Error_GetStats(SM_Handle_t sm, SM_ErrorStats_t *stats);
bool SM_Error_GetCurrent(SM_Handle_t sm, SM_ErrorInfo_t *info);
bool SM_Error_GetHistory(SM_Handle_t sm, uint8_t index, SM_ErrorInfo_t *info);
uint8_t SM_Error_GetHistoryCount(SM_Handle_t sm);
void SM_Error_RegisterRecoveryCallback(SM_Handle_t sm, SM_RecoveryCallback_t cb);
void SM_Error_RegisterNotifyCallback(SM_Handle_t sm, SM_ErrorCallback_t cb);
```

### Debug

```c
bool SM_Debug_Init(uint8_t interface);
void SM_Debug_Print(uint8_t level, const char *fmt, ...);
void SM_Debug_PrintRaw(const char *msg, uint32_t len);
void SM_Debug_HexDump(const void *data, uint32_t len);
void SM_Debug_EnableLevel(uint8_t level, bool enable);
int8_t SM_Debug_RegisterTag(const char *tag_name);
void SM_Debug_EnableTag(int8_t tag_id, bool enable);
void SM_Debug_PrintTagged(int8_t tag_id, uint8_t level, const char *fmt, ...);
void SM_Debug_SetPeriodicInterval(uint32_t interval_ms);
bool SM_Debug_CheckPeriodic(void);
```

Convenience macros (compile-time stripped by `SM_DEBUG_LEVEL`):

```c
SM_LOG_ERROR("fault code: %u", code);
SM_LOG_WARN("battery low: %u%%", pct);
SM_LOG_INFO("state=%u", SM_GetState(sm));
SM_LOG_VERBOSE("queue depth=%u", SM_EventQueueDepth(sm));
SM_LOG_TAG(my_tag, 3, "sensor value: %d", val);
```

## Platform Porting

The framework uses weak-symbol defaults (`sm_platform_weak.c`) that provide a simulation/development environment out of the box. To port to real hardware, override any subset of the `SM_Platform_*` functions by defining strong symbols in your platform source file.

### Core HAL functions

| Function | Purpose | Required |
|----------|---------|----------|
| `SM_Platform_GetTimeMs()` | Millisecond timestamp | Yes |
| `SM_Platform_EnterCritical()` | Disable interrupts / take mutex (nestable) | Yes |
| `SM_Platform_ExitCritical()` | Re-enable interrupts / release mutex | Yes |
| `SM_Platform_GetCriticalNesting()` | Query nesting depth | Optional |
| `SM_Platform_IsTimeout()` | Timeout check with 32-bit wrap handling | Optional |
| `SM_Platform_OutputInit()` | Initialize debug output interface | Optional |
| `SM_Platform_OutputSend()` | Send bytes to debug interface | Optional |
| `SM_Platform_Assert()` | Assertion failure handler | Recommended |

### Extended HAL functions (v3.0)

| Function | Purpose |
|----------|---------|
| `SM_Platform_WatchdogStart/Stop/Kick()` | Watchdog timer control |
| `SM_Platform_EnterSleep/ExitSleep()` | Low-power sleep modes (light/deep/standby) |
| `SM_Platform_NVS_Write/Read()` | Non-volatile storage (key-value, 16-bit keys) |
| `SM_Platform_GetResetReason()` | Last reset cause (POR, watchdog, software, external, brownout) |
| `SM_Platform_HasCapability()` | Runtime query of available subsystems |
| `SM_Platform_SimTick()` | Advance simulation time (test/sim builds only) |

### Example: STM32 port

```c
#include "sm_framework/sm_platform.h"
#include "stm32f4xx_hal.h"

uint32_t SM_Platform_GetTimeMs(void) {
    return HAL_GetTick();
}

void SM_Platform_EnterCritical(void) {
    __disable_irq();
}

void SM_Platform_ExitCritical(void) {
    __enable_irq();
}

void SM_Platform_Assert(const char *module, int id) {
    printf("ASSERT FAIL: %s:%d\n", module, id);
    while (1) { __BKPT(0); }
}
```

### Platform detection

The framework auto-detects the target at compile time and defines exactly one of:
- `SM_PLATFORM_ARM` -- ARM Cortex-M/A/AArch64
- `SM_PLATFORM_POSIX` -- Linux/Unix/macOS
- `SM_PLATFORM_SIM` -- Windows simulation, other

Override by pre-defining before including the framework headers.

## Examples

| File | Description |
|------|-------------|
| `basic_example.c` | Minimal 3-state FSM (INIT, RUNNING, STOPPED) with event posting from callbacks |
| `simulation_example.c` | Simulated timing with error injection and debug output |
| `blinky_example.c` | LED blink pattern using periodic time events |
| `sensor_pipeline_example.c` | Multi-state pipeline with guard conditions on transitions |

Examples are built when `-DBUILD_EXAMPLES=ON` is passed to CMake.

## Memory Footprint

| Configuration | RAM |
|---------------|-----|
| Baseline (no optional features) | ~544 bytes |
| With deferred events (`SM_FEATURE_DEFER=1`) | ~580 bytes |
| With statistics (`SM_FEATURE_STATISTICS=1`) | +20 + 4*SM_STATE_COUNT bytes |

All buffer sizes are set at compile time via `#define`. No heap allocations anywhere in the framework. Event queue items are 8 bytes each. Error history entries are ~16 bytes each.

Optimize for constrained targets:
- Reduce `SM_EVENT_QUEUE_SIZE` (default 8, min 1)
- Reduce `SM_ERROR_HISTORY_SIZE` (default 8, min 1)
- Set `SM_FEATURE_DEBUG` to 0 (strips all debug code and data)
- Set `SM_FEATURE_STATISTICS` to 0 (default)
- Build with `-Os`

## Testing

The framework includes 9 test suites built on [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.0 (fetched automatically via CMake `FetchContent`).

| Suite | Tests | Coverage |
|-------|-------|----------|
| `test_event_queue` | 10 | frontEvt, ring buffer, watermark, delivery order |
| `test_engine` | 21 | init, process, guards, timeout, dwell, history |
| `test_time_events` | 15 | ms deadlines, one-shot, periodic, drift/coalescing, capacity |
| `test_deferred` | 10 | defer, FIFO recall to true front, flush, capacity |
| `test_error` | 18 | 3-tier errors, DIS, stats, recovery callbacks |
| `test_debug` | 14 | levels, tags, periodic interval, hexdump |
| `test_safety` | 11 | DIS corruption detection, bounded loops, SM_REQUIRE |
| `test_hal` | 18 | critsec nesting, timeout wrap, capabilities |
| `test_integration` | 6 | full lifecycle, cross-subsystem scenarios |

Build and run:

```bash
cmake .. -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build .
ctest --output-on-failure
```

## License

MIT License. See [LICENSE](LICENSE).

## Version History

| Version | Date | Description |
|---------|------|-------------|
| **v4.1.0** | 2026-08-22 | Build-consistency release: `SM_EVT_TIMEOUT` is a fixed reserved id (0xFFFF) instead of `SM_EVENT_COUNT`, FSM dimensions set once per build and propagated to every target, `SM_Init` rejects a mismatched build, DIS field/shadow pairs written atomically, `SM_Reset` disarms the timer schedule, `SM_TimeEvt_Init` no longer truncates the timer list, `SM_DeferEvent` validates event ids. See MIGRATION.md |
| **v4.0.0** | 2026-08-03 | Semantic correction release: strict-FIFO event delivery for all sources, atomic exit→action→entry transitions, bounded multi-event drain per SM_Process, millisecond deadline-based drift-free time events with enforced capacity, public SM_EVT_TIMEOUT, exact SM_EventQueueIsFull, true front-insert recall that preserves the event on a full queue. See MIGRATION.md |
| v3.0.0 | 2026-04-18 | Complete rewrite: handle-based multi-instance, state-agnostic, QP/C patterns (frontEvt, DIS, time events, deferred events), 3-tier error handler, per-module debug tags, weak-symbol HAL with watchdog/sleep/NVS/capabilities, 118 unit tests |
| v2.0.0 | 2025-12-30 | Modular rewrite: platform abstraction, CMake build, 10 pre-configured states |
| v1.0.0 | 2025-10-25 | Initial release |
