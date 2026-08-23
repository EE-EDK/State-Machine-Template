/**
 * @file sm_framework.h
 * @brief Umbrella header for State Machine Framework v4.1
 * @version 4.1.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Include this single header to access the entire framework API.
 * Order of includes matters: config -> types -> platform -> engine -> error -> debug
 */

#ifndef SM_FRAMEWORK_H
#define SM_FRAMEWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * FRAMEWORK VERSION
 * ===========================================================================*/

#define SM_FRAMEWORK_VERSION_MAJOR  (4)
#define SM_FRAMEWORK_VERSION_MINOR  (1)
#define SM_FRAMEWORK_VERSION_PATCH  (0)
#define SM_FRAMEWORK_VERSION_STRING "4.1.0"

/* =============================================================================
 * FRAMEWORK HEADERS (order matters)
 * ===========================================================================*/

#include "sm_config.h"     /* Configuration defaults + user overrides */
#include "sm_types.h"      /* All types, structs, enums, callbacks */
#include "sm_platform.h"   /* HAL interface (timing, critical, output, etc.) */
#include "sm_safety.h"     /* Safety macros: DIS, bounded loops, numeric asserts */
#include "sm_engine.h"     /* Core API: SM_Init, SM_Process, SM_PostEvent, ... */
#include "sm_error.h"      /* Error handler API: SM_Error_Report, ... */
#include "sm_debug.h"      /* Debug API: SM_Debug_Init, SM_LOG_*, ... */

#ifdef __cplusplus
}
#endif

#endif /* SM_FRAMEWORK_H */
