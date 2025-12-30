#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SM_MAX_STATES 10U
#define SM_MAX_TRANSITIONS_PER_STATE 5U
#define SM_STATE_TIMEOUT_MS 5000U
#define SM_TASK_PERIOD_MS 10U

#define ERROR_MAX_RECOVERY_ATTEMPTS 3U
#define ERROR_MINOR_RETRY_COUNT 3U
#define ERROR_MINOR_TIMEOUT_MS 50U
#define ERROR_HISTORY_SIZE 16U

#define DEBUG_BUFFER_SIZE 256U
#define DEBUG_MAX_MESSAGE_LENGTH 128U
#define DEBUG_ENABLE_INIT_MESSAGES 1U
#define DEBUG_ENABLE_RUNTIME_MESSAGES 1U
#define DEBUG_ENABLE_PERIODIC_MESSAGES 1U
#define DEBUG_PERIODIC_INTERVAL_MS 1000U

#define COMM_PACKET_SIZE 64U
#define COMM_TIMEOUT_MS 100U
#define COMM_RETRY_COUNT 3U
#define COMM_VERIFICATION_COUNT 3U
#define COMM_VERIFICATION_WINDOW_MS 50U

typedef enum
{
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_PROCESSING,
    STATE_COMMUNICATING,
    STATE_MONITORING,
    STATE_CALIBRATING,
    STATE_DIAGNOSTICS,
    STATE_RECOVERY,
    STATE_CRITICAL_ERROR,
    STATE_MAX
} StateMachineState_t;

typedef enum
{
    EVENT_NONE = 0,
    EVENT_INIT_COMPLETE,
    EVENT_START,
    EVENT_STOP,
    EVENT_DATA_READY,
    EVENT_PROCESSING_DONE,
    EVENT_COMM_REQUEST,
    EVENT_COMM_COMPLETE,
    EVENT_TIMEOUT,
    EVENT_ERROR_MINOR,
    EVENT_ERROR_NORMAL,
    EVENT_ERROR_CRITICAL,
    EVENT_RECOVERY_SUCCESS,
    EVENT_RECOVERY_FAILED,
    EVENT_MAX
} StateMachineEvent_t;

typedef enum
{
    ERROR_LEVEL_NONE = 0,
    ERROR_LEVEL_MINOR,
    ERROR_LEVEL_NORMAL,
    ERROR_LEVEL_CRITICAL,
    ERROR_LEVEL_MAX
} ErrorLevel_t;

typedef enum
{
    ERROR_CODE_NONE = 0,
    ERROR_CODE_TIMEOUT,
    ERROR_CODE_COMM_LOST,
    ERROR_CODE_COMM_CORRUPT,
    ERROR_CODE_INVALID_DATA,
    ERROR_CODE_BUFFER_OVERFLOW,
    ERROR_CODE_RESOURCE_UNAVAILABLE,
    ERROR_CODE_CALIBRATION_FAILED,
    ERROR_CODE_HARDWARE_FAULT,
    ERROR_CODE_WATCHDOG_RESET,
    ERROR_CODE_MEMORY_CORRUPTION,
    ERROR_CODE_MAX
} ErrorCode_t;

typedef enum
{
    COMM_INTERFACE_UART = 0,
    COMM_INTERFACE_SPI,
    COMM_INTERFACE_I2C,
    COMM_INTERFACE_MAX
} CommInterface_t;

typedef enum
{
    DEBUG_MSG_INIT = 0,
    DEBUG_MSG_RUNTIME,
    DEBUG_MSG_PERIODIC,
    DEBUG_MSG_ERROR,
    DEBUG_MSG_WARNING,
    DEBUG_MSG_INFO,
    DEBUG_MSG_MAX
} DebugMessageType_t;

typedef struct
{
    ErrorLevel_t level;
    ErrorCode_t code;
    uint32_t timestamp;
    StateMachineState_t state;
    uint8_t retry_count;
    bool is_recovered;
} ErrorInfo_t;

typedef struct
{
    ErrorInfo_t current_error;
    ErrorInfo_t error_history[16];
    uint8_t history_index;
    uint32_t minor_error_timestamp;
    uint8_t minor_good_message_count;
    bool critical_lock_active;
} ErrorHandler_t;

typedef struct
{
    StateMachineEvent_t event;
    StateMachineState_t next_state;
} StateTransition_t;

typedef struct
{
    StateMachineState_t state_id;
    void (*on_entry)(void);
    void (*on_state)(void);
    void (*on_exit)(void);
    StateTransition_t transitions[5];
    uint8_t transition_count;
    uint32_t timeout_ms;
} StateConfig_t;

typedef struct
{
    StateMachineState_t current_state;
    StateMachineState_t previous_state;
    StateMachineEvent_t pending_event;
    uint32_t state_entry_time;
    uint32_t state_execution_count;
    bool state_changed;
    ErrorHandler_t error_handler;
} StateMachineContext_t;

typedef struct
{
    DebugMessageType_t type;
    char message[128];
    uint32_t timestamp;
} DebugMessage_t;

typedef struct
{
    CommInterface_t interface;
    bool enable_init_messages;
    bool enable_runtime_messages;
    bool enable_periodic_messages;
    uint32_t periodic_last_time;
} DebugConfig_t;

bool StateMachine_Init(void);
StateMachineState_t StateMachine_Execute(void);
bool StateMachine_PostEvent(StateMachineEvent_t event);
StateMachineState_t StateMachine_GetCurrentState(void);
StateMachineState_t StateMachine_GetPreviousState(void);
void StateMachine_Reset(void);

bool ErrorHandler_Init(void);
bool ErrorHandler_Report(ErrorLevel_t level, ErrorCode_t code);
bool ErrorHandler_AttemptRecovery(void);
bool ErrorHandler_IsCriticalLock(void);
void ErrorHandler_ClearError(void);
bool ErrorHandler_GetCurrentError(ErrorInfo_t *error_info);
bool ErrorHandler_HandleMinorError(ErrorCode_t code);
bool ErrorHandler_HandleNormalError(ErrorCode_t code);
void ErrorHandler_HandleCriticalError(ErrorCode_t code);

bool Debug_Init(CommInterface_t interface);
void Debug_SendMessage(DebugMessageType_t type, const char *format, ...);
void Debug_EnableInitMessages(bool enable);
void Debug_EnableRuntimeMessages(bool enable);
void Debug_EnablePeriodicMessages(bool enable);
void Debug_ProcessPeriodic(void);
bool Debug_SetInterface(CommInterface_t interface);

bool Comm_UART_Init(void);
uint32_t Comm_UART_Send(const uint8_t *data, uint32_t length);
bool Comm_SPI_Init(void);
uint32_t Comm_SPI_Send(const uint8_t *data, uint32_t length);
bool Comm_I2C_Init(void);
uint32_t Comm_I2C_Send(const uint8_t *data, uint32_t length);
bool Comm_VerifyChannel(void);

uint32_t GetCurrentTimeMs(void);
bool IsTimeout(uint32_t start_time, uint32_t timeout_ms);
const char *StateToString(StateMachineState_t state);
const char *ErrorCodeToString(ErrorCode_t code);

bool App_Main_Init(void);
void App_Main_Task(void);

#endif
