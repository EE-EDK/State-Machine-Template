/**
 * @file sm_error.h
 * @brief Error handler API for State Machine Framework v4.2
 * @version 4.2.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Two enforced tiers and one informational tier:
 *   MINOR:    RECORDED AND QUERYABLE. The framework takes no action of its own
 *             -- it does not know what recovering from your minor error would
 *             mean. Poll SM_Error_IsMinorActive / SM_Error_GetMinorTimestamp
 *             and apply your own policy; SM_Error_ClearMinor retires it.
 *             (Before v4.2 this tier was documented as "auto-recovery". It
 *             never was: the flag had no reader anywhere in the framework and
 *             no public accessor. See MIGRATION.md v4.1 -> v4.2.)
 *   NORMAL:   Managed recovery, application drives recovery logic
 *   CRITICAL: System lock, requires hardware reset or watchdog
 *
 * All functions take SM_Handle_t -- errors are per-instance.
 * DIS (Duplicate Inverse Storage) protects critical_lock from corruption.
 * Numeric assertion IDs 700-799, module "sm_error".
 *
 * ISR safety:
 *   SM_Error_IsCriticalLock -- ISR-safe (volatile read + DIS verify)
 *   All other functions     -- NOT ISR-safe
 *
 * Replaces the v2 sm_error_handler.h.
 */

#ifndef SM_ERROR_H
#define SM_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

/* =============================================================================
 * ERROR REPORTING
 * ===========================================================================*/

/**
 * @brief Report an error
 *
 * Records the error in history and sets current error.
 * Behavior depends on level:
 *   MINOR:    Records the error and stamps the time. Nothing else -- the
 *             application decides what, if anything, to do about it.
 *   NORMAL:   Sets current error, application should enter recovery
 *   CRITICAL: Sets critical_lock, system is locked
 *
 * @param sm    Handle to the state machine instance
 * @param level Error severity level
 * @param code  User-defined error code
 * @return true if error recorded, false if invalid parameters
 */
bool SM_Error_Report(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code);

/**
 * @brief Clear the current error
 *
 * Resets current error to SM_ERROR_NONE and clears stored state index and
 * timestamp on the current record.
 * Does NOT clear critical lock -- that requires SM_Reset() or hardware reset.
 *
 * @note An error timestamp of 0 ms is valid at boot; after Clear, 0 means “no
 *       timestamp recorded,” not necessarily boot time.
 *
 * @param sm Handle to the state machine instance
 */
void SM_Error_Clear(SM_Handle_t sm);

/* =============================================================================
 * ERROR STATUS QUERIES
 * ===========================================================================*/

/**
 * @brief Is a MINOR error currently outstanding?
 *
 * MINOR is the informational tier: SM_Error_Report records it and stamps the
 * time, and the framework does nothing further. This is how an application
 * reads that state so it can apply its own policy -- retry, degrade, log, or
 * ignore. Cleared by SM_Error_ClearMinor, by SM_Error_Clear, by reporting a
 * NORMAL or CRITICAL error, and by SM_Reset.
 *
 * @param sm Handle to the state machine instance
 * @return true if a MINOR error is outstanding; false if none, or sm is NULL
 *
 * @note NOT ISR-safe.
 */
bool SM_Error_IsMinorActive(SM_Handle_t sm);

/**
 * @brief When the outstanding MINOR error was reported
 *
 * The timestamp is what any time-based application policy needs -- "clear it
 * after 500 ms", "escalate if it has been up for a second". The framework
 * implements no such policy; it supplies the number.
 *
 * @param sm     Handle to the state machine instance
 * @param out_ms Receives the SM_Platform_GetTimeMs value at which the MINOR
 *               error was reported. Untouched when this returns false.
 * @return true if a MINOR error is outstanding and out_ms was written
 *
 * @note NOT ISR-safe.
 */
bool SM_Error_GetMinorTimestamp(SM_Handle_t sm, uint32_t *out_ms);

/**
 * @brief Retire the outstanding MINOR error, leaving the current error record
 *
 * Distinct from SM_Error_Clear, which also wipes the current error. Use this
 * when the application's minor-error policy has run its course but a NORMAL
 * error is still being worked on.
 *
 * @param sm Handle to the state machine instance
 *
 * @note NOT ISR-safe.
 */
void SM_Error_ClearMinor(SM_Handle_t sm);

/**
 * @brief Check if critical error lock is active (ISR-safe)
 *
 * Reads the volatile critical_lock field and verifies its DIS shadow.
 *
 * @param sm Handle to the state machine instance
 * @return true if system is locked in critical error state
 */
bool SM_Error_IsCriticalLock(SM_Handle_t sm);

/**
 * @brief Get current error information
 *
 * @param sm   Handle to the state machine instance
 * @param info Output: current error info
 * @return true if successful, false if info is NULL
 */
bool SM_Error_GetCurrent(SM_Handle_t sm, SM_ErrorInfo_t *info);

/**
 * @brief Get an error from history
 *
 * @param sm    Handle to the state machine instance
 * @param index History index (0 = most recent)
 * @param info  Output: error info at that index
 * @return true if successful, false if invalid index or NULL pointer
 */
bool SM_Error_GetHistory(SM_Handle_t sm, uint8_t index, SM_ErrorInfo_t *info);

/**
 * @brief Get number of errors stored in history
 *
 * @param sm Handle to the state machine instance
 * @return Actual number of errors in history (not always max)
 */
uint8_t SM_Error_GetHistoryCount(SM_Handle_t sm);

/* =============================================================================
 * ERROR RECOVERY
 * ===========================================================================*/

/**
 * @brief Attempt recovery from the current error
 *
 * Calls the registered recovery callback if set.
 * Increments retry_count. If retry_count >= SM_ERROR_MAX_RECOVERY,
 * returns false (caller should escalate).
 *
 * @param sm Handle to the state machine instance
 * @return true if recovery succeeded, false if failed or max retries exceeded
 *
 * @note Returns false immediately while critical_lock is active (CRITICAL path).
 */
bool SM_Error_AttemptRecovery(SM_Handle_t sm);

/* =============================================================================
 * CALLBACK REGISTRATION
 * ===========================================================================*/

/**
 * @brief Register a recovery callback
 *
 * Called by SM_Error_AttemptRecovery() to attempt error-specific recovery.
 *
 * @param sm Handle to the state machine instance
 * @param cb Recovery callback function (NULL to remove)
 */
void SM_Error_RegisterRecoveryCallback(SM_Handle_t sm, SM_RecoveryCallback_t cb);

/**
 * @brief Register an error notification callback
 *
 * Called by SM_Error_Report() whenever any error is reported.
 * Useful for logging, LED indication, etc.
 *
 * @param sm Handle to the state machine instance
 * @param cb Error notification callback function (NULL to remove)
 */
void SM_Error_RegisterNotifyCallback(SM_Handle_t sm, SM_ErrorCallback_t cb);

/* =============================================================================
 * ERROR STATISTICS
 * ===========================================================================*/

/**
 * @brief Get error statistics
 *
 * Copies cumulative error statistics: per-level counts, recovery
 * success/fail totals, and timestamp of the most recent error.
 *
 * @param sm    Handle to the state machine instance
 * @param stats Output: error statistics snapshot
 * @return true if successful, false if NULL parameter
 */
bool SM_Error_GetStats(SM_Handle_t sm, SM_ErrorStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* SM_ERROR_H */
