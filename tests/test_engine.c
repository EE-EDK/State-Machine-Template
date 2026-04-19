/**
 * @file test_engine.c
 * @brief Unity tests for SM_Init, SM_Process, SM_Reset, and state query APIs
 *
 * Covers:
 *   1.  SM_Init valid config -> true, state == initial_state
 *   2.  SM_Init NULL sm -> assertion 100
 *   3.  SM_Init NULL config -> assertion 101
 *   4.  SM_Init NULL states/transitions -> assertion 102/103
 *   5.  SM_Init initial_state >= SM_STATE_COUNT -> assertion 104
 *   6.  SM_Process triggers on_entry on first cycle
 *   7.  SM_Process calls on_execute each cycle
 *   8.  SM_Process handles transition: on_exit(old) -> action -> on_entry(new)
 *   9.  Guard returning false blocks transition
 *  10.  Guard returning true allows transition
 *  11.  Multi-guard fallthrough: first guard false, second guard true
 *  12.  State timeout fires exactly once
 *  13.  Min dwell time prevents early transition
 *  14.  SM_GetState returns current state
 *  15.  SM_GetPreviousState returns state before last transition
 *  16.  SM_GetStateTime returns time since state entry
 *  17.  SM_GetExecCount increments each SM_Process call in same state
 *  18.  SM_GetStateHistory returns recent transitions (most recent first)
 *  19.  SM_Reset returns to initial state, flushes queue
 *  20.  SM_Reset blocked when critical_lock is active
 *
 * Build: linked against sm_framework_test + unity_lib (see tests/CMakeLists.txt)
 */

#include "unity.h"

/* SM_STATE_COUNT and SM_EVENT_COUNT are set via compile defs in CMakeLists.txt */
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =============================================================================
 * FILE-SCOPE CALLBACK TRACKING
 * ===========================================================================*/

/* Invocation counters (reset in setUp) */
static int cb_init_entry_count;
static int cb_init_exit_count;
static int cb_init_execute_count;

static int cb_running_entry_count;
static int cb_running_exit_count;
static int cb_running_execute_count;

static int cb_stopped_entry_count;
static int cb_stopped_exit_count;
static int cb_stopped_execute_count;

static int cb_error_entry_count;
static int cb_error_exit_count;
static int cb_error_execute_count;

static int cb_action_count;
static uint16_t cb_action_last_event;
static uint32_t cb_action_last_data;

/* Guard return values -- selectable per test */
static bool guard_allow_value;
static bool guard_block_value;

/* Track call order for transition sequence test */
#define CALL_LOG_MAX 16
static int call_log[CALL_LOG_MAX];
static int call_log_idx;

enum CallLogID {
    CL_INIT_EXIT    = 1,
    CL_ACTION       = 2,
    CL_RUNNING_ENTRY = 3
};

/* =============================================================================
 * CALLBACKS
 * ===========================================================================*/

static void cb_init_entry(SM_Handle_t sm)    { (void)sm; cb_init_entry_count++; }
static void cb_init_exit(SM_Handle_t sm)     { (void)sm; cb_init_exit_count++; }
static void cb_init_execute(SM_Handle_t sm)  { (void)sm; cb_init_execute_count++; }

static void cb_running_entry(SM_Handle_t sm)   { (void)sm; cb_running_entry_count++; }
static void cb_running_exit(SM_Handle_t sm)    { (void)sm; cb_running_exit_count++; }
static void cb_running_execute(SM_Handle_t sm) { (void)sm; cb_running_execute_count++; }

static void cb_stopped_entry(SM_Handle_t sm)   { (void)sm; cb_stopped_entry_count++; }
static void cb_stopped_exit(SM_Handle_t sm)    { (void)sm; cb_stopped_exit_count++; }
static void cb_stopped_execute(SM_Handle_t sm) { (void)sm; cb_stopped_execute_count++; }

static void cb_error_entry(SM_Handle_t sm)   { (void)sm; cb_error_entry_count++; }
static void cb_error_exit(SM_Handle_t sm)    { (void)sm; cb_error_exit_count++; }
static void cb_error_execute(SM_Handle_t sm) { (void)sm; cb_error_execute_count++; }

/* Ordered callbacks for transition sequence test */
static void cb_init_exit_ordered(SM_Handle_t sm)
{
    (void)sm;
    cb_init_exit_count++;
    if (call_log_idx < CALL_LOG_MAX) {
        call_log[call_log_idx++] = CL_INIT_EXIT;
    }
}

static void cb_action_ordered(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    cb_action_count++;
    cb_action_last_event = event;
    cb_action_last_data = data;
    if (call_log_idx < CALL_LOG_MAX) {
        call_log[call_log_idx++] = CL_ACTION;
    }
}

static void cb_running_entry_ordered(SM_Handle_t sm)
{
    (void)sm;
    cb_running_entry_count++;
    if (call_log_idx < CALL_LOG_MAX) {
        call_log[call_log_idx++] = CL_RUNNING_ENTRY;
    }
}

/* Simple action callback (no ordering) */
static void cb_action_simple(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    cb_action_count++;
    cb_action_last_event = event;
    cb_action_last_data = data;
}

/* Guard callbacks */
static bool guard_allow(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm; (void)event; (void)data;
    return guard_allow_value;
}

static bool guard_block(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm; (void)event; (void)data;
    return guard_block_value;
}

/* =============================================================================
 * FILE-SCOPE STATE DESCRIPTORS AND TRANSITION TABLES
 * ===========================================================================*/

/* Standard state descriptors (no timeout, no dwell) */
static const SM_StateDesc_t test_states[4] = {
    [TEST_STATE_INIT]    = { cb_init_entry,    cb_init_execute,    cb_init_exit,    0, 0 },
    [TEST_STATE_RUNNING] = { cb_running_entry,  cb_running_execute, cb_running_exit, 0, 0 },
    [TEST_STATE_STOPPED] = { cb_stopped_entry,  cb_stopped_execute, cb_stopped_exit, 0, 0 },
    [TEST_STATE_ERROR]   = { cb_error_entry,    cb_error_execute,   cb_error_exit,   0, 0 },
};

/* Standard transitions */
static const SM_Transition_t test_transitions[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL,  NULL },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL,  NULL },
    { TEST_STATE_STOPPED, TEST_EVT_RESET, TEST_STATE_INIT,    0, NULL,  NULL },
    { TEST_STATE_RUNNING, TEST_EVT_ERROR, TEST_STATE_ERROR,   0, NULL,  NULL },
};

/* Transition table with an action callback (ordered version for sequence test) */
static const SM_StateDesc_t test_states_ordered[4] = {
    [TEST_STATE_INIT]    = { cb_init_entry,          cb_init_execute,    cb_init_exit_ordered,    0, 0 },
    [TEST_STATE_RUNNING] = { cb_running_entry_ordered, cb_running_execute, cb_running_exit,       0, 0 },
    [TEST_STATE_STOPPED] = { cb_stopped_entry,        cb_stopped_execute, cb_stopped_exit,        0, 0 },
    [TEST_STATE_ERROR]   = { cb_error_entry,          cb_error_execute,   cb_error_exit,          0, 0 },
};

static const SM_Transition_t test_transitions_with_action[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL,  cb_action_ordered },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL,  NULL },
    { TEST_STATE_STOPPED, TEST_EVT_RESET, TEST_STATE_INIT,    0, NULL,  NULL },
};

/* Transition table with a guard */
static const SM_Transition_t test_transitions_guarded[] = {
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0, guard_allow, cb_action_simple },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL,  NULL },
};

/* Transition table for multi-guard fallthrough: two entries for same (state, event) */
static const SM_Transition_t test_transitions_multi_guard[] = {
    /* First entry: guard_block -- if false, engine continues searching */
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_ERROR,   0, guard_block, NULL },
    /* Second entry: guard_allow -- if true, this transition fires */
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0, guard_allow, cb_action_simple },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL,  NULL },
};

/* State descriptors with timeout */
static const SM_StateDesc_t test_states_timeout[4] = {
    [TEST_STATE_INIT]    = { cb_init_entry,    cb_init_execute,    cb_init_exit,    100, 0 },
    [TEST_STATE_RUNNING] = { cb_running_entry,  cb_running_execute, cb_running_exit, 0,   0 },
    [TEST_STATE_STOPPED] = { cb_stopped_entry,  cb_stopped_execute, cb_stopped_exit, 0,   0 },
    [TEST_STATE_ERROR]   = { cb_error_entry,    cb_error_execute,   cb_error_exit,   0,   0 },
};

/*
 * Timeout transition: SM_INTERNAL_TIMEOUT == SM_EVENT_COUNT (8).
 * The engine uses sm_post_internal which bypasses the event-range check,
 * then sm_find_transition matches on the event field regardless of range.
 * So we define a transition on event == SM_EVENT_COUNT for the timeout.
 */
static const SM_Transition_t test_transitions_timeout[] = {
    { TEST_STATE_INIT,    SM_EVENT_COUNT, TEST_STATE_ERROR,   0, NULL, NULL },
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL, NULL },
};

/* State descriptors with min dwell time */
static const SM_StateDesc_t test_states_dwell[4] = {
    [TEST_STATE_INIT]    = { cb_init_entry,    cb_init_execute,    cb_init_exit,    0, 200 },
    [TEST_STATE_RUNNING] = { cb_running_entry,  cb_running_execute, cb_running_exit, 0, 0 },
    [TEST_STATE_STOPPED] = { cb_stopped_entry,  cb_stopped_execute, cb_stopped_exit, 0, 0 },
    [TEST_STATE_ERROR]   = { cb_error_entry,    cb_error_execute,   cb_error_exit,   0, 0 },
};

static const SM_Transition_t test_transitions_dwell[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL, NULL },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL, NULL },
};

/* =============================================================================
 * HELPER: reset all callback counters
 * ===========================================================================*/

static void reset_counters(void)
{
    cb_init_entry_count = 0;
    cb_init_exit_count = 0;
    cb_init_execute_count = 0;

    cb_running_entry_count = 0;
    cb_running_exit_count = 0;
    cb_running_execute_count = 0;

    cb_stopped_entry_count = 0;
    cb_stopped_exit_count = 0;
    cb_stopped_execute_count = 0;

    cb_error_entry_count = 0;
    cb_error_exit_count = 0;
    cb_error_execute_count = 0;

    cb_action_count = 0;
    cb_action_last_event = 0;
    cb_action_last_data = 0;

    guard_allow_value = true;
    guard_block_value = false;

    memset(call_log, 0, sizeof(call_log));
    call_log_idx = 0;
}

/* =============================================================================
 * UNITY SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    reset_counters();
}

void tearDown(void)
{
    /* nothing */
}

/* =============================================================================
 * TEST 1: SM_Init with valid config succeeds, state == initial_state
 * ===========================================================================*/

static void test_init_valid_config(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    bool result = SM_Init(&ctx, &cfg);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));
}

/* =============================================================================
 * TEST 2: SM_Init with NULL sm fires assertion 100
 * ===========================================================================*/

static void test_init_null_sm(void)
{
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    TEST_EXPECT_ASSERT(SM_Init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(100, test_assert_id);
    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
}

/* =============================================================================
 * TEST 3: SM_Init with NULL config fires assertion 101
 * ===========================================================================*/

static void test_init_null_config(void)
{
    SM_Context_t ctx;

    TEST_EXPECT_ASSERT(SM_Init(&ctx, NULL));
    TEST_ASSERT_EQUAL_INT(101, test_assert_id);
    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
}

/* =============================================================================
 * TEST 4: SM_Init with NULL states fires assertion 102,
 *          NULL transitions fires assertion 103
 * ===========================================================================*/

static void test_init_null_states(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg_no_states = {
        .states = NULL,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    TEST_EXPECT_ASSERT(SM_Init(&ctx, &cfg_no_states));
    TEST_ASSERT_EQUAL_INT(102, test_assert_id);
}

static void test_init_null_transitions(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg_no_trans = {
        .states = test_states,
        .transitions = NULL,
        .transition_count = 0,
        .initial_state = TEST_STATE_INIT,
    };

    TEST_EXPECT_ASSERT(SM_Init(&ctx, &cfg_no_trans));
    TEST_ASSERT_EQUAL_INT(103, test_assert_id);
}

/* =============================================================================
 * TEST 5: SM_Init with initial_state >= SM_STATE_COUNT fires assertion 104
 * ===========================================================================*/

static void test_init_invalid_initial_state(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg_bad_state = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = SM_STATE_COUNT, /* out of range */
    };

    TEST_EXPECT_ASSERT(SM_Init(&ctx, &cfg_bad_state));
    TEST_ASSERT_EQUAL_INT(104, test_assert_id);
}

/* =============================================================================
 * TEST 6: SM_Process triggers on_entry on first cycle
 * ===========================================================================*/

static void test_process_calls_on_entry_first_cycle(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* on_entry should NOT have been called yet by SM_Init */
    TEST_ASSERT_EQUAL_INT(0, cb_init_entry_count);

    /* First SM_Process should fire on_entry */
    SM_Process(&ctx);

    TEST_ASSERT_EQUAL_INT(1, cb_init_entry_count);

    /* Second SM_Process should NOT fire on_entry again */
    SM_Process(&ctx);

    TEST_ASSERT_EQUAL_INT(1, cb_init_entry_count);
}

/* =============================================================================
 * TEST 7: SM_Process calls on_execute each cycle
 * ===========================================================================*/

static void test_process_calls_on_execute_each_cycle(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* Run 5 cycles */
    for (int i = 0; i < 5; i++) {
        SM_Process(&ctx);
    }

    TEST_ASSERT_EQUAL_INT(5, cb_init_execute_count);
}

/* =============================================================================
 * TEST 8: SM_Process transition sequence: on_exit(old) -> action -> on_entry(new)
 * ===========================================================================*/

static void test_process_transition_sequence(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states_ordered,
        .transitions = test_transitions_with_action,
        .transition_count = sizeof(test_transitions_with_action) / sizeof(test_transitions_with_action[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* First cycle: on_entry(INIT) runs, on_execute(INIT) runs */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_INT(1, cb_init_entry_count);
    TEST_ASSERT_EQUAL_INT(1, cb_init_execute_count);

    /* Post START event */
    SM_PostEvent(&ctx, TEST_EVT_START, 42);

    /* Reset ordered call log before the transition cycle */
    call_log_idx = 0;
    memset(call_log, 0, sizeof(call_log));

    /* Process: should dequeue START, find transition, execute sequence */
    SM_Process(&ctx);

    /* Verify sequence: on_exit(INIT) -> action -> on_entry(RUNNING) happens
     * during this cycle. Note: on_entry(RUNNING) runs on the NEXT SM_Process
     * because state_entered is set to true, and the next cycle handles it. */

    /* on_exit(INIT) and action should have run this cycle */
    TEST_ASSERT_EQUAL_INT(1, cb_init_exit_count);
    TEST_ASSERT_EQUAL_INT(1, cb_action_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_START, cb_action_last_event);
    TEST_ASSERT_EQUAL_UINT32(42, cb_action_last_data);

    /* Verify order: exit before action */
    TEST_ASSERT_TRUE(call_log_idx >= 2);
    TEST_ASSERT_EQUAL_INT(CL_INIT_EXIT, call_log[0]);
    TEST_ASSERT_EQUAL_INT(CL_ACTION, call_log[1]);

    /* on_entry(RUNNING) runs on the next SM_Process call */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);

    /* Verify the full 3-step order */
    TEST_ASSERT_TRUE(call_log_idx >= 3);
    TEST_ASSERT_EQUAL_INT(CL_INIT_EXIT, call_log[0]);
    TEST_ASSERT_EQUAL_INT(CL_ACTION, call_log[1]);
    TEST_ASSERT_EQUAL_INT(CL_RUNNING_ENTRY, call_log[2]);

    /* Now in RUNNING state */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
}

/* =============================================================================
 * TEST 9: Guard returning false blocks transition
 * ===========================================================================*/

static void test_guard_blocks_transition(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions_guarded,
        .transition_count = sizeof(test_transitions_guarded) / sizeof(test_transitions_guarded[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* initial on_entry */

    /* Set guard to block */
    guard_allow_value = false;

    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);

    /* Should still be in INIT -- transition blocked */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(0, cb_action_count);
    TEST_ASSERT_EQUAL_INT(0, cb_running_entry_count);
}

/* =============================================================================
 * TEST 10: Guard returning true allows transition
 * ===========================================================================*/

static void test_guard_allows_transition(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions_guarded,
        .transition_count = sizeof(test_transitions_guarded) / sizeof(test_transitions_guarded[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* initial on_entry */

    /* Set guard to allow */
    guard_allow_value = true;

    SM_PostEvent(&ctx, TEST_EVT_START, 99);
    SM_Process(&ctx);

    /* Transition should have fired */
    TEST_ASSERT_EQUAL_INT(1, cb_action_count);
    TEST_ASSERT_EQUAL_UINT32(99, cb_action_last_data);

    /* Next cycle enters RUNNING */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);
}

/* =============================================================================
 * TEST 11: Multi-guard fallthrough
 * First guard returns false, second guard returns true -- second transition fires
 * ===========================================================================*/

static void test_multi_guard_fallthrough(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions_multi_guard,
        .transition_count = sizeof(test_transitions_multi_guard) / sizeof(test_transitions_multi_guard[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* initial on_entry */

    /* guard_block returns false, guard_allow returns true */
    guard_block_value = false;
    guard_allow_value = true;

    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);

    /* Should have skipped first entry (-> ERROR) and taken second (-> RUNNING) */
    SM_Process(&ctx); /* on_entry for new state */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(1, cb_action_count); /* action on second transition */
    TEST_ASSERT_EQUAL_INT(0, cb_error_entry_count); /* NOT the ERROR state */
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);
}

/* =============================================================================
 * TEST 12: State timeout fires exactly once
 * ===========================================================================*/

static void test_state_timeout_fires_once(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states_timeout,
        .transitions = test_transitions_timeout,
        .transition_count = sizeof(test_transitions_timeout) / sizeof(test_transitions_timeout[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* First cycle: on_entry sets state_entry_time, on_execute runs */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Advance sim time to just under timeout (100ms) */
    for (uint32_t i = 0; i < 99; i++) {
        SM_Platform_SimTick();
    }
    SM_Process(&ctx);
    /* Should still be in INIT -- timeout not reached yet */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Advance one more ms to reach 100ms */
    SM_Platform_SimTick();
    SM_Process(&ctx);

    /*
     * Timeout fires: SM_INTERNAL_TIMEOUT posted internally.
     * The event is dequeued and the transition INIT -> ERROR fires this cycle.
     * But the timeout event was just posted and hasn't been dequeued yet because
     * the timeout check happens AFTER on_execute and BEFORE event dequeue.
     * Actually, looking at SM_Process: timeout check happens first, posts event,
     * then event dequeue processes it in the SAME cycle (since dwell_ok is true
     * and there's an event in the queue now).
     *
     * So after this SM_Process, we should have transitioned or the timeout event
     * is pending. Let's check by running another cycle if needed.
     */

    /* The internal timeout event was posted and should trigger transition
     * in this same SM_Process call (timeout check runs before event dequeue). */

    /* Run another cycle to let on_entry(ERROR) fire */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_ERROR, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(1, cb_error_entry_count);

    /* Advance more time and process again -- timeout should NOT fire again */
    for (uint32_t i = 0; i < 200; i++) {
        SM_Platform_SimTick();
    }
    SM_Process(&ctx);
    SM_Process(&ctx);

    /* ERROR state has no timeout configured (timeout_ms == 0), so it stays */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_ERROR, SM_GetState(&ctx));

    /* on_entry(ERROR) called exactly once */
    TEST_ASSERT_EQUAL_INT(1, cb_error_entry_count);
}

/* =============================================================================
 * TEST 13: Min dwell time prevents early transition
 * ===========================================================================*/

static void test_min_dwell_prevents_early_transition(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states_dwell,
        .transitions = test_transitions_dwell,
        .transition_count = sizeof(test_transitions_dwell) / sizeof(test_transitions_dwell[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* First cycle: on_entry + on_execute */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Post START event immediately */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);

    /* Advance 50ms -- well under 200ms dwell */
    for (uint32_t i = 0; i < 50; i++) {
        SM_Platform_SimTick();
    }
    SM_Process(&ctx);

    /* Transition blocked -- event still in queue, still in INIT */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(0, cb_init_exit_count);

    /* Advance to 199ms total -- still under dwell */
    for (uint32_t i = 0; i < 149; i++) {
        SM_Platform_SimTick();
    }
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Advance to 200ms -- dwell satisfied */
    SM_Platform_SimTick();
    SM_Process(&ctx);

    /* Now the event should be dequeued and transition should fire */
    TEST_ASSERT_EQUAL_INT(1, cb_init_exit_count);

    /* Next cycle enters RUNNING */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);
}

/* =============================================================================
 * TEST 14: SM_GetState returns current state
 * ===========================================================================*/

static void test_get_state(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    SM_Process(&ctx); /* on_entry for INIT */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Transition to RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
}

/* =============================================================================
 * TEST 15: SM_GetPreviousState returns state before last transition
 * ===========================================================================*/

static void test_get_previous_state(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* Before any transition, previous == initial */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetPreviousState(&ctx));

    SM_Process(&ctx); /* on_entry INIT */

    /* Transition INIT -> RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);

    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetPreviousState(&ctx));

    /* Transition RUNNING -> STOPPED */
    SM_Process(&ctx); /* on_entry RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_STOP, 0);
    SM_Process(&ctx);

    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, SM_GetState(&ctx));
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetPreviousState(&ctx));
}

/* =============================================================================
 * TEST 16: SM_GetStateTime returns time since state entry
 * ===========================================================================*/

static void test_get_state_time(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* First SM_Process sets state_entry_time during on_entry handling */
    SM_Process(&ctx);

    /* State time should be 0 right after entry (sim time hasn't advanced) */
    TEST_ASSERT_EQUAL_UINT32(0, SM_GetStateTime(&ctx));

    /* Advance 150ms */
    for (uint32_t i = 0; i < 150; i++) {
        SM_Platform_SimTick();
    }

    TEST_ASSERT_EQUAL_UINT32(150, SM_GetStateTime(&ctx));

    /* Advance another 50ms */
    for (uint32_t i = 0; i < 50; i++) {
        SM_Platform_SimTick();
    }

    TEST_ASSERT_EQUAL_UINT32(200, SM_GetStateTime(&ctx));
}

/* =============================================================================
 * TEST 17: SM_GetExecCount increments each SM_Process call in same state
 * ===========================================================================*/

static void test_get_exec_count(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);

    /* Before any SM_Process, exec_count should be 0 */
    TEST_ASSERT_EQUAL_UINT32(0, SM_GetExecCount(&ctx));

    /*
     * First SM_Process: on_entry resets state_exec_count to 0,
     * then on_execute runs, then state_exec_count++ => count = 1.
     * But on_entry sets state_exec_count to 0 first, so after the first
     * SM_Process that includes on_entry: count should be 1.
     */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT32(1, SM_GetExecCount(&ctx));

    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT32(2, SM_GetExecCount(&ctx));

    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT32(3, SM_GetExecCount(&ctx));

    /* Transition resets exec_count */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx); /* transition cycle */

    /* After transition, state_entered = true for next cycle.
     * The exec_count should reset on the next on_entry cycle. */
    SM_Process(&ctx); /* on_entry(RUNNING) resets exec_count, then on_execute => 1 */
    TEST_ASSERT_EQUAL_UINT32(1, SM_GetExecCount(&ctx));
}

/* =============================================================================
 * TEST 18: SM_GetStateHistory returns recent transitions (most recent first)
 * ===========================================================================*/

static void test_get_state_history(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* on_entry INIT */

    /* After init, history has just the initial state */
    uint16_t hist[4];
    uint8_t count = 0;

    bool ok = SM_GetStateHistory(&ctx, hist, 4, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(count > 0);
    /* Most recent should be INIT */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[0]);

    /* Transition: INIT -> RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);
    SM_Process(&ctx); /* on_entry RUNNING */

    ok = SM_GetStateHistory(&ctx, hist, 4, &count);
    TEST_ASSERT_TRUE(ok);
    /* Most recent should be RUNNING */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, hist[0]);
    /* Second most recent should be INIT */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[1]);

    /* Transition: RUNNING -> STOPPED */
    SM_PostEvent(&ctx, TEST_EVT_STOP, 0);
    SM_Process(&ctx);
    SM_Process(&ctx); /* on_entry STOPPED */

    ok = SM_GetStateHistory(&ctx, hist, 4, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, hist[0]);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, hist[1]);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[2]);

    /* Transition: STOPPED -> INIT (via RESET event) */
    SM_PostEvent(&ctx, TEST_EVT_RESET, 0);
    SM_Process(&ctx);

    ok = SM_GetStateHistory(&ctx, hist, 4, &count);
    TEST_ASSERT_TRUE(ok);
    /* History ring of depth 4 now holds: INIT, RUNNING, STOPPED, INIT */
    /* Most recent first: INIT (new), STOPPED, RUNNING, INIT (original) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[0]);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, hist[1]);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, hist[2]);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[3]);
}

/* =============================================================================
 * TEST 19: SM_Reset returns to initial state, flushes queue
 * ===========================================================================*/

static void test_reset_returns_to_initial(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* on_entry INIT */

    /* Transition INIT -> RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);
    SM_Process(&ctx); /* on_entry RUNNING */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));

    /* Post some events that should be flushed */
    SM_PostEvent(&ctx, TEST_EVT_STOP, 0);
    SM_PostEvent(&ctx, TEST_EVT_DATA, 0);
    TEST_ASSERT_FALSE(SM_EventQueueIsEmpty(&ctx));

    /* Reset */
    SM_Reset(&ctx);

    /* Should be back to initial state */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(&ctx));

    /* Queue should be flushed */
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(&ctx));

    /* on_exit(RUNNING) should have been called by reset */
    TEST_ASSERT_EQUAL_INT(1, cb_running_exit_count);

    /* Next SM_Process should fire on_entry(INIT) again */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_INT(2, cb_init_entry_count); /* once from init, once from reset */
}

/* =============================================================================
 * TEST 20: SM_Reset blocked when critical_lock is active
 * ===========================================================================*/

static void test_reset_blocked_by_critical_lock(void)
{
    SM_Context_t ctx;
    const SM_Config_t cfg = {
        .states = test_states,
        .transitions = test_transitions,
        .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
        .initial_state = TEST_STATE_INIT,
    };

    SM_Init(&ctx, &cfg);
    SM_Process(&ctx); /* on_entry INIT */

    /* Transition INIT -> RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0);
    SM_Process(&ctx);
    SM_Process(&ctx); /* on_entry RUNNING */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));

    /* Simulate critical error to set critical_lock */
    SM_Error_Report(&ctx, SM_ERROR_CRITICAL, 999);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(&ctx));

    /* Attempt reset -- should be blocked */
    SM_Reset(&ctx);

    /* Should still be in RUNNING (reset was blocked) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));

    /* on_exit(RUNNING) should NOT have been called by the blocked reset */
    TEST_ASSERT_EQUAL_INT(0, cb_running_exit_count);
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_init_valid_config);
    RUN_TEST(test_init_null_sm);
    RUN_TEST(test_init_null_config);
    RUN_TEST(test_init_null_states);
    RUN_TEST(test_init_null_transitions);
    RUN_TEST(test_init_invalid_initial_state);

    /* SM_Process callback tests */
    RUN_TEST(test_process_calls_on_entry_first_cycle);
    RUN_TEST(test_process_calls_on_execute_each_cycle);
    RUN_TEST(test_process_transition_sequence);

    /* Guard condition tests */
    RUN_TEST(test_guard_blocks_transition);
    RUN_TEST(test_guard_allows_transition);
    RUN_TEST(test_multi_guard_fallthrough);

    /* Timing tests */
    RUN_TEST(test_state_timeout_fires_once);
    RUN_TEST(test_min_dwell_prevents_early_transition);

    /* State query tests */
    RUN_TEST(test_get_state);
    RUN_TEST(test_get_previous_state);
    RUN_TEST(test_get_state_time);
    RUN_TEST(test_get_exec_count);
    RUN_TEST(test_get_state_history);

    /* Reset tests */
    RUN_TEST(test_reset_returns_to_initial);
    RUN_TEST(test_reset_blocked_by_critical_lock);

    return UNITY_END();
}
