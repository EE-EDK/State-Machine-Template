/**
 * @file sm_debug.h
 * @brief Debug messaging system API
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * Multi-interface debug messaging system with runtime enable/disable
 * and minimal overhead when disabled.
 */

#ifndef SM_DEBUG_H
#define SM_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"
#include <stdarg.h>

/* =============================================================================
 * DEBUG SYSTEM INITIALIZATION
 * ===========================================================================*/

/**
 * @brief Initialize debug system
 *
 * @param interface Communication interface to use for debug output
 * @return true if initialization successful, false otherwise
 *
 * @note Must be called before using any other debug functions
 */
bool Debug_Init(CommInterface_t interface);

/* =============================================================================
 * DEBUG MESSAGE OUTPUT
 * ===========================================================================*/

/**
 * @brief Send a formatted debug message
 *
 * Works like printf() - supports format strings and variable arguments.
 * Message is filtered based on type and enabled flags before formatting.
 *
 * @param type Message type (INIT, RUNTIME, ERROR, etc.)
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 *
 * @note Messages are timestamped automatically
 * @note If message type is disabled, function returns immediately (low overhead)
 *
 * @example
 *   Debug_SendMessage(DEBUG_MSG_INFO, "Temperature: %d C", temp);
 *   Debug_SendMessage(DEBUG_MSG_ERROR, "Error %s in state %s",
 *                     ErrorCodeToString(code), StateToString(state));
 */
void Debug_SendMessage(DebugMessageType_t type, const char *format, ...);

/**
 * @brief Send a raw debug message (already formatted)
 *
 * @param type Message type
 * @param message Pre-formatted message string
 *
 * @note Useful when message is already formatted or doesn't need timestamp
 */
void Debug_SendRawMessage(DebugMessageType_t type, const char *message);

/* =============================================================================
 * MESSAGE TYPE CONTROL
 * ===========================================================================*/

/**
 * @brief Enable or disable initialization messages
 *
 * @param enable true to enable, false to disable
 *
 * @note Disable in production to reduce output during startup
 */
void Debug_EnableInitMessages(bool enable);

/**
 * @brief Enable or disable runtime messages
 *
 * @param enable true to enable, false to disable
 *
 * @note Disable in production to reduce verbosity
 */
void Debug_EnableRuntimeMessages(bool enable);

/**
 * @brief Enable or disable periodic status messages
 *
 * @param enable true to enable, false to disable
 *
 * @note Periodic messages are useful for health monitoring
 */
void Debug_EnablePeriodicMessages(bool enable);

/**
 * @brief Enable or disable error messages
 *
 * @param enable true to enable, false to disable
 *
 * @warning Disabling error messages not recommended
 */
void Debug_EnableErrorMessages(bool enable);

/**
 * @brief Enable or disable warning messages
 *
 * @param enable true to enable, false to disable
 */
void Debug_EnableWarningMessages(bool enable);

/**
 * @brief Enable or disable info messages
 *
 * @param enable true to enable, false to disable
 */
void Debug_EnableInfoMessages(bool enable);

/**
 * @brief Enable all message types
 */
void Debug_EnableAllMessages(void);

/**
 * @brief Disable all message types
 *
 * @note Use for production builds to minimize output
 */
void Debug_DisableAllMessages(void);

/* =============================================================================
 * PERIODIC STATUS MESSAGES
 * ===========================================================================*/

/**
 * @brief Process periodic debug messages
 *
 * Call this function regularly (e.g., in main task loop).
 * Sends periodic status messages at configured interval.
 *
 * @note Called automatically by StateMachine_Execute() if periodic messages enabled
 */
void Debug_ProcessPeriodic(void);

/**
 * @brief Set periodic message interval
 *
 * @param interval_ms Interval in milliseconds between periodic messages
 *
 * @note Default is DEBUG_PERIODIC_INTERVAL_MS from configuration
 */
void Debug_SetPeriodicInterval(uint32_t interval_ms);

/* =============================================================================
 * COMMUNICATION INTERFACE CONTROL
 * ===========================================================================*/

/**
 * @brief Change debug output interface at runtime
 *
 * @param interface New communication interface to use
 * @return true if interface changed successfully, false if invalid interface
 *
 * @note Useful for switching from UART to RTT during debugging
 */
bool Debug_SetInterface(CommInterface_t interface);

/**
 * @brief Get current debug interface
 *
 * @return Currently active communication interface
 */
CommInterface_t Debug_GetInterface(void);

/* =============================================================================
 * ADVANCED: CUSTOM MESSAGE FORMATTING
 * ===========================================================================*/

/**
 * @brief Function pointer type for custom message formatter
 *
 * @param type Message type
 * @param timestamp Message timestamp
 * @param message Message text
 * @param buffer Output buffer for formatted message
 * @param buffer_size Size of output buffer
 * @return Number of characters written to buffer
 */
typedef uint32_t (*DebugFormatter_t)(DebugMessageType_t type,
                                      uint32_t timestamp,
                                      const char *message,
                                      char *buffer,
                                      uint32_t buffer_size);

/**
 * @brief Set custom message formatter
 *
 * Allows application to override default message formatting.
 *
 * @param formatter Custom formatter function (NULL to use default)
 *
 * @note Default format: "[timestamp] message\n"
 * @example Custom format: "HH:MM:SS.mmm [TYPE] message\r\n"
 */
void Debug_SetFormatter(DebugFormatter_t formatter);

/* =============================================================================
 * UTILITY MACROS
 * ===========================================================================*/

/**
 * @brief Conditional debug message (only if condition is true)
 */
#define DEBUG_IF(condition, type, ...) \
    do { \
        if (condition) { \
            Debug_SendMessage(type, __VA_ARGS__); \
        } \
    } while(0)

/**
 * @brief Debug message with function name
 */
#define DEBUG_FUNC(type, ...) \
    Debug_SendMessage(type, "%s(): " __VA_ARGS__, __func__)

/**
 * @brief Debug assert (send error message if condition fails)
 */
#if FEATURE_ASSERT_ENABLED
    #define DEBUG_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                Debug_SendMessage(DEBUG_MSG_ERROR, \
                    "ASSERT FAILED: %s at %s:%d", #expr, __FILE__, __LINE__); \
            } \
        } while(0)
#else
    #define DEBUG_ASSERT(expr) ((void)0)
#endif

/**
 * @brief Compile-time debug message removal for production builds
 *
 * Define DEBUG_LEVEL to control which messages are compiled in:
 * 0 = None (all debug removed)
 * 1 = Errors only
 * 2 = Errors + Warnings
 * 3 = Errors + Warnings + Info
 * 4 = All messages
 */
#ifndef DEBUG_LEVEL
    #define DEBUG_LEVEL 4  /* Default: all messages */
#endif

#if DEBUG_LEVEL >= 1
    #define DEBUG_ERROR(...) Debug_SendMessage(DEBUG_MSG_ERROR, __VA_ARGS__)
#else
    #define DEBUG_ERROR(...) ((void)0)
#endif

#if DEBUG_LEVEL >= 2
    #define DEBUG_WARNING(...) Debug_SendMessage(DEBUG_MSG_WARNING, __VA_ARGS__)
#else
    #define DEBUG_WARNING(...) ((void)0)
#endif

#if DEBUG_LEVEL >= 3
    #define DEBUG_INFO(...) Debug_SendMessage(DEBUG_MSG_INFO, __VA_ARGS__)
#else
    #define DEBUG_INFO(...) ((void)0)
#endif

#if DEBUG_LEVEL >= 4
    #define DEBUG_RUNTIME(...) Debug_SendMessage(DEBUG_MSG_RUNTIME, __VA_ARGS__)
    #define DEBUG_INIT(...) Debug_SendMessage(DEBUG_MSG_INIT, __VA_ARGS__)
#else
    #define DEBUG_RUNTIME(...) ((void)0)
    #define DEBUG_INIT(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* SM_DEBUG_H */
