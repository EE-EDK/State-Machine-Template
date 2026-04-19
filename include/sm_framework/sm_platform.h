/**
 * @file sm_platform.h
 * @brief Platform abstraction layer (HAL) for State Machine Framework v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Defines the HAL interface that must be implemented for each target platform.
 * Default weak implementations are provided in sm_platform_weak.c for
 * simulation / development.
 *
 * Expanded from v2: adds watchdog, sleep modes, NVS, reset reason.
 * SM_Platform_IsTimeout() is now weak (overridable).
 * Output functions are generalized (no per-protocol init/send).
 *
 * All functions prefixed SM_Platform_ for namespace consistency.
 */

#ifndef SM_PLATFORM_H
#define SM_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * WEAK SYMBOL ATTRIBUTE
 * ===========================================================================*/

/**
 * @brief Weak symbol attribute for default implementations
 *
 * Allows user to override any HAL function by providing a strong definition.
 */
/**
 * @brief Weak symbol attribute for default HAL implementations
 *
 * Allows user to override any HAL function by providing a strong definition.
 *
 * Platform notes:
 *   - GCC/Clang (ELF + PE/COFF): __attribute__((weak)) supported
 *   - IAR ARM: __weak keyword
 *   - ARM Compiler 5/6: __attribute__((weak))
 *   - MSVC: not supported (override via linker order)
 *
 * On MinGW (PE/COFF), weak symbols work for .o files but NOT within
 * static archives (.a). The build system must link sm_platform_weak.o
 * as a separate object (not inside the archive) so the linker can
 * resolve weak definitions and allow user overrides.
 *
 * User can pre-define SM_WEAK before including this file to force behavior.
 */
#ifndef SM_WEAK
    #if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
        /*
         * PE/COFF (MinGW/Windows): __attribute__((weak)) compiles but the
         * linker treats weak symbols in static archives as unresolvable.
         * Define SM_WEAK as empty so platform defaults are strong symbols.
         *
         * On Windows, override platform functions by:
         *   1. Compiling the framework via add_subdirectory() and replacing
         *      sm_platform_weak.c with your own platform file, or
         *   2. Not linking sm_platform_defaults and providing your own .c
         */
        #define SM_WEAK
    #elif defined(__GNUC__) || defined(__clang__)
        #define SM_WEAK __attribute__((weak))
    #elif defined(__ICCARM__) || defined(__IAR_SYSTEMS_ICC__)
        #define SM_WEAK __weak
    #elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
        #define SM_WEAK __attribute__((weak))
    #else
        #define SM_WEAK
    #endif
#endif

/* =============================================================================
 * TIMING
 * ===========================================================================*/

/**
 * @brief Get current system time in milliseconds
 *
 * @return uint32_t Current time in ms (wraps at ~49.7 days)
 *
 * @note Implementation examples:
 *   - STM32 HAL:   return HAL_GetTick();
 *   - ESP-IDF:     return (uint32_t)(esp_timer_get_time() / 1000);
 *   - RP2040:      return to_ms_since_boot(get_absolute_time());
 *   - FreeRTOS:    return xTaskGetTickCount() * portTICK_PERIOD_MS;
 *   - Simulation:  clock-based or incrementing counter
 */
uint32_t SM_Platform_GetTimeMs(void);

/**
 * @brief Check if timeout has occurred (handles 32-bit wraparound)
 *
 * @param start      Starting time in milliseconds
 * @param timeout_ms Timeout duration in milliseconds
 * @return true if timeout has occurred
 *
 * @note Now weak -- user can override for platforms with hardware timers.
 */
bool SM_Platform_IsTimeout(uint32_t start, uint32_t timeout_ms);

/* =============================================================================
 * CRITICAL SECTIONS (must support nesting)
 * ===========================================================================*/

/**
 * @brief Enter critical section (disable interrupts or take mutex)
 *
 * Must support nesting -- track nesting level if needed.
 * Paired with SM_Platform_ExitCritical().
 *
 * @note Implementation examples:
 *   - Bare metal:  __disable_irq() with nesting counter
 *   - FreeRTOS:    taskENTER_CRITICAL()
 *   - Zephyr:      irq_lock()
 *   - Simulation:  no-op
 */
void SM_Platform_EnterCritical(void);

/**
 * @brief Exit critical section (re-enable interrupts or release mutex)
 *
 * Must match a prior SM_Platform_EnterCritical() call.
 */
void SM_Platform_ExitCritical(void);

/* =============================================================================
 * OUTPUT (generalized debug/communication output)
 * ===========================================================================*/

/**
 * @brief Initialize a debug output interface
 *
 * @param interface Interface ID (application-defined: 0=UART, 1=SPI, etc.)
 * @return true if initialization succeeded
 */
bool SM_Platform_OutputInit(uint8_t interface);

/**
 * @brief Send data to the debug output interface
 *
 * @param data   Pointer to data buffer
 * @param len    Number of bytes to send
 * @return Number of bytes actually sent
 */
uint32_t SM_Platform_OutputSend(const uint8_t *data, uint32_t len);

/* =============================================================================
 * WATCHDOG (new in v3)
 * ===========================================================================*/

/**
 * @brief Kick (feed) the watchdog timer
 *
 * Call periodically to prevent watchdog reset.
 */
void SM_Platform_WatchdogKick(void);

/**
 * @brief Start the watchdog timer
 *
 * @param timeout_ms Watchdog timeout in milliseconds
 */
void SM_Platform_WatchdogStart(uint32_t timeout_ms);

/**
 * @brief Stop the watchdog timer
 *
 * @note Not all platforms support stopping the watchdog once started.
 */
void SM_Platform_WatchdogStop(void);

/* =============================================================================
 * SLEEP MODES (new in v3)
 * ===========================================================================*/

/**
 * @brief Sleep mode selection
 */
typedef enum {
    SM_SLEEP_LIGHT   = 0,   /**< CPU halted, peripherals active, fast wake */
    SM_SLEEP_DEEP    = 1,   /**< Most clocks stopped, slow wake */
    SM_SLEEP_STANDBY = 2    /**< Near-off, wake via RTC/external only */
} SM_SleepMode_t;

/**
 * @brief Enter a low-power sleep mode
 *
 * @param mode Sleep mode to enter
 *
 * @note Returns when wake event occurs (or after WFI for light sleep).
 */
void SM_Platform_EnterSleep(SM_SleepMode_t mode);

/* =============================================================================
 * NON-VOLATILE STORAGE (new in v3)
 * ===========================================================================*/

/**
 * @brief Write data to non-volatile storage
 *
 * @param key  Application-defined key (0-65535)
 * @param data Pointer to data to write
 * @param len  Length in bytes
 * @return true if write succeeded
 */
bool SM_Platform_NVS_Write(uint16_t key, const void *data, uint16_t len);

/**
 * @brief Read data from non-volatile storage
 *
 * @param key  Application-defined key (0-65535)
 * @param data Pointer to buffer for read data
 * @param len  Length in bytes
 * @return true if read succeeded
 */
bool SM_Platform_NVS_Read(uint16_t key, void *data, uint16_t len);

/* =============================================================================
 * RESET REASON (new in v3)
 * ===========================================================================*/

/**
 * @brief Reset reason codes
 */
typedef enum {
    SM_RESET_POR       = 0,   /**< Power-on reset */
    SM_RESET_WATCHDOG  = 1,   /**< Watchdog reset */
    SM_RESET_SOFTWARE  = 2,   /**< Software-initiated reset */
    SM_RESET_EXTERNAL  = 3,   /**< External reset (pin) */
    SM_RESET_BROWNOUT  = 4,   /**< Brown-out reset */
    SM_RESET_UNKNOWN   = 5    /**< Unknown or unsupported */
} SM_ResetReason_t;

/**
 * @brief Get the reason for the last system reset
 *
 * @return Reset reason code
 */
SM_ResetReason_t SM_Platform_GetResetReason(void);

/* =============================================================================
 * ASSERTIONS
 * ===========================================================================*/

/**
 * @brief Platform-specific assertion handler
 *
 * Called when SM_ASSERT() fails. Should log the failure and halt or reset.
 *
 * @param expr Expression that failed (as string)
 * @param file Source file where assertion failed
 * @param line Line number where assertion failed
 */
void SM_Platform_Assert(const char *expr, const char *file, int line);

/* Include config for SM_FEATURE_ASSERT (already included transitively,
 * but be explicit for clarity) */
#include "sm_config.h"

/**
 * @brief Runtime assertion macro
 *
 * When SM_FEATURE_ASSERT == 1: evaluates expression, calls
 * SM_Platform_Assert() on failure.
 * When SM_FEATURE_ASSERT == 0: compiles to ((void)0).
 */
#if SM_FEATURE_ASSERT
    #define SM_ASSERT(expr) \
        do { if (!(expr)) SM_Platform_Assert(#expr, __FILE__, __LINE__); } while(0)
#else
    #define SM_ASSERT(expr) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* SM_PLATFORM_H */
