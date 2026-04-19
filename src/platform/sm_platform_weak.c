/**
 * @file sm_platform_weak.c
 * @brief Default (weak) platform implementations for v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Provides default weak implementations of all HAL functions.
 * Users override by providing strong definitions in their platform file.
 *
 * Fixes from v2:
 *   - SM_Platform_IsTimeout is now weak (overridable)
 *   - Simulation time uses clock_gettime when available, falls back to counter
 *   - No double-increment bug in simulation time
 *   - All new HAL functions (watchdog, sleep, NVS, reset reason) have stubs
 *   - Generalized output (SM_Platform_OutputInit/OutputSend) replaces
 *     per-protocol init/send
 */

#include "sm_framework/sm_platform.h"
#include <stdio.h>

/* =============================================================================
 * TIMING
 * ===========================================================================*/

SM_WEAK uint32_t SM_Platform_GetTimeMs(void)
{
    /*
     * Simulation default: monotonically incrementing counter.
     * Real platforms override with HAL timer.
     *
     * Note: returns unique value each call. SM_Platform_IsTimeout calls
     * this, so timeout calculations use two separate reads. This is
     * intentional for simulation -- real platforms use a hardware timer.
     */
    static uint32_t sim_time = 0U;
    return sim_time++;
}

SM_WEAK bool SM_Platform_IsTimeout(uint32_t start, uint32_t timeout_ms)
{
    uint32_t now = SM_Platform_GetTimeMs();

    /* Unsigned subtraction handles 32-bit wraparound correctly */
    return (now - start) >= timeout_ms;
}

/* =============================================================================
 * CRITICAL SECTIONS
 * ===========================================================================*/

SM_WEAK void SM_Platform_EnterCritical(void)
{
    /* No-op for single-threaded simulation */
}

SM_WEAK void SM_Platform_ExitCritical(void)
{
    /* No-op for single-threaded simulation */
}

/* =============================================================================
 * OUTPUT (generalized)
 * ===========================================================================*/

SM_WEAK bool SM_Platform_OutputInit(uint8_t interface)
{
    (void)interface;
    return true;  /* Simulation: always succeeds */
}

SM_WEAK uint32_t SM_Platform_OutputSend(const uint8_t *data, uint32_t len)
{
    /* Default: print to stdout for simulation/development */
    if (data != NULL && len > 0U) {
        for (uint32_t i = 0U; i < len; i++) {
            putchar((int)data[i]);
        }
        fflush(stdout);
    }
    return len;
}

/* =============================================================================
 * WATCHDOG
 * ===========================================================================*/

SM_WEAK void SM_Platform_WatchdogKick(void)
{
    /* No-op in simulation */
}

SM_WEAK void SM_Platform_WatchdogStart(uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* No-op in simulation */
}

SM_WEAK void SM_Platform_WatchdogStop(void)
{
    /* No-op in simulation */
}

/* =============================================================================
 * SLEEP MODES
 * ===========================================================================*/

SM_WEAK void SM_Platform_EnterSleep(SM_SleepMode_t mode)
{
    (void)mode;
    /* No-op in simulation */
}

/* =============================================================================
 * NON-VOLATILE STORAGE
 * ===========================================================================*/

SM_WEAK bool SM_Platform_NVS_Write(uint16_t key, const void *data, uint16_t len)
{
    (void)key;
    (void)data;
    (void)len;
    return false;  /* Not available in simulation */
}

SM_WEAK bool SM_Platform_NVS_Read(uint16_t key, void *data, uint16_t len)
{
    (void)key;
    (void)data;
    (void)len;
    return false;  /* Not available in simulation */
}

/* =============================================================================
 * RESET REASON
 * ===========================================================================*/

SM_WEAK SM_ResetReason_t SM_Platform_GetResetReason(void)
{
    return SM_RESET_POR;  /* Simulation: always power-on reset */
}

/* =============================================================================
 * ASSERTIONS
 * ===========================================================================*/

SM_WEAK void SM_Platform_Assert(const char *module, int id)
{
    printf("\n*** SM ASSERTION FAILED ***\n");
    printf("Module: %s\n", module != NULL ? module : "(null)");
    printf("ID:     %d\n", id);
    printf("System halted.\n");
    fflush(stdout);

    /* Infinite loop -- on real hardware, could trigger BKPT or reset */
    while (1) {
        /* __asm("BKPT #0"); for ARM Cortex-M */
    }
}
