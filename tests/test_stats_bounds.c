/**
 * @file test_stats_bounds.c
 * @brief SM_GetStats / SM_ResetStats (SM_FEATURE_STATISTICS)
 */
#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#if SM_FEATURE_STATISTICS

static const SM_StateDesc_t s_states[SM_STATE_COUNT] = {
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
    { NULL, NULL, NULL, 0U, 0U },
};
static const SM_Transition_t s_trans[] = {
    { TEST_STATE_INIT, TEST_EVT_START, TEST_STATE_RUNNING, 0U, NULL, NULL },
};
static const SM_Config_t s_cfg = {
    .states = s_states,
    .transitions = s_trans,
    .transition_count = 1U,
    .initial_state = TEST_STATE_INIT,
};

static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
}

void test_stats_contract_getstats_null_sm_false(void)
{
    SM_Stats_t st;
    TEST_ASSERT_FALSE(SM_GetStats(NULL, &st));
}

void test_stats_getstats_null_out_false(void)
{
    TEST_ASSERT_FALSE(SM_GetStats(s_sm, NULL));
}

void test_stats_resetstats_null_safe(void)
{
    SM_ResetStats(NULL);
    TEST_PASS();
}

void test_stats_snapshot_after_init_mostly_zero(void)
{
    SM_Stats_t st;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_EQUAL_UINT32(0U, st.total_transitions);
}

void test_stats_total_events_posted_increments_on_post(void)
{
    SM_Stats_t st;
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_EQUAL_UINT32(1U, st.total_events_posted);
}

void test_stats_reset_counters_zero(void)
{
    SM_PostEvent(s_sm, TEST_EVT_STOP, 0U);
    SM_ResetStats(s_sm);
    SM_Stats_t st;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_EQUAL_UINT32(0U, st.total_events_posted);
}

void test_stats_transitions_increment_on_state_change(void)
{
    SM_Process(s_sm);
    SM_PostEvent(s_sm, TEST_EVT_START, 0U);
    SM_Process(s_sm);
    SM_Stats_t st;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_TRUE(st.total_transitions > 0U);
}

void test_stats_entry_count_init_nonzero_after_process(void)
{
    SM_Process(s_sm);
    SM_Stats_t st;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_TRUE(st.state_entry_counts[TEST_STATE_INIT] > 0U);
}

void test_stats_roundtrip_memcpy_shape(void)
{
    SM_Stats_t a, b;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &a));
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &b));
    TEST_ASSERT_EQUAL_UINT32(a.total_events_posted, b.total_events_posted);
}

void test_stats_no_drops_until_queue_overflow(void)
{
    SM_Stats_t st;
    TEST_ASSERT_TRUE(SM_GetStats(s_sm, &st));
    TEST_ASSERT_EQUAL_UINT32(0U, st.total_events_dropped);
}

#else

void setUp(void)
{
}

void test_stats_disabled_stub(void)
{
    TEST_IGNORE_MESSAGE("SM_FEATURE_STATISTICS off");
}

#endif

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
#if SM_FEATURE_STATISTICS
    RUN_TEST(test_stats_contract_getstats_null_sm_false);
    RUN_TEST(test_stats_getstats_null_out_false);
    RUN_TEST(test_stats_resetstats_null_safe);
    RUN_TEST(test_stats_snapshot_after_init_mostly_zero);
    RUN_TEST(test_stats_total_events_posted_increments_on_post);
    RUN_TEST(test_stats_reset_counters_zero);
    RUN_TEST(test_stats_transitions_increment_on_state_change);
    RUN_TEST(test_stats_entry_count_init_nonzero_after_process);
    RUN_TEST(test_stats_roundtrip_memcpy_shape);
    RUN_TEST(test_stats_no_drops_until_queue_overflow);
#else
    RUN_TEST(test_stats_disabled_stub);
#endif
    return UNITY_END();
}
