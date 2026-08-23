/**
 * @file test_lifecycle_hardening.c
 * @brief v4.1 lifecycle fixes: timer schedule vs SM_Reset, re-init of a
 *        scheduled timer, deferred-event id validation, DIS pair atomicity.
 *
 * Each case pins a defect found in the 2026-08-22 review
 * (docs_dev/review_findings_2026-08-22.md):
 *   1.3  SM_Reset flushed both queues but left armed timers running
 *   1.4  SM_TimeEvt_Init on a scheduled timer orphaned the rest of the list
 *   1.9  SM_DeferEvent accepted ids SM_PostEvent would have rejected
 *   1.2  field and DIS shadow were written as two separately observable stores
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
};

static const SM_Transition_t s_trans[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0U, NULL, NULL },
};

static const SM_Config_t s_cfg = {
    .states           = s_states,
    .transitions      = s_trans,
    .transition_count = 2U,
    .initial_state    = TEST_STATE_INIT,
};

static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
}

void tearDown(void)
{
}

/* --- 1.3: SM_Reset clears the timer schedule ------------------------------ */

#if SM_FEATURE_TIME_EVENTS

/* A timer armed before SM_Reset must not fire into the reset machine. */
void test_reset_disarms_scheduled_timers(void)
{
    static SM_TimeEvt_t te;
    memset(&te, 0, sizeof(te));

    SM_TimeEvt_Init(&te, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 0U));

    SM_Reset(s_sm);

    /* Advance well past the original deadline and drain. */
    for (uint32_t i = 0U; i < 20U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }

    /* Had the timer survived, TEST_EVT_START would have driven
     * INIT -> RUNNING. The machine must still be in its initial state. */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
    TEST_ASSERT_TRUE(SM_EventQueueIsEmpty(s_sm));
}

/* SM_Reset must also mark the node itself unscheduled, so a later Disarm
 * reports "was not armed" rather than claiming a stale arming. */
void test_reset_marks_timer_nodes_disarmed(void)
{
    static SM_TimeEvt_t te;
    memset(&te, 0, sizeof(te));

    SM_TimeEvt_Init(&te, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 5U));

    SM_Reset(s_sm);

    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&te));
}

/* A timer can be armed again after SM_Reset -- the schedule is cleared, not
 * poisoned. */
void test_timer_rearms_after_reset(void)
{
    static SM_TimeEvt_t te;
    memset(&te, 0, sizeof(te));

    SM_TimeEvt_Init(&te, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 5U, 0U));
    SM_Reset(s_sm);

    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 3U, 0U));
    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

/* --- 1.4: re-initializing a scheduled timer keeps the list intact --------- */

/* te_a is re-initialized while still scheduled. Before v4.1 this cleared
 * te_a->next without unlinking, so te_b -- behind it in the list -- was
 * orphaned and never ticked again. */
void test_reinit_of_scheduled_timer_keeps_other_timers_alive(void)
{
    static SM_TimeEvt_t te_a;
    static SM_TimeEvt_t te_b;
    memset(&te_a, 0, sizeof(te_a));
    memset(&te_b, 0, sizeof(te_b));

    SM_TimeEvt_Init(&te_b, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_b, 4U, 0U));

    SM_TimeEvt_Init(&te_a, s_sm, TEST_EVT_DATA, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_a, 50U, 0U));

    /* Re-init te_a (still scheduled) -- must unlink it, not truncate. */
    SM_TimeEvt_Init(&te_a, s_sm, TEST_EVT_DATA, 0U);

    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }

    /* te_b still fired: INIT -> RUNNING happened. */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

/* The re-initialized timer is itself no longer scheduled. */
void test_reinit_of_scheduled_timer_unschedules_it(void)
{
    static SM_TimeEvt_t te;
    memset(&te, 0, sizeof(te));

    SM_TimeEvt_Init(&te, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te, 3U, 0U));

    SM_TimeEvt_Init(&te, s_sm, TEST_EVT_START, 0U);

    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
    TEST_ASSERT_FALSE(SM_TimeEvt_Disarm(&te));
}

/* Init on a never-armed timer must not touch the existing schedule. */
void test_init_of_fresh_timer_leaves_schedule_intact(void)
{
    static SM_TimeEvt_t te_a;
    static SM_TimeEvt_t te_b;
    memset(&te_a, 0, sizeof(te_a));
    memset(&te_b, 0, sizeof(te_b));

    SM_TimeEvt_Init(&te_a, s_sm, TEST_EVT_START, 0U);
    TEST_ASSERT_TRUE(SM_TimeEvt_Arm(&te_a, 3U, 0U));

    SM_TimeEvt_Init(&te_b, s_sm, TEST_EVT_DATA, 0U);   /* fresh, not armed */

    for (uint32_t i = 0U; i < 10U; i++) {
        SM_Platform_SimTick();
        SM_Process(s_sm);
    }
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

#endif /* SM_FEATURE_TIME_EVENTS */

/* --- 1.9: deferred events use the same accept range as SM_PostEvent ------- */

#if SM_FEATURE_DEFER

void test_defer_rejects_event_out_of_range(void)
{
    TEST_ASSERT_FALSE(SM_DeferEvent(s_sm, (uint16_t)SM_EVENT_COUNT, 0U));
}

void test_defer_rejects_reserved_timeout_id(void)
{
    TEST_ASSERT_FALSE(SM_DeferEvent(s_sm, SM_EVT_TIMEOUT, 0U));
}

/* A rejected defer must not consume a slot: the queue still accepts a valid
 * event afterwards, and recall delivers exactly that one. */
void test_defer_rejection_leaves_queue_usable(void)
{
    TEST_ASSERT_FALSE(SM_DeferEvent(s_sm, SM_EVT_TIMEOUT, 0U));
    TEST_ASSERT_TRUE(SM_DeferEvent(s_sm, TEST_EVT_START, 7U));

    SM_EventQueueFlush(s_sm);
    TEST_ASSERT_TRUE(SM_RecallEvent(s_sm));
    TEST_ASSERT_EQUAL_UINT8(1U, SM_EventQueueDepth(s_sm));

    SM_Process(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
}

#endif /* SM_FEATURE_DEFER */

/* --- 1.2: the DIS pair is never observable half-updated ------------------- */

/*
 * Scope note, deliberately explicit: these two cases do NOT prove the race is
 * gone. Observing a half-updated DIS pair requires a reader to run BETWEEN
 * the two stores, which needs real ISR interleaving -- the test platform has
 * no interrupt source, so no single-threaded test can catch it. (Review
 * finding 7.2 proposes injecting a synthetic "ISR" at every critical-section
 * boundary in test_platform.c; that harness does not exist yet.)
 *
 * What they do pin is that wrapping the stores in SM_DIS_ASSIGN left the
 * critical-section nesting balanced and the pairs verifying -- i.e. the fix
 * did not introduce a leak or a corrupted shadow. Both pass against the
 * pre-fix engine too, and that is expected.
 */
void test_transition_leaves_critical_nesting_balanced(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);

    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_RUNNING, SM_GetState(s_sm));
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

/* Same for the reset path and for the critical-lock pair in the error
 * handler: both use SM_DIS_ASSIGN, both must verify and stay balanced. */
void test_reset_and_critical_lock_dis_pairs_verify(void)
{
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);
    SM_Reset(s_sm);
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());

    TEST_ASSERT_TRUE(SM_Error_Report(s_sm, SM_ERROR_CRITICAL, 42U));
    TEST_ASSERT_TRUE(SM_Error_IsCriticalLock(s_sm));
    TEST_ASSERT_EQUAL_UINT32(0U, SM_Platform_GetCriticalNesting());
}

int main(void)
{
    UNITY_BEGIN();
#if SM_FEATURE_TIME_EVENTS
    RUN_TEST(test_reset_disarms_scheduled_timers);
    RUN_TEST(test_reset_marks_timer_nodes_disarmed);
    RUN_TEST(test_timer_rearms_after_reset);
    RUN_TEST(test_reinit_of_scheduled_timer_keeps_other_timers_alive);
    RUN_TEST(test_reinit_of_scheduled_timer_unschedules_it);
    RUN_TEST(test_init_of_fresh_timer_leaves_schedule_intact);
#endif
#if SM_FEATURE_DEFER
    RUN_TEST(test_defer_rejects_event_out_of_range);
    RUN_TEST(test_defer_rejects_reserved_timeout_id);
    RUN_TEST(test_defer_rejection_leaves_queue_usable);
#endif
    RUN_TEST(test_transition_leaves_critical_nesting_balanced);
    RUN_TEST(test_reset_and_critical_lock_dis_pairs_verify);
    return UNITY_END();
}
