/**
 * @file sm_config_template.h
 * @brief User configuration template for State Machine Framework v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * Copy this file to your project as "app_config.h" and customize the values.
 * Include app_config.h BEFORE including sm_framework.h in your build.
 *
 * USAGE:
 *   1. Copy this file: cp config/sm_config_template.h your_project/app_config.h
 *   2. Define SM_STATE_COUNT and SM_EVENT_COUNT (REQUIRED)
 *   3. Override any other defaults as needed
 *   4. Add app_config.h include path to your build system
 *   5. #include "app_config.h" before #include "sm_framework/sm_framework.h"
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* =============================================================================
 * REQUIRED: Application MUST define these
 * ===========================================================================*/

/**
 * @brief Number of states in your FSM
 *
 * Must match the number of entries in your state enum.
 * The framework does NOT define any states -- you define your own.
 *
 * Example:
 *   typedef enum { STATE_INIT = 0, STATE_RUNNING, STATE_STOPPED, MY_STATE_COUNT } MyState_t;
 *   #define SM_STATE_COUNT  MY_STATE_COUNT
 */
#define SM_STATE_COUNT    (4U)

/**
 * @brief Number of events in your FSM
 *
 * Must match the number of entries in your event enum.
 *
 * Example:
 *   typedef enum { EVT_START = 0, EVT_STOP, EVT_TIMEOUT, MY_EVT_COUNT } MyEvent_t;
 *   #define SM_EVENT_COUNT  MY_EVT_COUNT
 */
#define SM_EVENT_COUNT    (6U)

/* =============================================================================
 * EVENT QUEUE (optional overrides)
 * ===========================================================================*/

/* #define SM_EVENT_QUEUE_SIZE     (8U)  */  /* Default: 8 events */

/* =============================================================================
 * TRANSITION TABLE (optional overrides)
 * ===========================================================================*/

/* #define SM_MAX_TRANSITIONS      (32U) */  /* Runtime transition table size */

/* =============================================================================
 * ERROR HANDLING (optional overrides)
 * ===========================================================================*/

/* #define SM_ERROR_HISTORY_SIZE   (8U)  */  /* Error history ring size */
/* #define SM_ERROR_MAX_RECOVERY   (3U)  */  /* Max recovery attempts */

/* =============================================================================
 * STATE HISTORY (optional overrides)
 * ===========================================================================*/

/* #define SM_STATE_HISTORY_DEPTH  (4U)  */  /* State history ring depth */

/* =============================================================================
 * DEBUG (optional overrides)
 * ===========================================================================*/

/* #define SM_DEBUG_LEVEL          (4U)  */  /* 0=off, 1=err, 2=+warn, 3=+info, 4=+verbose */
/* #define SM_DEBUG_BUFFER_SIZE    (256U) */ /* Output buffer size */
/* #define SM_DEBUG_MSG_MAX_LEN    (128U) */ /* Max single message length */

/* =============================================================================
 * FEATURE FLAGS (optional overrides)
 * ===========================================================================*/

/* #define SM_FEATURE_HSM                   (0U) */  /* Hierarchical states */
/* #define SM_FEATURE_RUNTIME_TRANSITIONS   (0U) */  /* Runtime transition API */
/* #define SM_FEATURE_STATISTICS            (0U) */  /* Statistics collection */
/* #define SM_FEATURE_DEBUG                 (1U) */  /* Debug output */
/* #define SM_FEATURE_ASSERT                (1U) */  /* Runtime assertions */

/* =============================================================================
 * TASK PERIOD (optional override)
 * ===========================================================================*/

/* #define SM_TASK_PERIOD_MS       (10U) */  /* SM_Process call interval */

/* =============================================================================
 * PLATFORM-SPECIFIC SETTINGS (examples)
 * ===========================================================================*/

#if defined(STM32F407xx)
    /* STM32F4 specific */
    #define SYSTEM_CLOCK_HZ (168000000UL)

#elif defined(ESP32)
    /* ESP32 specific */
    #define SYSTEM_CLOCK_HZ (240000000UL)

#elif defined(RP2040)
    /* RP2040 specific */
    #define SYSTEM_CLOCK_HZ (133000000UL)

#else
    /* Generic / simulation */
    #define SYSTEM_CLOCK_HZ (100000000UL)
#endif

#endif /* APP_CONFIG_H */
