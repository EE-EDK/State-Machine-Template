/**
 * @file sm_framework.h
 * @brief Main header for State Machine Framework - include this in your application
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * This is the main entry point for using the State Machine Framework.
 * Include only this header in your application code.
 */

#ifndef SM_FRAMEWORK_H
#define SM_FRAMEWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Core framework components */
#include "sm_config.h"
#include "sm_types.h"
#include "sm_platform.h"
#include "sm_state_machine.h"
#include "sm_error_handler.h"
#include "sm_debug.h"

/* =============================================================================
 * FRAMEWORK VERSION
 * ===========================================================================*/

#define SM_FRAMEWORK_VERSION_MAJOR (2)
#define SM_FRAMEWORK_VERSION_MINOR (0)
#define SM_FRAMEWORK_VERSION_PATCH (0)
#define SM_FRAMEWORK_VERSION_STRING "2.0.0"

/* =============================================================================
 * SIMPLIFIED API (for backward compatibility and ease of use)
 * ===========================================================================*/

/**
 * @brief Initialize the complete state machine framework
 *
 * Convenience function that initializes all framework components.
 * Equivalent to calling StateMachine_Init() and Debug_Init().
 *
 * @param debug_interface Debug output interface (UART, SPI, I2C, etc.)
 * @return true if initialization successful, false otherwise
 *
 * @note This is the recommended initialization function for most applications
 */
bool App_Main_Init(CommInterface_t debug_interface);

/**
 * @brief Execute one iteration of the state machine framework
 *
 * Convenience function that executes state machine and processes periodic debug.
 * Should be called every SM_TASK_PERIOD_MS milliseconds.
 *
 * @note This is the recommended task function for most applications
 */
void App_Main_Task(void);

/**
 * @brief Get framework version string
 *
 * @return Pointer to version string (e.g., "2.0.0")
 */
const char *App_Main_GetVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* SM_FRAMEWORK_H */
