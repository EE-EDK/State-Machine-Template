/**
 * @file app_main.c
 * @brief Application-level convenience wrappers (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Provides App_Main_Init / App_Main_Task convenience functions that wrap
 * the handle-based SM_* API for simple single-instance usage.
 *
 * Users with multi-instance needs should call SM_Init / SM_Process directly.
 *
 * Note: This file is compiled as part of the framework library but is NOT
 * required. Users can omit it and call SM_* functions directly.
 * The static SM_Context_t here provides SM_STATE_COUNT and SM_EVENT_COUNT
 * sizing -- so app_config.h must be included before sm_framework.h.
 */

#include "sm_framework/sm_framework.h"

/**
 * @brief Get framework version string
 *
 * @return Pointer to version string (e.g., "3.0.0")
 */
const char *App_Main_GetVersion(void)
{
    return SM_FRAMEWORK_VERSION_STRING;
}
