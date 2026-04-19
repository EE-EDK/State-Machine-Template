/**
 * @file sm_debug.c
 * @brief Debug messaging system implementation (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Rewritten for v3 API. Features:
 *   - snprintf return value handled correctly (cast + clamp)
 *   - Runtime enable/disable per level via bitmask (5.1)
 *   - Per-module debug tags with filtering (5.2)
 *   - Periodic interval helper (5.3)
 *   - HexDump with ASCII column (5.7 enhancement)
 *   - No dependency on application state types
 *   - Uses generalized SM_Platform_OutputInit/OutputSend (not per-protocol)
 *
 * When SM_FEATURE_DEBUG == 0, this file compiles to nothing (all functions
 * are macro'd to no-ops in the header).
 */

/* SM_DEFINE_MODULE("sm_debug") -- added post-merge with sm_safety.h */

#include "sm_framework/sm_framework.h"

#if SM_FEATURE_DEBUG

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * INTERNAL STATE
 * ===========================================================================*/

/** Active output interface ID */
static uint8_t sm_debug_interface = 0U;

/** Whether debug system is initialized */
static bool sm_debug_initialized = false;

/* --- Runtime level mask (5.1) ---
 *
 * Each bit corresponds to a debug level:
 *   bit 0 = unused (level 0 does not exist)
 *   bit 1 = error
 *   bit 2 = warn
 *   bit 3 = info
 *   bit 4 = verbose
 *
 * Initialized by SM_Debug_Init to match SM_DEBUG_LEVEL.
 */
static uint8_t sm_debug_level_mask = 0U;

/* --- Per-module tag table (5.2) ---
 *
 * Static table of registered tag names. Tags are registered at module init
 * via SM_Debug_RegisterTag. A uint16_t bitmask tracks enabled/disabled state
 * (all enabled by default).
 */
static const char *sm_tag_names[SM_DEBUG_MAX_TAGS];
static uint8_t sm_tag_count = 0U;
static uint16_t sm_tag_enabled_mask = 0xFFFFU;  /* All 16 slots enabled */

/* --- Periodic interval (5.3) --- */
static uint32_t sm_periodic_interval_ms = 0U;
static uint32_t sm_periodic_last_check  = 0U;

/* --- Level tag strings --- */
static const char * const sm_level_tags[] = {
    "???",    /* 0 -- unused */
    "ERR",    /* 1 */
    "WRN",    /* 2 */
    "INF",    /* 3 */
    "VRB"     /* 4 */
};

/* =============================================================================
 * DEBUG INITIALIZATION
 * ===========================================================================*/

bool SM_Debug_Init(uint8_t interface)
{
    sm_debug_interface = interface;
    sm_debug_initialized = SM_Platform_OutputInit(interface);

    /* Initialize runtime level mask to match compile-time SM_DEBUG_LEVEL.
     * This enables all levels up to SM_DEBUG_LEVEL by default. User can
     * disable individual levels at runtime via SM_Debug_EnableLevel. */
    sm_debug_level_mask = 0U;
    for (uint8_t lvl = 1U; lvl <= 4U; lvl++) {
        if (lvl <= SM_DEBUG_LEVEL) {
            sm_debug_level_mask |= (uint8_t)(1U << lvl);
        }
    }

    /* Reset tag table */
    sm_tag_count = 0U;
    sm_tag_enabled_mask = 0xFFFFU;
    memset(sm_tag_names, 0, sizeof(sm_tag_names));

    /* Reset periodic timer */
    sm_periodic_interval_ms = 0U;
    sm_periodic_last_check = SM_Platform_GetTimeMs();

    return sm_debug_initialized;
}

/* =============================================================================
 * RUNTIME LEVEL CONTROL (5.1)
 * ===========================================================================*/

void SM_Debug_EnableLevel(uint8_t level, bool enable)
{
    if (level == 0U || level > 4U) {
        return;
    }

    if (enable) {
        sm_debug_level_mask |= (uint8_t)(1U << level);
    } else {
        sm_debug_level_mask &= (uint8_t)(~(1U << level));
    }
}

bool SM_Debug_IsLevelEnabled(uint8_t level)
{
    if (level == 0U || level > 4U) {
        return false;
    }
    return (sm_debug_level_mask & (uint8_t)(1U << level)) != 0U;
}

/* =============================================================================
 * PER-MODULE DEBUG TAGS (5.2)
 * ===========================================================================*/

int8_t SM_Debug_RegisterTag(const char *tag_name)
{
    if (tag_name == NULL) {
        return -1;
    }
    if (sm_tag_count >= SM_DEBUG_MAX_TAGS) {
        return -1;
    }

    int8_t id = (int8_t)sm_tag_count;
    sm_tag_names[sm_tag_count] = tag_name;
    sm_tag_count++;
    return id;
}

void SM_Debug_EnableTag(int8_t tag_id, bool enable)
{
    if (tag_id < 0 || tag_id >= SM_DEBUG_MAX_TAGS) {
        return;
    }

    if (enable) {
        sm_tag_enabled_mask |= (uint16_t)(1U << (uint8_t)tag_id);
    } else {
        sm_tag_enabled_mask &= (uint16_t)(~(1U << (uint8_t)tag_id));
    }
}

bool SM_Debug_IsTagEnabled(int8_t tag_id)
{
    /* Invalid tag IDs fail-open (return true) so unregistered modules
     * still get output rather than being silently suppressed. */
    if (tag_id < 0 || tag_id >= SM_DEBUG_MAX_TAGS) {
        return true;
    }
    return (sm_tag_enabled_mask & (uint16_t)(1U << (uint8_t)tag_id)) != 0U;
}

/* =============================================================================
 * PERIODIC INTERVAL HELPER (5.3)
 * ===========================================================================*/

void SM_Debug_SetPeriodicInterval(uint32_t interval_ms)
{
    sm_periodic_interval_ms = interval_ms;
    sm_periodic_last_check = SM_Platform_GetTimeMs();
}

bool SM_Debug_CheckPeriodic(void)
{
    if (sm_periodic_interval_ms == 0U) {
        return true;
    }

    uint32_t now = SM_Platform_GetTimeMs();
    /* Unsigned subtraction handles 32-bit wraparound correctly */
    uint32_t elapsed = now - sm_periodic_last_check;

    if (elapsed >= sm_periodic_interval_ms) {
        sm_periodic_last_check = now;
        return true;
    }

    return false;
}

/* =============================================================================
 * DEBUG OUTPUT
 * ===========================================================================*/

void SM_Debug_Print(uint8_t level, const char *fmt, ...)
{
    if (!sm_debug_initialized || fmt == NULL) {
        return;
    }

    /* Runtime level check (5.1) */
    if (level >= 1U && level <= 4U) {
        if ((sm_debug_level_mask & (uint8_t)(1U << level)) == 0U) {
            return;
        }
    }

    char msg_buf[SM_DEBUG_MSG_MAX_LEN];
    char out_buf[SM_DEBUG_BUFFER_SIZE];

    /* Format the user message */
    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Clamp to buffer size (vsnprintf returns what it *would* have written) */
    if (msg_len < 0) {
        msg_len = 0;
    }
    if ((size_t)msg_len >= sizeof(msg_buf)) {
        msg_len = (int)(sizeof(msg_buf) - 1U);
    }

    /* Level tag */
    const char *tag = (level <= 4U) ? sm_level_tags[level] : "???";

    /* Format with timestamp and level tag */
    uint32_t ts = SM_Platform_GetTimeMs();
    int out_len = snprintf(out_buf, sizeof(out_buf),
                           "[%lu][%s] %s\n",
                           (unsigned long)ts, tag, msg_buf);

    if (out_len < 0) {
        return;
    }
    if ((size_t)out_len >= sizeof(out_buf)) {
        out_len = (int)(sizeof(out_buf) - 1U);
    }

    SM_Platform_OutputSend((const uint8_t *)out_buf, (uint32_t)out_len);
}

void SM_Debug_PrintTagged(int8_t tag_id, uint8_t level, const char *fmt, ...)
{
    if (!sm_debug_initialized || fmt == NULL) {
        return;
    }

    /* Runtime level check (5.1) */
    if (level >= 1U && level <= 4U) {
        if ((sm_debug_level_mask & (uint8_t)(1U << level)) == 0U) {
            return;
        }
    }

    /* Tag filter check (5.2) */
    if (!SM_Debug_IsTagEnabled(tag_id)) {
        return;
    }

    char msg_buf[SM_DEBUG_MSG_MAX_LEN];
    char out_buf[SM_DEBUG_BUFFER_SIZE];

    /* Format the user message */
    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Clamp to buffer size */
    if (msg_len < 0) {
        msg_len = 0;
    }
    if ((size_t)msg_len >= sizeof(msg_buf)) {
        msg_len = (int)(sizeof(msg_buf) - 1U);
    }

    /* Level tag */
    const char *ltag = (level <= 4U) ? sm_level_tags[level] : "???";

    /* Resolve module tag name */
    const char *module_name = "???";
    if (tag_id >= 0 && tag_id < (int8_t)sm_tag_count && sm_tag_names[tag_id] != NULL) {
        module_name = sm_tag_names[tag_id];
    }

    /* Format with timestamp, level tag, and module tag */
    uint32_t ts = SM_Platform_GetTimeMs();
    int out_len = snprintf(out_buf, sizeof(out_buf),
                           "[%lu][%s][%s] %s\n",
                           (unsigned long)ts, ltag, module_name, msg_buf);

    if (out_len < 0) {
        return;
    }
    if ((size_t)out_len >= sizeof(out_buf)) {
        out_len = (int)(sizeof(out_buf) - 1U);
    }

    SM_Platform_OutputSend((const uint8_t *)out_buf, (uint32_t)out_len);
}

void SM_Debug_PrintRaw(const char *msg, uint32_t len)
{
    if (!sm_debug_initialized || msg == NULL || len == 0U) {
        return;
    }

    SM_Platform_OutputSend((const uint8_t *)msg, len);
}

/* =============================================================================
 * HEX DUMP WITH ASCII COLUMN (5.7 enhancement)
 * ===========================================================================*/

void SM_Debug_HexDump(const void *data, uint32_t len)
{
    if (!sm_debug_initialized || data == NULL || len == 0U) {
        return;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    char line_buf[SM_DEBUG_BUFFER_SIZE];

    for (uint32_t offset = 0U; offset < len; offset += 16U) {
        int pos = snprintf(line_buf, sizeof(line_buf), "%04lX: ", (unsigned long)offset);
        if (pos < 0) {
            return;
        }

        /* Number of bytes in this row */
        uint32_t row_len = (len - offset);
        if (row_len > 16U) {
            row_len = 16U;
        }

        /* Hex bytes */
        for (uint32_t i = 0U; i < 16U; i++) {
            if (i < row_len) {
                int written = snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                                       "%02X ", bytes[offset + i]);
                if (written > 0) {
                    pos += written;
                }
            } else {
                /* Pad short rows with spaces to align ASCII column */
                if ((size_t)pos + 3U < sizeof(line_buf)) {
                    line_buf[pos++] = ' ';
                    line_buf[pos++] = ' ';
                    line_buf[pos++] = ' ';
                }
            }
        }

        /* ASCII column separator */
        if ((size_t)pos + 1U < sizeof(line_buf)) {
            line_buf[pos++] = ' ';
        }

        /* ASCII representation */
        for (uint32_t i = 0U; i < row_len; i++) {
            uint8_t ch = bytes[offset + i];
            if ((size_t)pos + 1U < sizeof(line_buf)) {
                line_buf[pos++] = (ch >= 0x20U && ch <= 0x7EU) ? (char)ch : '.';
            }
        }

        /* Newline */
        if ((size_t)pos < sizeof(line_buf) - 1U) {
            line_buf[pos++] = '\n';
        }

        SM_Platform_OutputSend((const uint8_t *)line_buf, (uint32_t)pos);
    }
}

#endif /* SM_FEATURE_DEBUG */
