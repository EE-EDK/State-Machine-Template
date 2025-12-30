#include "app_main.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static StateMachineContext_t g_sm_context;
static StateConfig_t g_state_table[STATE_MAX];
static DebugConfig_t g_debug_config;
static struct
{
    uint8_t good_message_count;
    uint32_t window_start_time;
    bool is_verified;
} g_comm_status;

static void State_Init_OnEntry(void);
static void State_Init_OnState(void);
static void State_Init_OnExit(void);
static void State_Idle_OnEntry(void);
static void State_Idle_OnState(void);
static void State_Idle_OnExit(void);
static void State_Active_OnEntry(void);
static void State_Active_OnState(void);
static void State_Active_OnExit(void);
static void State_Processing_OnEntry(void);
static void State_Processing_OnState(void);
static void State_Processing_OnExit(void);
static void State_Communicating_OnEntry(void);
static void State_Communicating_OnState(void);
static void State_Communicating_OnExit(void);
static void State_Monitoring_OnEntry(void);
static void State_Monitoring_OnState(void);
static void State_Monitoring_OnExit(void);
static void State_Calibrating_OnEntry(void);
static void State_Calibrating_OnState(void);
static void State_Calibrating_OnExit(void);
static void State_Diagnostics_OnEntry(void);
static void State_Diagnostics_OnState(void);
static void State_Diagnostics_OnExit(void);
static void State_Recovery_OnEntry(void);
static void State_Recovery_OnState(void);
static void State_Recovery_OnExit(void);
static void State_CriticalError_OnEntry(void);
static void State_CriticalError_OnState(void);
static void State_CriticalError_OnExit(void);

static void InitializeStateTable(void);
static void PerformStateTransition(StateMachineState_t new_state);
static bool CheckStateTransition(StateMachineEvent_t event, StateMachineState_t *next_state);
static void AddErrorToHistory(const ErrorInfo_t *error_info);
static void SendDebugMessage(const DebugMessage_t *message);

bool StateMachine_Init(void)
{
    memset(&g_sm_context, 0, sizeof(StateMachineContext_t));
    g_sm_context.current_state = STATE_INIT;
    g_sm_context.previous_state = STATE_INIT;
    g_sm_context.pending_event = EVENT_NONE;
    g_sm_context.state_entry_time = GetCurrentTimeMs();
    g_sm_context.state_execution_count = 0;
    g_sm_context.state_changed = false;

    if (!ErrorHandler_Init())
    {
        return false;
    }
    InitializeStateTable();
    g_comm_status.good_message_count = 0;
    g_comm_status.window_start_time = GetCurrentTimeMs();
    g_comm_status.is_verified = false;
    Debug_SendMessage(DEBUG_MSG_INIT, "State Machine initialized");
    return true;
}

StateMachineState_t StateMachine_Execute(void)
{
    StateConfig_t *current_state_config = NULL;
    StateMachineState_t next_state;

    if (g_sm_context.error_handler.critical_lock_active)
    {
        if (g_sm_context.current_state != STATE_CRITICAL_ERROR)
        {
            PerformStateTransition(STATE_CRITICAL_ERROR);
        }
        return g_sm_context.current_state;
    }

    current_state_config = &g_state_table[g_sm_context.current_state];

    if (g_sm_context.state_changed)
    {
        if (current_state_config->on_entry != NULL)
        {
            current_state_config->on_entry();
        }
        g_sm_context.state_changed = false;
        g_sm_context.state_entry_time = GetCurrentTimeMs();
        g_sm_context.state_execution_count = 0;
    }

    if (current_state_config->on_state != NULL)
    {
        current_state_config->on_state();
    }
    g_sm_context.state_execution_count++;

    if (current_state_config->timeout_ms > 0)
    {
        if (IsTimeout(g_sm_context.state_entry_time, current_state_config->timeout_ms))
        {
            StateMachine_PostEvent(EVENT_TIMEOUT);
        }
    }

    if (g_sm_context.pending_event != EVENT_NONE)
    {
        if (CheckStateTransition(g_sm_context.pending_event, &next_state))
        {
            PerformStateTransition(next_state);
        }
        g_sm_context.pending_event = EVENT_NONE;
    }
    return g_sm_context.current_state;
}

bool StateMachine_PostEvent(StateMachineEvent_t event)
{
    if (event >= EVENT_MAX)
    {
        return false;
    }
    if (g_sm_context.pending_event != EVENT_NONE)
    {
        return false;
    }
    g_sm_context.pending_event = event;
    return true;
}

StateMachineState_t StateMachine_GetCurrentState(void)
{
    return g_sm_context.current_state;
}

StateMachineState_t StateMachine_GetPreviousState(void)
{
    return g_sm_context.previous_state;
}

void StateMachine_Reset(void)
{
    if (!g_sm_context.error_handler.critical_lock_active)
    {
        ErrorHandler_ClearError();
        PerformStateTransition(STATE_INIT);
    }
}

static void InitializeStateTable(void)
{
    uint8_t i;
    memset(g_state_table, 0, sizeof(g_state_table));

    g_state_table[STATE_INIT].state_id = STATE_INIT;
    g_state_table[STATE_INIT].on_entry = State_Init_OnEntry;
    g_state_table[STATE_INIT].on_state = State_Init_OnState;
    g_state_table[STATE_INIT].on_exit = State_Init_OnExit;
    g_state_table[STATE_INIT].timeout_ms = SM_STATE_TIMEOUT_MS;
    g_state_table[STATE_INIT].transitions[0].event = EVENT_INIT_COMPLETE;
    g_state_table[STATE_INIT].transitions[0].next_state = STATE_IDLE;
    g_state_table[STATE_INIT].transitions[1].event = EVENT_ERROR_CRITICAL;
    g_state_table[STATE_INIT].transitions[1].next_state = STATE_CRITICAL_ERROR;
    g_state_table[STATE_INIT].transition_count = 2;

    g_state_table[STATE_IDLE].state_id = STATE_IDLE;
    g_state_table[STATE_IDLE].on_entry = State_Idle_OnEntry;
    g_state_table[STATE_IDLE].on_state = State_Idle_OnState;
    g_state_table[STATE_IDLE].on_exit = State_Idle_OnExit;
    g_state_table[STATE_IDLE].timeout_ms = 0;
    g_state_table[STATE_IDLE].transitions[0].event = EVENT_START;
    g_state_table[STATE_IDLE].transitions[0].next_state = STATE_ACTIVE;
    g_state_table[STATE_IDLE].transition_count = 1;

    g_state_table[STATE_ACTIVE].state_id = STATE_ACTIVE;
    g_state_table[STATE_ACTIVE].on_entry = State_Active_OnEntry;
    g_state_table[STATE_ACTIVE].on_state = State_Active_OnState;
    g_state_table[STATE_ACTIVE].on_exit = State_Active_OnExit;
    g_state_table[STATE_ACTIVE].timeout_ms = 0;
    g_state_table[STATE_ACTIVE].transitions[0].event = EVENT_DATA_READY;
    g_state_table[STATE_ACTIVE].transitions[0].next_state = STATE_PROCESSING;
    g_state_table[STATE_ACTIVE].transitions[1].event = EVENT_STOP;
    g_state_table[STATE_ACTIVE].transitions[1].next_state = STATE_IDLE;
    g_state_table[STATE_ACTIVE].transition_count = 2;

    g_state_table[STATE_PROCESSING].state_id = STATE_PROCESSING;
    g_state_table[STATE_PROCESSING].on_entry = State_Processing_OnEntry;
    g_state_table[STATE_PROCESSING].on_state = State_Processing_OnState;
    g_state_table[STATE_PROCESSING].on_exit = State_Processing_OnExit;
    g_state_table[STATE_PROCESSING].timeout_ms = 3000U;
    g_state_table[STATE_PROCESSING].transitions[0].event = EVENT_PROCESSING_DONE;
    g_state_table[STATE_PROCESSING].transitions[0].next_state = STATE_COMMUNICATING;
    g_state_table[STATE_PROCESSING].transition_count = 1;

    g_state_table[STATE_COMMUNICATING].state_id = STATE_COMMUNICATING;
    g_state_table[STATE_COMMUNICATING].on_entry = State_Communicating_OnEntry;
    g_state_table[STATE_COMMUNICATING].on_state = State_Communicating_OnState;
    g_state_table[STATE_COMMUNICATING].on_exit = State_Communicating_OnExit;
    g_state_table[STATE_COMMUNICATING].timeout_ms = COMM_TIMEOUT_MS;
    g_state_table[STATE_COMMUNICATING].transitions[0].event = EVENT_COMM_COMPLETE;
    g_state_table[STATE_COMMUNICATING].transitions[0].next_state = STATE_MONITORING;
    g_state_table[STATE_COMMUNICATING].transition_count = 1;

    g_state_table[STATE_MONITORING].state_id = STATE_MONITORING;
    g_state_table[STATE_MONITORING].on_entry = State_Monitoring_OnEntry;
    g_state_table[STATE_MONITORING].on_state = State_Monitoring_OnState;
    g_state_table[STATE_MONITORING].on_exit = State_Monitoring_OnExit;
    g_state_table[STATE_MONITORING].timeout_ms = 0;
    g_state_table[STATE_MONITORING].transitions[0].event = EVENT_STOP;
    g_state_table[STATE_MONITORING].transitions[0].next_state = STATE_IDLE;
    g_state_table[STATE_MONITORING].transition_count = 1;

    g_state_table[STATE_CALIBRATING].state_id = STATE_CALIBRATING;
    g_state_table[STATE_CALIBRATING].on_entry = State_Calibrating_OnEntry;
    g_state_table[STATE_CALIBRATING].on_state = State_Calibrating_OnState;
    g_state_table[STATE_CALIBRATING].on_exit = State_Calibrating_OnExit;
    g_state_table[STATE_CALIBRATING].timeout_ms = 5000U;
    g_state_table[STATE_CALIBRATING].transitions[0].event = EVENT_PROCESSING_DONE;
    g_state_table[STATE_CALIBRATING].transitions[0].next_state = STATE_DIAGNOSTICS;
    g_state_table[STATE_CALIBRATING].transition_count = 1;

    g_state_table[STATE_DIAGNOSTICS].state_id = STATE_DIAGNOSTICS;
    g_state_table[STATE_DIAGNOSTICS].on_entry = State_Diagnostics_OnEntry;
    g_state_table[STATE_DIAGNOSTICS].on_state = State_Diagnostics_OnState;
    g_state_table[STATE_DIAGNOSTICS].on_exit = State_Diagnostics_OnExit;
    g_state_table[STATE_DIAGNOSTICS].timeout_ms = 2000U;
    g_state_table[STATE_DIAGNOSTICS].transitions[0].event = EVENT_PROCESSING_DONE;
    g_state_table[STATE_DIAGNOSTICS].transitions[0].next_state = STATE_ACTIVE;
    g_state_table[STATE_DIAGNOSTICS].transition_count = 1;

    g_state_table[STATE_RECOVERY].state_id = STATE_RECOVERY;
    g_state_table[STATE_RECOVERY].on_entry = State_Recovery_OnEntry;
    g_state_table[STATE_RECOVERY].on_state = State_Recovery_OnState;
    g_state_table[STATE_RECOVERY].on_exit = State_Recovery_OnExit;
    g_state_table[STATE_RECOVERY].timeout_ms = 2000U;
    g_state_table[STATE_RECOVERY].transitions[0].event = EVENT_RECOVERY_SUCCESS;
    g_state_table[STATE_RECOVERY].transitions[0].next_state = STATE_IDLE;
    g_state_table[STATE_RECOVERY].transitions[1].event = EVENT_RECOVERY_FAILED;
    g_state_table[STATE_RECOVERY].transitions[1].next_state = STATE_CRITICAL_ERROR;
    g_state_table[STATE_RECOVERY].transition_count = 2;

    g_state_table[STATE_CRITICAL_ERROR].state_id = STATE_CRITICAL_ERROR;
    g_state_table[STATE_CRITICAL_ERROR].on_entry = State_CriticalError_OnEntry;
    g_state_table[STATE_CRITICAL_ERROR].on_state = State_CriticalError_OnState;
    g_state_table[STATE_CRITICAL_ERROR].on_exit = State_CriticalError_OnExit;
    g_state_table[STATE_CRITICAL_ERROR].timeout_ms = 0;
    g_state_table[STATE_CRITICAL_ERROR].transition_count = 0;
}

static void PerformStateTransition(StateMachineState_t new_state)
{
    StateConfig_t *current_config;

    if (new_state >= STATE_MAX)
    {
        return;
    }

    current_config = &g_state_table[g_sm_context.current_state];
    if (current_config->on_exit != NULL)
    {
        current_config->on_exit();
    }

    g_sm_context.previous_state = g_sm_context.current_state;
    g_sm_context.current_state = new_state;
    g_sm_context.state_changed = true;
}

static bool CheckStateTransition(StateMachineEvent_t event, StateMachineState_t *next_state)
{
    StateConfig_t *current_config;
    uint8_t i;

    if (next_state == NULL)
    {
        return false;
    }

    current_config = &g_state_table[g_sm_context.current_state];

    for (i = 0; i < current_config->transition_count; i++)
    {
        if (current_config->transitions[i].event == event)
        {
            *next_state = current_config->transitions[i].next_state;
            return true;
        }
    }
    return false;
}

bool ErrorHandler_Init(void)
{
    memset(&g_sm_context.error_handler, 0, sizeof(ErrorHandler_t));
    g_sm_context.error_handler.current_error.level = ERROR_LEVEL_NONE;
    g_sm_context.error_handler.current_error.code = ERROR_CODE_NONE;
    g_sm_context.error_handler.critical_lock_active = false;
    return true;
}

bool ErrorHandler_Report(ErrorLevel_t level, ErrorCode_t code)
{
    ErrorInfo_t error_info;
    error_info.level = level;
    error_info.code = code;
    error_info.timestamp = GetCurrentTimeMs();
    error_info.state = g_sm_context.current_state;
    error_info.retry_count = 0;
    error_info.is_recovered = false;
    AddErrorToHistory(&error_info);

    switch (level)
    {
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

bool ErrorHandler_AttemptRecovery(void)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;

    if (handler->current_error.level == ERROR_LEVEL_NONE)
    {
        return true;
    }
    handler->current_error.retry_count++;

    if (handler->current_error.retry_count >= ERROR_MAX_RECOVERY_ATTEMPTS)
    {
        return false;
    }

    switch (handler->current_error.code)
    {
    case ERROR_CODE_COMM_LOST:
        if (Comm_VerifyChannel())
        {
            handler->current_error.is_recovered = true;
            return true;
        }
        break;
    case ERROR_CODE_TIMEOUT:
        handler->current_error.is_recovered = true;
        return true;
    default:
        break;
    }
    return false;
}

bool ErrorHandler_IsCriticalLock(void)
{
    return g_sm_context.error_handler.critical_lock_active;
}

void ErrorHandler_ClearError(void)
{
    g_sm_context.error_handler.current_error.level = ERROR_LEVEL_NONE;
    g_sm_context.error_handler.current_error.code = ERROR_CODE_NONE;
    g_sm_context.error_handler.current_error.retry_count = 0;
}

bool ErrorHandler_GetCurrentError(ErrorInfo_t *error_info)
{
    if (error_info == NULL)
    {
        return false;
    }
    memcpy(error_info, &g_sm_context.error_handler.current_error, sizeof(ErrorInfo_t));
    return true;
}

bool ErrorHandler_HandleMinorError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    uint32_t current_time = GetCurrentTimeMs();

    if (handler->minor_error_timestamp == 0)
    {
        handler->minor_error_timestamp = current_time;
        handler->minor_good_message_count = 0;
    }

    if ((current_time - handler->minor_error_timestamp) <= ERROR_MINOR_TIMEOUT_MS)
    {
        if (Comm_VerifyChannel())
        {
            handler->minor_good_message_count++;
            if (handler->minor_good_message_count >= COMM_VERIFICATION_COUNT)
            {
                handler->minor_error_timestamp = 0;
                handler->minor_good_message_count = 0;
                return true;
            }
        }
    }
    else
    {
        return ErrorHandler_HandleNormalError(code);
    }
    return true;
}

bool ErrorHandler_HandleNormalError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    handler->current_error.level = ERROR_LEVEL_NORMAL;
    handler->current_error.code = code;
    handler->current_error.timestamp = GetCurrentTimeMs();
    handler->current_error.state = g_sm_context.current_state;
    handler->current_error.retry_count = 0;
    handler->current_error.is_recovered = false;
    StateMachine_PostEvent(EVENT_ERROR_NORMAL);
    return true;
}

void ErrorHandler_HandleCriticalError(ErrorCode_t code)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    handler->current_error.level = ERROR_LEVEL_CRITICAL;
    handler->current_error.code = code;
    handler->current_error.timestamp = GetCurrentTimeMs();
    handler->current_error.state = g_sm_context.current_state;
    handler->current_error.retry_count = 0;
    handler->current_error.is_recovered = false;
    handler->critical_lock_active = true;
    StateMachine_PostEvent(EVENT_ERROR_CRITICAL);
}

static void AddErrorToHistory(const ErrorInfo_t *error_info)
{
    ErrorHandler_t *handler = &g_sm_context.error_handler;
    memcpy(&handler->error_history[handler->history_index], error_info, sizeof(ErrorInfo_t));
    handler->history_index = (handler->history_index + 1) % ERROR_HISTORY_SIZE;
}

bool Debug_Init(CommInterface_t interface)
{
    memset(&g_debug_config, 0, sizeof(DebugConfig_t));
    g_debug_config.interface = interface;
    g_debug_config.enable_init_messages = (DEBUG_ENABLE_INIT_MESSAGES != 0);
    g_debug_config.enable_runtime_messages = (DEBUG_ENABLE_RUNTIME_MESSAGES != 0);
    g_debug_config.enable_periodic_messages = (DEBUG_ENABLE_PERIODIC_MESSAGES != 0);
    g_debug_config.periodic_last_time = GetCurrentTimeMs();

    switch (interface)
    {
    case COMM_INTERFACE_UART:
        if (!Comm_UART_Init())
            return false;
        break;
    case COMM_INTERFACE_SPI:
        if (!Comm_SPI_Init())
            return false;
        break;
    case COMM_INTERFACE_I2C:
        if (!Comm_I2C_Init())
            return false;
        break;
    default:
        return false;
    }
    return true;
}

void Debug_SendMessage(DebugMessageType_t type, const char *format, ...)
{
    DebugMessage_t message;
    va_list args;

    if (type == DEBUG_MSG_INIT && !g_debug_config.enable_init_messages)
    {
        return;
    }
    if (type == DEBUG_MSG_RUNTIME && !g_debug_config.enable_runtime_messages)
    {
        return;
    }
    if (type == DEBUG_MSG_PERIODIC && !g_debug_config.enable_periodic_messages)
    {
        return;
    }

    message.type = type;
    message.timestamp = GetCurrentTimeMs();
    va_start(args, format);
    vsnprintf(message.message, DEBUG_MAX_MESSAGE_LENGTH, format, args);
    va_end(args);
    SendDebugMessage(&message);
}

void Debug_EnableInitMessages(bool enable)
{
    g_debug_config.enable_init_messages = enable;
}

void Debug_EnableRuntimeMessages(bool enable)
{
    g_debug_config.enable_runtime_messages = enable;
}

void Debug_EnablePeriodicMessages(bool enable)
{
    g_debug_config.enable_periodic_messages = enable;
}

void Debug_ProcessPeriodic(void)
{
    uint32_t current_time = GetCurrentTimeMs();
    if (!g_debug_config.enable_periodic_messages)
    {
        return;
    }
    if ((current_time - g_debug_config.periodic_last_time) >= DEBUG_PERIODIC_INTERVAL_MS)
    {
        Debug_SendMessage(DEBUG_MSG_PERIODIC, "State=%s Exec=%lu",
                          StateToString(g_sm_context.current_state),
                          (unsigned long)g_sm_context.state_execution_count);
        g_debug_config.periodic_last_time = current_time;
    }
}

bool Debug_SetInterface(CommInterface_t interface)
{
    if (interface >= COMM_INTERFACE_MAX)
    {
        return false;
    }
    g_debug_config.interface = interface;
    return true;
}

static void SendDebugMessage(const DebugMessage_t *message)
{
    char formatted_buffer[192];
    uint32_t length;
    length = snprintf(formatted_buffer, sizeof(formatted_buffer), "[%lu] %s\n",
                      (unsigned long)message->timestamp, message->message);

    switch (g_debug_config.interface)
    {
    case COMM_INTERFACE_UART:
        Comm_UART_Send((uint8_t *)formatted_buffer, length);
        break;
    case COMM_INTERFACE_SPI:
        Comm_SPI_Send((uint8_t *)formatted_buffer, length);
        break;
    case COMM_INTERFACE_I2C:
        Comm_I2C_Send((uint8_t *)formatted_buffer, length);
        break;
    default:
        break;
    }
}

bool Comm_UART_Init(void)
{
    return true;
}

uint32_t Comm_UART_Send(const uint8_t *data, uint32_t length)
{
    return length;
}

bool Comm_SPI_Init(void)
{
    return true;
}

uint32_t Comm_SPI_Send(const uint8_t *data, uint32_t length)
{
    return length;
}

bool Comm_I2C_Init(void)
{
    return true;
}

uint32_t Comm_I2C_Send(const uint8_t *data, uint32_t length)
{
    return length;
}

bool Comm_VerifyChannel(void)
{
    uint32_t current_time = GetCurrentTimeMs();
    if ((current_time - g_comm_status.window_start_time) <= COMM_VERIFICATION_WINDOW_MS)
    {
        g_comm_status.good_message_count++;
        if (g_comm_status.good_message_count >= COMM_VERIFICATION_COUNT)
        {
            g_comm_status.is_verified = true;
            return true;
        }
    }
    else
    {
        g_comm_status.window_start_time = current_time;
        g_comm_status.good_message_count = 1;
    }
    return false;
}

uint32_t GetCurrentTimeMs(void)
{
    static uint32_t simulated_time = 0;
    return simulated_time++;
}

bool IsTimeout(uint32_t start_time, uint32_t timeout_ms)
{
    uint32_t current_time = GetCurrentTimeMs();
    if (current_time >= start_time)
    {
        return (current_time - start_time) >= timeout_ms;
    }
    else
    {
        return ((0xFFFFFFFF - start_time) + current_time) >= timeout_ms;
    }
}

const char *StateToString(StateMachineState_t state)
{
    static const char *state_strings[] = {
        "INIT", "IDLE", "ACTIVE", "PROCESSING", "COMMUNICATING",
        "MONITORING", "CALIBRATING", "DIAGNOSTICS", "RECOVERY", "CRITICAL_ERROR"};
    if (state < STATE_MAX)
    {
        return state_strings[state];
    }
    return "UNKNOWN";
}

const char *ErrorCodeToString(ErrorCode_t code)
{
    static const char *error_strings[] = {
        "NONE", "TIMEOUT", "COMM_LOST", "COMM_CORRUPT", "INVALID_DATA",
        "BUFFER_OVERFLOW", "RESOURCE_UNAVAILABLE", "CALIBRATION_FAILED",
        "HARDWARE_FAULT", "WATCHDOG_RESET", "MEMORY_CORRUPTION"};
    if (code < ERROR_CODE_MAX)
    {
        return error_strings[code];
    }
    return "UNKNOWN";
}

static void State_Init_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_INIT, "Entering INIT state");
}

static void State_Init_OnState(void)
{
    static uint32_t init_steps = 0;
    init_steps++;
    if (init_steps >= 5)
    {
        Debug_SendMessage(DEBUG_MSG_INIT, "Initialization complete");
        StateMachine_PostEvent(EVENT_INIT_COMPLETE);
        init_steps = 0;
    }
}

static void State_Init_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_INIT, "Exiting INIT state");
}

static void State_Idle_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering IDLE state");
}

static void State_Idle_OnState(void)
{
    if (g_sm_context.state_execution_count > 10)
    {
        StateMachine_PostEvent(EVENT_START);
    }
}

static void State_Idle_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting IDLE state");
}

static void State_Active_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering ACTIVE state");
}

static void State_Active_OnState(void)
{
    if (g_sm_context.state_execution_count > 5)
    {
        StateMachine_PostEvent(EVENT_DATA_READY);
    }
}

static void State_Active_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting ACTIVE state");
}

static void State_Processing_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering PROCESSING state");
}

static void State_Processing_OnState(void)
{
    if (g_sm_context.state_execution_count > 20)
    {
        Debug_SendMessage(DEBUG_MSG_INFO, "Processing complete");
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Processing_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting PROCESSING state");
}

static void State_Communicating_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering COMMUNICATING state");
}

static void State_Communicating_OnState(void)
{
    static bool comm_started = false;
    if (!comm_started)
    {
        Debug_SendMessage(DEBUG_MSG_INFO, "Starting communication");
        comm_started = true;
    }
    if (g_sm_context.state_execution_count > 8)
    {
        if (Comm_VerifyChannel())
        {
            Debug_SendMessage(DEBUG_MSG_INFO, "Communication complete");
            StateMachine_PostEvent(EVENT_COMM_COMPLETE);
            comm_started = false;
        }
    }
}

static void State_Communicating_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting COMMUNICATING state");
}

static void State_Monitoring_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering MONITORING state");
}

static void State_Monitoring_OnState(void)
{
    if (g_sm_context.state_execution_count > 50)
    {
        StateMachine_PostEvent(EVENT_STOP);
    }
}

static void State_Monitoring_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting MONITORING state");
}

static void State_Calibrating_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering CALIBRATING state");
}

static void State_Calibrating_OnState(void)
{
    if (g_sm_context.state_execution_count > 30)
    {
        Debug_SendMessage(DEBUG_MSG_INFO, "Calibration complete");
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Calibrating_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting CALIBRATING state");
}

static void State_Diagnostics_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering DIAGNOSTICS state");
}

static void State_Diagnostics_OnState(void)
{
    if (g_sm_context.state_execution_count > 15)
    {
        Debug_SendMessage(DEBUG_MSG_INFO, "Diagnostics passed");
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Diagnostics_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting DIAGNOSTICS state");
}

static void State_Recovery_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Entering RECOVERY state");
}

static void State_Recovery_OnState(void)
{
    bool recovery_successful = ErrorHandler_AttemptRecovery();
    if (recovery_successful)
    {
        Debug_SendMessage(DEBUG_MSG_INFO, "Recovery successful");
        ErrorHandler_ClearError();
        StateMachine_PostEvent(EVENT_RECOVERY_SUCCESS);
    }
    else
    {
        ErrorInfo_t error_info;
        if (ErrorHandler_GetCurrentError(&error_info))
        {
            if (error_info.retry_count >= ERROR_MAX_RECOVERY_ATTEMPTS)
            {
                Debug_SendMessage(DEBUG_MSG_ERROR, "Recovery failed");
                StateMachine_PostEvent(EVENT_RECOVERY_FAILED);
            }
        }
    }
}

static void State_Recovery_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_RUNTIME, "Exiting RECOVERY state");
}

static void State_CriticalError_OnEntry(void)
{
    Debug_SendMessage(DEBUG_MSG_ERROR, "CRITICAL ERROR STATE");
    ErrorInfo_t error_info;
    if (ErrorHandler_GetCurrentError(&error_info))
    {
        Debug_SendMessage(DEBUG_MSG_ERROR, "Error: %s from state %s",
                          ErrorCodeToString(error_info.code),
                          StateToString(error_info.state));
    }
}

static void State_CriticalError_OnState(void)
{
    if ((g_sm_context.state_execution_count % 100) == 0)
    {
        Debug_SendMessage(DEBUG_MSG_ERROR, "System in critical error lock");
    }
}

static void State_CriticalError_OnExit(void)
{
    Debug_SendMessage(DEBUG_MSG_ERROR, "Exiting CRITICAL ERROR state");
}

bool App_Main_Init(void)
{
    Debug_SendMessage(DEBUG_MSG_INIT, "Application starting");
    if (!Debug_Init(COMM_INTERFACE_UART))
    {
        return false;
    }
    if (!StateMachine_Init())
    {
        Debug_SendMessage(DEBUG_MSG_ERROR, "State machine init failed");
        return false;
    }
    Debug_SendMessage(DEBUG_MSG_INIT, "Application initialized");
    return true;
}

void App_Main_Task(void)
{
    StateMachine_Execute();
    Debug_ProcessPeriodic();
}
