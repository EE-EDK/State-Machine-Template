/**
 * @file sensor_pipeline_example.c
 * @brief Sensor pipeline v3.0 example -- guard conditions and transition actions
 * @version 3.0.0
 * @date 2026-04-19
 *
 * Demonstrates the v3.0 guard and action features:
 *   - Guard condition blocks a transition when data quality is insufficient
 *   - Transition action prints captured sample count between exit and entry
 *   - Circular pipeline: IDLE -> SAMPLE -> PROCESS -> TRANSMIT -> IDLE
 *   - Abort path: PROCESS --EVT_START_SAMPLE--> IDLE (restart, no guard)
 *
 * States: IDLE, SAMPLE, PROCESS, TRANSMIT
 * Events: EVT_START_SAMPLE, EVT_SAMPLE_DONE, EVT_PROCESS_DONE, EVT_TX_DONE
 */

/* --- Application configuration (must come before framework headers) --- */

/* REQUIRED: tell the framework how many states and events we have */
#define SM_STATE_COUNT    (4U)
#define SM_EVENT_COUNT    (4U)

/* Optional: enable debug output */
#define SM_DEBUG_LEVEL    (4U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 * ===========================================================================*/

typedef enum {
    STATE_IDLE = 0,
    STATE_SAMPLE,
    STATE_PROCESS,
    STATE_TRANSMIT
    /* SM_STATE_COUNT = 4 defined above */
} PipelineState_t;

typedef enum {
    EVT_START_SAMPLE = 0,
    EVT_SAMPLE_DONE,
    EVT_PROCESS_DONE,
    EVT_TX_DONE
    /* SM_EVENT_COUNT = 4 defined above */
} PipelineEvent_t;

/* =============================================================================
 * FILE-SCOPE STATE FOR SIMULATION
 * ===========================================================================*/

/** Tracks how many times PROCESS on_execute has run across state entries. */
static uint32_t s_process_cycle_count = 0U;

/* =============================================================================
 * STATE NAMES (for debug output)
 * ===========================================================================*/

static const char *state_name(uint16_t state)
{
    switch (state) {
        case STATE_IDLE:     return "IDLE";
        case STATE_SAMPLE:   return "SAMPLE";
        case STATE_PROCESS:  return "PROCESS";
        case STATE_TRANSMIT: return "TRANSMIT";
        default:             return "???";
    }
}

/* =============================================================================
 * GUARD CONDITIONS
 * ===========================================================================*/

/**
 * @brief Guard for PROCESS -> TRANSMIT transition
 *
 * Only allows the transition if the event payload (simulated data quality
 * metric) exceeds the threshold of 50. This models a real-world data
 * validation check -- bad sensor readings stay in PROCESS for retry.
 *
 * @param sm    Handle to the state machine instance
 * @param event Event ID (EVT_PROCESS_DONE)
 * @param data  Data quality metric from processing stage
 * @return true if data is valid (quality > 50), false to block transition
 */
static bool guard_data_valid(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    (void)event;

    bool valid = (data > 50U);
    printf("  [GUARD] guard_data_valid: data=%lu, threshold=50 -> %s\n",
           (unsigned long)data, valid ? "PASS" : "BLOCK");
    return valid;
}

/* =============================================================================
 * TRANSITION ACTIONS
 * ===========================================================================*/

/**
 * @brief Action on SAMPLE -> PROCESS transition
 *
 * Executes between SAMPLE's on_exit and PROCESS's on_entry.
 * Prints the captured sample count carried in the event payload.
 *
 * @param sm    Handle to the state machine instance
 * @param event Event ID (EVT_SAMPLE_DONE)
 * @param data  Number of samples captured
 */
static void action_log_samples(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    (void)event;

    printf("  [ACTION] Captured %lu samples\n", (unsigned long)data);
}

/* =============================================================================
 * STATE CALLBACKS
 * ===========================================================================*/

/* --- IDLE --- */

static void on_idle_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [IDLE] Entry -- waiting for start command\n");
}

static void on_idle_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Nothing to do -- external event triggers sampling */
}

static void on_idle_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [IDLE] Exit\n");
}

/* --- SAMPLE --- */

static void on_sample_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [SAMPLE] Entry -- acquiring sensor data...\n");
}

static void on_sample_execute(SM_Handle_t sm)
{
    /* Simulate: after 3 exec cycles, sampling is complete */
    uint32_t count = SM_GetExecCount(sm);
    if (count == 3U) {
        printf("  [SAMPLE] Acquisition complete, posting EVT_SAMPLE_DONE (64 samples)\n");
        SM_PostEvent(sm, (uint16_t)EVT_SAMPLE_DONE, 64U);
    }
}

static void on_sample_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [SAMPLE] Exit\n");
}

/* --- PROCESS --- */

static void on_process_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [PROCESS] Entry -- analyzing data (cycle %lu)...\n",
           (unsigned long)s_process_cycle_count);
}

static void on_process_execute(SM_Handle_t sm)
{
    /* Simulate: after 2 exec cycles, processing is complete.
     * Alternate data payloads to demonstrate guard behavior:
     *   cycle 0: data=75  -> guard passes  (PROCESS -> TRANSMIT)
     *   cycle 1: data=30  -> guard blocks  (stays in PROCESS)
     *   cycle 2: data=80  -> guard passes  (PROCESS -> TRANSMIT)
     *   ... repeats modulo 3 */
    uint32_t count = SM_GetExecCount(sm);
    if (count == 2U) {
        static const uint32_t data_values[] = { 75U, 30U, 80U };
        uint32_t data = data_values[s_process_cycle_count % 3U];

        printf("  [PROCESS] Analysis done, posting EVT_PROCESS_DONE (data=%lu)\n",
               (unsigned long)data);
        SM_PostEvent(sm, (uint16_t)EVT_PROCESS_DONE, data);

        s_process_cycle_count++;
    }
}

static void on_process_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [PROCESS] Exit\n");
}

/* --- TRANSMIT --- */

static void on_transmit_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [TRANSMIT] Entry -- sending data...\n");
}

static void on_transmit_execute(SM_Handle_t sm)
{
    /* Simulate: after 1 exec cycle, transmission is complete */
    uint32_t count = SM_GetExecCount(sm);
    if (count == 1U) {
        printf("  [TRANSMIT] Transmission complete, posting EVT_TX_DONE\n");
        SM_PostEvent(sm, (uint16_t)EVT_TX_DONE, 0U);
    }
}

static void on_transmit_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [TRANSMIT] Exit\n");
}

/* =============================================================================
 * STATE DESCRIPTORS (const, flash)
 * ===========================================================================*/

static const SM_StateDesc_t pipeline_states[SM_STATE_COUNT] = {
    [STATE_IDLE] = {
        .on_entry   = on_idle_entry,
        .on_execute = on_idle_execute,
        .on_exit    = on_idle_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_SAMPLE] = {
        .on_entry   = on_sample_entry,
        .on_execute = on_sample_execute,
        .on_exit    = on_sample_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_PROCESS] = {
        .on_entry   = on_process_entry,
        .on_execute = on_process_execute,
        .on_exit    = on_process_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_TRANSMIT] = {
        .on_entry   = on_transmit_entry,
        .on_execute = on_transmit_execute,
        .on_exit    = on_transmit_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
};

/* =============================================================================
 * TRANSITION TABLE (const, flash)
 *
 * Pipeline: IDLE -> SAMPLE -> PROCESS -> TRANSMIT -> IDLE
 *
 * Guard on PROCESS -> TRANSMIT: guard_data_valid (blocks if data <= 50)
 * Action on SAMPLE -> PROCESS: action_log_samples (prints sample count)
 * Abort path: PROCESS --EVT_START_SAMPLE--> IDLE (no guard, restart pipeline)
 * ===========================================================================*/

static const SM_Transition_t pipeline_transitions[] = {
    /* IDLE --EVT_START_SAMPLE--> SAMPLE */
    { .from_state = STATE_IDLE,     .event = EVT_START_SAMPLE, .to_state = STATE_SAMPLE,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* SAMPLE --EVT_SAMPLE_DONE--> PROCESS (action: log sample count) */
    { .from_state = STATE_SAMPLE,   .event = EVT_SAMPLE_DONE,  .to_state = STATE_PROCESS,
      ._reserved = 0, .guard = NULL, .action = action_log_samples },

    /* PROCESS --EVT_PROCESS_DONE--> TRANSMIT (guard: data quality > 50) */
    { .from_state = STATE_PROCESS,  .event = EVT_PROCESS_DONE, .to_state = STATE_TRANSMIT,
      ._reserved = 0, .guard = guard_data_valid, .action = NULL },

    /* PROCESS --EVT_START_SAMPLE--> IDLE (abort/restart, no guard) */
    { .from_state = STATE_PROCESS,  .event = EVT_START_SAMPLE, .to_state = STATE_IDLE,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* TRANSMIT --EVT_TX_DONE--> IDLE (cycle complete) */
    { .from_state = STATE_TRANSMIT, .event = EVT_TX_DONE,      .to_state = STATE_IDLE,
      ._reserved = 0, .guard = NULL, .action = NULL },
};

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n");
    printf("================================================================\n");
    printf(" State Machine Framework v%s -- Sensor Pipeline Example\n",
           SM_FRAMEWORK_VERSION_STRING);
    printf(" Demonstrates: guard conditions, transition actions\n");
    printf("================================================================\n\n");

    /* Initialize debug output */
    SM_Debug_Init(0U);

    /* Static allocation of state machine context */
    SM_Context_t sm_ctx;

    /* Configuration */
    SM_Config_t config = {
        .states           = pipeline_states,
        .transitions      = pipeline_transitions,
        .transition_count = (uint16_t)(sizeof(pipeline_transitions) / sizeof(pipeline_transitions[0])),
        .initial_state    = STATE_IDLE,
    };

    /* Initialize */
    if (!SM_Init(&sm_ctx, &config)) {
        printf("ERROR: SM_Init failed!\n");
        return -1;
    }

    /* Kick off the pipeline by posting the first event */
    printf("Posting EVT_START_SAMPLE to begin pipeline...\n\n");
    SM_PostEvent(&sm_ctx, (uint16_t)EVT_START_SAMPLE, 0U);

    printf("Running state machine for 60 iterations...\n\n");

    /* Main loop */
    for (int i = 0; i < 60; i++) {
        SM_Process(&sm_ctx);

        if ((i % 10) == 0) {
            printf("[iter %2d] state=%-8s  prev=%-8s  exec_count=%lu  queue_depth=%u\n",
                   i,
                   state_name(SM_GetState(&sm_ctx)),
                   state_name(SM_GetPreviousState(&sm_ctx)),
                   (unsigned long)SM_GetExecCount(&sm_ctx),
                   (unsigned)SM_EventQueueDepth(&sm_ctx));
        }
    }

    printf("\n================================================================\n");
    printf(" Example completed!\n");
    printf(" Final state: %s (%u)\n",
           state_name(SM_GetState(&sm_ctx)),
           (unsigned)SM_GetState(&sm_ctx));
    printf(" Process cycles completed: %lu\n", (unsigned long)s_process_cycle_count);
    printf("================================================================\n\n");

    return 0;
}
