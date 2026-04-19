/**
 * @file sm_debug.c
 * @brief Debug messaging system implementation (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Rewritten for v3 API. Fixes from v2:
 *   - snprintf return value handled correctly (cast + clamp)
 *   - No no-op enable functions
 *   - No dependency on application state types
 *   - Uses generalized SM_Platform_OutputInit/OutputSend (not per-protocol)
 *
 * When SM_FEATURE_DEBUG == 0, this file compiles to nothing (all functions
 * are macro'd to no-ops in the header).
 */

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

/* =============================================================================
 * DEBUG INITIALIZATION
 * ===========================================================================*/

bool SM_Debug_Init(uint8_t interface)
{
    sm_debug_interface = interface;
    sm_debug_initialized = SM_Platform_OutputInit(interface);
    return sm_debug_initialized;
}

/* =============================================================================
 * DEBUG OUTPUT
 * ===========================================================================*/

void SM_Debug_Print(uint8_t level, const char *fmt, ...)
{
    if (!sm_debug_initialized || fmt == NULL) {
        return;
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
    static const char * const level_tags[] = {
        "???",    /* 0 -- unused */
        "ERR",    /* 1 */
        "WRN",    /* 2 */
        "INF",    /* 3 */
        "VRB"     /* 4 */
    };
    const char *tag = (level <= 4U) ? level_tags[level] : "???";

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

void SM_Debug_PrintRaw(const char *msg, uint32_t len)
{
    if (!sm_debug_initialized || msg == NULL || len == 0U) {
        return;
    }

    SM_Platform_OutputSend((const uint8_t *)msg, len);
}

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

        /* Hex bytes */
        for (uint32_t i = 0U; i < 16U && (offset + i) < len; i++) {
            int written = snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                                   "%02X ", bytes[offset + i]);
            if (written > 0) {
                pos += written;
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
