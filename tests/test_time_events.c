/**
 * @file test_time_events.c
 * @brief Unity tests for the time event subsystem (QP/C-inspired linked-list
 *        timers, D9)
 *
 * Covers:
 *   1.  SM_TimeEvt_Init field setup
 *   2.  SM_TimeEvt_Arm one-shot (ctr, interval)
 *   3.  SM_TimeEvt_Arm periodic (ctr, interval)
 *   4.  Disarm armed timer returns true
 *   5.  Disarm already-disarmed timer returns false
 *   6.  One-shot fires after exactly N ticks
 *   7.  Periodic fires every interval ticks
 *   8.  Multiple time events on same SM instance
 *   9.  Disarm mid-countdown prevents firing
 *  10.  Arm after disarm (re-arm)
 *  11.  Time event posts correct sig and data payload
 *
 * Time events are ticked internally by SM_Process via SM_TimeEvt_Tick_().
 * SM_Platform_SimTick() advances the platform clock so SM_Process can
 * track state timing.
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* =========================================================================
 * TEST MACHINE SETUP
 *
 * A minimal 2-state FSM: TEST_STATE_INIT -> TEST_STATE_RUNNING.
 * Time-event signals use TEST_EVT_TIMEOUT and TEST_EVT_DATA.
 * An action callback records fired events for verification.
 * ========================================================================= */

/* Tracking structure for action callbacks */
#define MAX_FIRE_LOG 32

static struct {
    uint16_t sig;
    uint32_t data;
} fire_log[MAX_FIRE_LOG];

static uint32_t fire_count;

static void fire_log_reset(void)
{
    memset(fire_log, 0, sizeof(fire_log));
    fire_count = 0U;
}

static void fire_log_record(uint16_t sig, uint32_t data)
{
    if (fire_count < MAX_FIRE_LOG) {
        fire_log[fire_count].sig  = sig;
        fire_log[fire_count].data = data;
        fire_count++;
    }
}

/* --- Action callback: records the event that triggered the transition --- */
static void action_log_event(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    (void)sm;
    fire_log_record(event, data);
}

/* --- State callbacks (minimal) --- */
static void on_entry_running(SM_Handle_t sm) { (void)sm; }
static void on_exec_noop(SM_Handle_t sm) { (void)sm; }

/* --- State descriptors (SM_STATE_COUNT == 4) --- */
static const SM_StateDesc_t test_states[4] = {
    /* TEST_STATE_INIT */
    { .on_entry = NULL, .on_execute = on_exec_noop, .on_exit = NULL,
      .timeout_ms = 0U, .min_dwell_ms = 0U },
    /* TEST_STATE_RUNNING */
    { .on_entry = on_entry_running, .on_execute = on_exec_noop, .on_exit = NULL,
      .timeout_ms = 0U, .min_dwell_ms = 0U },
    /* TEST_STATE_STOPPED (unused but required by SM_STATE_COUNT=4) */
    { .on_entry = NULL, .on_execute = NULL, .on_exit = NULL,
      .timeout_ms = 0U, .min_dwell_ms = 0U },
    /* TEST_STATE_ERROR (unused) */
    { .on_entry = NULL, .on_execute = NULL, .on_exit = NULL,
      .timeout_ms = 0U, .min_dwell_ms = 0U },
};

/*
 * Transitions:
 *   INIT  --START-->    RUNNING  (via TEST_EVT_START)
 *   RUNNING --TIMEOUT-> RUNNING  (self-loop, logs via action)
 *   RUNNING --DATA-->   RUNNING  (self-loop, logs via action)
 *   RUNNING --ACK-->    RUNNING  (self-loop, logs via action)
 *   RUNNING --CUSTOM--> RUNNING  (self-loop, logs via action)
 */
static const SM_Transition_t test_transitions[] = {
    { .from_state = TEST_STATE_INIT, .event = TEST_EVT_START,
      .to_state   = TEST_STATE_RUNNING, .guard = NULL, .action = NULL },
    { .from_state = TEST_STATE_RUNNING, .event = TEST_EVT_TIMEOUT,
      .to_state   = TEST_STATE_RUNNING, .guard = NULL,
      .action     = action_log_event },
    { .from_state = TEST_STATE_RUNNING, .event = TEST_EVT_DATA,
      .to_state   = TEST_STATE_RUNNING, .guard = NULL,
      .action     = action_log_event },
    { .from_state = TEST_STATE_RUNNING, .event = TEST_EVT_ACK,
      .to_state   = TEST_STATE_RUNNING, .guard = NULL,
      .action     = action_log_event },
    { .from_state = TEST_STATE_RUNNING, .event = TEST_EVT_CUSTOM,
      .to_state   = TEST_STATE_RUNNING, .guard = NULL,
      .action     = action_log_event },
};

static const SM_Config_t test_config = {
    .states           = test_states,
    .transitions      = test_transitions,
    .transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]),
    .initial_state    = TEST_STATE_INIT,
};

/* Shared context and time events */
static SM_Context_t ctx;

/* =========================================================================
 * HELPERS
 * ========================================================================= */

/**
 * @brief Initialize the state machine and advance to RUNNING state.
 *
 * After this call the SM is in TEST_STATE_RUNNING, the event queue is empty,
 * fire_log is cleared, and the sim clock is at a deterministic starting point.
 */
static void init_and_go_running(void)
{
    memset(&ctx, 0, sizeof(ctx));
    TEST_ASSERT_TRUE(SM_Init(&ctx, &test_config));

    /* Post START and run enough cycles to enter RUNNING */
    SM_PostEvent(&ctx, TEST_EVT_START, 0U);
    SM_Platform_SimTick();
    SM_Process(&ctx);           /* dequeues START, transitions to RUNNING */
    SM_Platform_SimTick();
    SM_Process(&ctx);           /* runs on_entry for RUNNING */

    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));

    /* Drain any remaining events */
    SM_EventQueueFlush(&ctx);
    fire_log_reset();
}

/**
 * @brief Tick the engine N times (SimTick + Process each iteration).
 */
static void tick_n(uint32_t n)
{
    for (uint32_t i = 0U; i < n; i++) {
        SM_Platform_SimTick();
        SM_Process(&ctx);
    }
}

/* =========================================================================
 * UNITY FIXTURES
 * ========================================================================= */

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    fire_log_reset();
}

void tearDown(void)
{
    /* nothing */
}

/* =========================================================================
 * TEST CASES
 * ========================================================================= */

/**
 * 1. SM_TimeEvt_Init sets fields correctly.
 */
static void test_init_sets_fields(void)
{
    SM_TimeEvt_t te;
    /* Fill with garbage first to confirm Init overwrites everything */
    memset(&te, 0xAA, sizeof(te));

    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 42U);

    TEST_ASSERT_EQUAL_PTR(&ctx, te.sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, te.sig);
    TEST_ASSERT_EQUAL_UINT32(42U, te.data);
    TEST_ASSERT_EQUAL_UINT32(0U, te.ctr);
    TEST_ASSERT_EQUAL_UINT32(0U, te.interval);
    TEST_ASSERT_NULL(te.next);
}

/**
 * 2. SM_TimeEvt_Arm one-shot: ctr = ticks, interval = 0.
 */
static void test_arm_oneshot(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    SM_TimeEvt_Arm(&te, 10U, 0U);

    TEST_ASSERT_EQUAL_UINT32(10U, te.ctr);
    TEST_ASSERT_EQUAL_UINT32(0U, te.interval);

    /* Clean up */
    SM_TimeEvt_Disarm(&te);
}

/**
 * 3. SM_TimeEvt_Arm periodic: ctr = ticks, interval = reload.
 */
static void test_arm_periodic(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    SM_TimeEvt_Arm(&te, 5U, 5U);

    TEST_ASSERT_EQUAL_UINT32(5U, te.ctr);
    TEST_ASSERT_EQUAL_UINT32(5U, te.interval);

    SM_TimeEvt_Disarm(&te);
}

/**
 * 4. Disarm armed timer returns true.
 */
static void test_disarm_armed_returns_true(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    SM_TimeEvt_Arm(&te, 10U, 0U);

    bool was_armed = SM_TimeEvt_Disarm(&te);
    TEST_ASSERT_TRUE(was_armed);
}

/**
 * 5. Disarm already-disarmed timer returns false.
 */
static void test_disarm_already_disarmed_returns_false(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);

    /* te was never armed (ctr == 0 from Init) */
    SM_TimeEvt_Arm(&te, 10U, 0U);
    SM_TimeEvt_Disarm(&te);  /* first disarm -- was armed */

    bool was_armed = SM_TimeEvt_Disarm(&te);
    TEST_ASSERT_FALSE(was_armed);
}

/**
 * 6. One-shot fires after exactly N ticks of SM_Process.
 *
 * Arm with ticks=5.  ctr goes 5->4->3->2->1->0 (fires on the 5th tick).
 * Verify:
 *   - No event after 4 ticks
 *   - Event posted on 5th tick
 *   - Timer disarmed (ctr == 0) after firing
 *   - No further events on subsequent ticks
 */
static void test_oneshot_fires_at_exact_tick(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 99U);
    SM_TimeEvt_Arm(&te, 5U, 0U);

    /* 4 ticks -- should NOT fire yet */
    tick_n(4);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);
    TEST_ASSERT_EQUAL_UINT32(1U, te.ctr);  /* one tick remaining */

    /* 5th tick -- should fire */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(0U, te.ctr);  /* disarmed after one-shot */

    /*
     * The event was posted into the queue by SM_TimeEvt_Tick_ at the end
     * of SM_Process. It will be dequeued on the NEXT SM_Process call.
     * Tick once more to let the transition action log it.
     */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(99U, fire_log[0].data);

    /* No further events on subsequent ticks */
    tick_n(5);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    /* Clean up (already disarmed, but remove from list) */
    SM_TimeEvt_Disarm(&te);
}

/**
 * 7. Periodic fires every interval ticks.
 *
 * Arm with ticks=5, interval=5.  Should fire at tick 5, 10, 15.
 */
static void test_periodic_fires_at_intervals(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    SM_TimeEvt_Arm(&te, 5U, 5U);

    /* --- First period: fire at tick 5 --- */
    tick_n(5);
    /* Event posted; dequeue + action on next process */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    /* Counter should have reloaded to interval */
    /* After 1 extra tick above: 5 (reload) - 1 (the extra tick) = 4 */
    TEST_ASSERT_EQUAL_UINT32(4U, te.ctr);

    /* --- Second period: fire at tick 10 (4 more ticks from current) --- */
    tick_n(4);
    /* Event posted on the 4th tick; dequeue on next */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);

    /* --- Third period: fire at tick 15 --- */
    tick_n(4);
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(3U, fire_count);

    SM_TimeEvt_Disarm(&te);
}

/**
 * 8. Multiple time events on same SM instance -- all tick correctly.
 *
 * Two timers: te_a fires at tick 3, te_b fires at tick 5.
 */
static void test_multiple_time_events(void)
{
    SM_TimeEvt_t te_a, te_b;
    init_and_go_running();

    SM_TimeEvt_Init(&te_a, &ctx, TEST_EVT_TIMEOUT, 0xAA);
    SM_TimeEvt_Init(&te_b, &ctx, TEST_EVT_DATA,    0xBB);

    SM_TimeEvt_Arm(&te_a, 3U, 0U);  /* one-shot at tick 3 */
    SM_TimeEvt_Arm(&te_b, 5U, 0U);  /* one-shot at tick 5 */

    /* Tick 1-2: nothing fires */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);

    /* Tick 3: te_a fires (posted) */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(0U, te_a.ctr);

    /* Tick 4: te_a event dequeued + processed */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(0xAA, fire_log[0].data);

    /* Tick 5: te_b fires (posted) */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(0U, te_b.ctr);

    /* Tick 6: te_b event dequeued + processed */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, fire_log[1].sig);
    TEST_ASSERT_EQUAL_UINT32(0xBB, fire_log[1].data);

    SM_TimeEvt_Disarm(&te_a);
    SM_TimeEvt_Disarm(&te_b);
}

/**
 * 9. Disarm mid-countdown prevents firing.
 */
static void test_disarm_mid_countdown(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    SM_TimeEvt_Arm(&te, 10U, 0U);

    /* Advance 5 ticks (halfway) */
    tick_n(5);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);
    TEST_ASSERT_TRUE(te.ctr > 0U);  /* still counting */

    /* Disarm */
    bool was_armed = SM_TimeEvt_Disarm(&te);
    TEST_ASSERT_TRUE(was_armed);
    TEST_ASSERT_EQUAL_UINT32(0U, te.ctr);

    /* Run past when it would have fired */
    tick_n(10);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);
}

/**
 * 10. Arm after disarm works correctly (re-arm).
 */
static void test_rearm_after_disarm(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);

    /* First arm + disarm */
    SM_TimeEvt_Arm(&te, 10U, 0U);
    tick_n(3);
    SM_TimeEvt_Disarm(&te);

    /* Re-arm with a shorter period */
    SM_TimeEvt_Arm(&te, 2U, 0U);
    TEST_ASSERT_EQUAL_UINT32(2U, te.ctr);

    /* Should fire after 2 ticks */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(0U, te.ctr);

    /* Dequeue on next tick */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    SM_TimeEvt_Disarm(&te);
}

/**
 * 11. Time event posts correct sig and data payload.
 *
 * Uses two timers with distinct (sig, data) pairs to verify each event
 * carries the payload set at Init time.
 */
static void test_correct_sig_and_data(void)
{
    SM_TimeEvt_t te_timeout, te_ack;
    init_and_go_running();

    SM_TimeEvt_Init(&te_timeout, &ctx, TEST_EVT_TIMEOUT, 0xDEADBEEF);
    SM_TimeEvt_Init(&te_ack,     &ctx, TEST_EVT_ACK,     0xCAFEBABE);

    SM_TimeEvt_Arm(&te_timeout, 2U, 0U);
    SM_TimeEvt_Arm(&te_ack,     3U, 0U);

    /* Tick 2: te_timeout fires */
    tick_n(2);
    /* Tick 3: te_timeout event dequeued; te_ack fires (posted) */
    tick_n(1);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, fire_log[0].data);

    /* Tick 4: te_ack event dequeued */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_ACK, fire_log[1].sig);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEBABE, fire_log[1].data);

    SM_TimeEvt_Disarm(&te_timeout);
    SM_TimeEvt_Disarm(&te_ack);
}

/* =========================================================================
 * MAIN
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_sets_fields);
    RUN_TEST(test_arm_oneshot);
    RUN_TEST(test_arm_periodic);
    RUN_TEST(test_disarm_armed_returns_true);
    RUN_TEST(test_disarm_already_disarmed_returns_false);
    RUN_TEST(test_oneshot_fires_at_exact_tick);
    RUN_TEST(test_periodic_fires_at_intervals);
    RUN_TEST(test_multiple_time_events);
    RUN_TEST(test_disarm_mid_countdown);
    RUN_TEST(test_rearm_after_disarm);
    RUN_TEST(test_correct_sig_and_data);

    return UNITY_END();
}
