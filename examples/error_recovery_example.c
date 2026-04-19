/**
 * @file error_recovery_example.c
 * @brief Error recovery example -- 3-tier error handling system (v3.0)
 * @version 3.0.0
 * @date 2026-04-19
 *
 * Demonstrates the v3.0 error handling subsystem:
 *   - SM_Error_Report at MINOR, NORMAL, and CRITICAL levels
 *   - Recovery callback with degrading success
 *   - Notification callback for all error events
 *   - SM_Error_AttemptRecovery with retry exhaustion (SM_ERROR_MAX_RECOVERY)
 *   - SM_Error_IsCriticalLock and DIS-protected system lock
 *   - SM_Error_GetStats for per-level error counts
 *   - SM_Error_GetHistory walking the circular history buffer
 *   - SM_Process skips dispatch when critical_lock is active
 *
 * States: NORMAL -> DEGRADED -> FAULT
 * Events: EVT_RUN, EVT_WARN, EVT_FAIL, EVT_RECOVER
 *
 * Scenario (4 phases):
 *   Phase 1 -- MINOR error, auto-clear, continue running
 *   Phase 2 -- NORMAL errors, recovery callback, retry exhaustion
 *   Phase 3 -- CRITICAL error, system lock
 *   Phase 4 -- Statistics and history inspection
 */

/* --- Application configuration (must come before framework headers) --- */

#define SM_STATE_COUNT    (3U)
#define SM_EVENT_COUNT    (4U)
#define SM_DEBUG_LEVEL    (4U)

#include "sm_framework/sm_framework.h"
#include <stdio.h>

/* =============================================================================
 * USER-DEFINED STATES AND EVENTS
 * ===========================================================================*/

typedef enum {
    STATE_NORMAL = 0,
    STATE_DEGRADED,
    STATE_FAULT
    /* SM_STATE_COUNT = 3 defined above */
} AppState_t;

typedef enum {
    EVT_RUN = 0,
    EVT_WARN,
    EVT_FAIL,
    EVT_RECOVER
    /* SM_EVENT_COUNT = 4 defined above */
} AppEvent_t;

/* =============================================================================
 * STATE CALLBACKS
 * ===========================================================================*/

static void on_normal_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [NORMAL] Entry -- system operating normally\n");
}

static void on_normal_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Nominal processing */
}

static void on_normal_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [NORMAL] Exit\n");
}

static void on_degraded_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [DEGRADED] Entry -- reduced capability\n");
}

static void on_degraded_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Degraded-mode processing */
}

static void on_degraded_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [DEGRADED] Exit\n");
}

static void on_fault_entry(SM_Handle_t sm)
{
    (void)sm;
    printf("  [FAULT] Entry -- system faulted, awaiting recovery\n");
}

static void on_fault_execute(SM_Handle_t sm)
{
    (void)sm;
    /* Fault-mode processing (minimal) */
}

static void on_fault_exit(SM_Handle_t sm)
{
    (void)sm;
    printf("  [FAULT] Exit\n");
}

/* =============================================================================
 * STATE DESCRIPTORS (const, flash)
 * ===========================================================================*/

static const SM_StateDesc_t app_states[SM_STATE_COUNT] = {
    [STATE_NORMAL] = {
        .on_entry   = on_normal_entry,
        .on_execute = on_normal_execute,
        .on_exit    = on_normal_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_DEGRADED] = {
        .on_entry   = on_degraded_entry,
        .on_execute = on_degraded_execute,
        .on_exit    = on_degraded_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
    [STATE_FAULT] = {
        .on_entry   = on_fault_entry,
        .on_execute = on_fault_execute,
        .on_exit    = on_fault_exit,
        .timeout_ms = 0U,
        .min_dwell_ms = 0U,
    },
};

/* =============================================================================
 * TRANSITION TABLE (const, flash)
 * ===========================================================================*/

static const SM_Transition_t app_transitions[] = {
    /* NORMAL --EVT_WARN--> DEGRADED */
    { .from_state = STATE_NORMAL,   .event = EVT_WARN,    .to_state = STATE_DEGRADED,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* DEGRADED --EVT_FAIL--> FAULT */
    { .from_state = STATE_DEGRADED, .event = EVT_FAIL,    .to_state = STATE_FAULT,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* DEGRADED --EVT_RECOVER--> NORMAL */
    { .from_state = STATE_DEGRADED, .event = EVT_RECOVER, .to_state = STATE_NORMAL,
      ._reserved = 0, .guard = NULL, .action = NULL },

    /* FAULT --EVT_RECOVER--> NORMAL */
    { .from_state = STATE_FAULT,    .event = EVT_RECOVER, .to_state = STATE_NORMAL,
      ._reserved = 0, .guard = NULL, .action = NULL },
};

/* =============================================================================
 * ERROR CALLBACKS
 * ===========================================================================*/

/**
 * @brief Recovery callback -- degrades over repeated calls
 *
 * Simulates a system whose ability to recover degrades over time.
 * Succeeds on the first 2 calls, fails on the 3rd and beyond.
 */
static bool my_recovery_cb(SM_Handle_t sm, uint16_t error_code)
{
    static int recovery_attempt = 0;
    (void)sm;

    recovery_attempt++;
    printf("    recovery_cb: attempt #%d for error code %u",
           recovery_attempt, (unsigned)error_code);

    if (recovery_attempt < 3) {
        printf(" -> SUCCESS\n");
        return true;
    }

    printf(" -> FAILED (recovery degraded)\n");
    return false;
}

/**
 * @brief Notification callback -- logs every error event
 */
static void my_notify_cb(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code)
{
    (void)sm;

    const char *level_str = "UNKNOWN";
    switch (level) {
        case SM_ERROR_MINOR:    level_str = "MINOR";    break;
        case SM_ERROR_NORMAL:   level_str = "NORMAL";   break;
        case SM_ERROR_CRITICAL: level_str = "CRITICAL"; break;
        default:                                        break;
    }

    printf("    notify_cb: level=%s code=%u\n", level_str, (unsigned)code);
}

/* =============================================================================
 * HELPER: print a divider between phases
 * ===========================================================================*/

static void print_phase(int phase, const char *title)
{
    printf("\n");
    printf("------------------------------------------------\n");
    printf(" Phase %d: %s\n", phase, title);
    printf("------------------------------------------------\n");
}

/* =============================================================================
 * MAIN
 * ===========================================================================*/

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf(" State Machine Framework v%s\n", SM_FRAMEWORK_VERSION_STRING);
    printf(" Error Recovery Example\n");
    printf("================================================\n");

    /* Initialize debug output */
    SM_Debug_Init(0U);

    /* Static allocation of state machine context */
    SM_Context_t sm_ctx;

    /* Configuration */
    SM_Config_t config = {
        .states           = app_states,
        .transitions      = app_transitions,
        .transition_count = (uint16_t)(sizeof(app_transitions) / sizeof(app_transitions[0])),
        .initial_state    = STATE_NORMAL,
    };

    /* Initialize */
    if (!SM_Init(&sm_ctx, &config)) {
        printf("ERROR: SM_Init failed!\n");
        return -1;
    }

    SM_Handle_t sm = &sm_ctx;

    /* Register error callbacks */
    SM_Error_RegisterRecoveryCallback(sm, my_recovery_cb);
    SM_Error_RegisterNotifyCallback(sm, my_notify_cb);
    printf("\nCallbacks registered (recovery + notify)\n");

    /* =====================================================================
     * Phase 1: MINOR Error -- Auto-Recovery
     * ===================================================================*/

    print_phase(1, "Minor Error (Auto-Recovery)");

    /* Run normally for 5 cycles */
    printf("\nRunning 5 normal cycles...\n");
    for (int i = 0; i < 5; i++) {
        SM_Process(sm);
    }
    printf("  5 cycles complete, state=%u (NORMAL)\n", (unsigned)SM_GetState(sm));

    /* Report a minor error */
    printf("\nReporting MINOR error (code 100)...\n");
    SM_Error_Report(sm, SM_ERROR_MINOR, 100U);

    /* Minor errors are informational -- clear and continue */
    printf("Clearing minor error...\n");
    SM_Error_Clear(sm);

    /* Run 3 more cycles to show normal operation resumes */
    printf("\nRunning 3 more cycles after clear...\n");
    for (int i = 0; i < 3; i++) {
        SM_Process(sm);
    }
    printf("  3 cycles complete, state=%u (NORMAL)\n", (unsigned)SM_GetState(sm));

    /* =====================================================================
     * Phase 2: NORMAL Error -- Managed Recovery with Retry Exhaustion
     * ===================================================================*/

    print_phase(2, "Normal Error + Recovery");

    /* Transition to DEGRADED so the state reflects the error condition */
    SM_PostEvent(sm, (uint16_t)EVT_WARN, 0U);
    SM_Process(sm);
    printf("  Transitioned to state=%u (DEGRADED)\n", (unsigned)SM_GetState(sm));

    /* Report a NORMAL error (code 200) -- recovery callback will succeed */
    printf("\nReporting NORMAL error (code 200)...\n");
    SM_Error_Report(sm, SM_ERROR_NORMAL, 200U);

    printf("Attempting recovery (should succeed -- attempt #1)...\n");
    bool recovered = SM_Error_AttemptRecovery(sm);
    printf("  Result: %s\n", recovered ? "RECOVERED" : "FAILED");

    if (recovered) {
        SM_Error_Clear(sm);
        printf("  Error cleared after successful recovery\n");
    }

    /* Report another NORMAL error (code 201) -- exhaust retries */
    printf("\nReporting NORMAL error (code 201)...\n");
    SM_Error_Report(sm, SM_ERROR_NORMAL, 201U);

    printf("Attempting recovery up to %u times (SM_ERROR_MAX_RECOVERY=%u)...\n",
           (unsigned)(SM_ERROR_MAX_RECOVERY + 1U), (unsigned)SM_ERROR_MAX_RECOVERY);

    /*
     * SM_Error_AttemptRecovery increments retry_count BEFORE checking.
     * retry_count starts at 0. On each call:
     *   Call 1: retry_count=1, < MAX(3), calls callback (attempt #2 -> SUCCESS)
     *   Call 2: retry_count=2, < MAX(3), calls callback (attempt #3 -> FAILED)
     *   Call 3: retry_count=3, >= MAX(3), returns false immediately (max exceeded)
     *   Call 4: retry_count=4, >= MAX(3), returns false immediately (max exceeded)
     */
    for (uint32_t attempt = 1; attempt <= 4U; attempt++) {
        recovered = SM_Error_AttemptRecovery(sm);
        printf("  Attempt %u: %s\n", (unsigned)attempt,
               recovered ? "RECOVERED" : "FAILED");
    }

    /* =====================================================================
     * Phase 3: CRITICAL Error -- System Lock
     * ===================================================================*/

    print_phase(3, "Critical Error (System Lock)");

    printf("\nReporting CRITICAL error (code 300)...\n");
    SM_Error_Report(sm, SM_ERROR_CRITICAL, 300U);

    bool locked = SM_Error_IsCriticalLock(sm);
    printf("  SM_Error_IsCriticalLock: %s\n", locked ? "TRUE (system locked)" : "FALSE");

    /* Try to run SM_Process -- should skip all processing */
    printf("\nCalling SM_Process while critical_lock is active...\n");
    uint16_t state_before = SM_GetState(sm);
    SM_Process(sm);
    uint16_t state_after = SM_GetState(sm);
    printf("  State before: %u, after: %u (unchanged = processing skipped)\n",
           (unsigned)state_before, (unsigned)state_after);

    /* Try to post an event -- should still enqueue, but Process won't run */
    printf("\nPosting EVT_RECOVER while locked...\n");
    bool posted = SM_PostEvent(sm, (uint16_t)EVT_RECOVER, 0U);
    printf("  SM_PostEvent returned: %s\n", posted ? "true (queued)" : "false");

    SM_Process(sm);
    printf("  SM_Process after post: state=%u (still unchanged)\n",
           (unsigned)SM_GetState(sm));

    /* =====================================================================
     * Phase 4: Statistics & History
     * ===================================================================*/

    print_phase(4, "Statistics & History");

    /* Error statistics */
    SM_ErrorStats_t stats;
    if (SM_Error_GetStats(sm, &stats)) {
        printf("\nError Statistics:\n");
        printf("  MINOR  errors: %lu\n", (unsigned long)stats.errors_by_level[SM_ERROR_MINOR]);
        printf("  NORMAL errors: %lu\n", (unsigned long)stats.errors_by_level[SM_ERROR_NORMAL]);
        printf("  CRITICAL errors: %lu\n", (unsigned long)stats.errors_by_level[SM_ERROR_CRITICAL]);
        printf("  Recovery successes: %lu\n", (unsigned long)stats.recovery_success);
        printf("  Recovery failures:  %lu\n", (unsigned long)stats.recovery_fail);
        printf("  Last error time: %lu ms\n", (unsigned long)stats.last_error_time);
    }

    /* Error history */
    uint8_t history_count = SM_Error_GetHistoryCount(sm);
    printf("\nError History (%u entries, most recent first):\n", (unsigned)history_count);

    for (uint8_t i = 0; i < history_count; i++) {
        SM_ErrorInfo_t info;
        if (SM_Error_GetHistory(sm, i, &info)) {
            const char *lvl = "???";
            switch (info.level) {
                case SM_ERROR_MINOR:    lvl = "MINOR";    break;
                case SM_ERROR_NORMAL:   lvl = "NORMAL";   break;
                case SM_ERROR_CRITICAL: lvl = "CRITICAL"; break;
                default:                                  break;
            }
            printf("  [%u] level=%-8s code=%-5u state=%-2u time=%lu ms\n",
                   (unsigned)i, lvl, (unsigned)info.code,
                   (unsigned)info.state, (unsigned long)info.timestamp);
        }
    }

    printf("\n================================================\n");
    printf(" Example completed!\n");
    printf(" Final state: %u, Critical lock: %s\n",
           (unsigned)SM_GetState(sm),
           SM_Error_IsCriticalLock(sm) ? "ACTIVE" : "inactive");
    printf("================================================\n\n");

    return 0;
}
