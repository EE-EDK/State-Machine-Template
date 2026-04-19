/**
 * @file multi_fsm_example.c
 * @brief Multi-FSM v3.0 example -- two independent state machines
 * @version 3.0.0
 * @date 2026-04-19
 *
 * Demonstrates the handle-based multi-instance pattern:
 *   - Two SM_Context_t instances on the stack (motor_ctx, led_ctx)
 *   - Each has its own SM_Config_t with different callback tables
 *   - Both share the same state/event enum space (state-agnostic)
 *   - Both processed in the same main loop
 *   - Events posted independently to each instance
 *
 * FSM 1 "Motor Controller": IDLE -> RUNNING -> STOPPING -> IDLE
 * FSM 2 "LED Controller":   IDLE -> RUNNING -> STOPPING -> IDLE
 *   (same topology, different behavior via callbacks)
 *
 * Scenario:
 *   1. Init both FSMs in IDLE
 *   2. Start motor (EVT_START), run 5 iterations
 *   3. Start LED (EVT_START), run 5 iterations (both running)
 *   4. Stop motor (EVT_STOP), run 5 iterations (motor stopping, LED running)
 *   5. Motor auto-completes after 3 exec cycles (posts EVT_DONE)
 *   6. Stop LED (EVT_STOP), run 5 iterations (LED stopping, motor idle)
 *   7. LED auto-completes after 2 exec cycles
 *   8. Print final states
 */

/* --- Application configuration (must come before framework headers) --- */

/* REQUIRED: tell the framework how many states and events we have */
#define SM_STATE_COUNT    (3U)
#define SM_EVENT_COUNT    (3U)

/* Optional: enable verbose debug output */
#define SM_DEBUG_LEVEL    (4U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 *
 * Both FSMs share these enums -- the framework is state-agnostic.
 * ===========================================================================*/

typedef enum {
    STATE_IDLE = 0,
    STATE_RUNNING,
    STATE_STOPPING
    /* SM_STATE_COUNT = 3 defined above */
} AppState_t;

typedef enum {
    EVT_START = 0,
    EVT_STOP,
    EVT_DONE
    /* SM_EVENT_COUNT = 3 defined above */
} AppEvent_t;

/* =============================================================================
 * MOTOR CONTROLLER CALLBACKS
 * ===========================================================================*/

static void motor_idle_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Entry IDLE -- motor off\n");
}

static void motor_idle_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Idle -- waiting for EVT_START */
}

static void motor_idle_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Exit IDLE\n");
}

static void motor_running_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Entry RUNNING -- motor spinning\n");
}

static void motor_running_execute(SM_Handle_t sm)
{
    uint32_t count = SM_GetExecCount(sm);
    if ((count % 2U) == 0U) {
        printf("  [MOTOR] Running... (cycle %lu)\n", (unsigned long)count);
    }
}

static void motor_running_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Exit RUNNING\n");
}

static void motor_stopping_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Entry STOPPING -- decelerating\n");
}

static void motor_stopping_execute(SM_Handle_t sm)
{
    uint32_t count = SM_GetExecCount(sm);
    printf("  [MOTOR] Stopping... (cycle %lu)\n", (unsigned long)count);

    /* After 3 exec cycles, motor has stopped -- post EVT_DONE to self */
    if (count >= 3U) {
        printf("  [MOTOR] Deceleration complete, posting EVT_DONE\n");
        SM_PostEvent(sm, (uint16_t)EVT_DONE, 0U);
    }
}

static void motor_stopping_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [MOTOR] Exit STOPPING\n");
}

/* =============================================================================
 * LED CONTROLLER CALLBACKS
 * ===========================================================================*/

static void led_idle_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Entry IDLE -- LED off\n");
}

static void led_idle_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Idle -- waiting for EVT_START */
}

static void led_idle_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Exit IDLE\n");
}

static void led_running_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Entry RUNNING -- LED blinking\n");
}

static void led_running_execute(SM_Handle_t sm)
{
    uint32_t count = SM_GetExecCount(sm);
    if ((count % 2U) == 0U) {
        printf("  [LED]   Blinking... (cycle %lu)\n", (unsigned long)count);
    }
}

static void led_running_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Exit RUNNING\n");
}

static void led_stopping_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Entry STOPPING -- LED fading out\n");
}

static void led_stopping_execute(SM_Handle_t sm)
{
    uint32_t count = SM_GetExecCount(sm);
    printf("  [LED]   Fading... (cycle %lu)\n", (unsigned long)count);

    /* After 2 exec cycles, fade-out complete -- post EVT_DONE to self */
    if (count >= 2U) {
        printf("  [LED]   Fade-out complete, posting EVT_DONE\n");
        SM_PostEvent(sm, (uint16_t)EVT_DONE, 0U);
    }
}

static void led_stopping_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [LED]   Exit STOPPING\n");
}

/* =============================================================================
 * STATE DESCRIPTORS (const, flash)
 *
 * Each FSM gets its own table with different callbacks.
 * ===========================================================================*/

static const SM_StateDesc_t motor_states[SM_STATE_COUNT] = {
    [STATE_IDLE] = {
        .on_entry     = motor_idle_entry,
        .on_execute   = motor_idle_execute,
        .on_exit      = motor_idle_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_RUNNING] = {
        .on_entry     = motor_running_entry,
        .on_execute   = motor_running_execute,
        .on_exit      = motor_running_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_STOPPING] = {
        .on_entry     = motor_stopping_entry,
        .on_execute   = motor_stopping_execute,
        .on_exit      = motor_stopping_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
};

static const SM_StateDesc_t led_states[SM_STATE_COUNT] = {
    [STATE_IDLE] = {
        .on_entry     = led_idle_entry,
        .on_execute   = led_idle_execute,
        .on_exit      = led_idle_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_RUNNING] = {
        .on_entry     = led_running_entry,
        .on_execute   = led_running_execute,
        .on_exit      = led_running_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_STOPPING] = {
        .on_entry     = led_stopping_entry,
        .on_execute   = led_stopping_execute,
        .on_exit      = led_stopping_exit,
        .timeout_ms   = 0U,
        .min_dwell_ms = 0U,
    },
};

/* =============================================================================
 * TRANSITION TABLES (const, flash)
 *
 * Both FSMs have identical topology: IDLE <-> RUNNING -> STOPPING -> IDLE.
 * Separate tables so each could diverge independently.
 * ===========================================================================*/

static const SM_Transition_t motor_transitions[] = {
    /* IDLE --EVT_START--> RUNNING */
    { .from_state = STATE_IDLE,     .event = EVT_START, .to_state = STATE_RUNNING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* RUNNING --EVT_STOP--> STOPPING */
    { .from_state = STATE_RUNNING,  .event = EVT_STOP,  .to_state = STATE_STOPPING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* STOPPING --EVT_DONE--> IDLE */
    { .from_state = STATE_STOPPING, .event = EVT_DONE,  .to_state = STATE_IDLE,
      ._reserved = 0, .guard = NULL, .action = NULL },
};

static const SM_Transition_t led_transitions[] = {
    /* IDLE --EVT_START--> RUNNING */
    { .from_state = STATE_IDLE,     .event = EVT_START, .to_state = STATE_RUNNING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* RUNNING --EVT_STOP--> STOPPING */
    { .from_state = STATE_RUNNING,  .event = EVT_STOP,  .to_state = STATE_STOPPING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* STOPPING --EVT_DONE--> IDLE */
    { .from_state = STATE_STOPPING, .event = EVT_DONE,  .to_state = STATE_IDLE,
      ._reserved = 0, .guard = NULL, .action = NULL },
};

/* =============================================================================
 * HELPER: state name for pretty printing
 * ===========================================================================*/

static const char *state_name(uint16_t state)
{
    switch (state) {
        case STATE_IDLE:     return "IDLE";
        case STATE_RUNNING:  return "RUNNING";
        case STATE_STOPPING: return "STOPPING";
        default:             return "UNKNOWN";
    }
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n");
    printf("================================================================\n");
    printf(" State Machine Framework v%s -- Multi-FSM Example\n",
           SM_FRAMEWORK_VERSION_STRING);
    printf("================================================================\n");
    printf(" Two independent state machines: Motor Controller + LED Controller\n");
    printf("================================================================\n\n");

    /* Initialize debug output */
    SM_Debug_Init(0U);

    /* --- Static allocation of two state machine contexts --- */
    SM_Context_t motor_ctx;
    SM_Context_t led_ctx;

    /* --- Configuration for each FSM --- */
    SM_Config_t motor_config = {
        .states           = motor_states,
        .transitions      = motor_transitions,
        .transition_count = (uint16_t)(sizeof(motor_transitions) / sizeof(motor_transitions[0])),
        .initial_state    = STATE_IDLE,
    };

    SM_Config_t led_config = {
        .states           = led_states,
        .transitions      = led_transitions,
        .transition_count = (uint16_t)(sizeof(led_transitions) / sizeof(led_transitions[0])),
        .initial_state    = STATE_IDLE,
    };

    /* --- Initialize both FSMs independently --- */
    if (!SM_Init(&motor_ctx, &motor_config)) {
        printf("ERROR: Motor FSM SM_Init failed!\n");
        return -1;
    }
    printf("[INIT] Motor FSM initialized in %s\n\n",
           state_name(SM_GetState(&motor_ctx)));

    if (!SM_Init(&led_ctx, &led_config)) {
        printf("ERROR: LED FSM SM_Init failed!\n");
        return -1;
    }
    printf("[INIT] LED FSM initialized in %s\n\n",
           state_name(SM_GetState(&led_ctx)));

    int iter = 0;

    /* =====================================================================
     * PHASE 1: Start motor, LED stays idle (5 iterations)
     * ===================================================================*/
    printf("--- Phase 1: Start motor (LED stays idle) ---\n");
    SM_PostEvent(&motor_ctx, (uint16_t)EVT_START, 0U);

    for (int i = 0; i < 5; i++, iter++) {
        SM_Process(&motor_ctx);
        SM_Process(&led_ctx);
    }
    printf("[iter %d] Motor=%s  LED=%s\n\n",
           iter, state_name(SM_GetState(&motor_ctx)),
           state_name(SM_GetState(&led_ctx)));

    /* =====================================================================
     * PHASE 2: Start LED, both running (5 iterations)
     * ===================================================================*/
    printf("--- Phase 2: Start LED (both running) ---\n");
    SM_PostEvent(&led_ctx, (uint16_t)EVT_START, 0U);

    for (int i = 0; i < 5; i++, iter++) {
        SM_Process(&motor_ctx);
        SM_Process(&led_ctx);
    }
    printf("[iter %d] Motor=%s  LED=%s\n\n",
           iter, state_name(SM_GetState(&motor_ctx)),
           state_name(SM_GetState(&led_ctx)));

    /* =====================================================================
     * PHASE 3: Stop motor, LED still running (5 iterations)
     *   Motor STOPPING on_execute posts EVT_DONE after 3 cycles
     * ===================================================================*/
    printf("--- Phase 3: Stop motor (LED still running) ---\n");
    SM_PostEvent(&motor_ctx, (uint16_t)EVT_STOP, 0U);

    for (int i = 0; i < 5; i++, iter++) {
        SM_Process(&motor_ctx);
        SM_Process(&led_ctx);
    }
    printf("[iter %d] Motor=%s  LED=%s\n\n",
           iter, state_name(SM_GetState(&motor_ctx)),
           state_name(SM_GetState(&led_ctx)));

    /* =====================================================================
     * PHASE 4: Stop LED, motor already idle (5 iterations)
     *   LED STOPPING on_execute posts EVT_DONE after 2 cycles
     * ===================================================================*/
    printf("--- Phase 4: Stop LED (motor already idle) ---\n");
    SM_PostEvent(&led_ctx, (uint16_t)EVT_STOP, 0U);

    for (int i = 0; i < 5; i++, iter++) {
        SM_Process(&motor_ctx);
        SM_Process(&led_ctx);
    }
    printf("[iter %d] Motor=%s  LED=%s\n\n",
           iter, state_name(SM_GetState(&motor_ctx)),
           state_name(SM_GetState(&led_ctx)));

    /* =====================================================================
     * FINAL STATUS
     * ===================================================================*/
    printf("================================================================\n");
    printf(" Multi-FSM Example completed!\n");
    printf(" Motor final state: %s (id=%u)\n",
           state_name(SM_GetState(&motor_ctx)),
           (unsigned)SM_GetState(&motor_ctx));
    printf(" LED   final state: %s (id=%u)\n",
           state_name(SM_GetState(&led_ctx)),
           (unsigned)SM_GetState(&led_ctx));
    printf("================================================================\n\n");

    return 0;
}
