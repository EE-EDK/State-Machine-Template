# Platform Implementation Guide

This guide shows you how to implement the platform abstraction layer for your specific microcontroller.

## Overview

The State Machine Framework uses **weak symbols** to provide default implementations that work for simulation. To use the framework on real hardware, you need to override these functions with platform-specific implementations.

## Quick Start

### 1. Create Platform Implementation File

Create a file in your project (e.g., `platform_impl.c`) with your platform-specific code:

```c
#include "sm_framework/sm_platform.h"

/* Your platform-specific includes */
#include "stm32f4xx_hal.h"  // Example for STM32

/* Override timing function */
uint32_t Platform_GetTimeMs(void)
{
    return HAL_GetTick();
}

/* Override UART send */
uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}

/* Override critical sections */
void Platform_EnterCritical(void)
{
    __disable_irq();
}

void Platform_ExitCritical(void)
{
    __enable_irq();
}

/* Implement other required functions... */
```

### 2. Link Platform Implementation

Add `platform_impl.c` to your build system (CMake, Makefile, IDE project).

The framework will automatically use your implementations instead of the weak defaults!

---

## Platform Functions Reference

### ⏱️ Timing Functions

#### `Platform_GetTimeMs()`
**Required**: ✅ Yes
**Purpose**: Get current system time in milliseconds

**Implementation Examples:**

```c
/* STM32 HAL */
uint32_t Platform_GetTimeMs(void) {
    return HAL_GetTick();
}

/* ESP32 */
uint32_t Platform_GetTimeMs(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* RP2040 */
uint32_t Platform_GetTimeMs(void) {
    return to_ms_since_boot(get_absolute_time());
}

/* FreeRTOS */
uint32_t Platform_GetTimeMs(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* Bare metal (using SysTick) */
static volatile uint32_t g_systick_ms = 0;

void SysTick_Handler(void) {
    g_systick_ms++;
}

uint32_t Platform_GetTimeMs(void) {
    return g_systick_ms;
}
```

---

### 🔒 Critical Sections

#### `Platform_EnterCritical()` & `Platform_ExitCritical()`
**Required**: ✅ Yes (if using interrupts or RTOS)
**Purpose**: Protect shared data from concurrent access

**Implementation Examples:**

```c
/* Bare Metal - Simple disable/enable interrupts */
void Platform_EnterCritical(void) {
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __enable_irq();
}

/* Bare Metal - Save/restore interrupt state (better) */
static uint32_t g_saved_primask = 0;

void Platform_EnterCritical(void) {
    g_saved_primask = __get_PRIMASK();
    __disable_irq();
}

void Platform_ExitCritical(void) {
    __set_PRIMASK(g_saved_primask);
}

/* FreeRTOS */
void Platform_EnterCritical(void) {
    taskENTER_CRITICAL();
}

void Platform_ExitCritical(void) {
    taskEXIT_CRITICAL();
}

/* FreeRTOS with mutex (alternative) */
static SemaphoreHandle_t g_sm_mutex = NULL;

void Platform_Init(void) {
    g_sm_mutex = xSemaphoreCreateMutex();
}

void Platform_EnterCritical(void) {
    xSemaphoreTake(g_sm_mutex, portMAX_DELAY);
}

void Platform_ExitCritical(void) {
    xSemaphoreGive(g_sm_mutex);
}
```

---

### 📡 UART Interface

#### `Platform_UART_Init()` & `Platform_UART_Send()`
**Required**: ⚠️ Only if using UART for debug output
**Purpose**: Initialize UART and send debug messages

**Implementation Examples:**

```c
/* STM32 HAL */
extern UART_HandleTypeDef huart1;  // Configured in CubeMX

bool Platform_UART_Init(void) {
    /* Already initialized by CubeMX */
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}

/* ESP32 */
bool Platform_UART_Init(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length) {
    return uart_write_bytes(UART_NUM_0, (const char *)data, length);
}

/* RP2040 */
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

---

### 🔌 Optional Interfaces (SPI, I2C, USB, RTT)

Only implement these if you need them for debug output:

```c
/* Example: SEGGER RTT (excellent for debugging) */
#include "SEGGER_RTT.h"

bool Platform_RTT_Init(void) {
    SEGGER_RTT_Init();
    return true;
}

uint32_t Platform_RTT_Send(const uint8_t *data, uint32_t length) {
    SEGGER_RTT_Write(0, data, length);
    return length;
}
```

---

## Complete Platform Examples

### STM32 with HAL

```c
/* platform_stm32.c */
#include "sm_framework/sm_platform.h"
#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart1;

uint32_t Platform_GetTimeMs(void)
{
    return HAL_GetTick();
}

void Platform_EnterCritical(void)
{
    __disable_irq();
}

void Platform_ExitCritical(void)
{
    __enable_irq();
}

bool Platform_UART_Init(void)
{
    /* Initialized by CubeMX */
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    HAL_UART_Transmit(&huart1, data, length, 100);
    return length;
}

/* Leave other functions as weak defaults (stubs) */
```

### ESP32 with ESP-IDF

```c
/* platform_esp32.c */
#include "sm_framework/sm_platform.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t Platform_GetTimeMs(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void Platform_EnterCritical(void)
{
    taskENTER_CRITICAL();
}

void Platform_ExitCritical(void)
{
    taskEXIT_CRITICAL();
}

bool Platform_UART_Init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    return uart_write_bytes(UART_NUM_0, (const char *)data, length);
}
```

### RP2040 with Pico SDK

```c
/* platform_rp2040.c */
#include "sm_framework/sm_platform.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

uint32_t Platform_GetTimeMs(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void Platform_EnterCritical(void)
{
    __disable_irq();
}

void Platform_ExitCritical(void)
{
    __enable_irq();
}

bool Platform_UART_Init(void)
{
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    return true;
}

uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    uart_write_blocking(uart0, data, length);
    return length;
}
```

---

## Integration Checklist

✅ Implement `Platform_GetTimeMs()` - **REQUIRED**
✅ Implement `Platform_EnterCritical()` / `Platform_ExitCritical()` - **REQUIRED if using interrupts/RTOS**
✅ Implement at least one debug interface (UART recommended)
✅ Add platform file to your build system
✅ Test timing with simple delays
✅ Test debug output appears correctly
✅ Test event posting from interrupts

---

## Debugging Tips

### Problem: Debug messages don't appear
- Check that UART pins are correct
- Verify baud rate matches terminal settings
- Ensure `Platform_UART_Init()` is called
- Check that debug messages are enabled in configuration

### Problem: System crashes when posting events from interrupt
- Verify `Platform_EnterCritical()` / `Platform_ExitCritical()` are implemented
- Check interrupt priority configuration
- Ensure no blocking calls in critical sections

### Problem: Timeouts don't work correctly
- Verify `Platform_GetTimeMs()` returns incrementing milliseconds
- Check that timer is configured correctly
- Test with simple delay loop

---

## Need Help?

See `examples/` directory for complete working examples.

The framework is designed to be platform-agnostic - if you implement these functions correctly, everything else just works!
