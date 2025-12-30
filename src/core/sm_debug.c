/**
 * @file sm_debug.c
 * @brief Debug messaging system implementation
 * @version 2.0.0
 */

#include "sm_framework/sm_framework.h"
#include "sm_framework/sm_platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Debug configuration */
static DebugConfig_t g_debug_config;

/* Custom formatter (optional) */
static DebugFormatter_t g_custom_formatter = NULL;

/* Forward declarations */
static void SendFormattedMessage(const DebugMessage_t *message);
static uint32_t DefaultFormatter(DebugMessageType_t type, uint32_t timestamp,
                                  const char *message, char *buffer, uint32_t buffer_size);

bool Debug_Init(CommInterface_t interface)
{
    memset(&g_debug_config, 0, sizeof(DebugConfig_t));
    
    g_debug_config.interface = interface;
    g_debug_config.enable_init_messages = (DEBUG_ENABLE_INIT_MESSAGES != 0);
    g_debug_config.enable_runtime_messages = (DEBUG_ENABLE_RUNTIME_MESSAGES != 0);
    g_debug_config.enable_periodic_messages = (DEBUG_ENABLE_PERIODIC_MESSAGES != 0);
    g_debug_config.periodic_last_time = Platform_GetTimeMs();
    
    /* Initialize platform interface */
    switch (interface) {
        case COMM_INTERFACE_UART:
            return Platform_UART_Init();
        case COMM_INTERFACE_SPI:
            return Platform_SPI_Init();
        case COMM_INTERFACE_I2C:
            return Platform_I2C_Init();
        case COMM_INTERFACE_USB:
            return Platform_USB_Init();
        case COMM_INTERFACE_RTT:
            return Platform_RTT_Init();
        default:
            return false;
    }
}

void Debug_SendMessage(DebugMessageType_t type, const char *format, ...)
{
    DebugMessage_t message;
    va_list args;
    
    /* Filter disabled message types */
    if (type == DEBUG_MSG_INIT && !g_debug_config.enable_init_messages) return;
    if (type == DEBUG_MSG_RUNTIME && !g_debug_config.enable_runtime_messages) return;
    if (type == DEBUG_MSG_PERIODIC && !g_debug_config.enable_periodic_messages) return;
    
    /* Format message */
    message.type = type;
    message.timestamp = Platform_GetTimeMs();
    va_start(args, format);
    vsnprintf(message.message, DEBUG_MAX_MESSAGE_LENGTH, format, args);
    va_end(args);
    
    /* Send */
    SendFormattedMessage(&message);
}

void Debug_SendRawMessage(DebugMessageType_t type, const char *message)
{
    DebugMessage_t msg;
    msg.type = type;
    msg.timestamp = Platform_GetTimeMs();
    strncpy(msg.message, message, DEBUG_MAX_MESSAGE_LENGTH - 1);
    msg.message[DEBUG_MAX_MESSAGE_LENGTH - 1] = '\0';
    SendFormattedMessage(&msg);
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

void Debug_EnableErrorMessages(bool enable)
{
    /* Always keep errors enabled for safety */
    (void)enable;
}

void Debug_EnableWarningMessages(bool enable)
{
    (void)enable;  /* Not separately tracked in current implementation */
}

void Debug_EnableInfoMessages(bool enable)
{
    (void)enable;  /* Not separately tracked in current implementation */
}

void Debug_EnableAllMessages(void)
{
    g_debug_config.enable_init_messages = true;
    g_debug_config.enable_runtime_messages = true;
    g_debug_config.enable_periodic_messages = true;
}

void Debug_DisableAllMessages(void)
{
    g_debug_config.enable_init_messages = false;
    g_debug_config.enable_runtime_messages = false;
    g_debug_config.enable_periodic_messages = false;
}

void Debug_ProcessPeriodic(void)
{
    uint32_t current_time = Platform_GetTimeMs();
    
    if (!g_debug_config.enable_periodic_messages) {
        return;
    }
    
    if ((current_time - g_debug_config.periodic_last_time) >= DEBUG_PERIODIC_INTERVAL_MS) {
        Debug_SendMessage(DEBUG_MSG_PERIODIC, "State=%s Exec=%lu",
                         StateMachine_StateToString(StateMachine_GetCurrentState()),
                         (unsigned long)StateMachine_GetExecutionCount());
        g_debug_config.periodic_last_time = current_time;
    }
}

void Debug_SetPeriodicInterval(uint32_t interval_ms)
{
    /* Update periodic interval - would need to add to config structure */
    (void)interval_ms;
}

bool Debug_SetInterface(CommInterface_t interface)
{
    if (interface >= COMM_INTERFACE_MAX) {
        return false;
    }
    
    g_debug_config.interface = interface;
    return true;
}

CommInterface_t Debug_GetInterface(void)
{
    return g_debug_config.interface;
}

void Debug_SetFormatter(DebugFormatter_t formatter)
{
    g_custom_formatter = formatter;
}

static void SendFormattedMessage(const DebugMessage_t *message)
{
    char formatted_buffer[DEBUG_BUFFER_SIZE];
    uint32_t length;
    
    /* Use custom formatter if set, otherwise default */
    if (g_custom_formatter != NULL) {
        length = g_custom_formatter(message->type, message->timestamp,
                                    message->message, formatted_buffer,
                                    DEBUG_BUFFER_SIZE);
    } else {
        length = DefaultFormatter(message->type, message->timestamp,
                                 message->message, formatted_buffer,
                                 DEBUG_BUFFER_SIZE);
    }
    
    /* Send via configured interface */
    switch (g_debug_config.interface) {
        case COMM_INTERFACE_UART:
            Platform_UART_Send((const uint8_t *)formatted_buffer, length);
            break;
        case COMM_INTERFACE_SPI:
            Platform_SPI_Send((const uint8_t *)formatted_buffer, length);
            break;
        case COMM_INTERFACE_I2C:
            Platform_I2C_Send((const uint8_t *)formatted_buffer, length);
            break;
        case COMM_INTERFACE_USB:
            Platform_USB_Send((const uint8_t *)formatted_buffer, length);
            break;
        case COMM_INTERFACE_RTT:
            Platform_RTT_Send((const uint8_t *)formatted_buffer, length);
            break;
        default:
            break;
    }
}

static uint32_t DefaultFormatter(DebugMessageType_t type, uint32_t timestamp,
                                  const char *message, char *buffer, uint32_t buffer_size)
{
    (void)type;  /* Type not used in default format, but could add [TYPE] prefix */
    return (uint32_t)snprintf(buffer, buffer_size, "[%lu] %s\n",
                             (unsigned long)timestamp, message);
}
