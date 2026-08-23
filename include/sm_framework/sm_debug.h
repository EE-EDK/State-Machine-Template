/**
 * @file sm_debug.h
 * @brief Debug messaging API for State Machine Framework v4.1
 * @version 4.1.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Multi-level debug output system with compile-time stripping.
 * When SM_FEATURE_DEBUG == 0, all functions and macros become no-ops.
 * When SM_FEATURE_DEBUG == 1, SM_DEBUG_LEVEL controls which log macros
 * compile in (0=off, 1=error, 2=+warn, 3=+info, 4=+verbose).
 *
 * v3.0 additions:
 *   - Runtime enable/disable per level (SM_Debug_EnableLevel)
 *   - Per-module debug tags with filtering (SM_Debug_RegisterTag, SM_LOG_TAG)
 *   - Periodic interval helper (SM_Debug_SetPeriodicInterval)
 *   - HexDump with ASCII column
 */

#ifndef SM_DEBUG_H
#define SM_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

/** @brief Maximum number of registered debug module tags */
#define SM_DEBUG_MAX_TAGS  (16)

#if SM_FEATURE_DEBUG

/* =============================================================================
 * DEBUG INITIALIZATION
 * ===========================================================================*/

/**
 * @brief Initialize the debug output subsystem
 *
 * Calls SM_Platform_OutputInit() with the specified interface.
 * Initializes runtime level mask based on SM_DEBUG_LEVEL and enables
 * all tag slots by default.
 *
 * @param interface Platform output interface ID (passed to HAL)
 * @return true if initialization succeeded
 *
 * @note Process-global subsystem: level mask and tag table are shared across
 *       all SM_Handle_t instances — not per state machine. Typical for UART/log
 *       routing; register tags once per firmware image.
 */
bool SM_Debug_Init(uint8_t interface);

/* =============================================================================
 * DEBUG OUTPUT FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Send a formatted debug message
 *
 * Printf-style formatted output. Message is prefixed with timestamp and level.
 * Checks both compile-time SM_DEBUG_LEVEL (via macros) and runtime level mask.
 *
 * @param level  Debug level (1=error, 2=warn, 3=info, 4=verbose)
 * @param fmt    Printf-style format string
 * @param ...    Variable arguments
 *
 * @note Messages with level > SM_DEBUG_LEVEL are compiled out by the macros.
 *       At runtime, disabled levels are suppressed via SM_Debug_EnableLevel.
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
 * @brief Dump data as hexadecimal with ASCII column
 *
 * Formats data as hex bytes with printable ASCII representation.
 * Output format:
 *   0000: 48 65 6C 6C 6F 20 57 6F 72 6C 64 00             Hello World.
 *
 * Non-printable characters (< 0x20 or > 0x7E) shown as '.'.
 *
 * @param data Pointer to data to dump
 * @param len  Length of data in bytes
 */
void SM_Debug_HexDump(const void *data, uint32_t len);

/* =============================================================================
 * RUNTIME LEVEL CONTROL (5.1)
 * ===========================================================================*/

/**
 * @brief Enable or disable a debug level at runtime
 *
 * Allows suppressing specific levels without recompiling. Levels that are
 * compiled out via SM_DEBUG_LEVEL remain unavailable regardless of this setting.
 *
 * @param level  Debug level to control (1=error, 2=warn, 3=info, 4=verbose)
 * @param enable true to enable, false to disable
 */
void SM_Debug_EnableLevel(uint8_t level, bool enable);

/**
 * @brief Check if a debug level is enabled at runtime
 *
 * @param level  Debug level to check (1-4)
 * @return true if the level is enabled in the runtime mask
 */
bool SM_Debug_IsLevelEnabled(uint8_t level);

/* =============================================================================
 * PER-MODULE DEBUG TAGS (5.2)
 * ===========================================================================*/

/**
 * @brief Register a module debug tag
 *
 * Registers a named tag for module-level debug filtering. Each module
 * calls this once at init to get a tag ID for use with SM_Debug_PrintTagged
 * and SM_LOG_TAG.
 *
 * @param tag_name Short module name (e.g. "sm_engine", "app_sensor").
 *                 Pointer must remain valid for program lifetime (use string literal).
 * @return tag ID (0 to SM_DEBUG_MAX_TAGS-1), or -1 if table is full
 */
int8_t SM_Debug_RegisterTag(const char *tag_name);

/**
 * @brief Enable or disable output for a specific tag
 *
 * @param tag_id Tag ID returned by SM_Debug_RegisterTag
 * @param enable true to enable, false to suppress output for this tag
 */
void SM_Debug_EnableTag(int8_t tag_id, bool enable);

/**
 * @brief Check if a tag is enabled
 *
 * @param tag_id Tag ID returned by SM_Debug_RegisterTag
 * @return true if the tag is enabled (or if tag_id is invalid -- fail-open)
 */
bool SM_Debug_IsTagEnabled(int8_t tag_id);

/**
 * @brief Send a tagged, formatted debug message
 *
 * Like SM_Debug_Print but includes a module tag in the output and checks
 * both level and tag filters.
 *
 * Output format: [timestamp][LEVEL][tag_name] message
 *
 * @param tag_id Tag ID from SM_Debug_RegisterTag (-1 or invalid = no tag filtering)
 * @param level  Debug level (1=error, 2=warn, 3=info, 4=verbose)
 * @param fmt    Printf-style format string
 * @param ...    Variable arguments
 */
void SM_Debug_PrintTagged(int8_t tag_id, uint8_t level, const char *fmt, ...);

/**
 * @brief Tagged log macro with compile-time level gate
 *
 * Usage:
 *   static int8_t my_tag = -1;
 *   my_tag = SM_Debug_RegisterTag("my_module");
 *   SM_LOG_TAG(my_tag, 3, "sensor value: %d", val);
 */
#define SM_LOG_TAG(tag_id, level, ...) \
    do { if ((level) <= SM_DEBUG_LEVEL) SM_Debug_PrintTagged((tag_id), (level), __VA_ARGS__); } while(0)

/* =============================================================================
 * PERIODIC INTERVAL HELPER (5.3)
 * ===========================================================================*/

/**
 * @brief Set the periodic debug interval
 *
 * Configures the interval for SM_Debug_CheckPeriodic(). Useful for
 * rate-limiting status messages in on_execute callbacks.
 *
 * @param interval_ms Interval in milliseconds (0 = always returns true)
 */
void SM_Debug_SetPeriodicInterval(uint32_t interval_ms);

/**
 * @brief Check if the periodic interval has elapsed
 *
 * Returns true if interval_ms has elapsed since the last time this function
 * returned true (or since SM_Debug_SetPeriodicInterval was called).
 * Uses SM_Platform_GetTimeMs() for timing.
 *
 * Typical usage in an on_execute callback:
 *   if (SM_Debug_CheckPeriodic()) {
 *       SM_LOG_INFO("heartbeat: state=%u", SM_GetState(sm));
 *   }
 *
 * @return true if interval has elapsed, false otherwise
 */
bool SM_Debug_CheckPeriodic(void);

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
 *
 * No function declarations are emitted. All calls resolve to ((void)0) or
 * constant returns. No stdio or stdarg dependency is introduced.
 * ===========================================================================*/

/*
 * Value-returning no-ops use static inline functions to avoid
 * -Wunused-value warnings when the result is discarded as a statement.
 * Macros that expand to bare constants trigger "statement with no effect"
 * under -Wall; inline functions do not.
 */
static inline bool SM_Debug_Init(uint8_t interface)
    { (void)interface; return true; }
static inline bool SM_Debug_IsLevelEnabled(uint8_t level)
    { (void)level; return false; }
static inline int8_t SM_Debug_RegisterTag(const char *tag_name)
    { (void)tag_name; return -1; }
static inline bool SM_Debug_IsTagEnabled(int8_t tag_id)
    { (void)tag_id; return false; }
static inline bool SM_Debug_CheckPeriodic(void)
    { return false; }

#define SM_Debug_Print(level, fmt, ...)              ((void)0)
#define SM_Debug_PrintRaw(msg, len)                  ((void)0)
#define SM_Debug_HexDump(data, len)                  ((void)0)

#define SM_Debug_EnableLevel(level, enable)           ((void)0)
#define SM_Debug_EnableTag(tag_id, enable)            ((void)0)
#define SM_Debug_PrintTagged(tag_id, level, fmt, ...) ((void)0)
#define SM_LOG_TAG(tag_id, level, ...)                ((void)0)

#define SM_Debug_SetPeriodicInterval(interval_ms)     ((void)0)

#define SM_LOG_ERROR(...)                ((void)0)
#define SM_LOG_WARN(...)                 ((void)0)
#define SM_LOG_INFO(...)                 ((void)0)
#define SM_LOG_VERBOSE(...)              ((void)0)

#endif /* SM_FEATURE_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* SM_DEBUG_H */
