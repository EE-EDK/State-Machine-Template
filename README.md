# State Machine Framework v2.0

**Production-ready state machine framework for embedded C/C++ systems**

Truly modular • Platform-agnostic • Thread-safe • Lean footprint (~1.5KB RAM, ~6KB Flash)

---

## Quick Start (3 Steps)

```bash
# 1. Build
mkdir build && cd build && cmake .. && make

# 2. Run example
./examples/basic_example

# 3. Use in your project
```

```c
#include "sm_framework/sm_framework.h"

int main(void) {
    App_Main_Init(COMM_INTERFACE_UART);
    while (1) {
        App_Main_Task();  // Call every 10ms
        delay_ms(10);
    }
}
```

**That's it!** See [Quick-Guide.md](Quick-Guide.md) for detailed usage.

---

## Features

### State Machine Core
- **10 Pre-configured States**: INIT → IDLE → ACTIVE → PROCESSING → COMMUNICATING → MONITORING → CALIBRATING → DIAGNOSTICS → RECOVERY → CRITICAL_ERROR
- **Event-Driven Architecture**: Thread-safe event posting (ISR-safe)
- **Non-Blocking Execution**: Designed for main loop or RTOS integration
- **Automatic Timeouts**: Per-state timeout with configurable recovery
- **State Callbacks**: OnEntry, OnState, OnExit for each state

### Error Handling (3-Tier System)
- **MINOR**: Auto-recovery without state change (e.g., lost packet)
- **NORMAL**: Managed recovery via RECOVERY state with retry limit
- **CRITICAL**: System lock requiring manual reset/watchdog
- **Error History**: Circular buffer tracks last 16 errors
- **Custom Handlers**: Register your own recovery functions

### Debug System
- **Multi-Interface**: UART, SPI, I2C, USB, RTT (extensible)
- **Message Categories**: INIT, RUNTIME, PERIODIC, ERROR, WARNING, INFO
- **Runtime Control**: Enable/disable message types individually
- **Printf-Style**: `Debug_SendMessage(DEBUG_MSG_INFO, "Temp: %d", temp)`
- **Low Overhead**: Messages filtered before formatting
- **Compile-Time Removal**: Set DEBUG_LEVEL to strip messages in production

### Platform Abstraction
- **Minimal Interface**: Only 5 functions required
- **Weak Symbols**: Default implementations automatically overridden
- **Any MCU**: STM32, ESP32, RP2040, nRF52, AVR, PIC, etc.
- **Thread-Safe Primitives**: Critical section support for RTOS

---

## Architecture

```
State-Machine-Framework/
├── include/sm_framework/      # Public API (include this)
│   ├── sm_framework.h         # Main header
│   ├── sm_state_machine.h     # State machine API
│   ├── sm_error_handler.h     # Error handling API
│   ├── sm_debug.h             # Debug system API
│   ├── sm_platform.h          # Platform HAL interface
│   ├── sm_types.h             # Type definitions
│   └── sm_config.h            # Configuration defaults
│
├── src/
│   ├── core/                  # Platform-independent
│   │   ├── sm_state_machine.c
│   │   ├── sm_error_handler.c
│   │   └── sm_debug.c
│   ├── platform/              # HAL abstraction
│   │   └── sm_platform_weak.c # Default (weak) implementations
│   └── app/
│       └── app_main.c         # Application glue
│
├── examples/                  # Working examples
├── config/                    # Configuration templates
└── Quick-Guide.md             # Quick reference guide
```

### Design Principles
✅ **Separation of Concerns**: Core logic separated from platform/app code
✅ **Dependency Inversion**: Core depends on abstractions (platform.h), not implementations
✅ **Single Responsibility**: Each module has one clear purpose
✅ **Open/Closed**: Extensible via platform layer without modifying framework

---

## Porting to Your Platform

### Required Functions (Only 5!)

Create `platform_impl.c` with these functions:

```c
#include "sm_framework/sm_platform.h"
#include "your_mcu_hal.h"

// 1. Get millisecond timestamp
uint32_t Platform_GetTimeMs(void) {
    return HAL_GetTick();  // STM32 example
}

// 2-3. Thread safety (disable/enable interrupts)
void Platform_EnterCritical(void) {
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __enable_irq();
}

// 4-5. Debug output (UART example)
bool Platform_UART_Init(void) {
    // Initialize your UART peripheral
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}
```

### Platform Examples

**STM32 with HAL:**
```c
uint32_t Platform_GetTimeMs(void) { return HAL_GetTick(); }
void Platform_EnterCritical(void) { __disable_irq(); }
void Platform_ExitCritical(void) { __enable_irq(); }
```

**ESP32 with ESP-IDF:**
```c
uint32_t Platform_GetTimeMs(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
void Platform_EnterCritical(void) { taskENTER_CRITICAL(); }
void Platform_ExitCritical(void) { taskEXIT_CRITICAL(); }
```

**RP2040 with Pico SDK:**
```c
uint32_t Platform_GetTimeMs(void) {
    return to_ms_since_boot(get_absolute_time());
}
void Platform_EnterCritical(void) { __disable_irq(); }
void Platform_ExitCritical(void) { __enable_irq(); }
```

**FreeRTOS:**
```c
uint32_t Platform_GetTimeMs(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}
void Platform_EnterCritical(void) { taskENTER_CRITICAL(); }
void Platform_ExitCritical(void) { taskEXIT_CRITICAL(); }
```

---

## State Machine Flow

```
                    INIT
                      │
            (init complete)
                      ↓
   ┌─────────────→  IDLE  ←──────────────┐
   │                  │                   │
   │             (start)              (stop)
   │                  ↓                   │
   │                ACTIVE ───────────────┤
   │                  │                   │
   │           (data ready)               │
   │                  ↓                   │
   │             PROCESSING               │
   │                  │                   │
   │           (processing done)          │
   │                  ↓                   │
   │            COMMUNICATING             │
   │                  │                   │
   │            (comm complete)           │
   │                  ↓                   │
   │              MONITORING ─────────────┘
   │                  │
   │         (any error occurs)
   │                  ↓
   └─────────────  RECOVERY
   (success)          │
                 (failed/timeout)
                      ↓
                CRITICAL_ERROR
                  (locked)
```

### Error Recovery Flow
```
Minor Error (e.g., lost packet)
    → Auto-verify channel (3 good msgs in 50ms)
    → If OK: Continue in current state
    → If timeout: Escalate to Normal Error

Normal Error (e.g., timeout)
    → Transition to RECOVERY state
    → Attempt recovery (max 3 attempts)
    → If successful: Return to IDLE
    → If failed: Escalate to Critical Error

Critical Error (e.g., hardware fault)
    → Immediate transition to CRITICAL_ERROR
    → System locked (requires reset)
    → Watchdog should trigger or manual reset
```

---

## Configuration

### Using the Template

```bash
# Copy configuration template
cp config/app_config_template.h your_project/app_config.h

# Edit values for your project
# Then include before framework
```

```c
#include "app_config.h"           // Your custom config
#include "sm_framework/sm_framework.h"
```

### Key Options

```c
/* State machine timing */
#define SM_TASK_PERIOD_MS (10U)          // Task execution period
#define SM_STATE_TIMEOUT_MS (5000U)      // Default state timeout

/* Error handling */
#define ERROR_MAX_RECOVERY_ATTEMPTS (3U) // Recovery retry limit
#define ERROR_HISTORY_SIZE (16U)         // Error log entries

/* Debug system */
#define DEBUG_ENABLE_INIT_MESSAGES (1U)     // Startup messages
#define DEBUG_ENABLE_RUNTIME_MESSAGES (0U)  // Verbose logs (disable for production)
#define DEBUG_ENABLE_PERIODIC_MESSAGES (1U) // Periodic status
#define DEBUG_BUFFER_SIZE (256U)            // Debug buffer size

/* Memory optimization */
#define FEATURE_STATISTICS_ENABLED (0U)  // Disable for production
#define FEATURE_ASSERT_ENABLED (1U)      // Enable for debug builds
```

---

## API Reference

### Core Functions

```c
/* Initialize framework */
bool App_Main_Init(CommInterface_t interface);

/* Execute state machine (call every SM_TASK_PERIOD_MS) */
void App_Main_Task(void);
```

### State Machine

```c
/* Post event (thread-safe, can call from ISR) */
bool StateMachine_PostEvent(StateMachineEvent_t event);

/* Query state */
StateMachineState_t StateMachine_GetCurrentState(void);
StateMachineState_t StateMachine_GetPreviousState(void);
uint32_t StateMachine_GetStateTime(void);  // Time in current state
```

### Error Handling

```c
/* Report errors */
ErrorHandler_Report(ERROR_LEVEL_MINOR, ERROR_CODE_COMM_LOST);
ErrorHandler_Report(ERROR_LEVEL_NORMAL, ERROR_CODE_TIMEOUT);
ErrorHandler_Report(ERROR_LEVEL_CRITICAL, ERROR_CODE_HARDWARE_FAULT);

/* Query errors */
bool ErrorHandler_IsCriticalLock(void);
bool ErrorHandler_GetCurrentError(ErrorInfo_t *error);
```

### Debug Messages

```c
/* Printf-style messaging */
Debug_SendMessage(DEBUG_MSG_INFO, "Temperature: %d C", temp);
Debug_SendMessage(DEBUG_MSG_ERROR, "Failed: %s", ErrorCodeToString(code));
Debug_SendMessage(DEBUG_MSG_WARNING, "Battery low: %d%%", level);

/* Control output */
Debug_EnableRuntimeMessages(false);   // Reduce verbosity
Debug_SetInterface(COMM_INTERFACE_RTT);  // Change output
```

---

## Integration Examples

### Bare Metal (STM32)
```c
int main(void) {
    HAL_Init();
    SystemClock_Config();

    App_Main_Init(COMM_INTERFACE_UART);

    while (1) {
        App_Main_Task();
        HAL_Delay(10);  // 10ms period
    }
}
```

### FreeRTOS Task
```c
void StateMachineTask(void *pvParameters) {
    App_Main_Init(COMM_INTERFACE_UART);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(10);

    while (1) {
        App_Main_Task();
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
```

### Event Posting from ISR
```c
void Button_IRQHandler(void) {
    if (button_pressed) {
        StateMachine_PostEvent(EVENT_START);  // Thread-safe!
    }
    clear_interrupt_flag();
}

void UART_RxComplete_Callback(void) {
    StateMachine_PostEvent(EVENT_DATA_READY);
}
```

---

## Memory Usage

**Typical Configuration** (16 error history, 256-byte debug buffer):

| Component | RAM | Flash |
|-----------|-----|-------|
| State machine context | ~400 bytes | ~2 KB |
| Error handler | ~320 bytes | ~2 KB |
| State table | ~600 bytes | ~2 KB |
| Debug system | ~100 bytes | ~2 KB |
| **Total** | **~1.5 KB** | **~6-8 KB** |

**Optimization Tips:**
- Reduce `ERROR_HISTORY_SIZE` to 4 (saves ~240 bytes RAM)
- Reduce `DEBUG_BUFFER_SIZE` to 128 (saves ~128 bytes RAM)
- Disable `FEATURE_STATISTICS_ENABLED` (saves ~100 bytes RAM)
- Use `-Os` optimization (reduces Flash by 20-30%)

---

## Build System

### CMake Options

```bash
# Platform selection
cmake .. -DSM_PLATFORM=SIMULATION  # Default (uses weak symbols)
cmake .. -DSM_PLATFORM=CUSTOM      # Your platform implementation

# Build configuration
cmake .. -DCMAKE_BUILD_TYPE=Release  # Optimize for size (-Os)
cmake .. -DCMAKE_BUILD_TYPE=Debug    # Debug symbols

# Feature flags
cmake .. -DENABLE_STATISTICS=ON   # Runtime statistics
cmake .. -DENABLE_ASSERTS=OFF     # Disable assertions
cmake .. -DBUILD_EXAMPLES=OFF     # Skip examples
```

### Integration

**Add to your CMake project:**
```cmake
add_subdirectory(path/to/State-Machine-Framework)
target_link_libraries(your_firmware sm_framework)
```

**Or use as library:**
```bash
cd State-Machine-Framework
mkdir build && cd build
cmake .. && make
# Link with libsm_framework.a
```

---

## Testing

### Run Examples
```bash
./examples/basic_example        # Basic demonstration
./examples/simulation_example   # Advanced testing
```

### Expected Output
```
[1] === State Machine Framework v2.0.0 ===
[2] Initializing...
[14] Initialization complete after 5 steps
[16] Event INIT_COMPLETE triggers transition INIT -> IDLE
[38] Event START triggers transition IDLE -> ACTIVE
✓ State transitions working
```

---

## What's New in v2.0

**Complete rewrite from v1.0** - See `docs/REVIEW_FINDINGS.md` for details.

### Critical Fixes
- ✅ Fixed hardcoded array sizes (now use config constants)
- ✅ Added `volatile` for ISR-shared data (proper thread safety)
- ✅ Fixed broken error recovery (all states can now recover)
- ✅ Added C++ compatibility (`extern "C"` guards)
- ✅ Removed static variables from callbacks (now reentrant)

### Major Improvements
- ✅ Fully modular architecture (core/platform/app separation)
- ✅ Platform abstraction layer with weak symbols
- ✅ Thread-safe event posting with critical sections
- ✅ Professional CMake build system
- ✅ Comprehensive Doxygen documentation
- ✅ Working, tested examples

---

## Troubleshooting

### Build Issues
```bash
# Missing headers
export C_INCLUDE_PATH=/path/to/include:$C_INCLUDE_PATH

# Link errors
# Make sure you link with sm_framework library
target_link_libraries(your_target sm_framework)
```

### Runtime Issues

**State machine not transitioning:**
- Check event is posted: `StateMachine_PostEvent()` returns true
- Verify transition exists in state table
- Ensure `App_Main_Task()` is called regularly
- Check if critical error lock is active

**Debug messages not appearing:**
- Verify `Platform_UART_Init()` succeeded
- Check message type is enabled: `Debug_EnableRuntimeMessages(true)`
- Confirm UART baud rate matches your terminal
- Verify pins are configured correctly

**System stuck in recovery:**
- Check error retry count: `ERROR_MAX_RECOVERY_ATTEMPTS`
- Verify recovery logic for specific error type
- Ensure timeout is reasonable (default 2s)
- Check if error is being continuously reported

---

## Best Practices

### ✅ DO:
- Call `App_Main_Task()` every `SM_TASK_PERIOD_MS` milliseconds
- Post events from interrupts (it's thread-safe)
- Use timeouts for all states that can block
- Disable verbose debug messages in production
- Feed watchdog in main loop (not in critical error state)
- Keep state callbacks short and non-blocking

### ❌ DON'T:
- Block in state callbacks (no `delay()` or infinite loops)
- Post multiple events rapidly (only one pending allowed)
- Modify state machine context directly (use API functions)
- Ignore minor errors (they may indicate issues)
- Leave all debug messages enabled in production
- Attempt automatic recovery from critical errors

---

## Support

**Documentation:**
- [Quick-Guide.md](Quick-Guide.md) - Quick reference and examples
- `include/sm_framework/*.h` - API documentation (Doxygen)
- `examples/` - Working code examples

**Platform Porting:**
- See [Quick-Guide.md](Quick-Guide.md) - Platform Implementation section
- Default weak implementations in `src/platform/sm_platform_weak.c`
- Override only what you need

**Issues:**
- Check examples compile and run first
- Verify platform functions are implemented
- Enable debug messages to trace execution

---

## License

MIT License - Free for commercial and open-source projects.

---

## Version History

**v2.0.0** (2025-12-30)
- Complete framework rewrite for modularity and portability
- Fixed 30+ issues from v1.0
- Added platform abstraction layer
- Professional build system
- Comprehensive documentation
- Working examples

**v1.0.0** (2025-10-25)
- Initial release
- Basic state machine implementation

---

**Built for embedded systems. Designed for reliability. Ready for production.**
