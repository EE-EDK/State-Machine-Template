/**
 * @file sm_debug.h
 * @brief Debug messaging API for State Machine Framework v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Multi-level debug output system with compile-time stripping.
 * When SM_FEATURE_DEBUG == 0, all functions and macros become no-ops.
 * When SM_FEATURE_DEBUG == 1, SM_DEBUG_LEVEL controls which log macros
 * compile in (0=off, 1=error, 2=+warn, 3=+info, 4=+verbose).
 */

#ifndef SM_DEBUG_H
#define SM_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

#if SM_FEATURE_DEBUG

/* =============================================================================
 * DEBUG INITIALIZATION
 * ===========================================================================*/

/**
 * @brief Initialize the debug output subsystem
 *
 * Calls SM_Platform_OutputInit() with the specified interface.
 *
 * @param interface Platform output interface ID (passed to HAL)
 * @return true if initialization succeeded
 */
bool SM_Debug_Init(uint8_t interface);

/* =============================================================================
 * DEBUG OUTPUT FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Send a formatted debug message
 *
 * Printf-style formatted output. Message is prefixed with timestamp and level.
 *
 * @param level  Debug level (1=error, 2=warn, 3=info, 4=verbose)
 * @param fmt    Printf-style format string
 * @param ...    Variable arguments
 *
 * @note Messages with level > SM_DEBUG_LEVEL are compiled out by the macros.
 *       This function itself does NOT filter -- the macros do.
 */
void SM_Debug_Print(uint8_t level, const char *fmt, ...);

/**
 * @brief Send a raw (pre-formatted) debug message
 *
 * Sends raw bytes to the output interface without formatting or timestamp.
 *
 * @param msg Pointer to message data
 * @param len Length of message in bytes
 */
void SM_Debug_PrintRaw(const char *msg, uint32_t len);

/**
 * @brief Dump data as hexadecimal
 *
 * Formats data as hex bytes and sends to debug output.
 * Useful for protocol debugging.
 *
 * @param data Pointer to data to dump
 * @param len  Length of data in bytes
 */
void SM_Debug_HexDump(const void *data, uint32_t len);

/* =============================================================================
 * LEVEL MACROS (compile-time stripping via SM_DEBUG_LEVEL)
 * ===========================================================================*/

#if SM_DEBUG_LEVEL >= 1
    #define SM_LOG_ERROR(...)   SM_Debug_Print(1, __VA_ARGS__)
#else
    #define SM_LOG_ERROR(...)   ((void)0)
#endif

#if SM_DEBUG_LEVEL >= 2
    #define SM_LOG_WARN(...)    SM_Debug_Print(2, __VA_ARGS__)
#else
    #define SM_LOG_WARN(...)    ((void)0)
#endif

#if SM_DEBUG_LEVEL >= 3
    #define SM_LOG_INFO(...)    SM_Debug_Print(3, __VA_ARGS__)
#else
    #define SM_LOG_INFO(...)    ((void)0)
#endif

#if SM_DEBUG_LEVEL >= 4
    #define SM_LOG_VERBOSE(...) SM_Debug_Print(4, __VA_ARGS__)
#else
    #define SM_LOG_VERBOSE(...) ((void)0)
#endif

#else /* SM_FEATURE_DEBUG == 0 */

/* =============================================================================
 * ALL DEBUG DISABLED -- everything becomes no-ops
 * ===========================================================================*/

#define SM_Debug_Init(interface)         (true)
#define SM_Debug_Print(level, fmt, ...)  ((void)0)
#define SM_Debug_PrintRaw(msg, len)      ((void)0)
#define SM_Debug_HexDump(data, len)      ((void)0)

#define SM_LOG_ERROR(...)                ((void)0)
#define SM_LOG_WARN(...)                 ((void)0)
#define SM_LOG_INFO(...)                 ((void)0)
#define SM_LOG_VERBOSE(...)              ((void)0)

#endif /* SM_FEATURE_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* SM_DEBUG_H */
