# Migration Guide

# v4.1 to v4.2

## Overview

v4.1 made the library and the application agree about `SM_STATE_COUNT` and
`SM_EVENT_COUNT`. It did not make them agree about anything else — and the
application is the side that allocates `SM_Context_t`.

That struct's layout depends on eight macros, not two:
`SM_STATE_HISTORY_DEPTH`, `SM_EVENT_QUEUE_SIZE`, `SM_ERROR_HISTORY_SIZE`,
`SM_MAX_TRANSITIONS`, `SM_DEFER_QUEUE_SIZE`, `SM_STATE_COUNT` and the
`SM_FEATURE_*` flags. A build that disagreed on any of the six v4.1 did not
check passed assertions 105 and 106 cleanly and then had the library read and
write at *its* field offsets inside an object the application had sized
differently.

This was reproduced, not argued. An application compiled with
`SM_EVENT_QUEUE_SIZE=4` against the shipped library (8):

```
application sizeof(SM_Context_t) = 1160  (queue size 4)
library    sizeof(SM_Context_t) = 1224  (queue size 8)

v4.1:  SM_Init returned: true        <- accepted
       canary bytes clobbered: 64    <- wrote 64 bytes past the caller's object

v4.2:  *** ASSERTION: sm_engine:107 ***
       canary intact                 <- rejected before the memset
```

Memory corruption, not a wrong answer — a worse failure than the one v4.1 was
released to fix, and it survived that release. `tests/test_abi_guard.c` is this
reproduction, wired into `ctest` so it cannot regress silently.

## Breaking changes

1. **`SM_Init_` takes a fourth argument.**

   ```c
   /* v4.1 */
   bool SM_Init_(SM_Handle_t sm, const SM_Config_t *config,
                 uint16_t app_state_count, uint16_t app_event_count);

   /* v4.2 */
   bool SM_Init_(SM_Handle_t sm, const SM_Config_t *config,
                 uint16_t app_state_count, uint16_t app_event_count,
                 uint32_t app_abi);
   ```

   - **Action: none** if you call `SM_Init(sm, &config)`, which is the
     documented API. The macro supplies the new argument.
   - If you called `SM_Init_` directly — it is `_`-suffixed and internal, but
     reachable — pass `(uint32_t)SM_ABI_FINGERPRINT` as the fourth argument.

2. **`SM_Init` now fails on any configuration mismatch, not just the two
   counts.** Assertion **107** compares `SM_ABI_FINGERPRINT`, which folds
   `sizeof(SM_Context_t)` together with every layout- and semantics-affecting
   macro.

   - **Action:** if `SM_Init` starts returning false (or asserting 107) on code
     that worked before, it was **already broken** — the check is reporting a
     divergence that was previously silent. The log line prints both
     fingerprints in hex. Set the configuration once for the whole build:

     ```
     cmake -DSM_STATE_COUNT=6 -DSM_EVENT_COUNT=12 ..
     ```

     or force-include your `app_config.h` into *every* target, framework
     sources included. Do not `#define` these macros in application sources
     only. See `config/sm_config_template.h`.

## New in v4.2

- **`SM_ABI_FINGERPRINT`** (`sm_types.h`) — the compile-time build signature.
  Usable by applications that want to log or transmit their own value. It is
  not a preprocessor constant (`sizeof` participates), so it cannot appear in
  `#if` or `SM_STATIC_ASSERT`.

## Not changed

- No engine semantics change. All six examples produce byte-identical output to
  v4.1 apart from the version banner.
- `SM_EVT_TIMEOUT` remains the fixed reserved id `0xFFFF`.
- Assertion IDs 100–106 keep their v4.1 meanings.


# v4.0 to v4.1

## Overview

v4.1 closes a build-consistency defect. v4.0's API is unchanged in shape, but
two things that used to be *per-translation-unit* are now global to a build,
because the library and the application were silently disagreeing about them.

The failure v4.1 fixes was reproducible: a library compiled with
`SM_STATE_COUNT=4 / SM_EVENT_COUNT=8` (as the shipped CMake did) linked
against an application that defined its own smaller values would (a) reject
valid initial states in `SM_Init`, (b) accept out-of-range events in
`SM_PostEvent`, and (c) make **every `SM_EVT_TIMEOUT` transition dead code** —
the engine posted its own value for the timeout signal while the application's
table matched on a different one. Every example in this repository carried a
timeout route that could never fire.

## Breaking changes

1. **`SM_EVT_TIMEOUT` is a fixed reserved id (`0xFFFF`), not `SM_EVENT_COUNT`.**
   Its value is now identical in every translation unit. User events occupy
   `0 .. SM_EVENT_COUNT-1` and can never collide with it.
   - **Action:** none, if you always wrote `SM_EVT_TIMEOUT` symbolically.
     If you hard-coded the numeric value (it used to equal your event count),
     replace the literal with the macro.
   - `SM_EVENT_COUNT`'s upper bound rises from 65534 to 65535, since the
     reserved id no longer has to sit just above the user range.

2. **`SM_STATE_COUNT` / `SM_EVENT_COUNT` are set once per build, not per file.**
   They are compile-time constants baked into the compiled framework, so the
   library and every application linked against it must see the same values.
   The build now sets them once and propagates them `PUBLIC` through the
   `sm_framework` target.
   - **Action:** remove `#define SM_STATE_COUNT` / `#define SM_EVENT_COUNT`
     (and any other layout macro such as `SM_EVENT_QUEUE_SIZE`) from your
     application sources. Set them in your CMake configuration instead:
     `cmake -DSM_STATE_COUNT=12 -DSM_EVENT_COUNT=24 ..`, or edit the cache
     variables at the top of `CMakeLists.txt`. If you consume the framework
     via `add_subdirectory`, linking `sm_framework` is enough to inherit them.
   - Your own state/event enums stay as small as you like — the build-wide
     values are a ceiling, not an exact size.

3. **`SM_Init` is now a macro over `SM_Init_`.**
   The macro forwards the *application's* compile-time dimensions so the
   library can compare them with its own. A mismatch fires assertion 105/106
   and returns `false` instead of failing silently later.
   - **Action:** none for ordinary calls. Code that took `&SM_Init` (a
     function pointer) must use `&SM_Init_` and pass the dimensions itself.

## Not changed

Engine execution semantics, the event queue, timers, deferred events, the
error handler and the HAL are untouched. All six examples produce
byte-identical output to v4.0, and the 21 C test suites pass unmodified.

# v3.0 to v4.0

## Overview

v4.0 is a **semantic** release: the API surface is almost unchanged, but the
engine's execution semantics were corrected in ways that alter observable
behavior. No flag preserves the old behavior for timers or transition
atomicity -- the v3.0 semantics were the defects.

## Breaking changes

1. **Time events are millisecond-based (was: SM_Process call counts).**
   `SM_TimeEvt_Arm(te, delay, interval)` arguments are now **milliseconds**
   measured against `SM_Platform_GetTimeMs()`, not ticks of `SM_Process`.
   A "500" timer now means 500 ms regardless of your call cadence; under
   v3.0 it meant 500 *calls*, which stretched silently when the scheduler
   was busy. Deadlines are wrap-safe (delay/interval < 2^31 ms). Periodic
   timers advance deadline by whole intervals: drift-free, and periods
   missed during a stall coalesce into ONE event (no catch-up burst).
   - **Action:** if your code called `Arm(te, N, N)` meaning "N process
     calls", convert to `Arm(te, N * SM_TASK_PERIOD_MS, ...)`.
   - `SM_TimeEvt_Arm` now returns `bool`: `false` on invalid arguments or
     when `SM_FEATURE_MAX_TIME_EVENTS` timers are already scheduled (v3.0
     silently accepted over-capacity timers that then never fired).
   - Expired one-shots unlink from the schedule; re-arm them freely.
   - `SM_TimeEvt_t` fields changed: `ctr` is replaced by `deadline` +
     `armed`. Code peeking at `te->ctr` (tests, debug dumps) must switch.

2. **Transitions are atomic (was: entry deferred one call).**
   `exit -> action -> state update -> entry` all run within the same
   `SM_Process` call. Under v3.0, `on_entry` of the new state ran on the
   *next* call, so `SM_GetState()` reported a state whose entry effects had
   not happened yet -- a real hazard for any observer. The deferred-entry
   path remains only for the initial state after `SM_Init` / `SM_Reset`.

3. **SM_Process drains multiple events (was: exactly one).**
   Up to `SM_MAX_EVENTS_PER_PROCESS` (default: `SM_EVENT_QUEUE_SIZE`)
   events are processed per call, each with run-to-completion semantics.
   Chained sequences (entry posts the next event) now complete in one call
   instead of one call per step. Set `SM_MAX_EVENTS_PER_PROCESS` to `1` to
   restore the v3.0 cadence if your timing analysis depended on it.

4. **Strict FIFO for internal events (was: queue-jumping).**
   Timeout and time-event posts now append behind queued backlog like any
   other event. v3.0 let them claim the front slot ahead of earlier events
   -- a priority inversion that could time out a state an already-queued
   event was about to exit legitimately.

5. **The timeout event is public: `SM_EVT_TIMEOUT`.**
   Use it directly in transition tables (it equals `SM_EVENT_COUNT`; the
   valid range for `SM_EVENT_COUNT` is now 1..65534). `SM_AddTransition`
   accepts it, so runtime transitions can handle timeouts (v3.0 rejected
   them). `SM_PostEvent` still rejects it -- only the engine may post it.
   The timeout latch is only set when the post succeeds, so a full queue
   can no longer permanently swallow a state's timeout.

## Bug fixes that change observable behavior

- **`SM_EventQueueIsFull` agreed with nothing.** It now mirrors
  `SM_PostEvent`'s accept logic exactly. (v3.0 could report "not full"
  while a post would be dropped -- deterministically, no ISR involved.)
- **`SM_RecallEvent` now truly inserts at the front.** With the front slot
  occupied it displaces the current front into the ring right behind the
  recalled event (QP/C postLIFO). v3.0 appended to the BACK in that case,
  contradicting its own documentation. On a full main queue the event now
  **stays deferred** (v3.0 lost it). Recall order among deferred events is
  FIFO -- oldest first -- as it always actually was; v3.0 docs claiming
  "LIFO recall" described code that never existed.
- **Timer capacity is enforced at `Arm`** (see item 1) instead of timers
  past the bound silently never firing; the re-arm-past-bound list
  corruption is gone with it.
- **`SM_TimeEvt_Tick_` no longer holds one critical section across the
  whole list walk plus every queue insertion.** Fires are collected under a
  short lock and posted outside it. A disarm can race an already-collected
  fire (the event may deliver once); that is the standard time-event
  contract and is now documented.

---

# v2.0 to v3.0

## Overview

Version 3.0 is a ground-up rewrite of the State Machine Framework. The primary
motivations:

- **Handle-based API** -- every function takes `SM_Handle_t` as its first
  parameter, enabling multiple independent state machine instances in a single
  application. The v2 global context is gone.
- **State-agnostic design** -- the framework no longer defines application
  states or events. You provide your own enums and tell the framework how many
  there are via `SM_STATE_COUNT` / `SM_EVENT_COUNT`.
- **Const flash transition tables** -- event dispatch is driven by a
  `SM_Transition_t[]` array stored in ROM, replacing the v2 switch-case logic
  inside the framework. Guards and actions are first-class members of each
  transition entry.
- **QP/C-inspired safety** -- Duplicate Inverse Storage (DIS) on
  safety-critical fields, hard-bounded loops, numeric assertion IDs, and a
  `frontEvt` queue optimization are drawn from Dr. Miro Samek's QP/C 8.x
  framework.

v3.0 also introduces time events, deferred events, a 3-tier error system with
DIS protection, a tag-based debug subsystem, and an expanded HAL with watchdog,
sleep, NVS, reset reason, and capability queries.

---

## Breaking Changes

Every public symbol has been renamed. There is no source-level backward
compatibility between v2 and v3. A mechanical find-and-replace pass, guided by
the mapping table below, covers most of the migration.

1. **Global context removed.** v2 used a single internal `StateMachine`
   context. v3 requires the user to allocate `SM_Context_t` and pass
   `&ctx` (as `SM_Handle_t`) to every API call.

2. **All callbacks receive `SM_Handle_t`.** v2 callbacks had signature
   `void (*)(void)`. v3 state callbacks have signature
   `void (*)(SM_Handle_t sm)`.

3. **States and events are user-defined.** v2 shipped 10 pre-configured
   states (`STATE_INIT`, `STATE_IDLE`, etc.) and a fixed event enum. v3
   defines zero application states or events -- you provide your own enums
   and set `SM_STATE_COUNT` / `SM_EVENT_COUNT` before including the
   framework.

4. **Transition tables replace switch-case dispatch.** v2 dispatched events
   through internal switch logic. v3 requires a const `SM_Transition_t[]`
   array that maps `(from_state, event)` pairs to `to_state`, with optional
   guard and action function pointers.

5. **State descriptors replace individual callback registration.** v2
   registered `OnEntry`/`OnState`/`OnExit` callbacks per state through an
   API or internal table. v3 uses a const `SM_StateDesc_t[]` array indexed by
   state ID.

6. **`SM_PostEvent` takes a data payload.** v2 signature:
   `StateMachine_PostEvent(event)`. v3 signature:
   `SM_PostEvent(sm, event, data)`. Pass `0U` for `data` if unused.

7. **`App_Main_Init` / `App_Main_Task` removed.** v3 has no application
   glue layer. Your `main()` calls `SM_Init()` and loops on `SM_Process()`
   directly.

8. **Header reorganization.** v2 headers: `sm_state_machine.h`,
   `sm_error_handler.h`, `sm_debug.h`, `sm_platform.h`, `sm_types.h`,
   `sm_config.h`. v3 headers: `sm_engine.h`, `sm_error.h`, `sm_debug.h`,
   `sm_platform.h`, `sm_types.h`, `sm_config.h`, `sm_safety.h`,
   `sm_framework.h` (umbrella).

9. **Platform function names changed.** `Platform_GetTimeMs` becomes
   `SM_Platform_GetTimeMs`, `Platform_EnterCritical` becomes
   `SM_Platform_EnterCritical`, etc.

10. **Error handler API renamed.** `ErrorHandler_Report(level, code)` becomes
    `SM_Error_Report(sm, level, code)`. Error level enum values changed from
    `ERROR_LEVEL_*` to `SM_ERROR_*`.

11. **Debug API renamed.** `Debug_SendMessage(type, fmt, ...)` becomes
    `SM_Debug_Print(level, fmt, ...)` or the level macros `SM_LOG_ERROR`,
    `SM_LOG_WARN`, `SM_LOG_INFO`, `SM_LOG_VERBOSE`.

12. **`SM_WEAK` is empty on Windows/MinGW.** Weak symbols do not work
    reliably in PE/COFF static archives. On Windows, override platform
    functions by replacing `sm_platform_weak.c` with your own file in the
    build system.

---

## API Mapping

| v2.0 | v3.0 |
|------|------|
| `App_Main_Init(interface)` | User's `main()` calls `SM_Init(sm, config)` |
| `App_Main_Task()` | `SM_Process(sm)` |
| `StateMachine_PostEvent(event)` | `SM_PostEvent(sm, event, data)` |
| `StateMachine_GetCurrentState()` | `SM_GetState(sm)` |
| `StateMachine_GetPreviousState()` | `SM_GetPreviousState(sm)` |
| `StateMachine_GetStateTime()` | `SM_GetStateTime(sm)` |
| (global context) | `SM_Context_t ctx; SM_Init(&ctx, &config)` |
| `void (*callback)(void)` | `void (*callback)(SM_Handle_t sm)` |
| switch-case event dispatch | `SM_Transition_t[]` const table |
| 10 built-in states | User-defined enum + `SM_STATE_COUNT` |
| `ErrorHandler_Report(level, code)` | `SM_Error_Report(sm, level, code)` |
| `ErrorHandler_IsCriticalLock()` | `SM_Error_IsCriticalLock(sm)` |
| `ErrorHandler_GetCurrentError(info)` | `SM_Error_GetCurrent(sm, info)` |
| `Debug_SendMessage(type, fmt, ...)` | `SM_LOG_INFO(fmt, ...)` / `SM_Debug_Print(level, fmt, ...)` |
| `Debug_EnableRuntimeMessages(b)` | `SM_Debug_EnableLevel(level, b)` |
| `Debug_SetInterface(iface)` | `SM_Debug_Init(iface)` |
| `Platform_GetTimeMs()` | `SM_Platform_GetTimeMs()` |
| `Platform_EnterCritical()` | `SM_Platform_EnterCritical()` |
| `Platform_ExitCritical()` | `SM_Platform_ExitCritical()` |
| `Platform_UART_Init()` | `SM_Platform_OutputInit(interface)` |
| `Platform_UART_Send(data, len)` | `SM_Platform_OutputSend(data, len)` |
| `ERROR_LEVEL_MINOR` | `SM_ERROR_MINOR` |
| `ERROR_LEVEL_NORMAL` | `SM_ERROR_NORMAL` |
| `ERROR_LEVEL_CRITICAL` | `SM_ERROR_CRITICAL` |

---

## Step-by-Step Migration

### 1. Define State and Event Counts

v3 requires `SM_STATE_COUNT` and `SM_EVENT_COUNT` to be defined before any
framework header is included. Without them, the build fails with `#error`.

Create `app_config.h` (copy from `config/sm_config_template.h`) and define
your counts to match your application enums.

**Before (v2):**
```c
#include "sm_framework/sm_framework.h"
/* States were pre-defined: STATE_INIT, STATE_IDLE, ... (10 total) */
```

**After (v3):**
```c
/* app_config.h -- include BEFORE framework headers */
#define SM_STATE_COUNT  (3U)
#define SM_EVENT_COUNT  (3U)

/* In your .c file: */
#include "app_config.h"
#include "sm_framework/sm_framework.h"

typedef enum {
    STATE_INIT = 0,
    STATE_RUNNING,
    STATE_STOPPED
} AppState_t;

typedef enum {
    EVT_START = 0,
    EVT_STOP,
    EVT_TICK
} AppEvent_t;
```

### 2. Replace Global Context with Handle

v2 maintained a single internal state machine context. v3 requires explicit
static allocation and a handle.

**Before (v2):**
```c
/* No context -- the framework owned it internally */
App_Main_Init(COMM_INTERFACE_UART);
```

**After (v3):**
```c
SM_Context_t sm_ctx;         /* User allocates storage */
SM_Handle_t sm = &sm_ctx;    /* Handle is just a pointer */

SM_Config_t config = {
    .states           = app_states,
    .transitions      = app_transitions,
    .transition_count = 3U,
    .initial_state    = STATE_INIT,
};

SM_Init(sm, &config);
```

### 3. Convert State Callbacks

All callbacks now receive `SM_Handle_t` as their first parameter. This lets
callbacks query state, post events, and interact with the API without relying
on globals.

**Before (v2):**
```c
void on_idle_entry(void) {
    Debug_SendMessage(DEBUG_MSG_INFO, "Entered IDLE");
}

void on_idle_execute(void) {
    if (some_condition) {
        StateMachine_PostEvent(EVENT_START);
    }
}
```

**After (v3):**
```c
static void on_idle_entry(SM_Handle_t sm) {
    (void)sm;
    SM_LOG_INFO("Entered IDLE");
}

static void on_idle_execute(SM_Handle_t sm) {
    if (some_condition) {
        SM_PostEvent(sm, (uint16_t)EVT_START, 0U);
    }
}
```

### 4. Create State Descriptor Table

Replace individual callback registrations with a single const array of
`SM_StateDesc_t`, one entry per state, indexed by state ID. This array lives
in flash.

**Before (v2):**
```c
/* Callbacks were registered internally by the framework or through
   a state table in the source.  The user could not easily add states. */
```

**After (v3):**
```c
static const SM_StateDesc_t app_states[SM_STATE_COUNT] = {
    [STATE_INIT] = {
        .on_entry     = on_init_entry,
        .on_execute   = on_init_execute,
        .on_exit      = on_init_exit,
        .timeout_ms   = 5000U,       /* Auto-timeout after 5 s */
        .min_dwell_ms = 0U,
    },
    [STATE_RUNNING] = {
        .on_entry     = on_running_entry,
        .on_execute   = on_running_execute,
        .on_exit      = on_running_exit,
        .timeout_ms   = 0U,          /* No timeout */
        .min_dwell_ms = 0U,
    },
    [STATE_STOPPED] = {
        .on_entry     = on_stopped_entry,
        .on_execute   = on_stopped_execute,
        .on_exit      = on_stopped_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
};
```

### 5. Create Transition Table

Replace switch-case event handling with a const `SM_Transition_t[]` array.
Each entry defines: source state, triggering event, destination state, an
optional guard function, and an optional action function.

**Before (v2):**
```c
/* Transitions were hard-coded in switch-case blocks inside the framework.
   Users could post events but could not define custom transitions. */
StateMachine_PostEvent(EVENT_START);
```

**After (v3):**
```c
/* Optional guard -- return true to allow the transition */
static bool guard_battery_ok(SM_Handle_t sm, uint16_t event, uint32_t data) {
    (void)sm; (void)event; (void)data;
    return battery_voltage > MIN_VOLTAGE;
}

/* Optional action -- runs between on_exit and on_entry */
static void action_log_start(SM_Handle_t sm, uint16_t event, uint32_t data) {
    (void)event;
    SM_LOG_INFO("Starting with data=%lu", (unsigned long)data);
}

static const SM_Transition_t app_transitions[] = {
    { .from_state = STATE_INIT,    .event = EVT_START,
      .to_state   = STATE_RUNNING, ._reserved = 0,
      .guard      = guard_battery_ok,
      .action     = action_log_start },

    { .from_state = STATE_RUNNING, .event = EVT_STOP,
      .to_state   = STATE_STOPPED, ._reserved = 0,
      .guard      = NULL,
      .action     = NULL },

    { .from_state = STATE_STOPPED, .event = EVT_START,
      .to_state   = STATE_RUNNING, ._reserved = 0,
      .guard      = NULL,
      .action     = NULL },
};
```

### 6. Update Initialization

**Before (v2):**
```c
int main(void) {
    HAL_Init();
    App_Main_Init(COMM_INTERFACE_UART);

    while (1) {
        App_Main_Task();
        HAL_Delay(10);
    }
}
```

**After (v3):**
```c
int main(void) {
    HAL_Init();

    /* Initialize debug output */
    SM_Debug_Init(0U);  /* 0 = UART, application-defined */

    /* Allocate and configure */
    SM_Context_t sm_ctx;
    SM_Config_t config = {
        .states           = app_states,
        .transitions      = app_transitions,
        .transition_count = (uint16_t)(sizeof(app_transitions)
                                       / sizeof(app_transitions[0])),
        .initial_state    = STATE_INIT,
    };

    if (!SM_Init(&sm_ctx, &config)) {
        /* Handle init failure */
        while (1) {}
    }

    /* Main loop */
    while (1) {
        SM_Process(&sm_ctx);
        HAL_Delay(SM_TASK_PERIOD_MS);
    }
}
```

### 7. Update Event Posting

`SM_PostEvent` now requires the handle and a `uint32_t data` payload.

**Before (v2):**
```c
void Button_IRQHandler(void) {
    StateMachine_PostEvent(EVENT_START);
}
```

**After (v3):**
```c
/* The handle must be accessible from the ISR.  A file-scope pointer works. */
static SM_Handle_t g_sm = NULL;

void setup(SM_Handle_t sm) {
    g_sm = sm;
}

void Button_IRQHandler(void) {
    if (g_sm != NULL) {
        SM_PostEvent(g_sm, (uint16_t)EVT_START, 0U);
    }
}
```

### 8. Update Platform Layer

All platform function names are now prefixed `SM_Platform_`. If you had custom
implementations, rename them.

**Before (v2):**
```c
uint32_t Platform_GetTimeMs(void) { return HAL_GetTick(); }
void Platform_EnterCritical(void) { __disable_irq(); }
void Platform_ExitCritical(void)  { __enable_irq(); }
bool Platform_UART_Init(void)     { /* ... */ return true; }
uint32_t Platform_UART_Send(const uint8_t *d, uint32_t l) { /* ... */ }
```

**After (v3):**
```c
uint32_t SM_Platform_GetTimeMs(void)     { return HAL_GetTick(); }
void SM_Platform_EnterCritical(void)     { __disable_irq(); critsec_nesting++; }
void SM_Platform_ExitCritical(void)      { if (--critsec_nesting == 0) __enable_irq(); }
uint32_t SM_Platform_GetCriticalNesting(void) { return critsec_nesting; }
bool SM_Platform_OutputInit(uint8_t iface) { /* ... */ return true; }
uint32_t SM_Platform_OutputSend(const uint8_t *d, uint32_t l) { /* ... */ }
```

v3 critical sections must support nesting. Track the nesting depth and only
re-enable interrupts when the outermost critical section exits. The default
weak implementation in `sm_platform_weak.c` does this for you in simulation.

---

## New Features to Adopt

These features are new in v3 and have no v2 equivalent. All are optional and
can be adopted incrementally after the core migration is complete.

### Time Events

Software timers managed by the framework. One-shot or periodic. The timer
posts an event to the owning state machine when it expires.

```c
SM_TimeEvt_t blink_timer;
SM_TimeEvt_Init(&blink_timer, sm, (uint16_t)EVT_TICK, 0U);
SM_TimeEvt_Arm(&blink_timer, 500U, 500U);  /* 500 tick periodic */
/* Disarm when no longer needed: */
SM_TimeEvt_Disarm(&blink_timer);
```

Enabled by default (`SM_FEATURE_TIME_EVENTS = 1`). Set to `0` to compile out.

### Deferred Events

Defer an event for later recall, typically across state transitions. Useful
when an event arrives in a state that is not ready to handle it.

```c
/* In a state callback: defer the event */
SM_DeferEvent(sm, (uint16_t)EVT_DATA_READY, payload);

/* In the next state's on_entry: recall deferred events */
SM_RecallEvent(sm);
```

Disabled by default. Enable with `#define SM_FEATURE_DEFER (1U)`.

### 3-Tier Error Handling with DIS

The error handler now protects `critical_lock` with Duplicate Inverse Storage.
Error statistics are tracked per severity level.

```c
SM_Error_Report(sm, SM_ERROR_MINOR, MY_ERR_SENSOR_GLITCH);
SM_Error_Report(sm, SM_ERROR_NORMAL, MY_ERR_COMM_TIMEOUT);
SM_Error_Report(sm, SM_ERROR_CRITICAL, MY_ERR_HARDWARE_FAULT);

SM_Error_RegisterRecoveryCallback(sm, my_recovery_fn);
SM_Error_AttemptRecovery(sm);

SM_ErrorStats_t stats;
SM_Error_GetStats(sm, &stats);
```

### Safety Macros

Numeric assertion IDs replace `__FILE__`/`__LINE__` for flash-constrained
targets.

```c
SM_DEFINE_MODULE("my_driver");       /* Once per .c file */
SM_REQUIRE(100, ptr != NULL);        /* Assertion ID 100 */
SM_REQUIRE(101, len <= MAX_LEN);     /* Assertion ID 101 */
```

DIS macros for your own safety-critical variables:

```c
SM_DIS_UPDATE(my_field, my_field_dis, uint16_t);
SM_DIS_VERIFY(my_field, my_field_dis, uint16_t, 200);
```

Hard-bounded loops:

```c
SM_BOUNDED_LOOP_BEGIN(i, 64, 300)
{
    if (found) break;
}
SM_BOUNDED_LOOP_END(i, 64, 300)
```

### Debug Tags and Levels

Per-module debug tags with runtime enable/disable. Up to 16 tags.

```c
static int8_t my_tag = -1;
my_tag = SM_Debug_RegisterTag("my_module");
SM_LOG_TAG(my_tag, 3, "sensor: %d", value);
SM_Debug_EnableTag(my_tag, false);  /* Silence this module */
```

Runtime level control without recompiling:

```c
SM_Debug_EnableLevel(4, false);   /* Disable verbose at runtime */
```

Hex dump:

```c
SM_Debug_HexDump(buffer, 32);
```

### Expanded HAL

New platform functions with weak defaults:

| Function | Purpose |
|----------|---------|
| `SM_Platform_WatchdogKick/Start/Stop` | Watchdog timer |
| `SM_Platform_EnterSleep/ExitSleep` | Low-power modes |
| `SM_Platform_NVS_Write/Read` | Non-volatile storage |
| `SM_Platform_GetResetReason` | Last reset cause |
| `SM_Platform_HasCapability` | Runtime capability query |
| `SM_Platform_GetCriticalNesting` | Critical section depth |
| `SM_Platform_SimTick` | Advance simulation time |

### Optional Features (compile-time flags)

| Flag | Default | Purpose |
|------|---------|---------|
| `SM_FEATURE_HSM` | 0 | Hierarchical states with parent fallback |
| `SM_FEATURE_RUNTIME_TRANSITIONS` | 0 | `SM_AddTransition()` at runtime |
| `SM_FEATURE_STATISTICS` | 0 | Transition/event counters |
| `SM_FEATURE_TIME_EVENTS` | 1 | Software timers |
| `SM_FEATURE_DEFER` | 0 | Deferred event queue |
| `SM_FEATURE_DEBUG` | 1 | Debug output subsystem |
| `SM_FEATURE_ASSERT` | 1 | Runtime assertions |

---

## Configuration Changes

### v2 Configuration Pattern

v2 used `#define` constants for behavior and sizing, but states and events
were framework-defined. Configuration controlled things like timeout durations,
error history size, and debug enable flags.

```c
/* v2 app_config.h */
#define SM_TASK_PERIOD_MS         (10U)
#define SM_STATE_TIMEOUT_MS       (5000U)
#define ERROR_MAX_RECOVERY_ATTEMPTS (3U)
#define ERROR_HISTORY_SIZE        (16U)
#define DEBUG_ENABLE_INIT_MESSAGES    (1U)
#define DEBUG_ENABLE_RUNTIME_MESSAGES (0U)
#define DEBUG_BUFFER_SIZE         (256U)
#define FEATURE_STATISTICS_ENABLED (0U)
```

### v3 Configuration Pattern

v3 uses a `#ifndef` / `#define` pattern in `sm_config.h`. User overrides are
placed in `app_config.h` and included before the framework. Two defines are
mandatory; everything else has sensible defaults.

```c
/* v3 app_config.h */
#define SM_STATE_COUNT            (3U)   /* REQUIRED */
#define SM_EVENT_COUNT            (3U)   /* REQUIRED */

/* Optional overrides: */
#define SM_EVENT_QUEUE_SIZE       (8U)
#define SM_ERROR_HISTORY_SIZE     (8U)
#define SM_ERROR_MAX_RECOVERY     (3U)
#define SM_DEBUG_LEVEL            (3U)   /* 0=off 1=err 2=warn 3=info 4=verbose */
#define SM_DEBUG_BUFFER_SIZE      (256U)
#define SM_TASK_PERIOD_MS         (10U)
#define SM_FEATURE_STATISTICS     (0U)
#define SM_FEATURE_TIME_EVENTS    (1U)
#define SM_FEATURE_DEFER          (0U)
#define SM_FEATURE_HSM            (0U)
```

### Key differences

| Aspect | v2 | v3 |
|--------|----|----|
| State/event definition | Framework-defined | User-defined (`SM_STATE_COUNT`, `SM_EVENT_COUNT` mandatory) |
| Timeout per state | Single global `SM_STATE_TIMEOUT_MS` | Per-state `timeout_ms` field in `SM_StateDesc_t` |
| Debug control | Per-message-type enable flags | Single `SM_DEBUG_LEVEL` (compile-time) + `SM_Debug_EnableLevel` (runtime) |
| Error history size | `ERROR_HISTORY_SIZE` | `SM_ERROR_HISTORY_SIZE` |
| Recovery attempts | `ERROR_MAX_RECOVERY_ATTEMPTS` | `SM_ERROR_MAX_RECOVERY` |
| Feature flags | `FEATURE_*_ENABLED` | `SM_FEATURE_*` |

---

## Common Pitfalls

1. **Forgetting `SM_STATE_COUNT` / `SM_EVENT_COUNT`.** The build will fail
   with `#error` if these are not defined before including any framework
   header. Define them in your `app_config.h`.

2. **Callback signature mismatch.** Every state callback now receives
   `SM_Handle_t sm`. If you forget to update a callback signature, the
   compiler will warn about incompatible function pointer types. Fix all of
   them -- do not cast to silence the warning.

3. **`SM_PostEvent` data parameter.** v3 requires a `uint32_t data` payload.
   Pass `0U` if your event carries no data. Forgetting this argument is a
   compile error.

4. **`SM_WEAK` is empty on Windows/MinGW.** The weak-symbol mechanism does
   not work reliably in PE/COFF static archives. On Windows, override
   platform functions by excluding `sm_platform_weak.c` from the build and
   providing your own implementations, or by compiling the framework as part
   of your project via `add_subdirectory()` and replacing the weak source
   file.

5. **Do not call `SM_Process` from ISR context.** `SM_Process` executes
   state callbacks, evaluates transitions, and ticks time events. It is not
   ISR-safe. Only `SM_PostEvent`, `SM_GetState`, and
   `SM_Error_IsCriticalLock` are safe to call from interrupt handlers.

6. **Config pointer must remain valid.** `SM_Init` stores the `SM_Config_t*`
   pointer -- it does not copy the struct. The config and the arrays it
   points to (states, transitions) must remain valid for the lifetime of the
   state machine. Use `static const` or file-scope variables.

7. **Critical sections must support nesting.** v3 assumes nested critical
   sections work correctly. If you override `SM_Platform_EnterCritical` /
   `SM_Platform_ExitCritical`, track the nesting depth and only re-enable
   interrupts when the outermost section exits.

8. **`SM_EVENT_QUEUE_SIZE` must match between library and application.**
   If you compile the framework as a static library, `SM_EVENT_QUEUE_SIZE`
   changes the layout of `SM_Context_t`. Both the library and your
   application must see the same value. Override it in your CMakeLists.txt
   (as a compile definition), not just in a header that only your app
   includes.

9. **Deferred events are not ISR-safe.** `SM_DeferEvent` and
   `SM_RecallEvent` must only be called from state callbacks or the
   `SM_Process` context, never from ISR handlers.

10. **State descriptor array must have exactly `SM_STATE_COUNT` entries.**
    If the array is shorter, the framework will read uninitialized memory
    when entering a higher-numbered state. Use designated initializers
    (`[STATE_FOO] = { ... }`) to make the mapping explicit.

---

## Quick Reference: Minimal v3 Application

```c
/* app_config.h */
#define SM_STATE_COUNT  (3U)
#define SM_EVENT_COUNT  (2U)

/* main.c */
#include "app_config.h"
#include "sm_framework/sm_framework.h"

enum { ST_INIT, ST_RUN, ST_STOP };
enum { EVT_GO, EVT_HALT };

static void init_entry(SM_Handle_t sm) { SM_PostEvent(sm, EVT_GO, 0U); }
static void run_entry(SM_Handle_t sm)  { (void)sm; /* start work */ }
static void stop_entry(SM_Handle_t sm) { (void)sm; /* halt */ }

static const SM_StateDesc_t states[SM_STATE_COUNT] = {
    [ST_INIT] = { .on_entry = init_entry },
    [ST_RUN]  = { .on_entry = run_entry },
    [ST_STOP] = { .on_entry = stop_entry },
};

static const SM_Transition_t trans[] = {
    { .from_state = ST_INIT, .event = EVT_GO,   .to_state = ST_RUN },
    { .from_state = ST_RUN,  .event = EVT_HALT, .to_state = ST_STOP },
};

int main(void) {
    SM_Debug_Init(0U);
    SM_Context_t ctx;
    SM_Config_t cfg = {
        .states           = states,
        .transitions      = trans,
        .transition_count = 2U,
        .initial_state    = ST_INIT,
    };
    SM_Init(&ctx, &cfg);
    while (1) { SM_Process(&ctx); }
}
```
