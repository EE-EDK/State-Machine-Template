# Examples

add_executable(basic_example
    basic_example.c
)

target_link_libraries(basic_example PRIVATE
    sm_framework
)

# Simulation example with custom timing
add_executable(simulation_example
    simulation_example.c
)

target_link_libraries(simulation_example PRIVATE
    sm_framework
)
