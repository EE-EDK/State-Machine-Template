/**
 * @file test_abi_guard.c
 * @brief W1 / D23 -- SM_Init's build-consistency checks (assertions 105/106/107)
 *
 * This suite is the reproduction, not an argument.
 *
 * The file is compiled TWICE by tests/CMakeLists.txt, from one source:
 *
 *   test_abi_guard          -- configured exactly like the library it links.
 *                              Everything agrees; SM_Init must succeed, and the
 *                              guard must not be a false positive.
 *   test_abi_guard_mismatch -- identical, except this TU is compiled with
 *                              -DSM_EVENT_QUEUE_SIZE=16 while the library it
 *                              links was compiled at 8. SM_Init must REJECT it.
 *
 * The second target is the point. Before v4.2 that program compiled cleanly and
 * passed assertions 105 and 106 -- SM_STATE_COUNT and SM_EVENT_COUNT still
 * agree, only the queue size differs -- and then had the library memset and
 * write at ITS field offsets inside a context the application had sized
 * differently. Nothing detected it. The failure mode was memory corruption at
 * some later, unrelated call, not a wrong answer.
 *
 * Two deliberate choices:
 *
 *   - The mismatched build uses a LARGER queue, so the application's object is
 *     bigger than the library believes. Even a total failure of the guard
 *     cannot then overflow what this test allocates. A detector must not be a
 *     hazard.
 *   - v4.1's own dimension guard (105/106) had no test at all. It gets one
 *     here, since this is the suite that owns the question.
 *
 * With SM_FEATURE_ASSERT=1 a failed SM_REQUIRE longjmps out of SM_Init, so the
 * `return false` paths below it are unreachable from these tests. That branch
 * is finding 3.4 and is covered by the W8 ASSERT=0 matrix config, not here.
 */

#if defined(SM_TEST_ABI_MISMATCH)
/* THE DEFECT, REPRODUCED.
 *
 * An application header that sets a layout-affecting macro for its own
 * translation units while the framework was compiled with the default. This is
 * precisely the mistake config/sm_config_template.h warns about at :25-51 and
 * that nothing enforced before v4.2. The library links at 8; this TU is at 16.
 *
 * Stated here rather than on the compile command line so a reader of the test
 * sees the divergence where it matters -- and so it mirrors how the mistake is
 * actually made in the field. */
#  undef SM_EVENT_QUEUE_SIZE
#  define SM_EVENT_QUEUE_SIZE (16U)
#endif

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------*/

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
    .states           = s_states,
    .transitions      = s_trans,
    .transition_count = 1U,
    .initial_state    = TEST_STATE_INIT,
};

static SM_Context_t s_ctx;
static SM_Handle_t  s_sm = &s_ctx;

void setUp(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    test_sim_time_reset();
    test_assert_clear();
}

void tearDown(void)
{
}

/* A rejected SM_Init must not have touched the caller's context -- the whole
 * reason assertion 107 sits above the memset. Shared by both builds. */
static void assert_context_untouched_by(void (*attempt)(void))
{
    static uint8_t canary[sizeof(SM_Context_t)];

    memset(&s_ctx, 0xA5, sizeof(s_ctx));
    memset(canary, 0xA5, sizeof(canary));

    attempt();

    TEST_ASSERT_EQUAL_MEMORY(canary, &s_ctx, sizeof(s_ctx));
}

/* ==========================================================================
 * MATCHED BUILD
 * ========================================================================*/
#if !defined(SM_TEST_ABI_MISMATCH)

/* The guard must not reject a correctly configured application. */
void test_matched_build_initializes(void)
{
    TEST_ASSERT_TRUE(SM_Init(s_sm, &s_cfg));
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, SM_GetState(s_sm));
}

/* The positive control for the whole suite: in a correctly configured build the
 * two sides of the boundary must compute the same value. If this ever fails,
 * every rejection test below is meaningless. */
void test_app_and_library_fingerprints_agree(void)
{
    TEST_ASSERT_EQUAL_UINT32(test_lib_abi_fingerprint(),
                             (uint32_t)SM_ABI_FINGERPRINT);
    TEST_ASSERT_EQUAL_UINT32(test_lib_sizeof_context(),
                             (uint32_t)sizeof(SM_Context_t));
}

/* A fingerprint that folded nothing would pass every mismatch test in this file
 * by accident. Pin that it is neither degenerate constant. */
void test_fingerprint_is_not_degenerate(void)
{
    uint32_t fp = (uint32_t)SM_ABI_FINGERPRINT;
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, fp);
    TEST_ASSERT_NOT_EQUAL_UINT32(0xFFFFFFFFU, fp);
}

/* sizeof(SM_Context_t) is the term that catches layout divergence with no
 * maintenance. Prove it actually participates: swapping the sizeof term for a
 * different value must change the result. */
void test_fingerprint_depends_on_context_size(void)
{
    uint32_t real = (uint32_t)SM_ABI_FINGERPRINT;
    uint32_t swapped =
        (real ^ ((uint32_t)sizeof(struct SM_Context) * 0x9E3779B1U))
              ^ ((uint32_t)(sizeof(struct SM_Context) + 4U) * 0x9E3779B1U);
    TEST_ASSERT_NOT_EQUAL_UINT32(real, swapped);
}

/* Assertion 107 in isolation: right dimensions, wrong fingerprint. */
void test_wrong_fingerprint_fires_107(void)
{
    TEST_EXPECT_ASSERT(
        (void)SM_Init_(s_sm, &s_cfg,
                       (uint16_t)SM_STATE_COUNT, (uint16_t)SM_EVENT_COUNT,
                       (uint32_t)SM_ABI_FINGERPRINT ^ 0x5A5A5A5AU));
    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
    TEST_ASSERT_EQUAL_INT(107, test_assert_id);
}

/* v4.1's dimension guard had no test of its own. It gets one here. */
void test_wrong_state_count_fires_105(void)
{
    TEST_EXPECT_ASSERT(
        (void)SM_Init_(s_sm, &s_cfg,
                       (uint16_t)(SM_STATE_COUNT + 1U),
                       (uint16_t)SM_EVENT_COUNT,
                       (uint32_t)SM_ABI_FINGERPRINT));
    TEST_ASSERT_EQUAL_INT(105, test_assert_id);
}

void test_wrong_event_count_fires_106(void)
{
    TEST_EXPECT_ASSERT(
        (void)SM_Init_(s_sm, &s_cfg,
                       (uint16_t)SM_STATE_COUNT,
                       (uint16_t)(SM_EVENT_COUNT + 1U),
                       (uint32_t)SM_ABI_FINGERPRINT));
    TEST_ASSERT_EQUAL_INT(106, test_assert_id);
}

static void attempt_bad_fingerprint(void)
{
    TEST_EXPECT_ASSERT(
        (void)SM_Init_(s_sm, &s_cfg,
                       (uint16_t)SM_STATE_COUNT, (uint16_t)SM_EVENT_COUNT,
                       (uint32_t)SM_ABI_FINGERPRINT ^ 0x5A5A5A5AU));
}

void test_rejected_init_leaves_context_untouched(void)
{
    assert_context_untouched_by(attempt_bad_fingerprint);
}

/* ==========================================================================
 * MISMATCHED BUILD -- the reproduction
 * ========================================================================*/
#else

/* Sanity: the divergence this target exists to create is really present.
 * Without this, a build-system mistake would make every test below pass by
 * measuring nothing. */
void test_the_mismatch_really_exists(void)
{
    TEST_ASSERT_EQUAL_UINT32(16U, (uint32_t)SM_EVENT_QUEUE_SIZE);
    TEST_ASSERT_EQUAL_UINT32(8U, test_lib_event_queue_size());
}

/* ...and that it really does move the struct, which is what makes it
 * dangerous rather than merely untidy. */
void test_the_mismatch_changes_the_context_layout(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(test_lib_sizeof_context(),
                                    (uint32_t)sizeof(SM_Context_t));
    TEST_ASSERT_NOT_EQUAL_UINT32(test_lib_abi_fingerprint(),
                                 (uint32_t)SM_ABI_FINGERPRINT);
}

/* v4.1's guard passes this program: the two count macros still agree.
 * This is the finding, stated as a test. */
void test_v41_dimension_guard_does_not_catch_it(void)
{
    TEST_ASSERT_EQUAL_UINT16(4U, (uint16_t)SM_STATE_COUNT);
    TEST_ASSERT_EQUAL_UINT16(8U, (uint16_t)SM_EVENT_COUNT);
}

/* v4.2's does. */
void test_queue_size_mismatch_is_rejected_at_init(void)
{
    TEST_EXPECT_ASSERT((void)SM_Init(s_sm, &s_cfg));
    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
    TEST_ASSERT_EQUAL_INT(107, test_assert_id);
}

static void attempt_mismatched_init(void)
{
    TEST_EXPECT_ASSERT((void)SM_Init(s_sm, &s_cfg));
}

void test_rejected_init_leaves_context_untouched(void)
{
    assert_context_untouched_by(attempt_mismatched_init);
}

#endif /* SM_TEST_ABI_MISMATCH */

/* --------------------------------------------------------------------------
 * Runner
 * ------------------------------------------------------------------------*/

int main(void)
{
    UNITY_BEGIN();

#if !defined(SM_TEST_ABI_MISMATCH)
    RUN_TEST(test_matched_build_initializes);
    RUN_TEST(test_app_and_library_fingerprints_agree);
    RUN_TEST(test_fingerprint_is_not_degenerate);
    RUN_TEST(test_fingerprint_depends_on_context_size);
    RUN_TEST(test_wrong_fingerprint_fires_107);
    RUN_TEST(test_wrong_state_count_fires_105);
    RUN_TEST(test_wrong_event_count_fires_106);
    RUN_TEST(test_rejected_init_leaves_context_untouched);
#else
    RUN_TEST(test_the_mismatch_really_exists);
    RUN_TEST(test_the_mismatch_changes_the_context_layout);
    RUN_TEST(test_v41_dimension_guard_does_not_catch_it);
    RUN_TEST(test_queue_size_mismatch_is_rejected_at_init);
    RUN_TEST(test_rejected_init_leaves_context_untouched);
#endif

    return UNITY_END();
}
