cmake_minimum_required(VERSION 3.15)

# =============================================================================
# UNITY TEST FRAMEWORK (via FetchContent)
# =============================================================================

include(FetchContent)

FetchContent_Declare(
    unity
    URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz
    URL_HASH MD5=12e245d902fb33d0187b2a55a7769b89
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(unity)

# Build Unity as a simple library from its source
add_library(unity_lib STATIC ${unity_SOURCE_DIR}/src/unity.c)
target_include_directories(unity_lib PUBLIC ${unity_SOURCE_DIR}/src)

# =============================================================================
# TEST-CONFIGURED FRAMEWORK LIBRARY
#
# Recompiles the framework sources with ALL features enabled and links
# against test_platform.c instead of sm_platform_weak.c. This avoids
# duplicate-symbol issues on Windows (SM_WEAK is empty on PE/COFF).
# =============================================================================

set(SM_TEST_DEFS
    SM_STATE_COUNT=4U
    SM_EVENT_COUNT=8U
    SM_EVENT_QUEUE_SIZE=8U
    SM_DEFER_QUEUE_SIZE=4U
    SM_ERROR_HISTORY_SIZE=8U
    SM_ERROR_MAX_RECOVERY=3U
    SM_STATE_HISTORY_DEPTH=4U
    SM_MAX_TRANSITIONS=32U
    SM_FEATURE_HSM=0U
    SM_FEATURE_RUNTIME_TRANSITIONS=1U
    SM_FEATURE_STATISTICS=1U
    SM_FEATURE_DEBUG=1U
    SM_FEATURE_ASSERT=1U
    SM_FEATURE_TIME_EVENTS=1U
    SM_FEATURE_DEFER=1U
    SM_FEATURE_MAX_TIME_EVENTS=16U
    SM_DEBUG_LEVEL=4U
    SM_DEBUG_BUFFER_SIZE=256U
    SM_DEBUG_MSG_MAX_LEN=128U
)

add_library(sm_framework_test STATIC
    ${CMAKE_SOURCE_DIR}/src/core/sm_engine.c
    ${CMAKE_SOURCE_DIR}/src/core/sm_error.c
    ${CMAKE_SOURCE_DIR}/src/core/sm_debug.c
    test_platform.c
)

target_include_directories(sm_framework_test PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(sm_framework_test PUBLIC ${SM_TEST_DEFS})

# =============================================================================
# TEST HELPER FUNCTION
# =============================================================================

function(add_sm_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE sm_framework_test unity_lib)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# =============================================================================
# TEST EXECUTABLES
# =============================================================================

add_sm_test(test_event_queue  test_event_queue.c)
add_sm_test(test_engine       test_engine.c)
add_sm_test(test_time_events  test_time_events.c)
add_sm_test(test_deferred     test_deferred.c)
add_sm_test(test_error        test_error.c)
add_sm_test(test_debug        test_debug.c)
add_sm_test(test_safety       test_safety.c)
add_sm_test(test_hal          test_hal.c)
add_sm_test(test_integration  test_integration.c)

# =============================================================================
# SMGEN MODEL TOOLING TESTS (Python, optional -- skipped without Python 3.11+)
# =============================================================================

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND NOT Python3_VERSION VERSION_LESS "3.11")
    add_test(NAME test_smgen_unit
             COMMAND ${Python3_EXECUTABLE} -m unittest discover
                     -s ${CMAKE_SOURCE_DIR}/smgen/tests -t ${CMAKE_SOURCE_DIR})
    add_test(NAME test_smgen_models
             COMMAND ${Python3_EXECUTABLE} -m smgen validate --strict
                     ${CMAKE_SOURCE_DIR}/models/blinky.toml
                     ${CMAKE_SOURCE_DIR}/models/basic.toml
                     ${CMAKE_SOURCE_DIR}/models/sensor_pipeline.toml
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # graphify: extractor unit tests + real-repo invariants (state writer
    # set, ISR contracts, model round-trip, no call cycles, G-check plumbing)
    add_test(NAME test_graphify_unit
             COMMAND ${Python3_EXECUTABLE} -m unittest discover
                     -s ${CMAKE_SOURCE_DIR}/graphify/tests -t ${CMAKE_SOURCE_DIR}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_sm_test(test_rt_transition    test_rt_transition.c)
add_sm_test(test_post_event_guards test_post_event_guards.c)
add_sm_test(test_error_null_api    test_error_null_api.c)
add_sm_test(test_stats_bounds      test_stats_bounds.c)
add_sm_test(test_state_query_null  test_state_query_null.c)
add_sm_test(test_reset_extras      test_reset_extras.c)
add_sm_test(test_process_null      test_process_null.c)
add_sm_test(test_queue_null_api    test_queue_null_api.c)
add_sm_test(test_debug_null_safe   test_debug_null_safe.c)
add_sm_test(test_recovery_edges    test_recovery_edges.c)

add_sm_test(test_lifecycle_hardening test_lifecycle_hardening.c)
