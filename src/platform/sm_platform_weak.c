/**
 * @file sm_platform_weak.c
 * @brief Default (weak) platform implementations
 * @version 2.0.0
 * 
 * These are default implementations that can be overridden by user.
 * Use weak symbols so users can provide their own implementations.
 */

#include "sm_framework/sm_platform.h"
#include <stdio.h>
#include <time.h>

/* ============================= TIMING ================================= */

SM_WEAK uint32_t Platform_GetTimeMs(void)
{
    /* Default: simulation time (increment on each call) */
    static uint32_t sim_time = 0;
    return sim_time++;
}

bool Platform_IsTimeout(uint32_t start_time_ms, uint32_t timeout_ms)
{
    uint32_t current_time = Platform_GetTimeMs();
    
    /* Handle wraparound correctly */
    if (current_time >= start_time_ms) {
        return (current_time - start_time_ms) >= timeout_ms;
    } else {
        /* Wraparound occurred */
        return ((0xFFFFFFFFUL - start_time_ms) + current_time) >= timeout_ms;
    }
}

/* ========================= CRITICAL SECTIONS ========================== */

SM_WEAK void Platform_EnterCritical(void)
{
    /* Default: no-op (single-threaded simulation) */
    /* User must override for real platform:
     * - Bare metal: __disable_irq();
     * - FreeRTOS: taskENTER_CRITICAL();
     * - Zephyr: irq_lock();
     */
}

SM_WEAK void Platform_ExitCritical(void)
{
    /* Default: no-op (single-threaded simulation) */
    /* User must override for real platform:
     * - Bare metal: __enable_irq();
     * - FreeRTOS: taskEXIT_CRITICAL();
     * - Zephyr: irq_unlock(key);
     */
}

/* ========================= UART INTERFACE ============================= */

SM_WEAK bool Platform_UART_Init(void)
{
    /* Default: success (simulation) */
    return true;
}

SM_WEAK uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length)
{
    /* Default: print to stdout for simulation */
    if (data != NULL && length > 0) {
        for (uint32_t i = 0; i < length; i++) {
            putchar(data[i]);
        }
        fflush(stdout);
    }
    return length;
}

SM_WEAK uint32_t Platform_UART_Receive(uint8_t *data, uint32_t max_length, uint32_t timeout_ms)
{
    (void)data;
    (void)max_length;
    (void)timeout_ms;
    return 0;  /* No data received in default implementation */
}

/* ========================== SPI INTERFACE ============================= */

SM_WEAK bool Platform_SPI_Init(void)
{
    return true;  /* Default: simulation */
}

SM_WEAK uint32_t Platform_SPI_Send(const uint8_t *data, uint32_t length)
{
    (void)data;
    (void)length;
    return length;  /* Pretend sent in default implementation */
}

/* ========================== I2C INTERFACE ============================= */

SM_WEAK bool Platform_I2C_Init(void)
{
    return true;  /* Default: simulation */
}

SM_WEAK uint32_t Platform_I2C_Send(const uint8_t *data, uint32_t length)
{
    (void)data;
    (void)length;
    return length;  /* Pretend sent in default implementation */
}

/* ========================== USB INTERFACE ============================= */

SM_WEAK bool Platform_USB_Init(void)
{
    return true;  /* Default: simulation */
}

SM_WEAK uint32_t Platform_USB_Send(const uint8_t *data, uint32_t length)
{
    (void)data;
    (void)length;
    return length;
}

/* =========================== RTT INTERFACE ============================ */

SM_WEAK bool Platform_RTT_Init(void)
{
    return true;  /* Default: simulation */
}

SM_WEAK uint32_t Platform_RTT_Send(const uint8_t *data, uint32_t length)
{
    (void)data;
    (void)length;
    return length;
}

/* =========================== ASSERTIONS =============================== */

SM_WEAK void Platform_Assert(const char *expr, const char *file, int line)
{
    /* Default: print and loop forever */
    printf("\n*** ASSERTION FAILED ***\n");
    printf("Expression: %s\n", expr);
    printf("File: %s\n", file);
    printf("Line: %d\n", line);
    printf("System halted.\n");
    fflush(stdout);
    
    /* Infinite loop */
    while (1) {
        /* Could trigger breakpoint here for debugging:
         * __asm("BKPT #0");  // ARM
         * __builtin_trap();  // GCC
         */
    }
}
