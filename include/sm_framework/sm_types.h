/**
 * @file sm_types.h
 * @brief Common type definitions for State Machine Framework
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * This file contains all common type definitions, enumerations, and structures
 * used throughout the state machine framework.
 */

#ifndef SM_TYPES_H
#define SM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include user configuration */
#include "sm_config.h"

/* =============================================================================
 * STATE AND EVENT ENUMERATIONS
 * ===========================================================================*/

/**
 * @brief State machine states
 *
 * These are the predefined states. Users can extend by adding more states
 * before STATE_MAX and increasing SM_MAX_STATES in configuration.
 */
typedef enum {
    STATE_INIT = 0,          /**< Initialization state */
    STATE_IDLE,              /**< Idle/waiting state */
    STATE_ACTIVE,            /**< Active monitoring state */
    STATE_PROCESSING,        /**< Data processing state */
    STATE_COMMUNICATING,     /**< Communication state */
    STATE_MONITORING,        /**< Health monitoring state */
    STATE_CALIBRATING,       /**< Calibration state */
    STATE_DIAGNOSTICS,       /**< Diagnostic state */
    STATE_RECOVERY,          /**< Error recovery state */
    STATE_CRITICAL_ERROR,    /**< Critical error lock state */
    STATE_MAX                /**< Number of states (must be last) */
} StateMachineState_t;

/**
 * @brief State machine events
 *
 * Events trigger state transitions. Users can extend by adding more events
 * before EVENT_MAX.
 */
typedef enum {
    EVENT_NONE = 0,          /**< No event pending */
    EVENT_INIT_COMPLETE,     /**< Initialization completed */
    EVENT_START,             /**< Start operation */
    EVENT_STOP,              /**< Stop operation */
    EVENT_DATA_READY,        /**< Data ready for processing */
    EVENT_PROCESSING_DONE,   /**< Processing completed */
    EVENT_COMM_REQUEST,      /**< Communication requested */
    EVENT_COMM_COMPLETE,     /**< Communication completed */
    EVENT_TIMEOUT,           /**< State timeout occurred */
    EVENT_ERROR_MINOR,       /**< Minor error reported */
    EVENT_ERROR_NORMAL,      /**< Normal error reported */
    EVENT_ERROR_CRITICAL,    /**< Critical error reported */
    EVENT_RECOVERY_SUCCESS,  /**< Recovery successful */
    EVENT_RECOVERY_FAILED,   /**< Recovery failed */
    EVENT_MAX                /**< Number of events (must be last) */
} StateMachineEvent_t;

/* =============================================================================
 * ERROR HANDLING TYPES
 * ===========================================================================*/

/**
 * @brief Error severity levels
 */
typedef enum {
    ERROR_LEVEL_NONE = 0,    /**< No error */
    ERROR_LEVEL_MINOR,       /**< Minor error - auto-recovery */
    ERROR_LEVEL_NORMAL,      /**< Normal error - managed recovery */
    ERROR_LEVEL_CRITICAL,    /**< Critical error - system lock */
    ERROR_LEVEL_MAX          /**< Number of error levels (must be last) */
} ErrorLevel_t;

/**
 * @brief Error codes
 *
 * Specific error conditions. Users can extend.
 */
typedef enum {
    ERROR_CODE_NONE = 0,             /**< No error */
    ERROR_CODE_TIMEOUT,              /**< Operation timeout */
    ERROR_CODE_COMM_LOST,            /**< Communication lost */
    ERROR_CODE_COMM_CORRUPT,         /**< Corrupted communication */
    ERROR_CODE_INVALID_DATA,         /**< Invalid data received */
    ERROR_CODE_BUFFER_OVERFLOW,      /**< Buffer overflow */
    ERROR_CODE_RESOURCE_UNAVAILABLE, /**< Resource not available */
    ERROR_CODE_CALIBRATION_FAILED,   /**< Calibration failed */
    ERROR_CODE_HARDWARE_FAULT,       /**< Hardware fault detected */
    ERROR_CODE_WATCHDOG_RESET,       /**< Watchdog reset occurred */
    ERROR_CODE_MEMORY_CORRUPTION,    /**< Memory corruption detected */
    ERROR_CODE_MAX                   /**< Number of error codes (must be last) */
} ErrorCode_t;

/**
 * @brief Error information structure
 *
 * Stores detailed information about an error occurrence.
 */
typedef struct {
    ErrorLevel_t level;          /**< Error severity level */
    ErrorCode_t code;            /**< Specific error code */
    uint32_t timestamp;          /**< Time when error occurred */
    StateMachineState_t state;   /**< State when error occurred */
    uint8_t retry_count;         /**< Number of recovery attempts */
    bool is_recovered;           /**< Recovery status */
} ErrorInfo_t;

/* =============================================================================
 * DEBUG SYSTEM TYPES
 * ===========================================================================*/

/**
 * @brief Debug message types
 */
typedef enum {
    DEBUG_MSG_INIT = 0,      /**< Initialization messages */
    DEBUG_MSG_RUNTIME,       /**< Runtime operation messages */
    DEBUG_MSG_PERIODIC,      /**< Periodic status messages */
    DEBUG_MSG_ERROR,         /**< Error messages */
    DEBUG_MSG_WARNING,       /**< Warning messages */
    DEBUG_MSG_INFO,          /**< Informational messages */
    DEBUG_MSG_MAX            /**< Number of message types (must be last) */
} DebugMessageType_t;

/**
 * @brief Communication interface types
 */
typedef enum {
    COMM_INTERFACE_UART = 0, /**< UART/Serial interface */
    COMM_INTERFACE_SPI,      /**< SPI interface */
    COMM_INTERFACE_I2C,      /**< I2C interface */
    COMM_INTERFACE_USB,      /**< USB interface */
    COMM_INTERFACE_RTT,      /**< SEGGER RTT interface */
    COMM_INTERFACE_MAX       /**< Number of interfaces (must be last) */
} CommInterface_t;

/**
 * @brief Debug message structure
 *
 * FIXED: Uses DEBUG_MAX_MESSAGE_LENGTH constant instead of hardcoded 128
 */
typedef struct {
    DebugMessageType_t type;                   /**< Message type */
    char message[DEBUG_MAX_MESSAGE_LENGTH];    /**< Message text */
    uint32_t timestamp;                        /**< Message timestamp */
} DebugMessage_t;

/* =============================================================================
 * STATE MACHINE CORE TYPES
 * ===========================================================================*/

/**
 * @brief State transition definition
 */
typedef struct {
    StateMachineEvent_t event;   /**< Event that triggers transition */
    StateMachineState_t next_state; /**< Target state */
} StateTransition_t;

/**
 * @brief State configuration
 *
 * FIXED: Uses SM_MAX_TRANSITIONS_PER_STATE constant instead of hardcoded 5
 */
typedef struct {
    StateMachineState_t state_id;                              /**< State identifier */
    void (*on_entry)(void);                                     /**< Entry callback */
    void (*on_state)(void);                                     /**< State callback */
    void (*on_exit)(void);                                      /**< Exit callback */
    StateTransition_t transitions[SM_MAX_TRANSITIONS_PER_STATE]; /**< Transition table */
    uint8_t transition_count;                                   /**< Number of transitions */
    uint32_t timeout_ms;                                        /**< State timeout */
} StateConfig_t;

/**
 * @brief Error handler context
 *
 * FIXED: Uses ERROR_HISTORY_SIZE constant instead of hardcoded 16
 */
typedef struct {
    ErrorInfo_t current_error;                 /**< Current active error */
    ErrorInfo_t error_history[ERROR_HISTORY_SIZE]; /**< Error history buffer */
    uint8_t history_index;                     /**< Circular buffer index */
    uint32_t minor_error_timestamp;            /**< Minor error tracking */
    uint8_t minor_good_message_count;          /**< Good message counter */
    bool critical_lock_active;                 /**< Critical error lock flag */
} ErrorHandler_t;

/**
 * @brief State machine context
 *
 * FIXED: Added volatile qualifier for ISR-accessed field
 * Contains all runtime state for the state machine.
 */
typedef struct {
    StateMachineState_t current_state;     /**< Current active state */
    StateMachineState_t previous_state;    /**< Previous state */
    volatile StateMachineEvent_t pending_event; /**< Pending event (ISR-safe) */
    uint32_t state_entry_time;             /**< Time when state was entered */
    uint32_t state_execution_count;        /**< Number of times state executed */
    bool state_changed;                    /**< State change flag */
    ErrorHandler_t error_handler;          /**< Error handler context */
} StateMachineContext_t;

/**
 * @brief Debug system configuration
 */
typedef struct {
    CommInterface_t interface;         /**< Active communication interface */
    bool enable_init_messages;         /**< Enable init messages */
    bool enable_runtime_messages;      /**< Enable runtime messages */
    bool enable_periodic_messages;     /**< Enable periodic messages */
    uint32_t periodic_last_time;       /**< Last periodic message time */
} DebugConfig_t;

/* =============================================================================
 * COMPILE-TIME VALIDATION
 * ===========================================================================*/

/* Ensure STATE_MAX doesn't exceed configured maximum */
_Static_assert(STATE_MAX <= SM_MAX_STATES,
    "STATE_MAX exceeds SM_MAX_STATES - increase SM_MAX_STATES in configuration");

/* Ensure EVENT_MAX fits in reasonable size */
_Static_assert(EVENT_MAX < 256,
    "EVENT_MAX exceeds 255 - consider using uint16_t for event type");

/* Ensure error history size is reasonable */
_Static_assert(ERROR_HISTORY_SIZE > 0 && ERROR_HISTORY_SIZE <= 255,
    "ERROR_HISTORY_SIZE must be between 1 and 255");

#ifdef __cplusplus
}
#endif

#endif /* SM_TYPES_H */
