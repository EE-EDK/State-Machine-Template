/**
 * @file test_time_events.c
 * @brief Unity tests for the time event subsystem (deadline-based ms timers,
 *        D9 revised in v4.0)
 *
 * Covers:
 *   1.  SM_TimeEvt_Init field setup (armed=false, deadline/interval zeroed)
 *   2.  SM_TimeEvt_Arm one-shot (armed, deadline = now + delay)
 *   3.  SM_TimeEvt_Arm periodic (interval stored)
 *   4.  Disarm armed timer returns true
 *   5.  Disarm already-disarmed timer returns false
 *   6.  One-shot fires at its ms deadline, delivered same SM_Process call
 *   7.  Periodic fires every interval ms, drift-free
 *   8.  Multiple time events on same SM instance
 *   9.  Disarm mid-countdown prevents firing
 *  10.  Arm after disarm (re-arm)
 *  11.  Time event posts correct sig and data payload
 *  12.  Firing depends on elapsed TIME, not SM_Process call count (the v3.0
 *       defect: timers stretched when SM_Process calls were missed)
 *  13.  Stalled periodic timer coalesces missed periods into ONE event and
 *       stays phase-aligned
 *  14.  Arm fails when SM_FEATURE_MAX_TIME_EVENTS timers already scheduled
 *  15.  Re-arming a scheduled timer updates deadline without duplicating it
 *
 * Timers are ticked by SM_Process via SM_TimeEvt_Tick_(), which runs BEFORE
 * the event drain: a timer that fires in a cycle is delivered in that same
 * cycle. SM_Platform_SimTick() advances the platform clock by 1 ms.
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

    /* Post START; the transition (and on_entry of RUNNING) completes within
     * one SM_Process call (v4.0 atomic transitions). */
    SM_PostEvent(&ctx, TEST_EVT_START, 0U);
    SM_Platform_SimTick();
    SM_Process(&ctx);

    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(&ctx));

    /* Drain any remaining events */
    SM_EventQueueFlush(&ctx);
    fire_log_reset();
}

/**
 * @brief Tick the engine N times (SimTick 1ms + Process each iteration).
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
    TEST_ASSERT_FALSE(te.armed);
    TEST_ASSERT_EQUAL_UINT32(0U, te.deadline);
    TEST_ASSERT_EQUAL_UINT32(0U, te.interval);
    TEST_ASSERT_NULL(te.next);
}

/**
 * 2. SM_TimeEvt_Arm one-shot: armed, deadline = now + delay, interval = 0.
 */
static void test_arm_oneshot(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    uint32_t now = SM_Platform_GetTimeMs();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));

    TEST_ASSERT_TRUE(te.armed);
    TEST_ASSERT_EQUAL_UINT32(now + 10U, te.deadline);
    TEST_ASSERT_EQUAL_UINT32(0U, te.interval);

    /* Clean up */
    SM_TimeEvt_Disarm(&te);
}

/**
 * 3. SM_TimeEvt_Arm periodic: interval stored.
 */
static void test_arm_periodic(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    uint32_t now = SM_Platform_GetTimeMs();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 5U));

    TEST_ASSERT_TRUE(te.armed);
    TEST_ASSERT_EQUAL_UINT32(now + 5U, te.deadline);
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
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));

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

    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));
    SM_TimeEvt_Disarm(&te);  /* first disarm -- was armed */

    bool was_armed = SM_TimeEvt_Disarm(&te);
    TEST_ASSERT_FALSE(was_armed);
}

/**
 * 6. One-shot fires at its ms deadline; delivery in the SAME SM_Process call.
 *
 * Arm with delay_ms=5 at t=T. The deadline passes at T+5, so the 5th
 * tick (SimTick to T+5, then SM_Process) both fires AND delivers the event
 * (SM_TimeEvt_Tick_ runs before the drain in v4.0).
 */
static void test_oneshot_fires_at_exact_ms(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 99U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 0U));

    /* 4 ticks -- deadline not reached (t = T+4 < T+5) */
    tick_n(4);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);
    TEST_ASSERT_TRUE(te.armed);

    /* 5th tick -- fires AND delivers in the same SM_Process call */
    tick_n(1);
    TEST_ASSERT_FALSE(te.armed);   /* one-shot disarmed after firing */
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(99U, fire_log[0].data);

    /* No further events on subsequent ticks */
    tick_n(5);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    /* Clean up (already disarmed + unlinked, false expected) */
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&te));
}

/**
 * 7. Periodic fires every interval ms, drift-free.
 *
 * Arm with delay=5, interval=5 at t=T. Fires at T+5, T+10, T+15 exactly.
 */
static void test_periodic_fires_at_intervals(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 5U));

    tick_n(4);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);

    tick_n(1);   /* t = T+5: first fire */
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    tick_n(4);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);

    tick_n(1);   /* t = T+10: second fire */
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);

    tick_n(5);   /* t = T+15: third fire */
    TEST_ASSERT_EQUAL_UINT32(3U, fire_count);

    TEST_ASSERT_TRUE(SM_TimeEvt_Disarm(&te));
}

/**
 * 8. Multiple time events on same SM instance -- independent deadlines.
 *
 * te_a fires at T+3, te_b at T+5.
 */
static void test_multiple_time_events(void)
{
    SM_TimeEvt_t te_a, te_b;
    init_and_go_running();

    SM_TimeEvt_Init(&te_a, &ctx, TEST_EVT_TIMEOUT, 0xAA);
    SM_TimeEvt_Init(&te_b, &ctx, TEST_EVT_DATA,    0xBB);

    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_a, 3U, 0U));
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_b, 5U, 0U));

    /* t = T+2: nothing fires */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);

    /* t = T+3: te_a fires + delivers */
    tick_n(1);
    TEST_ASSERT_FALSE(te_a.armed);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(0xAA, fire_log[0].data);

    /* t = T+5: te_b fires + delivers */
    tick_n(2);
    TEST_ASSERT_FALSE(te_b.armed);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_DATA, fire_log[1].sig);
    TEST_ASSERT_EQUAL_UINT32(0xBB, fire_log[1].data);
}

/**
 * 9. Disarm mid-countdown prevents firing.
 */
static void test_disarm_mid_countdown(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));

    /* Advance 5 ticks (halfway) */
    tick_n(5);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);
    TEST_ASSERT_TRUE(te.armed);

    /* Disarm */
    bool was_armed = SM_TimeEvt_Disarm(&te);
    TEST_ASSERT_TRUE(was_armed);
    TEST_ASSERT_FALSE(te.armed);

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
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));
    tick_n(3);
    SM_TimeEvt_Disarm(&te);

    /* Re-arm with a shorter deadline */
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 2U, 0U));
    TEST_ASSERT_TRUE(te.armed);

    /* Fires + delivers 2 ms later */
    tick_n(2);
    TEST_ASSERT_FALSE(te.armed);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
}

/**
 * 11. Time event posts correct sig and data payload.
 */
static void test_correct_sig_and_data(void)
{
    SM_TimeEvt_t te_timeout, te_ack;
    init_and_go_running();

    SM_TimeEvt_Init(&te_timeout, &ctx, TEST_EVT_TIMEOUT, 0xDEADBEEF);
    SM_TimeEvt_Init(&te_ack,     &ctx, TEST_EVT_ACK,     0xCAFEBABE);

    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_timeout, 2U, 0U));
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_ack,     3U, 0U));

    /* t = T+2: te_timeout fires + delivers */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, fire_log[0].data);

    /* t = T+3: te_ack fires + delivers */
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_ACK, fire_log[1].sig);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEBABE, fire_log[1].data);
}

/**
 * 12. Firing tracks elapsed TIME, not SM_Process call count.
 *
 * The v3.0 defect: timers decremented once per SM_Process call, so missed
 * calls stretched real-time behavior silently. v4.0: advance the sim clock
 * 10 ms with NO SM_Process calls, then a single SM_Process must fire a
 * 10 ms timer immediately.
 */
static void test_fires_on_elapsed_time_not_call_count(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_TIMEOUT, 0x71AE);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 10U, 0U));

    /* Advance time 10 ms WITHOUT calling SM_Process (simulates a stalled
     * scheduler). Under v3.0 tick-counting this would leave ctr=10. */
    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
    }

    /* One SM_Process: deadline already passed -> fire + deliver now */
    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    TEST_ASSERT_EQUAL_UINT16(TEST_EVT_TIMEOUT, fire_log[0].sig);
    TEST_ASSERT_EQUAL_UINT32(0x71AE, fire_log[0].data);
}

/**
 * 13. Stalled periodic timer: missed periods coalesce into ONE event and
 *     the next deadline stays on the original phase grid.
 *
 * Arm interval=5 at t=T (first fire T+5). Stall until T+17 (missing the
 * T+5, T+10, T+15 fires). One SM_Process fires exactly ONCE; the next
 * fire lands at T+20 (phase-aligned), not T+22 (17+5, drifted).
 */
static void test_stalled_periodic_coalesces_and_keeps_phase(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_DATA, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 5U));
    uint32_t t0 = SM_Platform_GetTimeMs();

    /* Stall: advance clock to T+17 with no SM_Process */
    for (uint32_t i = 0U; i < 17U; i++) {
        SM_Platform_SimTick();
    }

    SM_Process(&ctx);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);          /* coalesced, not 3 */
    TEST_ASSERT_EQUAL_UINT32(t0 + 20U, te.deadline);   /* phase-aligned */

    /* Resume normal ticking: next fire exactly at T+20 (3 ticks away) */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    tick_n(1);
    TEST_ASSERT_EQUAL_UINT32(2U, fire_count);

    TEST_ASSERT_TRUE(SM_TimeEvt_Disarm(&te));
}

/**
 * 14. Arm fails once SM_FEATURE_MAX_TIME_EVENTS timers are scheduled.
 *
 * v3.0 accepted over-capacity arms whose timers then silently never fired
 * (and re-arming one could corrupt the list into a cycle). v4.0 rejects
 * at Arm time with a false return.
 */
static void test_arm_fails_at_capacity(void)
{
    static SM_TimeEvt_t pool[SM_FEATURE_MAX_TIME_EVENTS];
    SM_TimeEvt_t overflow;
    init_and_go_running();

    for (uint32_t i = 0U; i < SM_FEATURE_MAX_TIME_EVENTS; i++) {
        SM_TimeEvt_Init(&pool[i], &ctx, TEST_EVT_DATA, i);
        TEST_ASSERT_TRUE_MESSAGE(SM_TimeEvt_Arm(&pool[i], 1000U, 0U),
                                 "Arm within capacity must succeed");
    }

    SM_TimeEvt_Init(&overflow, &ctx, TEST_EVT_ACK, 0U);
    TEST_ASSERT_FALSE_MESSAGE(SM_TimeEvt_Arm(&overflow, 1000U, 0U),
                              "Arm beyond capacity must fail");
    TEST_ASSERT_FALSE(overflow.armed);

    /* Disarming one frees a slot */
    TEST_ASSERT_TRUE(SM_TimeEvt_Disarm(&pool[0]));
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&overflow, 1000U, 0U));

    /* Clean up */
    TEST_ASSERT_TRUE(SM_TimeEvt_Disarm(&overflow));
    for (uint32_t i = 1U; i < SM_FEATURE_MAX_TIME_EVENTS; i++) {
        TEST_ASSERT_TRUE(SM_TimeEvt_Disarm(&pool[i]));
    }
}

/**
 * 15. Re-arming a scheduled timer updates its deadline in place -- exactly
 *     one fire results, proving no duplicate list entry was created.
 */
static void test_rearm_while_armed_updates_deadline_no_duplicate(void)
{
    SM_TimeEvt_t te;
    init_and_go_running();

    SM_TimeEvt_Init(&te, &ctx, TEST_EVT_CUSTOM, 7U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 3U, 0U));

    /* Re-arm before the first deadline: pushes the fire out to now+8 */
    tick_n(1);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 8U, 0U));

    /* Old deadline (2 more ticks) must NOT fire */
    tick_n(2);
    TEST_ASSERT_EQUAL_UINT32(0U, fire_count);

    /* New deadline fires exactly once */
    tick_n(6);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
    tick_n(10);
    TEST_ASSERT_EQUAL_UINT32(1U, fire_count);
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
    RUN_TEST(test_oneshot_fires_at_exact_ms);
    RUN_TEST(test_periodic_fires_at_intervals);
    RUN_TEST(test_multiple_time_events);
    RUN_TEST(test_disarm_mid_countdown);
    RUN_TEST(test_rearm_after_disarm);
    RUN_TEST(test_correct_sig_and_data);
    RUN_TEST(test_fires_on_elapsed_time_not_call_count);
    RUN_TEST(test_stalled_periodic_coalesces_and_keeps_phase);
    RUN_TEST(test_arm_fails_at_capacity);
    RUN_TEST(test_rearm_while_armed_updates_deadline_no_duplicate);

    return UNITY_END();
}
