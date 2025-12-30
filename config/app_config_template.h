/**
 * @file app_config_template.h
 * @brief User configuration template
 * @version 2.0.0
 *
 * Copy this file to your project as "app_config.h" and customize the values.
 * Include app_config.h BEFORE including sm_framework.h in your main application.
 *
 * USAGE:
 *   1. Copy this file: cp config/app_config_template.h  your_project/app_config.h
 *   2. Edit app_config.h with your custom values
 *   3. In your main.c: #include "app_config.h"
 *                      #include "sm_framework/sm_framework.h"
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* =============================================================================
 * PROJECT IDENTIFICATION
 * ===========================================================================*/

#define PROJECT_NAME "My Embedded Project"
#define PROJECT_VERSION "1.0.0"
#define HARDWARE_PLATFORM "Generic"

/* =============================================================================
 * STATE MACHINE CONFIGURATION
 * ===========================================================================*/

/* Maximum number of states (must be >= STATE_MAX) */
#define SM_MAX_STATES (10U)

/* Maximum transitions per state */
#define SM_MAX_TRANSITIONS_PER_STATE (5U)

/* Default state timeout in milliseconds */
#define SM_STATE_TIMEOUT_MS (5000U)

/* Task execution period in milliseconds
 * Adjust based on your system requirements:
 * - Fast response systems: 1-10ms
 * - Normal systems: 10-50ms
 * - Low power systems: 100-1000ms
 */
#define SM_TASK_PERIOD_MS (10U)

/* =============================================================================
 * ERROR HANDLING CONFIGURATION
 * ===========================================================================*/

/* Maximum recovery attempts for normal errors */
#define ERROR_MAX_RECOVERY_ATTEMPTS (3U)

/* Minor error retry count */
#define ERROR_MINOR_RETRY_COUNT (3U)

/* Minor error timeout window (ms) */
#define ERROR_MINOR_TIMEOUT_MS (50U)

/* Error history buffer size
 * Adjust based on available RAM:
 * - Constrained systems: 4-8 entries
 * - Normal systems: 16-32 entries
 * - Debug builds: 64+ entries
 */
#define ERROR_HISTORY_SIZE (16U)

/* =============================================================================
 * DEBUG SYSTEM CONFIGURATION
 * ===========================================================================*/

/* Debug buffer size */
#define DEBUG_BUFFER_SIZE (256U)

/* Maximum debug message length */
#define DEBUG_MAX_MESSAGE_LENGTH (128U)

/* Enable/disable message types
 * Set to 0 to disable in production builds
 */
#define DEBUG_ENABLE_INIT_MESSAGES (1U)      /* Initialization messages */
#define DEBUG_ENABLE_RUNTIME_MESSAGES (1U)   /* Runtime messages */
#define DEBUG_ENABLE_PERIODIC_MESSAGES (1U)  /* Periodic status */

/* Periodic message interval (ms) */
#define DEBUG_PERIODIC_INTERVAL_MS (1000U)

/* =============================================================================
 * COMMUNICATION CONFIGURATION
 * ===========================================================================*/

/* Packet size */
#define COMM_PACKET_SIZE (64U)

/* Communication timeout (ms) */
#define COMM_TIMEOUT_MS (100U)

/* Retry count */
#define COMM_RETRY_COUNT (3U)

/* Channel verification parameters */
#define COMM_VERIFICATION_COUNT (3U)         /* Good messages needed */
#define COMM_VERIFICATION_WINDOW_MS (50U)    /* Verification window */

/* =============================================================================
 * FEATURE FLAGS
 * ===========================================================================*/

/* Enable runtime statistics collection
 * Set to 1 to collect transition counts, timing, etc.
 * Adds ~100 bytes RAM overhead
 */
#define FEATURE_STATISTICS_ENABLED (0U)

/* Enable runtime assertions
 * Set to 1 for development, 0 for production
 */
#define FEATURE_ASSERT_ENABLED (1U)

/* =============================================================================
 * PLATFORM-SPECIFIC SETTINGS (EXAMPLES)
 * ===========================================================================*/

#if defined(STM32F407xx)
    /* STM32F4 specific settings */
    #define SYSTEM_CLOCK_HZ (168000000UL)
    #define FLASH_SIZE_KB (1024U)
    #define RAM_SIZE_KB (192U)

#elif defined(ESP32)
    /* ESP32 specific settings */
    #define SYSTEM_CLOCK_HZ (240000000UL)
    #define FLASH_SIZE_KB (4096U)
    #define RAM_SIZE_KB (520U)

#elif defined(RP2040)
    /* RP2040 specific settings */
    #define SYSTEM_CLOCK_HZ (133000000UL)
    #define FLASH_SIZE_KB (2048U)
    #define RAM_SIZE_KB (264U)

#else
    /* Generic/simulation */
    #define SYSTEM_CLOCK_HZ (100000000UL)
    #define FLASH_SIZE_KB (512U)
    #define RAM_SIZE_KB (128U)
#endif

#endif /* APP_CONFIG_H */
