/**
 * @file sm_error.c
 * @brief Error handler implementation (v3.0 Phase 3)
 * @version 4.1.0
 * @date 2026-04-19
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Per-instance error handling via SM_Handle_t.
 * DIS on critical_lock (D7). Numeric assertion IDs 700-799.
 * Error statistics tracking with per-level counts and recovery metrics.
 *
 * Assertion ID ranges:
 *   700-709  SM_Error_Report
 *   710-719  SM_Error_IsCriticalLock / query functions
 *   720-729  SM_Error_AttemptRecovery
 *   730-739  SM_Error_GetStats / SM_Error_GetHistory
 *   740-749  internal helpers
 *   750-759  MINOR tier accessors (v4.2, D18)
 */

#include "sm_framework/sm_framework.h"
#include <string.h>

SM_DEFINE_MODULE("sm_error");

/* =============================================================================
 * INTERNAL HELPERS
 * ===========================================================================*/

static void sm_error_add_to_history(SM_Handle_t sm, const SM_ErrorInfo_t *info)
{
    SM_REQUIRE(740, sm != NULL);
    SM_REQUIRE(741, info != NULL);

    SM_ErrorHandler_t *eh = &sm->error;

    SM_REQUIRE(742, eh->history_index < SM_ERROR_HISTORY_SIZE);

    eh->history[eh->history_index] = *info;
    eh->history_index = (uint8_t)((eh->history_index + 1U) % SM_ERROR_HISTORY_SIZE);

    if (eh->history_count < SM_ERROR_HISTORY_SIZE) {
        eh->history_count++;
    }
}

/* =============================================================================
 * ERROR REPORTING
 * ===========================================================================*/

bool SM_Error_Report(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code)
{
    if (sm == NULL || !sm->initialized) {
        return false;
    }

    if (level == SM_ERROR_NONE || level >= SM_ERROR_LEVEL_COUNT) {
        return false;
    }

    /* Build error record */
    SM_ErrorInfo_t info;
    info.level = level;
    info.code = code;
    info.state = sm->current_state;
    info.timestamp = SM_Platform_GetTimeMs();
    info.retry_count = 0U;
    info.recovered = false;

    /* Add to history */
    sm_error_add_to_history(sm, &info);

    /* Set as current error */
    sm->error.current = info;

    /* Update statistics */
    SM_REQUIRE(700, (uint8_t)level < SM_ERROR_LEVEL_COUNT);
    sm->error.stats.errors_by_level[level]++;
    sm->error.stats.last_error_time = info.timestamp;

    /* Handle based on severity */
    switch (level) {
        case SM_ERROR_MINOR:
            sm->error.minor_active = true;
            sm->error.minor_timestamp = info.timestamp;
            SM_LOG_WARN("SM_Error: MINOR code=%u state=%u",
                        (unsigned)code, (unsigned)info.state);
            break;

        case SM_ERROR_NORMAL:
            sm->error.minor_active = false;
            SM_LOG_WARN("SM_Error: NORMAL code=%u state=%u",
                        (unsigned)code, (unsigned)info.state);
            break;

        case SM_ERROR_CRITICAL:
            sm->error.minor_active = false;
            /* Indivisible (v4.1): SM_Error_IsCriticalLock is documented
             * ISR-safe and verifies this pair, so it must never observe the
             * lock set with a stale shadow. */
            SM_DIS_ASSIGN(sm->error.critical_lock,
                          sm->error.critical_lock_dis, uint8_t, true);
            SM_LOG_ERROR("SM_Error: CRITICAL code=%u state=%u -- SYSTEM LOCKED",
                         (unsigned)code, (unsigned)info.state);
            break;

        default:
            break;
    }

    /* Notify callback if registered */
    if (sm->error_cb != NULL) {
        sm->error_cb(sm, level, code);
    }

    return true;
}

void SM_Error_Clear(SM_Handle_t sm)
{
    if (sm == NULL) {
        return;
    }

    sm->error.current.level = SM_ERROR_NONE;
    sm->error.current.code = 0U;
    sm->error.current.state = 0U;
    sm->error.current.timestamp = 0U;
    sm->error.current.retry_count = 0U;
    sm->error.current.recovered = false;
    sm->error.minor_active = false;

    /* critical_lock is NOT cleared -- requires SM_Reset or HW reset.
     * DIS shadow remains consistent (unchanged since last write). */
}

/* =============================================================================
 * ERROR STATUS QUERIES
 * ===========================================================================*/

bool SM_Error_IsMinorActive(SM_Handle_t sm)
{
    SM_REQUIRE(750, sm != NULL);

    if (sm == NULL) {
        return false;
    }
    return sm->error.minor_active;
}

bool SM_Error_GetMinorTimestamp(SM_Handle_t sm, uint32_t *out_ms)
{
    SM_REQUIRE(751, sm != NULL);
    SM_REQUIRE(752, out_ms != NULL);

    if (sm == NULL || out_ms == NULL) {
        return false;
    }
    if (!sm->error.minor_active) {
        return false;   /* out_ms deliberately untouched */
    }
    *out_ms = sm->error.minor_timestamp;
    return true;
}

void SM_Error_ClearMinor(SM_Handle_t sm)
{
    SM_REQUIRE(753, sm != NULL);

    if (sm == NULL) {
        return;
    }
    sm->error.minor_active = false;
    /* minor_timestamp is left alone: it is only meaningful while
     * minor_active, and GetMinorTimestamp gates on that flag. */
}

bool SM_Error_IsCriticalLock(SM_Handle_t sm)
{
    if (sm == NULL) {
        return false;
    }

    SM_DIS_VERIFY(sm->error.critical_lock ? 1U : 0U,
                  sm->error.critical_lock_dis, uint8_t, 710);

    return sm->error.critical_lock;
}

bool SM_Error_GetCurrent(SM_Handle_t sm, SM_ErrorInfo_t *info)
{
    if (sm == NULL || info == NULL) {
        return false;
    }

    *info = sm->error.current;
    return true;
}

bool SM_Error_GetHistory(SM_Handle_t sm, uint8_t index, SM_ErrorInfo_t *info)
{
    if (sm == NULL || info == NULL) {
        return false;
    }

    if (index >= sm->error.history_count) {
        return false;
    }

    SM_REQUIRE(730, sm->error.history_index <= SM_ERROR_HISTORY_SIZE);

    /* index 0 = most recent */
    uint8_t actual = (uint8_t)((sm->error.history_index + SM_ERROR_HISTORY_SIZE
                                - 1U - index) % SM_ERROR_HISTORY_SIZE);

    SM_REQUIRE(731, actual < SM_ERROR_HISTORY_SIZE);

    *info = sm->error.history[actual];
    return true;
}

uint8_t SM_Error_GetHistoryCount(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->error.history_count;
}

/* =============================================================================
 * ERROR RECOVERY
 * ===========================================================================*/

bool SM_Error_AttemptRecovery(SM_Handle_t sm)
{
    if (sm == NULL || !sm->initialized) {
        return false;
    }

    if (sm->error.critical_lock) {
        return false;
    }

    if (sm->error.current.level == SM_ERROR_NONE) {
        return true;  /* No error -- nothing to recover */
    }

    sm->error.current.retry_count++;

    if (sm->error.current.retry_count >= SM_ERROR_MAX_RECOVERY) {
        SM_LOG_ERROR("SM_Error_AttemptRecovery: max retries (%u) exceeded",
                     (unsigned)SM_ERROR_MAX_RECOVERY);
        sm->error.stats.recovery_fail++;
        return false;
    }

    /* Call registered recovery callback if available */
    if (sm->recovery_cb != NULL) {
        bool recovered = sm->recovery_cb(sm, sm->error.current.code);
        if (recovered) {
            sm->error.current.recovered = true;
            sm->error.stats.recovery_success++;
            SM_LOG_INFO("SM_Error_AttemptRecovery: recovered (code=%u)",
                        (unsigned)sm->error.current.code);
        } else {
            sm->error.stats.recovery_fail++;
        }
        return recovered;
    }

    /* No callback registered -- cannot recover */
    sm->error.stats.recovery_fail++;
    return false;
}

/* =============================================================================
 * CALLBACK REGISTRATION
 * ===========================================================================*/

void SM_Error_RegisterRecoveryCallback(SM_Handle_t sm, SM_RecoveryCallback_t cb)
{
    if (sm == NULL) {
        return;
    }
    sm->recovery_cb = cb;
}

void SM_Error_RegisterNotifyCallback(SM_Handle_t sm, SM_ErrorCallback_t cb)
{
    if (sm == NULL) {
        return;
    }
    sm->error_cb = cb;
}

/* =============================================================================
 * ERROR STATISTICS
 * ===========================================================================*/

bool SM_Error_GetStats(SM_Handle_t sm, SM_ErrorStats_t *stats)
{
    if (sm == NULL || stats == NULL) {
        return false;
    }

    *stats = sm->error.stats;
    return true;
}
