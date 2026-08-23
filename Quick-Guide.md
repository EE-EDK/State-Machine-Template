# Quick Guide -- State Machine Framework v4.1

Handle-based, multi-instance, zero-heap, ISR-safe state machine framework for embedded C.

v4 semantics in one paragraph: events are delivered strict-FIFO in post
order; each `SM_Process` call drains up to `SM_MAX_EVENTS_PER_PROCESS`
events; a transition runs exit → action → entry atomically within that
call; time events are millisecond deadlines against
`SM_Platform_GetTimeMs()`; a state's `timeout_ms` posts the public
`SM_EVT_TIMEOUT` event, which you handle in your transition table like any
other event.

---

## Step 1: Define Your States and Events

Define application states and events as enums. The framework imposes no
application semantics; it only needs to know how many of each there are.

```c
/* app_config.h */
#include <stdint.h>

typedef enum {
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_RUNNING,
    STATE_ERROR
} AppState_t;

typedef enum {
    EVT_START = 0,
    EVT_STOP,
    EVT_FAULT,
    EVT_RECOVER,
    EVT_TIMEOUT
} AppEvent_t;
```

Your enums stay in your source. The **dimensions** go in the build, because
the framework itself is compiled with them (v4.1):

```bash
cmake -DSM_STATE_COUNT=4 -DSM_EVENT_COUNT=5 ..
```

Do not `#define SM_STATE_COUNT` in an application file: the framework's own
`.c` files never see it, and `SM_Init` will reject the mismatch. See the
Configuration section of `README.md`.

---

## Step 2: Write State Callbacks

Each callback receives `SM_Handle_t` -- use it to query state, post events, etc.
Callbacks must be non-blocking. No delays or infinite loops.

```c
static void idle_entry(SM_Handle_t sm) {
    (void)sm;
    led_off();
}

static void idle_execute(SM_Handle_t sm) {
    if (SM_GetStateTime(sm) > 10000) {
        SM_PostEvent(sm, EVT_TIMEOUT, 0);
    }
}

static void idle_exit(SM_Handle_t sm) {
    (void)sm;
}
```

---

## Step 3: Create State Descriptors

Array of `SM_StateDesc_t` indexed by state enum. Lives in flash (const).

```c
static const SM_StateDesc_t states[SM_STATE_COUNT] = {
    /* STATE_INIT */
    { .on_entry = init_entry, .on_execute = NULL, .on_exit = NULL,
      .timeout_ms = 5000, .min_dwell_ms = 0 },

    /* STATE_IDLE */
    { .on_entry = idle_entry, .on_execute = idle_execute, .on_exit = idle_exit,
      .timeout_ms = 0, .min_dwell_ms = 100 },

    /* STATE_RUNNING */
    { .on_entry = run_entry, .on_execute = run_execute, .on_exit = run_exit,
      .timeout_ms = 30000, .min_dwell_ms = 500 },

    /* STATE_ERROR */
    { .on_entry = error_entry, .on_execute = NULL, .on_exit = NULL,
      .timeout_ms = 0, .min_dwell_ms = 0 },
};
```

---

## Step 4: Create Transition Table

Array of `SM_Transition_t` -- const, lives in flash. Each entry specifies
source state, triggering event, destination state, optional guard, and optional action.

A state with `timeout_ms > 0` posts `SM_EVT_TIMEOUT` when the time
elapses -- route it like any other event (e.g. the STATE_INIT row below).

```c
static const SM_Transition_t transitions[] = {
    { STATE_INIT, SM_EVT_TIMEOUT, STATE_ERROR, 0, NULL,         NULL },
    { STATE_INIT, EVT_START,   STATE_IDLE,    0, NULL,          NULL },
    { STATE_IDLE, EVT_START,   STATE_RUNNING, 0, is_ready,      log_start },
    { STATE_RUNNING, EVT_STOP, STATE_IDLE,    0, NULL,          NULL },
    { STATE_RUNNING, EVT_FAULT, STATE_ERROR,  0, NULL,          log_fault },
    { STATE_ERROR, EVT_RECOVER, STATE_IDLE,   0, can_recover,   NULL },
};
```

---

## Step 5: Initialize and Run

Statically allocate `SM_Context_t`. Fill `SM_Config_t`. Call `SM_Init()` once,
then call `SM_Process()` periodically.

```c
#include "app_config.h"
#include "sm_framework/sm_framework.h"

static SM_Context_t ctx;

int main(void) {
    SM_Handle_t sm = &ctx;

    SM_Config_t config = {
        .states           = states,
        .transitions      = transitions,
        .transition_count = sizeof(transitions) / sizeof(transitions[0]),
        .initial_state    = STATE_INIT,
    };

    if (!SM_Init(sm, &config)) {
        while (1) { /* init failed */ }
    }

    while (1) {
        SM_Process(sm);
        /* delay SM_TASK_PERIOD_MS (default 10ms) */
    }
}
```

---

## Guard Conditions

Guards return `true` to allow the transition, `false` to block it.

```c
static bool is_ready(SM_Handle_t sm, uint16_t event, uint32_t data) {
    (void)event;
    (void)data;
    return (sensor_read() > THRESHOLD) && (SM_GetStateTime(sm) > 200);
}
```

---

## Transition Actions

Actions execute between exit and entry callbacks during a transition.

```c
static void log_start(SM_Handle_t sm, uint16_t event, uint32_t data) {
    (void)sm;
    (void)event;
    SM_LOG_INFO("system started with data=%u", (unsigned)data);
}
```

---

## Time Events

Statically allocate `SM_TimeEvt_t`. Init once, arm/disarm as needed.
Requires `SM_FEATURE_TIME_EVENTS=1` (default on).

Timers are **millisecond deadlines** against `SM_Platform_GetTimeMs()`
(v4.0): drift-free periodic reload, wrap-safe, delay/interval < 2^31 ms.
`SM_TimeEvt_Arm` returns false on bad arguments or when
`SM_FEATURE_MAX_TIME_EVENTS` timers are already scheduled.

```c
static SM_TimeEvt_t heartbeat_te;

/* At init */
SM_TimeEvt_Init(&heartbeat_te, sm, EVT_TIMEOUT, 0);

/* Arm: first fire in 100 ms, then every 100 ms (periodic) */
if (!SM_TimeEvt_Arm(&heartbeat_te, 100, 100)) { /* capacity reached */ }

/* One-shot: fires once 500 ms from now, interval=0 */
SM_TimeEvt_Arm(&heartbeat_te, 500, 0);

/* Disarm (returns true if it was armed) */
SM_TimeEvt_Disarm(&heartbeat_te);
```

---

## Deferred Events

Defer events for later recall. Requires `SM_FEATURE_DEFER=1` (default off).
Call only from state callbacks -- not ISR-safe.

```c
/* In on_execute: defer an event the current state cannot handle */
SM_DeferEvent(sm, EVT_START, 0);

/* In on_entry of another state: recall the OLDEST deferred event (FIFO)
 * to the true front of the main queue -- processed before any backlog.
 * Returns false if none deferred or the main queue is full (event kept). */
SM_RecallEvent(sm);

/* Discard all deferred events */
SM_FlushDeferred(sm);
```

---

## Error Handling

Two enforced tiers and one informational: MINOR (recorded and queryable -- the framework takes no action, you decide what a minor error means), NORMAL (managed), CRITICAL (system lock).

```c
/* Report errors */
SM_Error_Report(sm, SM_ERROR_MINOR, 0x01);     /* recorded only */

/* MINOR is yours to act on. The framework stores the flag and the time; the
 * policy -- retry, degrade, escalate, ignore -- is the application's. */
uint32_t since;
if (SM_Error_IsMinorActive(sm) && SM_Error_GetMinorTimestamp(sm, &since)) {
    if ((SM_Platform_GetTimeMs() - since) > 500U) {
        SM_Error_ClearMinor(sm);   /* leaves any current error record intact */
    }
}
SM_Error_Report(sm, SM_ERROR_NORMAL, 0x10);    /* managed recovery */
SM_Error_Report(sm, SM_ERROR_CRITICAL, 0xFF);  /* system lock, requires reset */

/* Register recovery callback */
static bool my_recovery(SM_Handle_t sm, uint16_t error_code) {
    (void)sm;
    if (error_code == 0x10) {
        reinit_sensor();
        return true;   /* recovery succeeded */
    }
    return false;      /* recovery failed */
}
SM_Error_RegisterRecoveryCallback(sm, my_recovery);

/* Attempt recovery (calls registered callback) */
SM_Error_AttemptRecovery(sm);

/* Check critical lock (ISR-safe) */
if (SM_Error_IsCriticalLock(sm)) { /* system is locked */ }

/* Get stats */
SM_ErrorStats_t stats;
SM_Error_GetStats(sm, &stats);
```

---

## Debug Output

Requires `SM_FEATURE_DEBUG=1` (default on). Compile-time level stripping
via `SM_DEBUG_LEVEL` (0=off, 1=error, 2=+warn, 3=+info, 4=+verbose).

```c
/* Initialize debug output */
SM_Debug_Init(0);  /* interface ID 0 = UART, etc. */

/* Log macros (compiled out if level exceeds SM_DEBUG_LEVEL) */
SM_LOG_ERROR("fault detected: code=%u", code);
SM_LOG_WARN("battery low: %u mV", voltage);
SM_LOG_INFO("state entered: %u", SM_GetState(sm));
SM_LOG_VERBOSE("sensor raw: %u", adc_val);

/* Per-module tags (up to 16) */
static int8_t tag = -1;
tag = SM_Debug_RegisterTag("app_sensor");
SM_LOG_TAG(tag, 3, "temp=%d C", temp);    /* level 3 = info */

/* Runtime control */
SM_Debug_EnableLevel(4, false);           /* suppress verbose at runtime */
SM_Debug_EnableTag(tag, false);           /* suppress this module */

/* Hex dump */
SM_Debug_HexDump(buffer, 32);
```

---

## Configuration Reference

### Mandatory Defines

| Define | Description |
|--------|-------------|
| `SM_STATE_COUNT` | Number of application states |
| `SM_EVENT_COUNT` | Number of application events |

### Optional Defines (with defaults)

| Define | Default | Description |
|--------|---------|-------------|
| `SM_EVENT_QUEUE_SIZE` | 8 | Event ring buffer depth (1-64, 8 bytes/slot) |
| `SM_MAX_TRANSITIONS` | 32 | Runtime transition table capacity |
| `SM_ERROR_HISTORY_SIZE` | 8 | Error history ring depth (1-255) |
| `SM_ERROR_MAX_RECOVERY` | 3 | Max recovery attempts before escalation |
| `SM_STATE_HISTORY_DEPTH` | 4 | Recent state transition history depth |
| `SM_DEBUG_LEVEL` | 4 | Compile-time debug level (0-4) |
| `SM_DEBUG_BUFFER_SIZE` | 256 | Debug output buffer (bytes) |
| `SM_DEBUG_MSG_MAX_LEN` | 128 | Max single debug message length (bytes) |
| `SM_TASK_PERIOD_MS` | 10 | Expected SM_Process() call interval (ms) |
| `SM_DEFER_QUEUE_SIZE` | 4 | Deferred event queue depth (1-32) |
| `SM_HSM_MAX_DEPTH` | 6 | Max HSM nesting depth |

### Feature Flags

| Flag | Default | Description |
|------|---------|-------------|
| `SM_FEATURE_DEBUG` | 1 | Debug output subsystem |
| `SM_FEATURE_ASSERT` | 1 | Runtime assertions |
| `SM_FEATURE_TIME_EVENTS` | 1 | Time event subsystem |
| `SM_FEATURE_MAX_TIME_EVENTS` | 16 | Hard bound on time event list walk |
| `SM_FEATURE_DEFER` | 0 | Deferred event queue |
| `SM_FEATURE_HSM` | 0 | Hierarchical state machine support |
| `SM_FEATURE_RUNTIME_TRANSITIONS` | 0 | Runtime transition table modification |
| `SM_FEATURE_STATISTICS` | 0 | Transition/event/timeout counters |

---

## Safety and integration reminders

- **`SM_BOUNDED_LOOP_BEGIN` / `SM_BOUNDED_LOOP_END`** — Bounded `for` loop; after the loop, the framework checks that the counter did not exceed the declared maximum (normal exhaustion `0..N-1` is valid).
- **`SM_Process`** — Call from task/main loop only; **never** from ISR or **recursively** from state callbacks / guards / transition actions on the same `SM_Handle_t`.
- **Posting events** — Use **`SM_PostEvent`’s `bool` return** to know if the enqueue succeeded; avoid “if not full then post” patterns across ISR and thread (race).
- **Queue depth / full / empty** — Fine for diagnostics; not atomic with concurrent ISR posts (see README “Integration notes”).
- **Debug (`SM_Debug_*`)** — Runtime level mask and tag table are **process-global**, not per state machine instance.

---

## Platform Porting

Override these `SM_Platform_*` weak-symbol functions for your target.
Default implementations (in `sm_platform_weak.c`) provide simulation/desktop behavior.

### Required for any real target

| Function | Purpose |
|----------|---------|
| `SM_Platform_GetTimeMs(void)` | System time in ms (wraps at ~49.7 days) |
| `SM_Platform_EnterCritical(void)` | Disable interrupts (must support nesting) |
| `SM_Platform_ExitCritical(void)` | Re-enable interrupts |

### Debug output

| Function | Purpose |
|----------|---------|
| `SM_Platform_OutputInit(uint8_t interface)` | Init debug interface (UART, SPI, RTT) |
| `SM_Platform_OutputSend(const uint8_t *data, uint32_t len)` | Send debug bytes |

### Optional (no-op defaults)

| Function | Purpose |
|----------|---------|
| `SM_Platform_IsTimeout(uint32_t start, uint32_t timeout_ms)` | Timeout check with 32-bit wrap handling |
| `SM_Platform_GetCriticalNesting(void)` | Query critical section nesting depth |
| `SM_Platform_WatchdogKick/Start/Stop(...)` | Watchdog timer control |
| `SM_Platform_EnterSleep/ExitSleep(...)` | Low-power sleep modes |
| `SM_Platform_NVS_Write/Read(...)` | Non-volatile storage |
| `SM_Platform_GetResetReason(void)` | Last reset reason (POR, watchdog, etc.) |
| `SM_Platform_HasCapability(SM_PlatformCap_t cap)` | Runtime capability query |
| `SM_Platform_Assert(const char *module, int id)` | Assertion handler (numeric ID pattern) |

### STM32 HAL example (minimal)

```c
uint32_t SM_Platform_GetTimeMs(void) { return HAL_GetTick(); }
void SM_Platform_EnterCritical(void) { __disable_irq(); }
void SM_Platform_ExitCritical(void)  { __enable_irq(); }
```

---

**Build:**
```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON && cmake --build . && ctest
```

**Memory:** ~544 bytes RAM baseline. ~580 with deferred events. Zero heap.

**Standard:** C99. All headers have `extern "C"` guards for C++ compatibility.
