/**
 * @file sm_config_template.h
 * @brief User configuration template for State Machine Framework v4.1
 * @version 4.1.0
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

/* =============================================================================
 * BUILD-WIDE DIMENSIONS -- READ THIS FIRST (v4.1)
 *
 * The two macros below are compiled into the FRAMEWORK's translation units,
 * not just yours: they drive SM_Init's initial-state check, SM_PostEvent's
 * accept range, and (with statistics on) SM_Context_t's layout.
 *
 * Defining them in a header that only YOUR sources include leaves the
 * framework compiled with different values, and the two sides then disagree
 * silently. Use one of:
 *
 *   1. Set them in your build system, for every target:
 *          cmake -DSM_STATE_COUNT=4 -DSM_EVENT_COUNT=6 ..
 *      (the sm_framework target propagates them PUBLIC to whatever links it)
 *
 *   2. Compile the framework as part of your application and force-include
 *      this file into EVERY target, framework sources included:
 *          target_compile_options(<tgt> PRIVATE -include app_config.h)
 *
 * SM_Init verifies the match at runtime and fails with assertion 105/106
 * rather than misbehaving later.
 *
 * The same rule applies to every macro further down that changes
 * SM_Context_t's layout: SM_EVENT_QUEUE_SIZE, SM_ERROR_HISTORY_SIZE,
 * SM_STATE_HISTORY_DEPTH, SM_MAX_TRANSITIONS, SM_DEFER_QUEUE_SIZE and the
 * SM_FEATURE_* flags.
 * ===========================================================================*/

/**
 * @brief Number of states in your FSM
 *
 * Must be at least the number of entries in your state enum.
 * The framework does NOT define any states -- you define your own.
 *
 * Example:
 *   typedef enum { STATE_INIT = 0, STATE_RUNNING, STATE_STOPPED, MY_STATE_COUNT } MyState_t;
 *   #define SM_STATE_COUNT  MY_STATE_COUNT
 */
#ifndef SM_STATE_COUNT
#define SM_STATE_COUNT    (4U)
#endif

/**
 * @brief Number of events in your FSM
 *
 * Must be at least the number of entries in your event enum. The reserved
 * id SM_EVT_TIMEOUT (0xFFFF) sits outside this range and costs you nothing.
 *
 * Example:
 *   typedef enum { EVT_START = 0, EVT_STOP, EVT_FAULT, MY_EVT_COUNT } MyEvent_t;
 *   #define SM_EVENT_COUNT  MY_EVT_COUNT
 */
#ifndef SM_EVENT_COUNT
#define SM_EVENT_COUNT    (6U)
#endif

/* =============================================================================
 * EVENT QUEUE (optional overrides)
 * ===========================================================================*/

/* #define SM_EVENT_QUEUE_SIZE     (8U)  */  /* Default: 8 events */

/* Max events drained per SM_Process call (v4.0). Default: SM_EVENT_QUEUE_SIZE.
 * Set to 1 for the v3.0 one-event-per-call cadence. */
/* #define SM_MAX_EVENTS_PER_PROCESS (SM_EVENT_QUEUE_SIZE) */

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
