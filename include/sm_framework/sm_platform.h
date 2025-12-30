/**
 * @file sm_platform.h
 * @brief Platform abstraction layer interface for State Machine Framework
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * This file defines the platform abstraction layer (PAL) interface that must
 * be implemented for each target platform. This allows the state machine
 * framework to remain completely portable across different microcontrollers.
 *
 * IMPLEMENTATION GUIDE:
 *   1. Create a file platform_impl.c in your project
 *   2. Implement all functions declared in this header
 *   3. Link platform_impl.c with the state machine framework
 *
 * See examples/platform_template.c for implementation template
 */

#ifndef SM_PLATFORM_H
#define SM_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sm_types.h"

/* =============================================================================
 * TIMING FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Get current system time in milliseconds
 *
 * @return uint32_t Current time in milliseconds (wraps at 32-bit limit)
 *
 * @note Implementation examples:
 *   - STM32 HAL: return HAL_GetTick();
 *   - ESP32: return (uint32_t)(esp_timer_get_time() / 1000);
 *   - RP2040: return to_ms_since_boot(get_absolute_time());
 *   - FreeRTOS: return xTaskGetTickCount() * portTICK_PERIOD_MS;
 *   - Bare metal: return timer_counter_ms;
 */
uint32_t Platform_GetTimeMs(void);

/**
 * @brief Check if timeout has occurred (handles 32-bit wraparound)
 *
 * @param start_time_ms Starting time in milliseconds
 * @param timeout_ms Timeout duration in milliseconds
 * @return true if timeout has occurred, false otherwise
 *
 * @note This is provided as a helper and handles uint32_t wraparound correctly.
 *       You typically don't need to implement this - use the provided version.
 */
bool Platform_IsTimeout(uint32_t start_time_ms, uint32_t timeout_ms);

/* =============================================================================
 * CRITICAL SECTION / THREAD SAFETY
 * ===========================================================================*/

/**
 * @brief Enter critical section (disable interrupts or take mutex)
 *
 * Used to protect shared data structures from concurrent access.
 * Must be paired with Platform_ExitCritical().
 *
 * @note Implementation examples:
 *   - Bare metal: __disable_irq(); or save and disable interrupt state
 *   - FreeRTOS: taskENTER_CRITICAL(); or xSemaphoreTake(mutex, portMAX_DELAY);
 *   - Zephyr: irq_lock();
 *   - Simulation: (empty function if single-threaded)
 *
 * @warning These calls may nest - track nesting level if needed
 */
void Platform_EnterCritical(void);

/**
 * @brief Exit critical section (re-enable interrupts or release mutex)
 *
 * Must be called after Platform_EnterCritical() to restore interrupt state.
 *
 * @note Implementation examples:
 *   - Bare metal: __enable_irq(); or restore saved interrupt state
 *   - FreeRTOS: taskEXIT_CRITICAL(); or xSemaphoreGive(mutex);
 *   - Zephyr: irq_unlock(key);
 *   - Simulation: (empty function if single-threaded)
 */
void Platform_ExitCritical(void);

/* =============================================================================
 * COMMUNICATION INTERFACE - UART
 * ===========================================================================*/

/**
 * @brief Initialize UART interface for debug output
 *
 * @return true if initialization successful, false otherwise
 *
 * @note Configure baud rate, pins, and mode as needed for your platform
 */
bool Platform_UART_Init(void);

/**
 * @brief Send data over UART
 *
 * @param data Pointer to data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes actually sent (may be less than length if error)
 *
 * @note This should be a blocking call or handle buffering internally
 */
uint32_t Platform_UART_Send(const uint8_t *data, uint32_t length);

/**
 * @brief Receive data from UART (optional)
 *
 * @param data Pointer to buffer to store received data
 * @param max_length Maximum number of bytes to receive
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking)
 * @return Number of bytes actually received
 *
 * @note This function is optional - only implement if needed
 */
uint32_t Platform_UART_Receive(uint8_t *data, uint32_t max_length, uint32_t timeout_ms);

/* =============================================================================
 * COMMUNICATION INTERFACE - SPI
 * ===========================================================================*/

/**
 * @brief Initialize SPI interface for debug output
 *
 * @return true if initialization successful, false otherwise
 *
 * @note Configure clock, mode, pins as needed for your platform
 */
bool Platform_SPI_Init(void);

/**
 * @brief Send data over SPI
 *
 * @param data Pointer to data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes actually sent
 */
uint32_t Platform_SPI_Send(const uint8_t *data, uint32_t length);

/* =============================================================================
 * COMMUNICATION INTERFACE - I2C
 * ===========================================================================*/

/**
 * @brief Initialize I2C interface for debug output
 *
 * @return true if initialization successful, false otherwise
 *
 * @note Configure clock, address, pins as needed for your platform
 */
bool Platform_I2C_Init(void);

/**
 * @brief Send data over I2C
 *
 * @param data Pointer to data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes actually sent
 */
uint32_t Platform_I2C_Send(const uint8_t *data, uint32_t length);

/* =============================================================================
 * COMMUNICATION INTERFACE - USB (Optional)
 * ===========================================================================*/

/**
 * @brief Initialize USB interface for debug output (optional)
 *
 * @return true if initialization successful, false otherwise
 */
bool Platform_USB_Init(void);

/**
 * @brief Send data over USB (optional)
 *
 * @param data Pointer to data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes actually sent
 */
uint32_t Platform_USB_Send(const uint8_t *data, uint32_t length);

/* =============================================================================
 * COMMUNICATION INTERFACE - RTT (Optional, for debugging)
 * ===========================================================================*/

/**
 * @brief Initialize SEGGER RTT interface (optional)
 *
 * @return true if initialization successful, false otherwise
 *
 * @note RTT is useful for debugging without affecting timing
 */
bool Platform_RTT_Init(void);

/**
 * @brief Send data over RTT (optional)
 *
 * @param data Pointer to data buffer to send
 * @param length Number of bytes to send
 * @return Number of bytes actually sent
 */
uint32_t Platform_RTT_Send(const uint8_t *data, uint32_t length);

/* =============================================================================
 * OPTIONAL: ASSERTIONS
 * ===========================================================================*/

/**
 * @brief Platform-specific assertion handler
 *
 * @param expr Expression that failed (as string)
 * @param file File where assertion failed
 * @param line Line number where assertion failed
 *
 * @note Default behavior: infinite loop or trigger breakpoint
 *       Production: log error and reset system
 */
void Platform_Assert(const char *expr, const char *file, int line);

/* =============================================================================
 * ASSERTION MACRO
 * ===========================================================================*/

#if FEATURE_ASSERT_ENABLED
    #define SM_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                Platform_Assert(#expr, __FILE__, __LINE__); \
            } \
        } while (0)
#else
    #define SM_ASSERT(expr) ((void)0)
#endif

/* =============================================================================
 * WEAK SYMBOL HELPERS
 * ===========================================================================*/

/**
 * @brief Weak symbol attribute for default implementations
 *
 * Allows default implementations to be overridden by user implementations.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define SM_WEAK __attribute__((weak))
#elif defined(__ICCARM__) || defined(__IAR_SYSTEMS_ICC__)
    #define SM_WEAK __weak
#elif defined(__CC_ARM)
    #define SM_WEAK __attribute__((weak))
#else
    #define SM_WEAK
    #warning "Weak symbols not supported on this compiler"
#endif

#ifdef __cplusplus
}
#endif

#endif /* SM_PLATFORM_H */
