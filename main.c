#include "app_main.h"

#ifdef USE_SIMULATION
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("Starting State Machine\n");

    if (!App_Main_Init())
    {
        printf("ERROR: Init failed\n");
        return -1;
    }

    printf("Initialized successfully\n");

    for (int i = 0; i < 1000; i++)
    {
        App_Main_Task();
        usleep(10000);
        if ((i % 100) == 0)
        {
            printf("Iteration %d - State: %s\n", i, StateToString(StateMachine_GetCurrentState()));
        }
    }

    printf("Simulation complete\n");
    return 0;
}
#endif

#ifdef USE_BARE_METAL
int main(void)
{
    App_Main_Init();
    while (1)
    {
        App_Main_Task();
    }
}
#endif

#ifdef USE_FREERTOS
void AppTask(void *pvParameters)
{
    App_Main_Init();
    while (1)
    {
        App_Main_Task();
    }
}

int main(void)
{
    while (1)
    {
    }
}
#endif
