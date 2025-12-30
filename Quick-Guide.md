# Quick Guide - State Machine Framework

**Practical reference for getting started and everyday use**

---

## Table of Contents

1. [5-Minute Quick Start](#5-minute-quick-start)
2. [Basic Operations](#basic-operations)
3. [Platform Implementation](#platform-implementation)
4. [Common Patterns](#common-patterns)
5. [Configuration Guide](#configuration-guide)
6. [API Cheat Sheet](#api-cheat-sheet)
7. [Debugging Tips](#debugging-tips)
8. [FAQ](#faq)

---

## 5-Minute Quick Start

### Step 1: Get the Code
```bash
git clone <repository>
cd State-Machine-Template
```

### Step 2: Build
```bash
mkdir build && cd build
cmake ..
make -j4
```

### Step 3: Run Example
```bash
./examples/basic_example
```

### Step 4: Create Your Application
```c
#include "sm_framework/sm_framework.h"

int main(void) {
    /* Your hardware init */
    HAL_Init();
    SystemClock_Config();

    /* Framework init */
    App_Main_Init(COMM_INTERFACE_UART);

    /* Main loop */
    while (1) {
        App_Main_Task();
        HAL_Delay(10);  // Match SM_TASK_PERIOD_MS
    }
}
```

**Done!** The state machine is running.

---

## Basic Operations

### Initialize Framework
```c
/* Simple initialization */
if (!App_Main_Init(COMM_INTERFACE_UART)) {
    /* Handle error */
    while(1);
}

/* Or with custom debug interface */
App_Main_Init(COMM_INTERFACE_SPI);  // Use SPI
App_Main_Init(COMM_INTERFACE_RTT);  // Use SEGGER RTT
```

### Post Events (Thread-Safe!)
```c
/* From main code */
StateMachine_PostEvent(EVENT_START);
StateMachine_PostEvent(EVENT_DATA_READY);
StateMachine_PostEvent(EVENT_STOP);

/* From interrupt handler */
void Button_IRQHandler(void) {
    if (button_pressed) {
        StateMachine_PostEvent(EVENT_START);  // Safe from ISR
    }
    clear_interrupt_flag();
}
```

### Query State
```c
/* Get current state */
StateMachineState_t state = StateMachine_GetCurrentState();
if (state == STATE_ACTIVE) {
    // Do something
}

/* Get previous state */
StateMachineState_t prev = StateMachine_GetPreviousState();

/* Get time in current state */
uint32_t time_ms = StateMachine_GetStateTime();
```

### Report Errors
```c
/* Minor error - auto-recovery */
ErrorHandler_Report(ERROR_LEVEL_MINOR, ERROR_CODE_COMM_LOST);

/* Normal error - managed recovery */
ErrorHandler_Report(ERROR_LEVEL_NORMAL, ERROR_CODE_TIMEOUT);

/* Critical error - system lock */
ErrorHandler_Report(ERROR_LEVEL_CRITICAL, ERROR_CODE_HARDWARE_FAULT);
```

### Debug Messages
```c
/* Different message types */
Debug_SendMessage(DEBUG_MSG_INIT, "System starting");
Debug_SendMessage(DEBUG_MSG_INFO, "Temperature: %d C", temp);
Debug_SendMessage(DEBUG_MSG_WARNING, "Low battery: %d%%", battery);
Debug_SendMessage(DEBUG_MSG_ERROR, "Error: %s", ErrorCodeToString(code));

/* Control message types */
Debug_EnableRuntimeMessages(false);  // Reduce verbosity
Debug_EnablePeriodicMessages(true);  // Status updates
```

---

## Platform Implementation

### Minimal Implementation (5 Functions)

Create `platform_impl.c` in your project:

```c
#include "sm_framework/sm_platform.h"
#include "your_mcu_hal.h"  // Your HAL

/* 1. TIME: Get milliseconds since boot */
uint32_t Platform_GetTimeMs(void)
{
    return HAL_GetTick();  // STM32 HAL example
}

/* 2. CRITICAL SECTION: Disable interrupts */
void Platform_EnterCritical(void)
{
    __disable_irq();
}

/* 3. CRITICAL SECTION: Enable interrupts */
void Platform_ExitCritical(void)
{
    __enable_irq();
}

/* 4. UART: Initialize debug output */
bool Platform_UART_Init(void)
{
    // Already initialized by CubeMX/your setup
    return true;
}

/* 5. UART: Send debug data */
uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}
```

### Platform-Specific Examples

#### STM32 with STM32CubeMX HAL
```c
extern UART_HandleTypeDef huart1;  // From main.c

uint32_t Platform_GetTimeMs(void) {
    return HAL_GetTick();
}

void Platform_EnterCritical(void) {
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __enable_irq();
}

bool Platform_UART_Init(void) {
    return true;  // CubeMX already initialized
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}
```

#### ESP32 with ESP-IDF
```c
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t Platform_GetTimeMs(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void Platform_EnterCritical(void) {
    taskENTER_CRITICAL();
}

void Platform_ExitCritical(void) {
    taskEXIT_CRITICAL();
}

bool Platform_UART_Init(void) {
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    return uart_write_bytes(UART_NUM_0, (const char *)data, length);
}
```

#### RP2040 with Pico SDK
```c
#include "pico/stdlib.h"
#include "hardware/uart.h"

uint32_t Platform_GetTimeMs(void) {
    return to_ms_since_boot(get_absolute_time());
}

void Platform_EnterCritical(void) {
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __enable_irq();
}

bool Platform_UART_Init(void) {
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);  // TX
    gpio_set_function(1, GPIO_FUNC_UART);  // RX
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    uart_write_blocking(uart0, data, length);
    return length;
}
```

#### Bare Metal with SysTick
```c
static volatile uint32_t g_systick_ms = 0;

void SysTick_Handler(void) {
    g_systick_ms++;
}

uint32_t Platform_GetTimeMs(void) {
    return g_systick_ms;
}

void Platform_EnterCritical(void) {
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __enable_irq();
}
```

#### FreeRTOS
```c
uint32_t Platform_GetTimeMs(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void Platform_EnterCritical(void) {
    taskENTER_CRITICAL();
}

void Platform_ExitCritical(void) {
    taskEXIT_CRITICAL();
}
```

---

## Common Patterns

### Pattern 1: Wait for State
```c
void WaitForIdle(void) {
    while (StateMachine_GetCurrentState() != STATE_IDLE) {
        App_Main_Task();
        HAL_Delay(10);
    }
}
```

### Pattern 2: Safe Shutdown
```c
void SafeShutdown(void) {
    /* Request stop */
    StateMachine_PostEvent(EVENT_STOP);

    /* Wait for IDLE with timeout */
    uint32_t start = Platform_GetTimeMs();
    while (StateMachine_GetCurrentState() != STATE_IDLE) {
        App_Main_Task();

        if (Platform_IsTimeout(start, 5000)) {
            /* Force reset if timeout */
            StateMachine_Reset();
            break;
        }
    }
}
```

### Pattern 3: Trigger Processing
```c
void ProcessData(uint8_t *data, uint32_t length) {
    /* Store data somewhere accessible to state machine */
    memcpy(g_data_buffer, data, length);
    g_data_length = length;

    /* Trigger processing */
    StateMachine_PostEvent(EVENT_DATA_READY);
}
```

### Pattern 4: Monitor System Health
```c
void MonitorSystem(void) {
    /* Check temperature */
    if (temperature > MAX_TEMP) {
        ErrorHandler_Report(ERROR_LEVEL_NORMAL, ERROR_CODE_HARDWARE_FAULT);
    }

    /* Check battery */
    if (battery_voltage < MIN_VOLTAGE) {
        ErrorHandler_Report(ERROR_LEVEL_CRITICAL, ERROR_CODE_HARDWARE_FAULT);
    }

    /* Check communication */
    if (comm_packet_lost) {
        ErrorHandler_Report(ERROR_LEVEL_MINOR, ERROR_CODE_COMM_LOST);
    }
}
```

### Pattern 5: RTOS Task
```c
void StateMachineTask(void *pvParameters) {
    App_Main_Init(COMM_INTERFACE_UART);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(10);  // 10ms

    while (1) {
        App_Main_Task();
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* In main() */
xTaskCreate(StateMachineTask, "SM", 512, NULL, 2, NULL);
```

---

## Configuration Guide

### Step 1: Copy Template
```bash
cp config/app_config_template.h your_project/app_config.h
```

### Step 2: Edit Values
```c
/* app_config.h */

/* Fast response system */
#define SM_TASK_PERIOD_MS (1U)           // 1ms task period
#define SM_STATE_TIMEOUT_MS (1000U)      // 1s timeout

/* Normal system */
#define SM_TASK_PERIOD_MS (10U)          // 10ms task period
#define SM_STATE_TIMEOUT_MS (5000U)      // 5s timeout

/* Low power system */
#define SM_TASK_PERIOD_MS (100U)         // 100ms task period
#define SM_STATE_TIMEOUT_MS (30000U)     // 30s timeout
```

### Step 3: Memory Optimization
```c
/* Constrained system (< 8KB RAM) */
#define ERROR_HISTORY_SIZE (4U)          // Minimal history
#define DEBUG_BUFFER_SIZE (128U)         // Smaller buffer
#define DEBUG_MAX_MESSAGE_LENGTH (64U)   // Shorter messages
#define FEATURE_STATISTICS_ENABLED (0U)  // Disable stats

/* Normal system (16KB+ RAM) */
#define ERROR_HISTORY_SIZE (16U)
#define DEBUG_BUFFER_SIZE (256U)
#define DEBUG_MAX_MESSAGE_LENGTH (128U)
#define FEATURE_STATISTICS_ENABLED (0U)

/* Debug build */
#define ERROR_HISTORY_SIZE (32U)         // More history
#define DEBUG_BUFFER_SIZE (512U)         // Bigger buffer
#define FEATURE_STATISTICS_ENABLED (1U)  // Enable stats
#define FEATURE_ASSERT_ENABLED (1U)      // Enable asserts
```

### Step 4: Production Settings
```c
/* Disable verbose debug output */
#define DEBUG_ENABLE_INIT_MESSAGES (0U)
#define DEBUG_ENABLE_RUNTIME_MESSAGES (0U)
#define DEBUG_ENABLE_PERIODIC_MESSAGES (1U)  // Keep status updates

/* Optimize for reliability */
#define ERROR_MAX_RECOVERY_ATTEMPTS (5U)     // More retries

/* Disable development features */
#define FEATURE_STATISTICS_ENABLED (0U)
#define FEATURE_ASSERT_ENABLED (0U)
```

### Step 5: Include in Your Code
```c
#include "app_config.h"           // Your configuration
#include "sm_framework/sm_framework.h"  // Framework
```

---

## API Cheat Sheet

### Initialization
```c
bool App_Main_Init(CommInterface_t interface);
void App_Main_Task(void);
```

### State Machine
```c
bool StateMachine_PostEvent(StateMachineEvent_t event);
StateMachineState_t StateMachine_GetCurrentState(void);
StateMachineState_t StateMachine_GetPreviousState(void);
uint32_t StateMachine_GetStateTime(void);
void StateMachine_Reset(void);
```

### Error Handling
```c
bool ErrorHandler_Report(ErrorLevel_t level, ErrorCode_t code);
bool ErrorHandler_IsCriticalLock(void);
bool ErrorHandler_GetCurrentError(ErrorInfo_t *error);
void ErrorHandler_ClearError(void);
```

### Debug
```c
void Debug_SendMessage(DebugMessageType_t type, const char *fmt, ...);
void Debug_EnableRuntimeMessages(bool enable);
void Debug_SetInterface(CommInterface_t interface);
```

### Utilities
```c
const char *StateMachine_StateToString(StateMachineState_t state);
const char *ErrorHandler_CodeToString(ErrorCode_t code);
bool Platform_IsTimeout(uint32_t start, uint32_t timeout_ms);
```

### Events
```c
EVENT_NONE
EVENT_INIT_COMPLETE
EVENT_START
EVENT_STOP
EVENT_DATA_READY
EVENT_PROCESSING_DONE
EVENT_COMM_REQUEST
EVENT_COMM_COMPLETE
EVENT_TIMEOUT
EVENT_ERROR_MINOR
EVENT_ERROR_NORMAL
EVENT_ERROR_CRITICAL
EVENT_RECOVERY_SUCCESS
EVENT_RECOVERY_FAILED
```

### States
```c
STATE_INIT
STATE_IDLE
STATE_ACTIVE
STATE_PROCESSING
STATE_COMMUNICATING
STATE_MONITORING
STATE_CALIBRATING
STATE_DIAGNOSTICS
STATE_RECOVERY
STATE_CRITICAL_ERROR
```

---

## Debugging Tips

### Enable Debug Output
```c
/* At compile time */
#define DEBUG_ENABLE_RUNTIME_MESSAGES (1U)

/* At runtime */
Debug_EnableRuntimeMessages(true);
Debug_EnablePeriodicMessages(true);
```

### Trace State Transitions
```c
/* Framework automatically logs:
   "[timestamp] State transition: IDLE -> ACTIVE"
   "[timestamp] Event START triggers transition IDLE -> ACTIVE"
*/

/* Or query manually */
Debug_SendMessage(DEBUG_MSG_INFO, "Current state: %s",
                 StateMachine_StateToString(StateMachine_GetCurrentState()));
```

### Monitor Error History
```c
ErrorInfo_t error;
if (ErrorHandler_GetCurrentError(&error)) {
    Debug_SendMessage(DEBUG_MSG_ERROR,
        "Active error: %s (retry: %d, recovered: %d)",
        ErrorHandler_CodeToString(error.code),
        error.retry_count,
        error.is_recovered);
}
```

### Check Periodic Status
```c
/* Automatic every DEBUG_PERIODIC_INTERVAL_MS:
   "[timestamp] State=ACTIVE Exec=1234"
*/
```

### Use SEGGER RTT (Best for Real-Time)
```c
/* In platform_impl.c */
#include "SEGGER_RTT.h"

bool Platform_RTT_Init(void) {
    SEGGER_RTT_Init();
    return true;
}

uint32_t Platform_RTT_Send(const uint8_t *data, uint32_t length) {
    SEGGER_RTT_Write(0, data, length);
    return length;
}

/* In main.c */
App_Main_Init(COMM_INTERFACE_RTT);
```

---

## FAQ

**Q: Can I call StateMachine_PostEvent() from an interrupt?**
A: Yes! It's thread-safe and uses critical sections.

**Q: What happens if I post an event while one is pending?**
A: The new event is dropped and the function returns false. Only one pending event allowed.

**Q: Can state callbacks block (use delays)?**
A: NO! Callbacks must be non-blocking. Use execution counts or timeouts instead.

**Q: How do I implement a delay in a state?**
A: Use state execution count or time checks:
```c
if (StateMachine_GetExecutionCount() > 10) { /* 10 cycles */ }
if (StateMachine_GetStateTime() > 1000) { /* 1 second */ }
```

**Q: How do I recover from CRITICAL_ERROR?**
A: Critical errors require manual reset or watchdog timeout. This is by design for safety.

**Q: Can I add more states?**
A: Yes! Increase `SM_MAX_STATES` and add your state configuration in the init table.

**Q: How do I change debug output at runtime?**
A: Use `Debug_SetInterface(COMM_INTERFACE_RTT)` to switch interfaces.

**Q: What's the memory footprint?**
A: ~1.5KB RAM, ~6-8KB Flash with typical configuration. See README for optimization.

**Q: Does it work with C++?**
A: Yes! All headers have `extern "C"` guards for C++ compatibility.

**Q: Can I use this without an RTOS?**
A: Yes! Works great in bare metal. Just call `App_Main_Task()` in your main loop.

---

## Quick Troubleshooting

### State machine not transitioning
- ✓ Check `StateMachine_PostEvent()` returns true
- ✓ Verify transition exists in state table
- ✓ Ensure `App_Main_Task()` is called regularly
- ✓ Check if critical error lock is active

### Debug messages not appearing
- ✓ Verify `Platform_UART_Init()` returns true
- ✓ Check message type is enabled
- ✓ Confirm UART baud rate (115200 default)
- ✓ Verify TX pin is configured correctly

### System stuck in recovery
- ✓ Check `ERROR_MAX_RECOVERY_ATTEMPTS` value
- ✓ Verify recovery logic for your error type
- ✓ Check timeout is reasonable (default 2s)
- ✓ Ensure error isn't continuously reported

### Build errors
- ✓ Include path set correctly
- ✓ Link with `sm_framework` library
- ✓ Check all platform functions implemented
- ✓ Verify C99 standard enabled

---

**Need more details?** See [README.md](README.md) for comprehensive documentation.

**Ready to start?** Just implement the 5 platform functions and you're good to go!
