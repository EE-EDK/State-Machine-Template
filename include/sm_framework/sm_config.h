/**
 * @file sm_config.h
 * @brief Configuration defaults for State Machine Framework
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * This file provides default configuration values. Users should create their
 * own app_config.h and define values there to override these defaults.
 *
 * USAGE:
 *   1. Copy config/app_config_template.h to your project as app_config.h
 *   2. Define your configuration values in app_config.h
 *   3. Include this header (sm_config.h) - it will use your overrides
 */

#ifndef SM_CONFIG_H
#define SM_CONFIG_H

/* =============================================================================
 * STATE MACHINE CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Maximum number of states the state machine can handle
 *
 * Increase this if you need more than 10 states. Must be >= STATE_MAX.
 */
#ifndef SM_MAX_STATES
#define SM_MAX_STATES (10U)
#endif

/**
 * @brief Maximum number of transitions per state
 *
 * Each state can have up to this many defined transitions.
 */
#ifndef SM_MAX_TRANSITIONS_PER_STATE
#define SM_MAX_TRANSITIONS_PER_STATE (5U)
#endif

/**
 * @brief Default state timeout in milliseconds
 *
 * Used when a state doesn't specify a custom timeout.
 */
#ifndef SM_STATE_TIMEOUT_MS
#define SM_STATE_TIMEOUT_MS (5000U)
#endif

/**
 * @brief State machine task execution period in milliseconds
 *
 * How often App_Main_Task() should be called.
 * - Fast systems: 1-10ms
 * - Normal systems: 10-50ms
 * - Low power systems: 100-1000ms
 */
#ifndef SM_TASK_PERIOD_MS
#define SM_TASK_PERIOD_MS (10U)
#endif

/* =============================================================================
 * ERROR HANDLING CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Maximum number of recovery attempts for normal errors
 *
 * After this many failed recovery attempts, error escalates to critical.
 */
#ifndef ERROR_MAX_RECOVERY_ATTEMPTS
#define ERROR_MAX_RECOVERY_ATTEMPTS (3U)
#endif

/**
 * @brief Number of retries for minor errors before escalation
 *
 * Minor errors attempt auto-recovery this many times.
 */
#ifndef ERROR_MINOR_RETRY_COUNT
#define ERROR_MINOR_RETRY_COUNT (3U)
#endif

/**
 * @brief Timeout for minor error recovery window in milliseconds
 *
 * Minor error must be resolved within this time or escalates to normal error.
 */
#ifndef ERROR_MINOR_TIMEOUT_MS
#define ERROR_MINOR_TIMEOUT_MS (50U)
#endif

/**
 * @brief Size of circular error history buffer
 *
 * Keeps track of last N errors for debugging.
 * - Small systems: 4-8 entries
 * - Normal systems: 16-32 entries
 * - Debug builds: 64+ entries
 */
#ifndef ERROR_HISTORY_SIZE
#define ERROR_HISTORY_SIZE (16U)
#endif

/* =============================================================================
 * DEBUG SYSTEM CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Debug message buffer size
 *
 * Maximum size of formatted debug output buffer.
 */
#ifndef DEBUG_BUFFER_SIZE
#define DEBUG_BUFFER_SIZE (256U)
#endif

/**
 * @brief Maximum debug message length
 *
 * Maximum length of a single debug message string.
 */
#ifndef DEBUG_MAX_MESSAGE_LENGTH
#define DEBUG_MAX_MESSAGE_LENGTH (128U)
#endif

/**
 * @brief Enable initialization debug messages
 *
 * Set to 0 to disable init messages in production.
 */
#ifndef DEBUG_ENABLE_INIT_MESSAGES
#define DEBUG_ENABLE_INIT_MESSAGES (1U)
#endif

/**
 * @brief Enable runtime debug messages
 *
 * Set to 0 to disable runtime messages in production.
 */
#ifndef DEBUG_ENABLE_RUNTIME_MESSAGES
#define DEBUG_ENABLE_RUNTIME_MESSAGES (1U)
#endif

/**
 * @brief Enable periodic debug messages
 *
 * Set to 0 to disable periodic status messages in production.
 */
#ifndef DEBUG_ENABLE_PERIODIC_MESSAGES
#define DEBUG_ENABLE_PERIODIC_MESSAGES (1U)
#endif

/**
 * @brief Interval for periodic debug messages in milliseconds
 *
 * How often to send periodic status updates.
 */
#ifndef DEBUG_PERIODIC_INTERVAL_MS
#define DEBUG_PERIODIC_INTERVAL_MS (1000U)
#endif

/* =============================================================================
 * COMMUNICATION CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Communication packet size
 *
 * Maximum size of communication packets.
 */
#ifndef COMM_PACKET_SIZE
#define COMM_PACKET_SIZE (64U)
#endif

/**
 * @brief Communication timeout in milliseconds
 *
 * Maximum time to wait for communication to complete.
 */
#ifndef COMM_TIMEOUT_MS
#define COMM_TIMEOUT_MS (100U)
#endif

/**
 * @brief Communication retry count
 *
 * Number of times to retry failed communication.
 */
#ifndef COMM_RETRY_COUNT
#define COMM_RETRY_COUNT (3U)
#endif

/**
 * @brief Number of good messages needed to verify channel
 *
 * Used for minor error recovery - need this many consecutive good messages.
 */
#ifndef COMM_VERIFICATION_COUNT
#define COMM_VERIFICATION_COUNT (3U)
#endif

/**
 * @brief Verification window in milliseconds
 *
 * Good messages must arrive within this window to verify channel.
 */
#ifndef COMM_VERIFICATION_WINDOW_MS
#define COMM_VERIFICATION_WINDOW_MS (50U)
#endif

/* =============================================================================
 * FEATURE FLAGS
 * ===========================================================================*/

/**
 * @brief Enable statistics collection
 *
 * Collects runtime statistics (transitions, errors, timing, etc.)
 */
#ifndef FEATURE_STATISTICS_ENABLED
#define FEATURE_STATISTICS_ENABLED (0U)
#endif

/**
 * @brief Enable runtime assertions
 *
 * Enable assert checks for development/debugging.
 */
#ifndef FEATURE_ASSERT_ENABLED
#define FEATURE_ASSERT_ENABLED (1U)
#endif

/* =============================================================================
 * CONFIGURATION VALIDATION
 * ===========================================================================*/

#if (SM_TASK_PERIOD_MS == 0)
#error "SM_TASK_PERIOD_MS cannot be zero"
#endif

#if (ERROR_MAX_RECOVERY_ATTEMPTS == 0)
#warning "ERROR_MAX_RECOVERY_ATTEMPTS is zero - no recovery will be attempted"
#endif

#if (DEBUG_MAX_MESSAGE_LENGTH < 32)
#warning "DEBUG_MAX_MESSAGE_LENGTH is very small - messages may be truncated"
#endif

#if (SM_MAX_STATES < STATE_MAX)
#error "SM_MAX_STATES must be >= STATE_MAX"
#endif

#endif /* SM_CONFIG_H */
