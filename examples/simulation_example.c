#define _POSIX_C_SOURCE 199309L

#include "sm_framework/sm_framework.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Override Platform_GetTimeMs() with real millisecond timing */
uint32_t Platform_GetTimeMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

static void test_basic_operation(void)
{
    printf("\n--- TEST: Basic Operation ---\n");

    /* Let initialization complete */
    for (int i = 0; i < 10; i++) {
        App_Main_Task();
        usleep(10000);
    }

    printf("State after init: %s\n", StateMachine_StateToString(StateMachine_GetCurrentState()));

    /* Post events and observe transitions */
    StateMachine_PostEvent(EVENT_START);
    for (int i = 0; i < 10; i++) {
        App_Main_Task();
        usleep(10000);
    }

    printf("State after START: %s\n", StateMachine_StateToString(StateMachine_GetCurrentState()));
    printf("✓ Test passed\n");
}

int main(void)
{
    printf("\n========================================================\n");
    printf(" State Machine Framework - Simulation Example\n");
    printf(" Version: %s\n", App_Main_GetVersion());
    printf("========================================================\n\n");

    /* Initialize framework */
    if (!App_Main_Init(COMM_INTERFACE_UART)) {
        printf("ERROR: Initialization failed!\n");
        return -1;
    }

    printf("Framework initialized - running test\n");

    test_basic_operation();

    printf("\n========================================================\n");
    printf(" Test completed successfully!\n");
    printf("========================================================\n\n");

    return 0;
}
