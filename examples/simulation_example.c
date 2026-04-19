/**
 * @file simulation_example.c
 * @brief Simulation example with real timing (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * Demonstrates:
 *   - Overriding SM_Platform_GetTimeMs with real clock
 *   - Event queue with payloads
 *   - Error reporting
 *   - State history queries
 */

#define _POSIX_C_SOURCE 199309L

/* --- Application configuration --- */
#define SM_STATE_COUNT    (3U)
#define SM_EVENT_COUNT    (4U)
#define SM_EVENT_QUEUE_SIZE (8U)
#define SM_DEBUG_LEVEL    (3U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* =============================================================================
 * PLATFORM OVERRIDE: real millisecond timing
 *
 * On ELF targets (Linux, ARM bare metal), the SM_WEAK attribute on the
 * default implementation allows this strong definition to override it.
 * On PE/COFF targets (Windows/MinGW), SM_WEAK is empty so the default
 * is already strong -- this override is excluded to avoid duplicate symbols.
 * Windows simulation uses the incrementing counter from sm_platform_weak.c.
 * ===========================================================================*/

#if !defined(_WIN32) && !defined(__CYGWIN__)
uint32_t SM_Platform_GetTimeMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
#endif

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 * ===========================================================================*/

enum { STATE_IDLE = 0, STATE_WORKING, STATE_DONE };
enum { EVT_START = 0, EVT_FINISH, EVT_RESET, EVT_ERROR };

/* =============================================================================
 * STATE CALLBACKS
 * ===========================================================================*/

static void idle_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [IDLE] Waiting for work...\n");
}

static void idle_exec(SM_Handle_t sm)
{
    (void)sm;
}

static void working_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [WORKING] Processing...\n");
}

static void working_exec(SM_Handle_t sm)
{
    if (SM_GetExecCount(sm) >= 3U) {
        printf("  [WORKING] Done after %lu cycles\n", (unsigned long)SM_GetExecCount(sm));
        SM_PostEvent(sm, (uint16_t)EVT_FINISH, 42U);
    }
}

static void done_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [DONE] Work complete.\n");
}

static void done_exec(SM_Handle_t sm)
{
    (void)sm;
}

/* =============================================================================
 * TABLES
 * ===========================================================================*/

static const SM_StateDesc_t states[SM_STATE_COUNT] = {
    [STATE_IDLE]    = { idle_entry, idle_exec, NULL, 0, 0 },
    [STATE_WORKING] = { working_entry, working_exec, NULL, 0, 0 },
    [STATE_DONE]    = { done_entry, done_exec, NULL, 0, 0 },
};

static const SM_Transition_t transitions[] = {
    { STATE_IDLE,    EVT_START,  STATE_WORKING, 0, NULL, NULL },
    { STATE_WORKING, EVT_FINISH, STATE_DONE,    0, NULL, NULL },
    { STATE_DONE,    EVT_RESET,  STATE_IDLE,    0, NULL, NULL },
};

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n========================================================\n");
    printf(" State Machine Framework v%s -- Simulation Example\n", SM_FRAMEWORK_VERSION_STRING);
    printf("========================================================\n\n");

    SM_Debug_Init(0U);

    SM_Context_t ctx;
    SM_Config_t config = {
        .states           = states,
        .transitions      = transitions,
        .transition_count = (uint16_t)(sizeof(transitions) / sizeof(transitions[0])),
        .initial_state    = STATE_IDLE,
    };

    if (!SM_Init(&ctx, &config)) {
        printf("ERROR: SM_Init failed!\n");
        return -1;
    }

    printf("--- Phase 1: IDLE -> WORKING -> DONE ---\n");

    /* Post start event */
    SM_PostEvent(&ctx, (uint16_t)EVT_START, 0U);

    for (int i = 0; i < 10; i++) {
        SM_Process(&ctx);
        usleep(10000); /* 10ms */
    }

    /* Check event queue state */
    printf("\nQueue empty: %s, depth: %u\n",
           SM_EventQueueIsEmpty(&ctx) ? "yes" : "no",
           (unsigned)SM_EventQueueDepth(&ctx));

    /* Test error reporting */
    printf("\n--- Phase 2: Error reporting ---\n");
    SM_Error_Report(&ctx, SM_ERROR_MINOR, 1U);
    SM_Error_Report(&ctx, SM_ERROR_NORMAL, 2U);

    SM_ErrorInfo_t err_info;
    if (SM_Error_GetCurrent(&ctx, &err_info)) {
        printf("Current error: level=%d code=%u\n",
               (int)err_info.level, (unsigned)err_info.code);
    }
    printf("History count: %u\n", (unsigned)SM_Error_GetHistoryCount(&ctx));
    printf("Critical lock: %s\n", SM_Error_IsCriticalLock(&ctx) ? "yes" : "no");

    printf("\n========================================================\n");
    printf(" Simulation completed.\n");
    printf(" Final state: %u\n", (unsigned)SM_GetState(&ctx));
    printf("========================================================\n\n");

    return 0;
}
