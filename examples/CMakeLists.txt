# Examples for State Machine Framework v3.0
#
# Each example defines SM_STATE_COUNT and SM_EVENT_COUNT in its source via
# #define before #include "sm_framework/sm_framework.h". These override the
# #ifndef defaults because the #define in the .c file occurs before the header
# include chain.
#
# Note: The library is compiled with default SM_STATE_COUNT/SM_EVENT_COUNT
# (see root CMakeLists.txt). Struct layouts match as long as SM_FEATURE_STATISTICS
# is off (default), since SM_STATE_COUNT only sizes the stats array.

# Basic example: 3-state FSM
add_executable(basic_example
    basic_example.c
)

target_link_libraries(basic_example PRIVATE
    sm_framework
)

# Simulation example: real timing + error reporting
add_executable(simulation_example
    simulation_example.c
)

target_link_libraries(simulation_example PRIVATE
    sm_framework
)
