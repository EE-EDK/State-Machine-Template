/**
 * @file test_safety.c
 * @brief Unity tests for safety macros: DIS, bounded loops, numeric assertions
 *
 * Tests SM_DIS_UPDATE / SM_DIS_VERIFY, SM_REQUIRE / SM_ASSERT_ID / SM_INVARIANT,
 * SM_BOUNDED_LOOP_BEGIN / SM_BOUNDED_LOOP_END, and DIS corruption detection
 * through SM_Init, SM_Process, and SM_Error_IsCriticalLock.
 *
 * Assertion ID ranges exercised:
 *   200   init_dis corruption (sm_engine)
 *   201   state_dis corruption (sm_engine)
 *   710   critical_lock_dis corruption (sm_error)
 *   999   bounded loop invariant (test_safety)
 */

#include "unity.h"
#include "sm_framework/sm_framework.h"
#include "test_common.h"
#include <string.h>
#include <stdio.h>

SM_DEFINE_MODULE("test_safety");

/* =============================================================================
 * TEST FIXTURE: minimal 4-state FSM with no callbacks
 * ===========================================================================*/

static SM_Context_t sm;
static SM_Config_t  cfg;

static const SM_StateDesc_t test_states[4] = {
    /* INIT */    { NULL, NULL, NULL, 0, 0 },
    /* RUNNING */ { NULL, NULL, NULL, 0, 0 },
    /* STOPPED */ { NULL, NULL, NULL, 0, 0 },
    /* ERROR */   { NULL, NULL, NULL, 0, 0 }
};

static const SM_Transition_t test_transitions[] = {
    { TEST_STATE_INIT,    TEST_EVT_START, TEST_STATE_RUNNING, 0, NULL, NULL },
    { TEST_STATE_RUNNING, TEST_EVT_STOP,  TEST_STATE_STOPPED, 0, NULL, NULL },
    { TEST_STATE_STOPPED, TEST_EVT_RESET, TEST_STATE_INIT,    0, NULL, NULL }
};

static void init_sm(void)
{
    memset(&sm, 0, sizeof(sm));
    cfg.states           = test_states;
    cfg.transitions      = test_transitions;
    cfg.transition_count = sizeof(test_transitions) / sizeof(test_transitions[0]);
    cfg.initial_state    = TEST_STATE_INIT;

    bool ok = SM_Init(&sm, &cfg);
    TEST_ASSERT_TRUE(ok);
}

/* =============================================================================
 * setUp / tearDown
 * ===========================================================================*/

void setUp(void)
{
    test_sim_time_reset();
    test_assert_clear();
}

void tearDown(void)
{
    /* nothing */
}

/* =============================================================================
 * TEST 1: SM_DIS_UPDATE + SM_DIS_VERIFY — update a uint16_t, verify passes
 * ===========================================================================*/

void test_dis_update_verify_pass(void)
{
    uint16_t field = 42U;
    uint16_t shadow = 0U;

    SM_DIS_UPDATE(field, shadow, uint16_t);

    /* Shadow should be bitwise inverse of field */
    TEST_ASSERT_EQUAL_HEX16((uint16_t)~42U, shadow);

    /* SM_DIS_VERIFY should not fire an assertion */
    TEST_ASSERT_NO_ASSERT(SM_DIS_VERIFY(field, shadow, uint16_t, 900));
}

/* =============================================================================
 * TEST 2: SM_DIS_VERIFY fires assertion on corrupted DIS
 * ===========================================================================*/

void test_dis_verify_fires_on_corruption(void)
{
    uint16_t field = 42U;
    uint16_t shadow = 0U;

    SM_DIS_UPDATE(field, shadow, uint16_t);

    /* Inject corruption: wrong inverse */
    shadow = 0xBEEFU;

    TEST_EXPECT_ASSERT(SM_DIS_VERIFY(field, shadow, uint16_t, 900));

    TEST_ASSERT_EQUAL_STRING("test_safety", test_assert_module);
    TEST_ASSERT_EQUAL_INT(900, test_assert_id);
}

/* =============================================================================
 * TEST 3: SM_Init sets state_dis correctly
 * ===========================================================================*/

void test_init_sets_state_dis(void)
{
    init_sm();

    /* state_dis must be the bitwise inverse of current_state (uint16_t) */
    TEST_ASSERT_EQUAL_HEX16(
        (uint16_t)~(uint16_t)sm.current_state,
        sm.state_dis
    );

    /* Also confirm the current state is what we asked for */
    TEST_ASSERT_EQUAL_UINT16(TEST_STATE_INIT, sm.current_state);
}

/* =============================================================================
 * TEST 4: SM_Init sets init_dis correctly (initialized = true => ~1)
 * ===========================================================================*/

void test_init_sets_init_dis(void)
{
    init_sm();

    /* initialized == true, so DIS stores ~(uint8_t)(1) */
    TEST_ASSERT_TRUE(sm.initialized);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)~(uint8_t)1U, sm.init_dis);
}

/* =============================================================================
 * TEST 5: Corrupted state_dis detected on SM_Process (assertion 201)
 * ===========================================================================*/

void test_corrupted_state_dis_detected_on_process(void)
{
    init_sm();

    /* Inject bad state_dis */
    sm.state_dis = 0xBEEFU;

    TEST_EXPECT_ASSERT(SM_Process(&sm));

    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
    TEST_ASSERT_EQUAL_INT(201, test_assert_id);
}

/* =============================================================================
 * TEST 6: Corrupted init_dis detected on SM_Process (assertion 200)
 * ===========================================================================*/

void test_corrupted_init_dis_detected_on_process(void)
{
    init_sm();

    /* Inject bad init_dis */
    sm.init_dis = 0xAAU;

    TEST_EXPECT_ASSERT(SM_Process(&sm));

    TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
    TEST_ASSERT_EQUAL_INT(200, test_assert_id);
}

/* =============================================================================
 * TEST 7: Corrupted critical_lock_dis detected on SM_Error_IsCriticalLock
 *         (assertion 710)
 * ===========================================================================*/

void test_corrupted_critical_lock_dis_detected(void)
{
    init_sm();

    /* After init, critical_lock == false, critical_lock_dis == ~(uint8_t)0 = 0xFF.
     * Inject corruption into the DIS shadow. */
    sm.error.critical_lock_dis = 0x42U;

    TEST_EXPECT_ASSERT(SM_Error_IsCriticalLock(&sm));

    TEST_ASSERT_EQUAL_STRING("sm_error", test_assert_module);
    TEST_ASSERT_EQUAL_INT(710, test_assert_id);
}

/* =============================================================================
 * TEST 8: SM_REQUIRE fires assertion with correct module name and ID
 * ===========================================================================*/

void test_require_fires_with_correct_module_and_id(void)
{
    TEST_EXPECT_ASSERT(SM_REQUIRE(555, false));

    /* SM_DEFINE_MODULE("test_safety") at file scope sets the module name */
    TEST_ASSERT_EQUAL_STRING("test_safety", test_assert_module);
    TEST_ASSERT_EQUAL_INT(555, test_assert_id);
}

/* =============================================================================
 * TEST 9: SM_REQUIRE with true expression does not fire
 * ===========================================================================*/

void test_require_true_does_not_fire(void)
{
    TEST_ASSERT_NO_ASSERT(SM_REQUIRE(556, true));

    /* If we reach here, no assertion was raised -- pass */
    TEST_ASSERT_FALSE(test_assert_fired);
}

/* =============================================================================
 * TEST 10: SM_BOUNDED_LOOP — loop terminates within bound, no assertion
 * ===========================================================================*/

void test_bounded_loop_within_bound_no_assert(void)
{
    volatile uint32_t sum = 0U;

    TEST_ASSERT_NO_ASSERT({
        SM_BOUNDED_LOOP_BEGIN(i, 5, 999)
        {
            sum += i;
            if (i == 3U) {
                break;
            }
        }
        SM_BOUNDED_LOOP_END(i, 5, 999)
    });

    /* Loop ran and exited early (i=3 < bound=5), no invariant fired */
    TEST_ASSERT_EQUAL_UINT32(0U + 1U + 2U + 3U, sum);
}

/* =============================================================================
 * TEST 11: SM_BOUNDED_LOOP — counter exhausts bound, fires SM_INVARIANT
 * ===========================================================================*/

void test_bounded_loop_exhausted_fires_invariant(void)
{
    TEST_EXPECT_ASSERT({
        SM_BOUNDED_LOOP_BEGIN(i, 5, 999)
        {
            /* Never break -- let the counter exhaust the bound */
            (void)i;
        }
        SM_BOUNDED_LOOP_END(i, 5, 999)
    });

    TEST_ASSERT_EQUAL_STRING("test_safety", test_assert_module);
    TEST_ASSERT_EQUAL_INT(999, test_assert_id);
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    UNITY_BEGIN();

    /* DIS macros */
    RUN_TEST(test_dis_update_verify_pass);
    RUN_TEST(test_dis_verify_fires_on_corruption);

    /* SM_Init DIS fields */
    RUN_TEST(test_init_sets_state_dis);
    RUN_TEST(test_init_sets_init_dis);

    /* DIS corruption detection in engine */
    RUN_TEST(test_corrupted_state_dis_detected_on_process);
    RUN_TEST(test_corrupted_init_dis_detected_on_process);

    /* DIS corruption detection in error module */
    RUN_TEST(test_corrupted_critical_lock_dis_detected);

    /* SM_REQUIRE */
    RUN_TEST(test_require_fires_with_correct_module_and_id);
    RUN_TEST(test_require_true_does_not_fire);

    /* Bounded loops */
    RUN_TEST(test_bounded_loop_within_bound_no_assert);
    RUN_TEST(test_bounded_loop_exhausted_fires_invariant);

    return UNITY_END();
}
