/**
 * @file sm_error_handler.h
 * @brief Error handling system API
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * Three-tier error handling system:
 * - MINOR: Auto-recovery, no state change
 * - NORMAL: Managed recovery via RECOVERY state
 * - CRITICAL: System lock, requires reset
 */

#ifndef SM_ERROR_HANDLER_H
#define SM_ERROR_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

/* =============================================================================
 * ERROR HANDLER INITIALIZATION
 * ===========================================================================*/

/**
 * @brief Initialize error handler
 *
 * @return true if initialization successful, false otherwise
 *
 * @note Called automatically by StateMachine_Init()
 */
bool ErrorHandler_Init(void);

/* =============================================================================
 * ERROR REPORTING
 * ===========================================================================*/

/**
 * @brief Report an error
 *
 * Main entry point for error reporting. Automatically handles error based on
 * severity level:
 * - MINOR: Attempts auto-recovery
 * - NORMAL: Transitions to RECOVERY state
 * - CRITICAL: Locks system in CRITICAL_ERROR state
 *
 * @param level Error severity level
 * @param code Specific error code
 * @return true if error handled, false if invalid parameters
 *
 * @note All errors are logged in error history
 */
bool ErrorHandler_Report(ErrorLevel_t level, ErrorCode_t code);

/**
 * @brief Handle minor error with auto-recovery
 *
 * Attempts automatic recovery without state transition.
 * If recovery fails within timeout, escalates to normal error.
 *
 * @param code Error code
 * @return true if recovery initiated, false if escalated
 *
 * @note Used internally by ErrorHandler_Report(), but can be called directly
 */
bool ErrorHandler_HandleMinorError(ErrorCode_t code);

/**
 * @brief Handle normal error with managed recovery
 *
 * Triggers transition to RECOVERY state where recovery will be attempted.
 *
 * @param code Error code
 * @return true if error handled, false if invalid parameters
 */
bool ErrorHandler_HandleNormalError(ErrorCode_t code);

/**
 * @brief Handle critical error
 *
 * Immediately locks system in CRITICAL_ERROR state.
 * System cannot recover without manual reset or watchdog.
 *
 * @param code Error code
 */
void ErrorHandler_HandleCriticalError(ErrorCode_t code);

/* =============================================================================
 * ERROR RECOVERY
 * ===========================================================================*/

/**
 * @brief Attempt to recover from current error
 *
 * Called from RECOVERY state to attempt error recovery.
 * Implements recovery logic based on error code.
 *
 * @return true if recovery successful, false if recovery failed
 *
 * @note After ERROR_MAX_RECOVERY_ATTEMPTS failed attempts, error escalates
 *       to critical level.
 */
bool ErrorHandler_AttemptRecovery(void);

/**
 * @brief Clear current error
 *
 * Clears the current error status. Used after successful recovery.
 *
 * @note Does NOT clear critical error lock - that requires reset
 */
void ErrorHandler_ClearError(void);

/* =============================================================================
 * ERROR STATUS QUERIES
 * ===========================================================================*/

/**
 * @brief Check if critical error lock is active
 *
 * @return true if system is locked in critical error state, false otherwise
 */
bool ErrorHandler_IsCriticalLock(void);

/**
 * @brief Get current error information
 *
 * @param error_info Pointer to structure to fill with error information
 * @return true if successful, false if error_info is NULL
 */
bool ErrorHandler_GetCurrentError(ErrorInfo_t *error_info);

/**
 * @brief Get error from history
 *
 * @param index History index (0 = most recent, 1 = second most recent, etc.)
 * @param error_info Pointer to structure to fill with error information
 * @return true if successful, false if invalid index or NULL pointer
 *
 * @note History is circular buffer of size ERROR_HISTORY_SIZE
 */
bool ErrorHandler_GetHistoryError(uint8_t index, ErrorInfo_t *error_info);

/**
 * @brief Get number of errors in history
 *
 * @return Number of logged errors (up to ERROR_HISTORY_SIZE)
 */
uint8_t ErrorHandler_GetHistoryCount(void);

/* =============================================================================
 * STRING CONVERSION UTILITIES
 * ===========================================================================*/

/**
 * @brief Convert error code to string
 *
 * @param code Error code to convert
 * @return Pointer to constant string (e.g., "TIMEOUT", "COMM_LOST")
 *
 * @note Returns "UNKNOWN" for invalid error codes
 */
const char *ErrorHandler_CodeToString(ErrorCode_t code);

/**
 * @brief Convert error level to string
 *
 * @param level Error level to convert
 * @return Pointer to constant string (e.g., "MINOR", "CRITICAL")
 *
 * @note Returns "UNKNOWN" for invalid error levels
 */
const char *ErrorHandler_LevelToString(ErrorLevel_t level);

/* =============================================================================
 * COMMUNICATION VERIFICATION (for minor error recovery)
 * ===========================================================================*/

/**
 * @brief Verify communication channel is functioning
 *
 * Used for minor error recovery. Checks if communication channel is working
 * by requiring COMM_VERIFICATION_COUNT good messages within
 * COMM_VERIFICATION_WINDOW_MS.
 *
 * @return true if channel verified, false if verification incomplete
 *
 * @note Call this function when a good message is received to increment counter
 */
bool ErrorHandler_VerifyCommChannel(void);

/* =============================================================================
 * ADVANCED: CUSTOM RECOVERY HANDLERS
 * ===========================================================================*/

/**
 * @brief Function pointer type for custom error recovery handler
 *
 * @param code Error code to recover from
 * @return true if recovery successful, false if recovery failed
 */
typedef bool (*ErrorRecoveryHandler_t)(ErrorCode_t code);

/**
 * @brief Register custom recovery handler for specific error code
 *
 * Allows application to provide custom recovery logic for specific errors.
 *
 * @param code Error code to handle
 * @param handler Recovery handler function (NULL to remove handler)
 * @return true if handler registered, false if invalid parameters
 *
 * @note If custom handler is registered, it will be called instead of
 *       default recovery logic for that error code.
 */
bool ErrorHandler_RegisterRecoveryHandler(ErrorCode_t code, ErrorRecoveryHandler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* SM_ERROR_HANDLER_H */
