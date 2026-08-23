/**
 * @file test_common.h
 * @brief Shared test infrastructure for State Machine Framework v3.0
 *
 * Provides:
 *   - Test state/event enums matching SM_STATE_COUNT=4 / SM_EVENT_COUNT=8
 *   - Assert-capture macros (setjmp/longjmp) for verifying SM_REQUIRE fires
 *   - Simulation time reset helper
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

/* Test enums -- must match compile-time SM_STATE_COUNT=4, SM_EVENT_COUNT=8 */
enum TestStates {
    TEST_STATE_INIT    = 0,
    TEST_STATE_RUNNING = 1,
    TEST_STATE_STOPPED = 2,
    TEST_STATE_ERROR   = 3
};

enum TestEvents {
    TEST_EVT_START   = 0,
    TEST_EVT_STOP    = 1,
    TEST_EVT_RESET   = 2,
    TEST_EVT_ERROR   = 3,
    TEST_EVT_TIMEOUT = 4,
    TEST_EVT_DATA    = 5,
    TEST_EVT_ACK     = 6,
    TEST_EVT_CUSTOM  = 7
};

/* =============================================================================
 * ASSERT CAPTURE INFRASTRUCTURE
 *
 * SM_Platform_Assert normally loops forever. In test_platform.c, when
 * test_assert_expecting is true, it longjmps instead, so the test can
 * verify that an assertion fired with the correct module + ID.
 * ===========================================================================*/

extern jmp_buf test_assert_jmp_buf;
extern volatile bool test_assert_expecting;
extern volatile bool test_assert_fired;
extern const char *test_assert_module;
extern int test_assert_id;

/** Tell the assert handler to longjmp on next assertion instead of halting. */
void test_assert_expect(void);

/** Clear assert-capture state. Called in setUp(). */
void test_assert_clear(void);

/** Reset simulation time counter to 0. */
void test_sim_time_reset(void);

/* =============================================================================
 * LIBRARY-SIDE ABI PROBES (v4.2, W1)
 *
 * test_platform.c is compiled into sm_framework_test, so these report the
 * values the LIBRARY was built with. A test executable's own translation unit
 * reports its own. Comparing the two is how test_abi_guard demonstrates a real
 * application/library divergence instead of asserting a hardcoded constant.
 * ===========================================================================*/

/** sizeof(SM_Context_t) as the linked library sees it. */
uint32_t test_lib_sizeof_context(void);

/** SM_ABI_FINGERPRINT as the linked library computed it. */
uint32_t test_lib_abi_fingerprint(void);

/** SM_EVENT_QUEUE_SIZE the linked library was compiled with. */
uint32_t test_lib_event_queue_size(void);

/**
 * @brief Verify that a code block fires an assertion.
 *
 * Usage:
 *   TEST_EXPECT_ASSERT(SM_Init(NULL, NULL));
 *   TEST_ASSERT_EQUAL_STRING("sm_engine", test_assert_module);
 *   TEST_ASSERT_EQUAL_INT(100, test_assert_id);
 */
#define TEST_EXPECT_ASSERT(code_) \
    do { \
        test_assert_expect(); \
        if (setjmp(test_assert_jmp_buf) == 0) { \
            code_; \
            TEST_FAIL_MESSAGE("Expected assertion did not fire"); \
        } \
        /* If we get here via longjmp, assertion was captured */ \
    } while (0)

/**
 * @brief Verify that a code block does NOT fire an assertion.
 */
#define TEST_ASSERT_NO_ASSERT(code_) \
    do { \
        test_assert_expect(); \
        if (setjmp(test_assert_jmp_buf) == 0) { \
            code_; \
            /* Good -- no assertion fired */ \
            test_assert_clear(); \
        } else { \
            char msg[128]; \
            snprintf(msg, sizeof(msg), \
                     "Unexpected assertion: %s:%d", \
                     test_assert_module ? test_assert_module : "?", \
                     test_assert_id); \
            TEST_FAIL_MESSAGE(msg); \
        } \
    } while (0)

#endif /* TEST_COMMON_H */
