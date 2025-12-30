# Robust C State Machine Framework

## Overview

This is a production-ready, fully-featured state machine framework designed for embedded systems. It provides comprehensive error handling, debug messaging, and modular architecture suitable for various microcontrollers (STM32, ESP32, nRF52, etc.).

## Key Features

### ✅ State Machine Core
- **10 Pre-configured States**: INIT, IDLE, ACTIVE, PROCESSING, COMMUNICATING, MONITORING, CALIBRATING, DIAGNOSTICS, RECOVERY, CRITICAL_ERROR
- **Non-blocking Execution**: Designed for main loop or RTOS task integration
- **Atomic Event Handling**: Thread-safe event posting mechanism
- **Configurable Timeouts**: Per-state timeout management
- **State Callbacks**: Separate OnEntry, OnState, and OnExit handlers for each state

### 🛡️ Error Handling Engine
- **Three-tier Error System**:
  - **Minor Errors**: Auto-recovery with communication verification (e.g., lost packets)
  - **Normal Errors**: Managed recovery with retry logic (escalates to critical if failed)
  - **Critical Errors**: System lock requiring manual intervention or reset
- **Error History**: Circular buffer tracking last 16 errors
- **Configurable Recovery**: Customizable retry counts and timeouts
- **Example Recovery Logic**: Communication channel verification with good message counting

### 📡 Debug Messaging System
- **Multi-interface Support**: UART, SPI, I2C (easily extensible)
- **Message Categories**: INIT, RUNTIME, PERIODIC, ERROR, WARNING, INFO
- **Runtime Enable/Disable**: Individual control over message types for production
- **Formatted Output**: Timestamped messages with variable arguments (printf-style)
- **Low Overhead**: Messages filtered before formatting to minimize CPU usage

### 🔧 Modular Architecture
- **Chipset Agnostic**: Abstraction layer for different MCU families
- **Easy Integration**: Simple two-function API (Init and Task)
- **No Magic Numbers**: All configuration via macros at top of files
- **Doxygen Documentation**: Comprehensive inline documentation
- **Clear Section Separation**: Code organized into logical sections

## File Structure

```
app_main.h          - Header file with all structures, enums, and function prototypes
app_main.c          - Implementation of state machine, error handling, and debug system
main.c              - Integration examples for various platforms (STM32, ESP32, FreeRTOS, etc.)
```

## Quick Start

### 1. Basic Integration

```c
#include "app_main.h"

int main(void) {
    // Initialize your hardware
    // HAL_Init();
    // SystemClock_Config();
    
    // Initialize application
    if (!App_Main_Init()) {
        // Handle initialization failure
        while(1);
    }
    
    // Main loop
    while (1) {
        App_Main_Task();  // Non-blocking execution
        
        // HAL_Delay(10);  // Match SM_TASK_PERIOD_MS (10ms default)
    }
}
```

### 2. RTOS Integration

```c
void AppTask(void *pvParameters) {
    const TickType_t xPeriod = pdMS_TO_TICKS(10);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    App_Main_Init();
    
    while (1) {
        App_Main_Task();
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
```

### 3. Posting Events

```c
// From main code
StateMachine_PostEvent(EVENT_START);

// From interrupt handler
void Button_IRQ_Handler(void) {
    if (button_pressed) {
        StateMachine_PostEvent(EVENT_DATA_READY);
    }
}
```

## Configuration

All configuration is done via macros in `app_main.h`:

### State Machine Configuration
```c
#define SM_MAX_STATES                   (10U)     // Number of states
#define SM_STATE_TIMEOUT_MS             (5000U)   // Default timeout
#define SM_TASK_PERIOD_MS               (10U)     // Execution period
```

### Error Handling Configuration
```c
#define ERROR_MAX_RECOVERY_ATTEMPTS     (3U)      // Normal error retries
#define ERROR_MINOR_RETRY_COUNT         (3U)      // Minor error retries
#define ERROR_MINOR_TIMEOUT_MS          (50U)     // Minor error window
#define ERROR_HISTORY_SIZE              (16U)     // Error history buffer
```

### Debug Configuration
```c
#define DEBUG_ENABLE_INIT_MESSAGES      (1U)      // Init messages on/off
#define DEBUG_ENABLE_RUNTIME_MESSAGES   (1U)      // Runtime messages on/off
#define DEBUG_ENABLE_PERIODIC_MESSAGES  (1U)      // Periodic messages on/off
#define DEBUG_PERIODIC_INTERVAL_MS      (1000U)   // Periodic interval
```

### Communication Configuration
```c
#define COMM_RETRY_COUNT                (3U)      // Comm retry count
#define COMM_VERIFICATION_COUNT         (3U)      // Good messages needed
#define COMM_VERIFICATION_WINDOW_MS     (50U)     // Verification window
```

## State Descriptions

| State | Purpose | Timeout | Example Use |
|-------|---------|---------|-------------|
| INIT | System initialization | 5s | Hardware setup, self-test |
| IDLE | Low-power waiting | None | Wait for external trigger |
| ACTIVE | Active monitoring | None | Poll sensors, check conditions |
| PROCESSING | Data processing | 3s | Run algorithms, calculations |
| COMMUNICATING | Data transmission | 100ms | Send/receive packets |
| MONITORING | Health checks | None | Monitor temperatures, voltages |
| CALIBRATING | System calibration | 5s | Adjust offsets, calibrate sensors |
| DIAGNOSTICS | System diagnostics | 2s | Run self-tests |
| RECOVERY | Error recovery | 2s | Attempt to recover from errors |
| CRITICAL_ERROR | System lock | None | Critical failure - requires reset |

## Error Handling Flow

### Minor Error Example (Lost Packet)
```
1. Packet lost during communication
2. ErrorHandler_HandleMinorError() called
3. Attempts to verify channel (3 good messages in 50ms)
4. If successful: Auto-recovery, continue operation
5. If timeout: Escalates to Normal Error
```

### Normal Error Example (Timeout)
```
1. State timeout or resource unavailable
2. ErrorHandler_HandleNormalError() called
3. Transitions to RECOVERY state
4. Attempts recovery up to 3 times
5. If successful: Returns to IDLE
6. If failed: Escalates to Critical Error
```

### Critical Error Example (Hardware Fault)
```
1. Hardware fault detected
2. ErrorHandler_HandleCriticalError() called
3. System immediately locks in CRITICAL_ERROR state
4. All outputs disabled, safe mode activated
5. Requires manual reset or watchdog timeout
```

## Debug Message Examples

```c
// Initialization message
Debug_SendMessage(DEBUG_MSG_INIT, "System starting, version %d.%d", 1, 0);

// Runtime message
Debug_SendMessage(DEBUG_MSG_RUNTIME, "Processing %d samples", count);

// Error message
Debug_SendMessage(DEBUG_MSG_ERROR, "Communication failed: %s", 
                 ErrorCodeToString(error_code));

// Periodic status (auto-generated every 1s)
// "Status: State=ACTIVE, Executions=1234"
```

## Platform-Specific Implementation

### STM32 HAL
Replace placeholder functions in `app_main.c`:
```c
uint32_t GetCurrentTimeMs(void) {
    return HAL_GetTick();
}

bool Comm_UART_Init(void) {
    return HAL_UART_Init(&huart1) == HAL_OK;
}

uint32_t Comm_UART_Send(const uint8_t* data, uint32_t length) {
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}
```

### ESP32
```c
uint32_t GetCurrentTimeMs(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
```

### FreeRTOS
```c
uint32_t GetCurrentTimeMs(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}
```

## Customizing States

### Adding a New State

1. Add to enum in `app_main.h`:
```c
typedef enum {
    // ... existing states ...
    STATE_MY_NEW_STATE,
    STATE_MAX
} StateMachineState_t;
```

2. Implement callbacks in `app_main.c`:
```c
static void State_MyNewState_OnEntry(void) {
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering MY_NEW_STATE");
    // Initialize state resources
}

static void State_MyNewState_OnState(void) {
    // Execute state logic
    if (condition_met) {
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_MyNewState_OnExit(void) {
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting MY_NEW_STATE");
    // Clean up resources
}
```

3. Configure in `InitializeStateTable()`:
```c
g_state_table[STATE_MY_NEW_STATE].state_id = STATE_MY_NEW_STATE;
g_state_table[STATE_MY_NEW_STATE].on_entry = State_MyNewState_OnEntry;
g_state_table[STATE_MY_NEW_STATE].on_state = State_MyNewState_OnState;
g_state_table[STATE_MY_NEW_STATE].on_exit = State_MyNewState_OnExit;
g_state_table[STATE_MY_NEW_STATE].timeout_ms = 2000U;
g_state_table[STATE_MY_NEW_STATE].transitions[0] = 
    (StateTransition_t){EVENT_PROCESSING_DONE, STATE_ACTIVE};
g_state_table[STATE_MY_NEW_STATE].transition_count = 1;
```

## Best Practices

### ✅ DO:
- Keep state callbacks lightweight and non-blocking
- Use timeouts for all states that can potentially hang
- Post events from interrupts, not direct state changes
- Enable only necessary debug messages in production
- Test error recovery paths thoroughly
- Use the periodic debug messages for health monitoring
- Feed watchdog in main loop, not in critical error state

### ❌ DON'T:
- Block in state callbacks (no delays or infinite loops)
- Post multiple events rapidly (only one pending event allowed)
- Ignore minor errors - they may indicate system issues
- Leave all debug messages enabled in production
- Modify state machine context directly (use provided APIs)
- Attempt recovery from critical errors automatically

## Memory Usage

Approximate memory requirements (typical configuration):

- **RAM**: ~1.5 KB
  - State machine context: ~400 bytes
  - Error history: ~320 bytes
  - State table: ~600 bytes
  - Debug buffers: ~256 bytes
  
- **Flash**: ~8-12 KB (depending on optimization)
  - Core state machine: ~3 KB
  - Error handling: ~2 KB
  - Debug system: ~2 KB
  - State callbacks: ~3 KB

## Testing

### Simulation Mode
Compile with `-DUSE_SIMULATION` to run on PC:
```bash
gcc -DUSE_SIMULATION main.c app_main.c -o state_machine_test
./state_machine_test
```

### Unit Testing Hooks
Key functions to test:
```c
StateMachine_PostEvent()       // Event handling
ErrorHandler_Report()          // Error reporting
CheckStateTransition()         // Transition logic
IsTimeout()                    // Timeout calculation
```

## Troubleshooting

### State Machine Not Transitioning
- Check that events are being posted correctly
- Verify transition table has entry for that event
- Ensure timeout is appropriate for state
- Check if critical error lock is active

### Debug Messages Not Appearing
- Verify interface is initialized (UART/SPI/I2C)
- Check message type is enabled (Init/Runtime/Periodic)
- Confirm debug buffer size is adequate
- Verify baud rate and pin configuration

### Stuck in Recovery State
- Check error retry count configuration
- Verify recovery logic for specific error type
- Ensure timeout is reasonable (default 2s)
- Check if error is continuously being reported

### Memory Issues
- Reduce ERROR_HISTORY_SIZE if needed
- Decrease DEBUG_BUFFER_SIZE
- Optimize state callback implementations
- Enable compiler optimizations (-Os or -O2)

## License

This framework is provided as-is for embedded systems development. Modify as needed for your application.

## Version History

- **v1.0.0** (2025-10-25)
  - Initial release
  - 10 pre-configured states
  - Three-tier error handling
  - Multi-interface debug system
  - Complete Doxygen documentation

## Contact & Support

For questions, improvements, or bug reports, please refer to your development team's internal processes.

---

**Built with attention to embedded systems best practices**
- Non-blocking design
- Atomic operations
- Configurable timeouts
- Comprehensive error handling
- Production-ready debug system
