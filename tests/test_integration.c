/**
 * @file test_integration.c
 * @brief Integration tests exercising multiple SM framework subsystems together
 *
 * Covers cross-cutting scenarios that no single unit test file can verify:
 *   1.  Full lifecycle: INIT -> RUNNING -> STOPPED -> INIT (via SM_Reset)
 *   2.  Time event + state transition: periodic timer drives state changes
 *   3.  Error injection + recovery: MINOR/NORMAL/CRITICAL tiers, stats
 *   4.  Deferred events: defer in one state, recall on entry to another
 *   5.  Guard-protected multi-path transition: data-driven routing
 *   6.  Statistics tracking: transition/event/per-state counts
 *
 * Build: linked against sm_framework_test + unity_lib (see tests/CMakeLists.txt)
 *
 * Compile defs (from CMakeLists.txt):
 *   SM_STATE_COUNT=4   SM_EVENT_COUNT=8
 *   SM_FEATURE_TIME_EVENTS=1  SM_FEATURE_DEFER=1  SM_FEATURE_STATISTICS=1
 *   SM_FEATURE_DEBUG=1  SM_FEATURE_ASSERT=1
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =============================================================================
 * FILE-SCOPE CALLBACK TRACKING
 *
 * Counters, flags, and data logs that state callbacks use to record their
 * invocations.  All reset in setUp().
 * ===========================================================================*/

/* Per-state entry/exit/execute counters */
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

/* Transition action log */
#define ACTION_LOG_MAX 32
static struct {
    uint16_t event;
    uint32_t data;
} action_log[ACTION_LOG_MAX];
static int action_log_count;

/* Time event for timer tests */
static SM_TimeEvt_t g_periodic_timer;

/* Flag: should on_entry(RUNNING) arm the periodic timer? */
static bool arm_timer_on_running_entry;

/* Flag: should on_exit(RUNNING) disarm the periodic timer? */
static bool disarm_timer_on_running_exit;

/* Flag: should on_execute(RUNNING) defer EVT_CUSTOM when seen? */
static bool defer_custom_in_running;
static bool custom_was_deferred;

/* Flag: should on_entry(STOPPED) recall deferred events? */
static bool recall_on_stopped_entry;
static bool recall_succeeded;

/* Recovery callback return value (settable per test) */
static bool recovery_cb_return_value;
static int  recovery_cb_call_count;

/* Guard data threshold for multi-path test */
static uint32_t guard_data_threshold;

/* Shared SM context */
static SM_Context_t g_ctx;
static SM_Handle_t  g_sm;

/* =============================================================================
 * CALLBACKS
 * ===========================================================================*/

/* --- INIT state --- */
static void cb_init_entry(SM_Handle_t sm)
{
    (void)sm;
    cb_init_entry_count++;
}

static void cb_init_exit(SM_Handle_t sm)
{
    (void)sm;
    cb_init_exit_count++;
}

static void cb_init_execute(SM_Handle_t sm)
{
    (void)sm;
    cb_init_execute_count++;
}

/* --- RUNNING state --- */
static void cb_running_entry(SM_Handle_t sm)
{
    cb_running_entry_count++;

    if (arm_timer_on_running_entry) {
        /* Arm a periodic timer: fires EVT_DATA every 3 ticks */
        SM_TimeEvt_Init(&g_periodic_timer, sm, TEST_EVT_DATA, 0xDA7A);
        SM_TimeEvt_Arm(&g_periodic_timer, 3U, 3U);
    }
}

static void cb_running_exit(SM_Handle_t sm)
{
    cb_running_exit_count++;

    if (disarm_timer_on_running_exit) {
        SM_TimeEvt_Disarm(&g_periodic_timer);
    }
}

static void cb_running_execute(SM_Handle_t sm)
{
    cb_running_execute_count++;

    /* Optionally defer EVT_CUSTOM if it arrives while we are here.
     * The defer happens explicitly via SM_DeferEvent in on_execute when
     * the test posts EVT_CUSTOM and we want to defer it instead of
     * processing the normal transition. For this test, we defer from
     * the callback when our flag says so and the event hasn't been
     * deferred yet. */
    if (defer_custom_in_running && !custom_was_deferred) {
        SM_DeferEvent(sm, TEST_EVT_CUSTOM, 0xCCCC);
        custom_was_deferred = true;
    }
}

/* --- STOPPED state --- */
static void cb_stopped_entry(SM_Handle_t sm)
{
    cb_stopped_entry_count++;

    if (recall_on_stopped_entry) {
        recall_succeeded = SM_RecallEvent(sm);
    }
}

static void cb_stopped_exit(SM_Handle_t sm)
{
    (void)sm;
    cb_stopped_exit_count++;
}

static void cb_stopped_execute(SM_Handle_t sm)
{
    (void)sm;
    cb_stopped_execute_count++;
}

/* --- ERROR state --- */
static void cb_error_entry(SM_Handle_t sm)
{
    (void)sm;
    cb_error_entry_count++;
}

static void cb_error_exit(SM_Handle_t sm)
{
    (void)sm;
    cb_error_exit_count++;
}

static void cb_error_execute(SM_Handle_t sm)
{
    (void)sm;
    cb_error_execute_count++;
}

/* --- Transition action --- */
static void action_log_event(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    if (action_log_count < ACTION_LOG_MAX) {
        action_log[action_log_count].event = event;
        action_log[action_log_count].data = data;
        action_log_count++;
    }
}

/* --- Recovery callback --- */
static bool recovery_callback(SM_Handle_t sm, uint16_t error_code)
{
    (void)sm;
    (void)error_code;
    recovery_cb_call_count++;
    return recovery_cb_return_value;
}

/* --- Guard: data > threshold --- */
static bool guard_data_above_threshold(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    (void)event;
    return data > guard_data_threshold;
}

/* =============================================================================
 * STATE DESCRIPTORS AND TRANSITION TABLES
 * ===========================================================================*/

/* --- Standard state descriptors (all callbacks wired) --- */
static const SM_StateDesc_t integ_states[SM_STATE_COUNT] = {
    [TEST_STATE_INIT]    = { cb_init_entry,    cb_init_execute,    cb_init_exit,    0, 0 },
    [TEST_STATE_RUNNING] = { cb_running_entry,  cb_running_execute, cb_running_exit, 0, 0 },
    [TEST_STATE_STOPPED] = { cb_stopped_entry,  cb_stopped_execute, cb_stopped_exit, 0, 0 },
    [TEST_STATE_ERROR]   = { cb_error_entry,    cb_error_execute,   cb_error_exit,   0, 0 },
};

/* --- Lifecycle transitions (test 1, 2, 4) --- */
static const SM_Transition_t integ_transitions_lifecycle[] = {
    { TEST_STATE_INIT,    TEST_EVT_START,  TEST_STATE_RUNNING, 0, NULL, action_log_event },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,   TEST_STATE_STOPPED, 0, NULL, action_log_event },
    { TEST_STATE_STOPPED, TEST_EVT_RESET,  TEST_STATE_INIT,    0, NULL, action_log_event },
    /* Self-loop for timer-posted DATA events while RUNNING */
    { TEST_STATE_RUNNING, TEST_EVT_DATA,   TEST_STATE_RUNNING, 0, NULL, action_log_event },
    /* Self-loop for recalled CUSTOM events while STOPPED */
    { TEST_STATE_STOPPED, TEST_EVT_CUSTOM, TEST_STATE_STOPPED, 0, NULL, action_log_event },
};

/* --- Guard-protected transitions (test 5) ---
 *
 * Two transitions from RUNNING on EVT_DATA:
 *   1. Guarded: data > threshold -> ERROR
 *   2. Unguarded (NULL guard) -> STOPPED
 *
 * The engine searches the table in order (multi-guard fallthrough): if the
 * guarded entry's guard returns false, the engine falls through to the
 * unguarded entry.
 */
static const SM_Transition_t integ_transitions_guarded[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL,                      NULL },
    { TEST_STATE_RUNNING, TEST_EVT_DATA,  TEST_STATE_ERROR,   0, guard_data_above_threshold, action_log_event },
    { TEST_STATE_RUNNING, TEST_EVT_DATA,  TEST_STATE_STOPPED, 0, NULL,                      action_log_event },
};

/* =============================================================================
 * HELPER: reset all callback counters and flags
 * ===========================================================================*/

static void reset_all(void)
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

    memset(action_log, 0, sizeof(action_log));
    action_log_count = 0;

    arm_timer_on_running_entry = false;
    disarm_timer_on_running_exit = false;
    defer_custom_in_running = false;
    custom_was_deferred = false;
    recall_on_stopped_entry = false;
    recall_succeeded = false;
    recovery_cb_return_value = false;
    recovery_cb_call_count = 0;
    guard_data_threshold = 100;

    memset(&g_periodic_timer, 0, sizeof(g_periodic_timer));
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_sm = &g_ctx;
}

/**
 * @brief Initialize SM with lifecycle config and advance past the first
 *        on_entry(INIT) cycle so we start from a clean baseline.
 */
static void init_lifecycle_sm(void)
{
    static const SM_Config_t cfg = {
        .states           = integ_states,
        .transitions      = integ_transitions_lifecycle,
        .transition_count = sizeof(integ_transitions_lifecycle) /
                            sizeof(integ_transitions_lifecycle[0]),
        .initial_state    = TEST_STATE_INIT,
    };

    bool ok = SM_Init(g_sm, &cfg);
    TEST_ASSERT_TRUE_MESSAGE(ok, "SM_Init failed");

    /* First SM_Process: on_entry(INIT) fires */
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_INT(1, cb_init_entry_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(g_sm));
}

/**
 * @brief Tick the engine: advance sim clock by 1ms and call SM_Process.
 */
static void tick(void)
{
    SM_Platform_SimTick();
    SM_Process(g_sm);
}

/**
 * @brief Tick N times.
 */
static void tick_n(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        tick();
    }
}

/* =============================================================================
 * UNITY SETUP / TEARDOWN
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    reset_all();
}

void tearDown(void)
{
    /* nothing */
}

/* =============================================================================
 * TEST 1: Full lifecycle INIT -> RUNNING -> STOPPED -> INIT (via SM_Reset)
 *
 * Verifies:
 *   - Correct state at each step
 *   - on_entry / on_exit callbacks fire at expected times
 *   - SM_GetPreviousState tracks correctly
 *   - State history ring captures the full cycle
 *   - SM_Reset returns to initial state, calls on_exit, flushes queue
 * ===========================================================================*/

static void test_full_lifecycle(void)
{
    init_lifecycle_sm();

    /* ---- INIT -> RUNNING ---- */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);  /* dequeues START, on_exit(INIT) + action, transitions */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetPreviousState(g_sm));
    TEST_ASSERT_EQUAL_INT(1, cb_init_exit_count);

    SM_Process(g_sm);  /* on_entry(RUNNING) fires */
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);

    /* Execute a couple of cycles in RUNNING */
    SM_Process(g_sm);
    SM_Process(g_sm);
    TEST_ASSERT_TRUE(cb_running_execute_count >= 2);

    /* ---- RUNNING -> STOPPED ---- */
    SM_PostEvent(g_sm, TEST_EVT_STOP, 0);
    SM_Process(g_sm);  /* dequeues STOP, transitions */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, SM_GetState(g_sm));
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetPreviousState(g_sm));
    TEST_ASSERT_EQUAL_INT(1, cb_running_exit_count);

    SM_Process(g_sm);  /* on_entry(STOPPED) */
    TEST_ASSERT_EQUAL_INT(1, cb_stopped_entry_count);

    /* ---- STOPPED -> INIT via SM_Reset ---- */
    SM_Reset(g_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(g_sm));
    TEST_ASSERT_EQUAL_INT(1, cb_stopped_exit_count);

    /* Queue should be empty after reset */
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(g_sm));

    /* Next SM_Process should fire on_entry(INIT) again */
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_INT(2, cb_init_entry_count);

    /* ---- Verify state history ---- */
    uint16_t hist[SM_STATE_HISTORY_DEPTH];
    uint8_t  count = 0;
    bool ok = SM_GetStateHistory(g_sm, hist, SM_STATE_HISTORY_DEPTH, &count);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(count > 0);

    /* Most recent should be INIT (from the reset) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, hist[0]);

    /* History ring depth=4 should contain (most recent first):
     * INIT(reset), STOPPED, RUNNING, INIT(original) */
    if (count >= 4) {
        TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT,    hist[0]);
        TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED,  hist[1]);
        TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING,  hist[2]);
        TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT,    hist[3]);
    }
}

/* =============================================================================
 * TEST 2: Time event + state transition
 *
 * Arm a periodic timer on RUNNING entry (every 3 ticks, posts EVT_DATA).
 * Verify the timer fires and the self-loop transition + action are invoked.
 * Disarm on RUNNING exit.
 * ===========================================================================*/

static void test_time_event_with_transition(void)
{
    arm_timer_on_running_entry = true;
    disarm_timer_on_running_exit = true;

    init_lifecycle_sm();

    /* Transition to RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);  /* transition fires */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    tick();  /* on_entry(RUNNING): arms the periodic timer */
    TEST_ASSERT_EQUAL_INT(1, cb_running_entry_count);
    /* Timer armed with ctr=3, but SM_TimeEvt_Tick_ runs at the end of this
     * same SM_Process cycle, so ctr is already decremented: 3->2. */
    TEST_ASSERT_EQUAL_UINT32(2U, g_periodic_timer.ctr);

    /* Reset the action log so we only see timer-generated events */
    action_log_count = 0;

    /* The timer ctr is at 2. It fires when ctr reaches 0:
     *   tick 1: ctr 2->1
     *   tick 2: ctr 1->0, fires (posts EVT_DATA), reloads to 3
     * Then the event is dequeued on the next tick. */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(0U, action_log_count);

    /* Dequeue tick: SM_Process dequeues EVT_DATA, self-loop action fires.
     * But the self-loop transition also runs on_exit(RUNNING) which disarms
     * the timer, then sets state_entered=true. On the NEXT cycle,
     * on_entry(RUNNING) re-arms it. */
    tick();
    TEST_ASSERT_TRUE(action_log_count >= 1);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, action_log[0].event);
    TEST_ASSERT_EQUAL_UINT32(0xDA7A, action_log[0].data);

    /* The self-loop transition triggered on_exit (disarmed timer) and set
     * state_entered. Next tick: on_entry re-arms timer (ctr=3), then
     * SM_TimeEvt_Tick_ decrements -> ctr=2. */
    int initial_count = action_log_count;
    tick();  /* on_entry re-arms; ticked once -> ctr=2 */
    TEST_ASSERT_EQUAL_UINT32(2U, g_periodic_timer.ctr);

    /* Wait for second firing: ctr 2->1->0 (2 ticks) + 1 dequeue tick */
    tick_n(3);
    TEST_ASSERT_TRUE(action_log_count > initial_count);

    /* Transition to STOPPED -> on_exit(RUNNING) disarms timer.
     * Note: cb_running_exit_count is already > 0 because each self-loop
     * RUNNING->RUNNING also calls on_exit. We only check that after this
     * transition the SM is in STOPPED. */
    int exit_before_stop = cb_running_exit_count;
    SM_PostEvent(g_sm, TEST_EVT_STOP, 0);
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, SM_GetState(g_sm));
    TEST_ASSERT_EQUAL_INT(exit_before_stop + 1, cb_running_exit_count);

    /* Timer should be disarmed */
    TEST_ASSERT_EQUAL_UINT32(0U, g_periodic_timer.ctr);

    /* Further ticks should produce no more timer events */
    int count_before = action_log_count;
    tick_n(10);
    /* Only the stopped on_execute runs, no more DATA events from timer.
     * Any action_log entries from this point would be from EVT_DATA
     * transitions which should not fire because the timer is disarmed. */
    TEST_ASSERT_EQUAL_INT(count_before, action_log_count);
}

/* =============================================================================
 * TEST 3: Error injection + recovery
 *
 * a) Report MINOR -> attempt recovery -> success
 * b) Report NORMAL -> attempt recovery -> success
 * c) Report CRITICAL -> SM_Process skips (critical_lock)
 * d) Verify error stats reflect all reports and recovery attempts
 * ===========================================================================*/

static void test_error_injection_and_recovery(void)
{
    init_lifecycle_sm();

    /* Move to RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);  /* transition */
    SM_Process(g_sm);  /* on_entry(RUNNING) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    /* Register recovery callback */
    SM_Error_RegisterRecoveryCallback(g_sm, recovery_callback);

    /* ---- (a) MINOR error ---- */
    recovery_cb_return_value = true;
    bool reported = SM_Error_Report(g_sm, SM_ERROR_MINOR, 10);
    TEST_ASSERT_TRUE(reported);

    SM_ErrorInfo_t err_info;
    SM_Error_GetCurrent(g_sm, &err_info);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_MINOR, err_info.level);
    TEST_ASSERT_EQUAL_UINT16(10, err_info.code);

    bool recovered = SM_Error_AttemptRecovery(g_sm);
    TEST_ASSERT_TRUE(recovered);
    TEST_ASSERT_EQUAL_INT(1, recovery_cb_call_count);

    /* Clear error for next scenario */
    SM_Error_Clear(g_sm);

    /* ---- (b) NORMAL error ---- */
    reported = SM_Error_Report(g_sm, SM_ERROR_NORMAL, 20);
    TEST_ASSERT_TRUE(reported);

    SM_Error_GetCurrent(g_sm, &err_info);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_NORMAL, err_info.level);
    TEST_ASSERT_EQUAL_UINT16(20, err_info.code);

    recovered = SM_Error_AttemptRecovery(g_sm);
    TEST_ASSERT_TRUE(recovered);
    TEST_ASSERT_EQUAL_INT(2, recovery_cb_call_count);

    SM_Error_Clear(g_sm);

    /* ---- (c) CRITICAL error ---- */
    reported = SM_Error_Report(g_sm, SM_ERROR_CRITICAL, 30);
    TEST_ASSERT_TRUE(reported);
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(g_sm));

    /* SM_Process should now skip all processing due to critical_lock */
    int exec_before = cb_running_execute_count;
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_INT(exec_before, cb_running_execute_count);

    /* Post an event -- it should go into the queue but never be processed */
    SM_PostEvent(g_sm, TEST_EVT_STOP, 0);
    SM_Process(g_sm);
    /* Still in RUNNING because critical_lock blocks event processing */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    /* SM_Reset should also be blocked by critical_lock */
    SM_Reset(g_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    /* ---- (d) Verify error stats ---- */
    SM_ErrorStats_t stats;
    bool ok = SM_Error_GetStats(g_sm, &stats);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_UINT32(1, stats.errors_by_level[SM_ERROR_MINOR]);
    TEST_ASSERT_EQUAL_UINT32(1, stats.errors_by_level[SM_ERROR_NORMAL]);
    TEST_ASSERT_EQUAL_UINT32(1, stats.errors_by_level[SM_ERROR_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT32(2, stats.recovery_success);
    /* recovery_fail: 0 because both attempts succeeded */
    TEST_ASSERT_EQUAL_UINT32(0, stats.recovery_fail);

    /* Verify error history count */
    uint8_t hist_count = SM_Error_GetHistoryCount(g_sm);
    TEST_ASSERT_EQUAL_UINT8(3, hist_count);

    /* Most recent error in history (index 0) should be CRITICAL */
    SM_ErrorInfo_t hist_info;
    ok = SM_Error_GetHistory(g_sm, 0, &hist_info);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(SM_ERROR_CRITICAL, hist_info.level);
    TEST_ASSERT_EQUAL_UINT16(30, hist_info.code);
}

/* =============================================================================
 * TEST 4: Deferred events
 *
 * In RUNNING, defer EVT_CUSTOM (via on_execute flag logic).
 * Transition to STOPPED. In on_entry(STOPPED), recall the deferred event.
 * Verify the recalled EVT_CUSTOM arrives and the self-loop action fires.
 * ===========================================================================*/

static void test_deferred_events_across_states(void)
{
    defer_custom_in_running = true;
    recall_on_stopped_entry = true;

    init_lifecycle_sm();

    /* Transition to RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);  /* transition fires */
    SM_Process(g_sm);  /* on_entry(RUNNING) fires */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    /* on_execute(RUNNING) runs and defers EVT_CUSTOM */
    SM_Process(g_sm);
    TEST_ASSERT_TRUE(custom_was_deferred);

    /* Reset action log before the transition to STOPPED */
    action_log_count = 0;

    /* Transition to STOPPED */
    SM_PostEvent(g_sm, TEST_EVT_STOP, 0);
    SM_Process(g_sm);  /* dequeues STOP, on_exit(RUNNING) + transition */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, SM_GetState(g_sm));

    /* on_entry(STOPPED) runs next cycle and recalls the deferred event */
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_INT(1, cb_stopped_entry_count);
    TEST_ASSERT_TRUE(recall_succeeded);

    /* The recalled EVT_CUSTOM was posted to the front of the main queue.
     * Next SM_Process should dequeue it and fire the self-loop action. */
    SM_Process(g_sm);
    TEST_ASSERT_TRUE(action_log_count >= 1);

    /* Find the EVT_CUSTOM action in the log. The STOP action was logged
     * before we cleared action_log_count, so the first entry should be
     * from EVT_CUSTOM. */
    bool found_custom = false;
    for (int i = 0; i < action_log_count; i++) {
        if (action_log[i].event == TEST_EVT_CUSTOM) {
            TEST_ASSERT_EQUAL_UINT32(0xCCCC, action_log[i].data);
            found_custom = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_custom,
        "Recalled EVT_CUSTOM was not processed in STOPPED state");
}

/* =============================================================================
 * TEST 5: Guard-protected multi-path transition
 *
 * Two transitions from RUNNING on EVT_DATA:
 *   - guard (data > 100) -> ERROR
 *   - no guard (fallthrough) -> STOPPED
 *
 * Post EVT_DATA with data=50  -> should go to STOPPED (guard false, fallthrough)
 * Reset, post EVT_DATA with data=200 -> should go to ERROR (guard true)
 * ===========================================================================*/

static void test_guard_multipath_data_routing(void)
{
    static const SM_Config_t cfg = {
        .states           = integ_states,
        .transitions      = integ_transitions_guarded,
        .transition_count = sizeof(integ_transitions_guarded) /
                            sizeof(integ_transitions_guarded[0]),
        .initial_state    = TEST_STATE_INIT,
    };

    guard_data_threshold = 100;

    /* Initialize */
    bool ok = SM_Init(g_sm, &cfg);
    TEST_ASSERT_TRUE(ok);
    SM_Process(g_sm);  /* on_entry(INIT) */

    /* Transition to RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);  /* transition INIT -> RUNNING */
    SM_Process(g_sm);  /* on_entry(RUNNING) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    /* ---- Path A: data=50, guard false -> fallthrough to STOPPED ---- */
    action_log_count = 0;
    SM_PostEvent(g_sm, TEST_EVT_DATA, 50);
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_STOPPED, SM_GetState(g_sm));

    /* The action for the fallthrough transition should have been called */
    TEST_ASSERT_TRUE(action_log_count >= 1);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, action_log[0].event);
    TEST_ASSERT_EQUAL_UINT32(50, action_log[0].data);

    /* ---- Reset and try Path B: data=200, guard true -> ERROR ---- */
    reset_all();
    g_sm = &g_ctx;

    ok = SM_Init(g_sm, &cfg);
    TEST_ASSERT_TRUE(ok);
    SM_Process(g_sm);  /* on_entry(INIT) */

    guard_data_threshold = 100;

    /* Go to RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);
    SM_Process(g_sm);  /* on_entry(RUNNING) */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(g_sm));

    action_log_count = 0;
    SM_PostEvent(g_sm, TEST_EVT_DATA, 200);
    SM_Process(g_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_ERROR, SM_GetState(g_sm));

    /* The action for the guarded transition should have fired */
    TEST_ASSERT_TRUE(action_log_count >= 1);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, action_log[0].event);
    TEST_ASSERT_EQUAL_UINT32(200, action_log[0].data);
}

/* =============================================================================
 * TEST 6: Statistics tracking
 *
 * Run through several transitions and verify SM_GetStats reports correct:
 *   - total_transitions
 *   - total_events_posted
 *   - per-state entry counts
 * ===========================================================================*/

static void test_statistics_tracking(void)
{
    init_lifecycle_sm();

    /* SM_Init already incremented state_entry_counts[INIT] by 1 (init) */

    /* Transition INIT -> RUNNING */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);
    SM_Process(g_sm);  /* on_entry(RUNNING) */

    /* Transition RUNNING -> STOPPED */
    SM_PostEvent(g_sm, TEST_EVT_STOP, 0);
    SM_Process(g_sm);
    SM_Process(g_sm);  /* on_entry(STOPPED) */

    /* Transition STOPPED -> INIT (via EVT_RESET) */
    SM_PostEvent(g_sm, TEST_EVT_RESET, 0);
    SM_Process(g_sm);
    SM_Process(g_sm);  /* on_entry(INIT) second time */

    /* Transition INIT -> RUNNING again */
    SM_PostEvent(g_sm, TEST_EVT_START, 0);
    SM_Process(g_sm);
    SM_Process(g_sm);  /* on_entry(RUNNING) second time */

    /* Snapshot statistics */
    SM_Stats_t stats;
    bool ok = SM_GetStats(g_sm, &stats);
    TEST_ASSERT_TRUE(ok);

    /* 4 transitions: INIT->RUNNING, RUNNING->STOPPED, STOPPED->INIT, INIT->RUNNING */
    TEST_ASSERT_EQUAL_UINT32(4, stats.total_transitions);

    /* 4 events posted by the test */
    TEST_ASSERT_EQUAL_UINT32(4, stats.total_events_posted);

    /* No events should have been dropped */
    TEST_ASSERT_EQUAL_UINT32(0, stats.total_events_dropped);

    /* Per-state entry counts:
     * INIT:     1 (SM_Init) + 1 (STOPPED->INIT) = 2
     * RUNNING:  1 (INIT->RUNNING) + 1 (INIT->RUNNING again) = 2
     * STOPPED:  1 (RUNNING->STOPPED)
     * ERROR:    0
     */
    TEST_ASSERT_EQUAL_UINT32(2, stats.state_entry_counts[TEST_STATE_INIT]);
    TEST_ASSERT_EQUAL_UINT32(2, stats.state_entry_counts[TEST_STATE_RUNNING]);
    TEST_ASSERT_EQUAL_UINT32(1, stats.state_entry_counts[TEST_STATE_STOPPED]);
    TEST_ASSERT_EQUAL_UINT32(0, stats.state_entry_counts[TEST_STATE_ERROR]);

    /* Reset stats and verify they go to zero */
    SM_ResetStats(g_sm);
    ok = SM_GetStats(g_sm, &stats);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(0, stats.total_transitions);
    TEST_ASSERT_EQUAL_UINT32(0, stats.total_events_posted);
    for (uint16_t i = 0; i < SM_STATE_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, stats.state_entry_counts[i]);
    }
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_full_lifecycle);
    RUN_TEST(test_time_event_with_transition);
    RUN_TEST(test_error_injection_and_recovery);
    RUN_TEST(test_deferred_events_across_states);
    RUN_TEST(test_guard_multipath_data_routing);
    RUN_TEST(test_statistics_tracking);

    return UNITY_END();
}
