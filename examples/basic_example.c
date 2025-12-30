/**
 * @file basic_example.c
 * @brief Basic example showing framework usage
 * @version 2.0.0
 *
 * This example demonstrates:
 * - Initializing the framework
 * - Running the state machine in a main loop
 * - Posting events from application code
 */

#include "sm_framework/sm_framework.h"
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf(" State Machine Framework - Basic Example\n");
    printf(" Version: %s\n", App_Main_GetVersion());
    printf("================================================\n\n");

    /* Initialize framework with UART debug output */
    if (!App_Main_Init(COMM_INTERFACE_UART)) {
        printf("ERROR: Initialization failed!\n");
        return -1;
    }

    printf("\nRunning state machine for 100 iterations...\n\n");

    /* Main loop - run for demo purposes */
    for (int i = 0; i < 100; i++) {
        /* Execute state machine task */
        App_Main_Task();

        /* Simulate periodic execution (10ms period) */
        usleep(10000);  /* 10ms */

        /* Example: Post events based on iteration */
        if (i == 20) {
            printf("\n>>> Posting EVENT_START\n\n");
            StateMachine_PostEvent(EVENT_START);
        }

        if (i == 40) {
            printf("\n>>> Posting EVENT_DATA_READY\n\n");
            StateMachine_PostEvent(EVENT_DATA_READY);
        }

        if (i == 80) {
            printf("\n>>> Posting EVENT_STOP\n\n");
            StateMachine_PostEvent(EVENT_STOP);
        }

        /* Print status every 10 iterations */
        if ((i % 10) == 0 && i > 0) {
            printf("\n[Iteration %d] Current State: %s\n",
                   i, StateMachine_StateToString(StateMachine_GetCurrentState()));
        }
    }

    printf("\n================================================\n");
    printf(" Example completed successfully!\n");
    printf(" Final State: %s\n",
           StateMachine_StateToString(StateMachine_GetCurrentState()));
    printf("================================================\n\n");

    return 0;
}
