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
# add_sm_test(test_time_events  test_time_events.c)  # Phase 6 TODO
add_sm_test(test_deferred     test_deferred.c)
add_sm_test(test_error        test_error.c)
add_sm_test(test_debug        test_debug.c)
add_sm_test(test_safety       test_safety.c)
add_sm_test(test_hal          test_hal.c)
# add_sm_test(test_integration  test_integration.c)  # Phase 6 TODO
