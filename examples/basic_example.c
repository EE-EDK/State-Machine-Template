/**
 * @file basic_example.c
 * @brief Basic v3.0 example -- minimal 3-state FSM
 * @version 3.0.0
 * @date 2026-04-18
 *
 * Demonstrates the v3.0 API pattern:
 *   - User defines their own state and event enums
 *   - User provides state descriptors and transition table (const, flash)
 *   - User statically allocates SM_Context_t
 *   - SM_Init / SM_Process / SM_PostEvent handle-based calls
 *
 * States: INIT -> RUNNING -> STOPPED
 * Events: EVT_START, EVT_STOP, EVT_TICK
 */

/* --- Application configuration (must come before framework headers) --- */

/* REQUIRED: tell the framework how many states and events we have */
#define SM_STATE_COUNT    (3U)
#define SM_EVENT_COUNT    (3U)

/* Optional: override non-struct-sizing defaults.
 * NOTE: Do NOT override SM_EVENT_QUEUE_SIZE here when linking against a
 * pre-compiled library -- it changes SM_Context_t layout. Override in
 * CMakeLists.txt so both library and app see the same value, or compile
 * the framework as part of your project via add_subdirectory(). */
#define SM_DEBUG_LEVEL        (4U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 * ===========================================================================*/

typedef enum {
    STATE_INIT = 0,
    STATE_RUNNING,
    STATE_STOPPED
    /* SM_STATE_COUNT = 3 defined above */
} AppState_t;

typedef enum {
    EVT_START = 0,
    EVT_STOP,
    EVT_TICK
    /* SM_EVENT_COUNT = 3 defined above */
} AppEvent_t;

/* =============================================================================
 * STATE CALLBACKS
 * ===========================================================================*/

static void on_init_entry(SM_Handle_t sm)
{
    printf("  [INIT] Entry -- initializing hardware...\n");
    /* Simulate init complete: post EVT_START */
    SM_PostEvent(sm, (uint16_t)EVT_START, 0U);
}

static void on_init_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Nothing to do while waiting for EVT_START */
}

static void on_init_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [INIT] Exit\n");
}

static void on_running_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [RUNNING] Entry -- system active\n");
}

static void on_running_execute(SM_Handle_t sm)
{
    /* Post a tick event with exec count as payload */
    uint32_t count = SM_GetExecCount(sm);
    if (count == 5U) {
        printf("  [RUNNING] 5 cycles done, posting EVT_STOP\n");
        SM_PostEvent(sm, (uint16_t)EVT_STOP, count);
    }
}

static void on_running_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [RUNNING] Exit\n");
}

static void on_stopped_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [STOPPED] Entry -- system halted\n");
}

static void on_stopped_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Stay stopped */
}

static void on_stopped_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [STOPPED] Exit\n");
}

/* =============================================================================
 * STATE DESCRIPTORS (const, flash)
 * ===========================================================================*/

static const SM_StateDesc_t app_states[SM_STATE_COUNT] = {
    [STATE_INIT] = {
        .on_entry   = on_init_entry,
        .on_execute = on_init_execute,
        .on_exit    = on_init_exit,
        .timeout_ms = 5000U,
        .min_dwell_ms = 0U,
    },
    [STATE_RUNNING] = {
        .on_entry   = on_running_entry,
        .on_execute = on_running_execute,
        .on_exit    = on_running_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_STOPPED] = {
        .on_entry   = on_stopped_entry,
        .on_execute = on_stopped_execute,
        .on_exit    = on_stopped_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
};

/* =============================================================================
 * TRANSITION TABLE (const, flash)
 * ===========================================================================*/

static const SM_Transition_t app_transitions[] = {
    /* INIT --EVT_START--> RUNNING */
    { .from_state = STATE_INIT,    .event = EVT_START, .to_state = STATE_RUNNING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* INIT --SM_EVT_TIMEOUT--> STOPPED (failsafe: STATE_INIT sets
     * timeout_ms = 5000, so if no start command arrives within 5 s the
     * machine halts instead of waiting forever. This demo starts within a
     * few ms, so the route exists but never fires here.) */
    { .from_state = STATE_INIT,    .event = SM_EVT_TIMEOUT, .to_state = STATE_STOPPED,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* RUNNING --EVT_STOP--> STOPPED */
    { .from_state = STATE_RUNNING, .event = EVT_STOP,  .to_state = STATE_STOPPED,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* STOPPED --EVT_START--> RUNNING (restart) */
    { .from_state = STATE_STOPPED, .event = EVT_START, .to_state = STATE_RUNNING,
      ._reserved = 0, .guard = NULL, .action = NULL },
};

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf(" State Machine Framework v%s -- Basic Example\n", SM_FRAMEWORK_VERSION_STRING);
    printf("================================================\n\n");

    /* Initialize debug output */
    SM_Debug_Init(0U);

    /* Static allocation of state machine context */
    SM_Context_t sm_ctx;

    /* Configuration */
    SM_Config_t config = {
        .states           = app_states,
        .transitions      = app_transitions,
        .transition_count = (uint16_t)(sizeof(app_transitions) / sizeof(app_transitions[0])),
        .initial_state    = STATE_INIT,
    };

    /* Initialize */
    if (!SM_Init(&sm_ctx, &config)) {
        printf("ERROR: SM_Init failed!\n");
        return -1;
    }

    printf("Running state machine for 20 iterations...\n\n");

    /* Main loop -- advance the sim clock 1 ms per iteration so time-based
     * features (STATE_INIT's 5 s timeout failsafe) are live, not
     * decorative. On hardware the clock advances on its own. */
    for (int i = 0; i < 20; i++) {
        SM_Platform_SimTick();
        SM_Process(&sm_ctx);

        if ((i % 5) == 0) {
            printf("[iter %d] state=%u prev=%u exec_count=%lu queue_depth=%u\n",
                   i,
                   (unsigned)SM_GetState(&sm_ctx),
                   (unsigned)SM_GetPreviousState(&sm_ctx),
                   (unsigned long)SM_GetExecCount(&sm_ctx),
                   (unsigned)SM_EventQueueDepth(&sm_ctx));
        }
    }

    printf("\n================================================\n");
    printf(" Example completed!\n");
    printf(" Final state: %u\n", (unsigned)SM_GetState(&sm_ctx));
    printf("================================================\n\n");

    return 0;
}
