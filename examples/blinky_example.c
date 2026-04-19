/**
 * @file blinky_example.c
 * @brief v3.0 example -- LED blinker with periodic time events
 * @version 3.0.0
 * @date 2026-04-19
 *
 * Demonstrates the v3.0 time event API (SM_TimeEvt_*):
 *   - Arm a periodic timer on state entry, disarm on exit
 *   - Timer fires EVT_BLINK_TICK every 5 SM_Process() calls
 *   - Pause/resume blinking via EVT_PAUSE and EVT_TOGGLE events
 *
 * States: OFF -> BLINKING -> PAUSED -> BLINKING (resume)
 * Events: EVT_TOGGLE, EVT_BLINK_TICK, EVT_PAUSE
 */

/* --- Application configuration (must come before framework headers) --- */

/* REQUIRED: tell the framework how many states and events we have */
#define SM_STATE_COUNT    (3U)
#define SM_EVENT_COUNT    (3U)

/* Optional: enable verbose debug output for demonstration */
#define SM_DEBUG_LEVEL        (4U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 * ===========================================================================*/

typedef enum {
    STATE_OFF = 0,
    STATE_BLINKING,
    STATE_PAUSED
    /* SM_STATE_COUNT = 3 defined above */
} AppState_t;

typedef enum {
    EVT_TOGGLE = 0,
    EVT_BLINK_TICK,
    EVT_PAUSE
    /* SM_EVENT_COUNT = 3 defined above */
} AppEvent_t;

/* =============================================================================
 * TIME EVENT (static allocation)
 * ===========================================================================*/

/**
 * @brief Periodic blink timer
 *
 * Posts EVT_BLINK_TICK every 5 SM_Process() calls while armed.
 * Armed on STATE_BLINKING entry, disarmed on STATE_BLINKING exit.
 */
static SM_TimeEvt_t blink_timer;

/* =============================================================================
 * STATE CALLBACKS
 * ===========================================================================*/

/* --- STATE_OFF --- */

static void on_off_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [OFF] Entry -- LED is off, waiting for EVT_TOGGLE\n");
}

static void on_off_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Idle -- nothing to do until EVT_TOGGLE arrives */
}

static void on_off_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [OFF] Exit\n");
}

/* --- STATE_BLINKING --- */

static void on_blinking_entry(SM_Handle_t sm)
{
    printf("  [BLINKING] Entry -- arming blink timer (period=5 ticks)\n");

    /* Initialize and arm the periodic time event.
     * SM_TimeEvt_Init sets the owning SM and the event signal.
     * SM_TimeEvt_Arm starts the down-counter: first fire after 5 ticks,
     * then auto-reload every 5 ticks (periodic). */
    SM_TimeEvt_Init(&blink_timer, sm, (uint16_t)EVT_BLINK_TICK, 0U);
    SM_TimeEvt_Arm(&blink_timer, 5U, 5U);
}

static void on_blinking_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Nothing here -- the timer posts EVT_BLINK_TICK which triggers
     * a self-transition (BLINKING -> BLINKING) with the toggle action.
     * See the transition table and action callback below. */
}

static void on_blinking_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [BLINKING] Exit -- disarming blink timer\n");
    SM_TimeEvt_Disarm(&blink_timer);
}

/* --- STATE_PAUSED --- */

static void on_paused_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [PAUSED] Entry -- blinking paused, EVT_TOGGLE to resume\n");
}

static void on_paused_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Idle while paused */
}

static void on_paused_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [PAUSED] Exit\n");
}

/* =============================================================================
 * TRANSITION ACTION
 * ===========================================================================*/

/**
 * @brief Action executed on each blink tick (LED toggle)
 *
 * In a real embedded system this would flip a GPIO pin.
 */
static void action_toggle_led(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    (void)event;
    (void)data;
    printf("    ** LED toggled **\n");
}

/* =============================================================================
 * STATE DESCRIPTORS (const, flash)
 * ===========================================================================*/

static const SM_StateDesc_t app_states[SM_STATE_COUNT] = {
    [STATE_OFF] = {
        .on_entry   = on_off_entry,
        .on_execute = on_off_execute,
        .on_exit    = on_off_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_BLINKING] = {
        .on_entry   = on_blinking_entry,
        .on_execute = on_blinking_execute,
        .on_exit    = on_blinking_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_PAUSED] = {
        .on_entry   = on_paused_entry,
        .on_execute = on_paused_execute,
        .on_exit    = on_paused_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
};

/* =============================================================================
 * TRANSITION TABLE (const, flash)
 * ===========================================================================*/

static const SM_Transition_t app_transitions[] = {
    /* OFF --EVT_TOGGLE--> BLINKING */
    { .from_state = STATE_OFF,      .event = EVT_TOGGLE,     .to_state = STATE_BLINKING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* BLINKING --EVT_PAUSE--> PAUSED */
    { .from_state = STATE_BLINKING, .event = EVT_PAUSE,      .to_state = STATE_PAUSED,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* PAUSED --EVT_TOGGLE--> BLINKING (resume) */
    { .from_state = STATE_PAUSED,   .event = EVT_TOGGLE,     .to_state = STATE_BLINKING,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* BLINKING --EVT_BLINK_TICK--> BLINKING (self-transition, toggles LED) */
    { .from_state = STATE_BLINKING, .event = EVT_BLINK_TICK, .to_state = STATE_BLINKING,
      ._reserved = 0, .guard = NULL, .action = action_toggle_led },
};

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf(" State Machine Framework v%s -- Blinky Example\n", SM_FRAMEWORK_VERSION_STRING);
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
        .initial_state    = STATE_OFF,
    };

    /* Initialize */
    if (!SM_Init(&sm_ctx, &config)) {
        printf("ERROR: SM_Init failed!\n");
        return -1;
    }

    /* Start blinking: post EVT_TOGGLE to leave STATE_OFF */
    printf("Posting EVT_TOGGLE to start blinking...\n\n");
    SM_PostEvent(&sm_ctx, (uint16_t)EVT_TOGGLE, 0U);

    /* Run for 60 iterations total:
     *   0-29:  BLINKING (timer fires LED toggle every 5 ticks)
     *   30:    post EVT_PAUSE -> PAUSED
     *   30-44: PAUSED (no blinks)
     *   45:    post EVT_TOGGLE -> BLINKING (resume)
     *   45-59: BLINKING again
     */
    for (int i = 0; i < 60; i++) {
        if (i == 30) {
            printf("\n--- [iter %d] Posting EVT_PAUSE ---\n\n", i);
            SM_PostEvent(&sm_ctx, (uint16_t)EVT_PAUSE, 0U);
        }
        if (i == 45) {
            printf("\n--- [iter %d] Posting EVT_TOGGLE to resume ---\n\n", i);
            SM_PostEvent(&sm_ctx, (uint16_t)EVT_TOGGLE, 0U);
        }

        SM_Process(&sm_ctx);

        /* Print status every 10 iterations */
        if ((i % 10) == 0) {
            printf("[iter %d] state=%u exec_count=%lu queue_depth=%u\n",
                   i,
                   (unsigned)SM_GetState(&sm_ctx),
                   (unsigned long)SM_GetExecCount(&sm_ctx),
                   (unsigned)SM_EventQueueDepth(&sm_ctx));
        }
    }

    printf("\n================================================\n");
    printf(" Blinky example completed!\n");
    printf(" Final state: %u (%s)\n",
           (unsigned)SM_GetState(&sm_ctx),
           SM_GetState(&sm_ctx) == STATE_BLINKING ? "BLINKING" :
           SM_GetState(&sm_ctx) == STATE_PAUSED   ? "PAUSED"   :
                                                     "OFF");
    printf("================================================\n\n");

    return 0;
}
