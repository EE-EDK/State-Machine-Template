/**
 * @file sm_error_handler.c
 * @brief Error handler implementation
 * @version 2.0.0
 */

#include "sm_framework/sm_framework.h"
#include "sm_framework/sm_platform.h"
#include <string.h>

/* Communication verification state */
static struct {
    uint32_t window_start_time;
    uint8_t good_message_count;
    bool is_verified;
} g_comm_verification;

/* Custom recovery handlers (optional advanced feature) */
static ErrorRecoveryHandler_t g_recovery_handlers[ERROR_CODE_MAX];

/* Forward declarations */
static void AddErrorToHistory(const ErrorInfo_t *error_info);
extern StateMachineContext_t g_sm_context;  /* From sm_state_machine.c */

bool ErrorHandler_Init(void)
{
    memset(&g_sm_context.error_handler, 0, sizeof(ErrorHandler_t));
    memset(&g_comm_verification, 0, sizeof(g_comm_verification));
    memset(g_recovery_handlers, 0, sizeof(g_recovery_handlers));
    
    g_sm_context.error_handler.current_error.level = ERROR_LEVEL_NONE;
    g_sm_context.error_handler.current_error.code = ERROR_CODE_NONE;
    g_sm_context.error_handler.critical_lock_active = false;
    
    return true;
}

bool ErrorHandler_Report(ErrorLevel_t level, ErrorCode_t code)
{
    ErrorInfo_t error_info;
    
    /* Create error info */
    error_info.level = level;
    error_info.code = code;
    error_info.timestamp = Platform_GetTimeMs();
    error_info.state = StateMachine_GetCurrentState();
    error_info.retry_count = 0;
    error_info.is_recovered = false;
    
    /* Add to history */
    AddErrorToHistory(&error_info);
    
    /* Handle based on severity */
    switch (level) {
        case ERROR_LEVEL_MINOR:
            return ErrorHandler_HandleMinorError(code);
        case ERROR_LEVEL_NORMAL:
            return ErrorHandler_HandleNormalError(code);
        case ERROR_LEVEL_CRITICAL:
            ErrorHandler_HandleCriticalError(code);
            return true;
        default:
            return false;
    }
}

bool ErrorHandler_HandleMinorError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    uint32_t current_time = Platform_GetTimeMs();
    
    /* Start timer if first minor error */
    if (handler->minor_error_timestamp == 0) {
        handler->minor_error_timestamp = current_time;
        handler->minor_good_message_count = 0;
    }
    
    /* Check if within timeout window */
    if ((current_time - handler->minor_error_timestamp) <= ERROR_MINOR_TIMEOUT_MS) {
        /* Verify channel */
        if (ErrorHandler_VerifyCommChannel()) {
            handler->minor_good_message_count++;
            if (handler->minor_good_message_count >= COMM_VERIFICATION_COUNT) {
                /* Auto-recovery successful */
                handler->minor_error_timestamp = 0;
                handler->minor_good_message_count = 0;
                DEBUG_INFO("Minor error auto-recovered");
                return true;
            }
        }
    } else {
        /* Timeout - escalate to normal error */
        DEBUG_WARNING("Minor error timeout - escalating to normal");
        return ErrorHandler_HandleNormalError(code);
    }
    
    return true;
}

bool ErrorHandler_HandleNormalError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    
    handler->current_error.level = ERROR_LEVEL_NORMAL;
    handler->current_error.code = code;
    handler->current_error.timestamp = Platform_GetTimeMs();
    handler->current_error.state = StateMachine_GetCurrentState();
    handler->current_error.retry_count = 0;
    handler->current_error.is_recovered = false;
    
    DEBUG_WARNING("Normal error reported: %s", ErrorHandler_CodeToString(code));
    StateMachine_PostEvent(EVENT_ERROR_NORMAL);
    
    return true;
}

void ErrorHandler_HandleCriticalError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    
    handler->current_error.level = ERROR_LEVEL_CRITICAL;
    handler->current_error.code = code;
    handler->current_error.timestamp = Platform_GetTimeMs();
    handler->current_error.state = StateMachine_GetCurrentState();
    handler->current_error.retry_count = 0;
    handler->current_error.is_recovered = false;
    handler->critical_lock_active = true;
    
    DEBUG_ERROR("CRITICAL ERROR: %s", ErrorHandler_CodeToString(code));
    StateMachine_PostEvent(EVENT_ERROR_CRITICAL);
}

bool ErrorHandler_AttemptRecovery(void)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    
    if (handler->current_error.level == ERROR_LEVEL_NONE) {
        return true;  /* No error to recover from */
    }
    
    handler->current_error.retry_count++;
    
    /* Check retry limit */
    if (handler->current_error.retry_count >= ERROR_MAX_RECOVERY_ATTEMPTS) {
        DEBUG_ERROR("Max recovery attempts exceeded");
        return false;
    }
    
    /* Try custom handler if registered */
    if (g_recovery_handlers[handler->current_error.code] != NULL) {
        return g_recovery_handlers[handler->current_error.code](handler->current_error.code);
    }
    
    /* Default recovery logic */
    switch (handler->current_error.code) {
        case ERROR_CODE_COMM_LOST:
            if (ErrorHandler_VerifyCommChannel()) {
                handler->current_error.is_recovered = true;
                return true;
            }
            break;
            
        case ERROR_CODE_TIMEOUT:
            /* Timeout errors can usually retry */
            handler->current_error.is_recovered = true;
            return true;
            
        default:
            /* Unknown error - cannot recover */
            break;
    }
    
    return false;
}

void ErrorHandler_ClearError(void)
{
    g_sm_context.error_handler.current_error.level = ERROR_LEVEL_NONE;
    g_sm_context.error_handler.current_error.code = ERROR_CODE_NONE;
    g_sm_context.error_handler.current_error.retry_count = 0;
}

bool ErrorHandler_IsCriticalLock(void)
{
    return g_sm_context.error_handler.critical_lock_active;
}

bool ErrorHandler_GetCurrentError(ErrorInfo_t *error_info)
{
    if (error_info == NULL) {
        return false;
    }
    
    memcpy(error_info, &g_sm_context.error_handler.current_error, sizeof(ErrorInfo_t));
    return true;
}

bool ErrorHandler_GetHistoryError(uint8_t index, ErrorInfo_t *error_info)
{
    if (error_info == NULL || index >= ERROR_HISTORY_SIZE) {
        return false;
    }
    
    uint8_t actual_index = (g_sm_context.error_handler.history_index + ERROR_HISTORY_SIZE - index - 1) % ERROR_HISTORY_SIZE;
    memcpy(error_info, &g_sm_context.error_handler.error_history[actual_index], sizeof(ErrorInfo_t));
    
    return true;
}

uint8_t ErrorHandler_GetHistoryCount(void)
{
    return ERROR_HISTORY_SIZE;  /* Full circular buffer */
}

bool ErrorHandler_VerifyCommChannel(void)
{
    uint32_t current_time = Platform_GetTimeMs();
    
    /* Check if within verification window */
    if ((current_time - g_comm_verification.window_start_time) <= COMM_VERIFICATION_WINDOW_MS) {
        g_comm_verification.good_message_count++;
        if (g_comm_verification.good_message_count >= COMM_VERIFICATION_COUNT) {
            g_comm_verification.is_verified = true;
            return true;
        }
    } else {
        /* Reset window */
        g_comm_verification.window_start_time = current_time;
        g_comm_verification.good_message_count = 1;
    }
    
    return false;
}

bool ErrorHandler_RegisterRecoveryHandler(ErrorCode_t code, ErrorRecoveryHandler_t handler)
{
    if (code >= ERROR_CODE_MAX) {
        return false;
    }
    
    g_recovery_handlers[code] = handler;
    return true;
}

const char *ErrorHandler_CodeToString(ErrorCode_t code)
{
    static const char *error_strings[] = {
        "NONE", "TIMEOUT", "COMM_LOST", "COMM_CORRUPT", "INVALID_DATA",
        "BUFFER_OVERFLOW", "RESOURCE_UNAVAILABLE", "CALIBRATION_FAILED",
        "HARDWARE_FAULT", "WATCHDOG_RESET", "MEMORY_CORRUPTION"
    };
    
    if (code < ERROR_CODE_MAX) {
        return error_strings[code];
    }
    return "UNKNOWN";
}

const char *ErrorHandler_LevelToString(ErrorLevel_t level)
{
    static const char *level_strings[] = {
        "NONE", "MINOR", "NORMAL", "CRITICAL"
    };
    
    if (level < ERROR_LEVEL_MAX) {
        return level_strings[level];
    }
    return "UNKNOWN";
}

static void AddErrorToHistory(const ErrorInfo_t *error_info)
{
    if (error_info == NULL) {
        return;
    }
    
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    memcpy(&handler->error_history[handler->history_index], error_info, sizeof(ErrorInfo_t));
    handler->history_index = (handler->history_index + 1) % ERROR_HISTORY_SIZE;
}
