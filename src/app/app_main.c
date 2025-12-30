/**
 * @file app_main.c
 * @brief Application-level glue code - simplified API
 * @version 2.0.0
 */

#include "sm_framework/sm_framework.h"

bool App_Main_Init(CommInterface_t debug_interface)
{
    /* Initialize debug system first */
    if (!Debug_Init(debug_interface)) {
        return false;
    }
    
    DEBUG_INIT("=== State Machine Framework v%s ===", SM_FRAMEWORK_VERSION_STRING);
    DEBUG_INIT("Initializing...");
    
    /* Initialize state machine */
    if (!StateMachine_Init()) {
        DEBUG_ERROR("State machine initialization failed");
        return false;
    }
    
    DEBUG_INIT("Initialization complete");
    return true;
}

void App_Main_Task(void)
{
    /* Execute state machine */
    StateMachine_Execute();
    
    /* Process periodic debug messages */
    Debug_ProcessPeriodic();
}

const char *App_Main_GetVersion(void)
{
    return SM_FRAMEWORK_VERSION_STRING;
}
