# Examples for State Machine Framework v4.1
#
# Examples do NOT define SM_STATE_COUNT / SM_EVENT_COUNT themselves. Those are
# compile-time constants baked into the sm_framework library, and an
# application compiled with different values than the library it links against
# disagrees with it about SM_Init's range check, SM_PostEvent's accept range
# and (with statistics enabled) SM_Context_t's layout. The root CMakeLists.txt
# sets them once and propagates them PUBLIC through sm_framework, so linking is
# enough to inherit the correct values; SM_Init verifies the match at runtime.
#
# Each example's own state/event enums are smaller than the build-wide bounds,
# which is fine -- the bounds are a ceiling, not an exact size.

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

# Blinky example: timer events
add_executable(blinky_example
    blinky_example.c
)

target_link_libraries(blinky_example PRIVATE
    sm_framework
)

# Sensor pipeline: guard conditions
add_executable(sensor_pipeline_example
    sensor_pipeline_example.c
)

target_link_libraries(sensor_pipeline_example PRIVATE
    sm_framework
)

# Error recovery: 3-tier error handling
add_executable(error_recovery_example
    error_recovery_example.c
)

target_link_libraries(error_recovery_example PRIVATE
    sm_framework
)

# Multi-FSM: two independent state machine instances
add_executable(multi_fsm_example
    multi_fsm_example.c
)

target_link_libraries(multi_fsm_example PRIVATE
    sm_framework
)
