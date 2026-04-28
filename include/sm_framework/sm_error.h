/**
 * @file sm_error.h
 * @brief Error handler API for State Machine Framework v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Three-tier error handling system:
 *   MINOR:    Auto-recovery, no state change required
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
 *   MINOR:    Sets minor_active flag, application can auto-recover
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
 * Resets current error to SM_ERROR_NONE.
 * Does NOT clear critical lock -- that requires SM_Reset() or hardware reset.
 *
 * @note An error timestamp of 0 ms is valid at boot; do not treat it as a
 *       special sentinel unless your platform guarantees time never starts at 0.
 *
 * @param sm Handle to the state machine instance
 */
void SM_Error_Clear(SM_Handle_t sm);

/* =============================================================================
 * ERROR STATUS QUERIES
 * ===========================================================================*/

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
