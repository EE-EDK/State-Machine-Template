cmake_minimum_required(VERSION 3.15)

project(StateMachineFramework
    VERSION 2.0.0
    DESCRIPTION "Modular State Machine Framework for Embedded Systems"
    LANGUAGES C
)

# C Standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Options
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_STATISTICS "Enable runtime statistics collection" OFF)
option(ENABLE_ASSERTS "Enable runtime assertions" ON)

# Platform selection (default to simulation)
set(SM_PLATFORM "SIMULATION" CACHE STRING "Target platform (SIMULATION, STM32, ESP32, RP2040, CUSTOM)")
set_property(CACHE SM_PLATFORM PROPERTY STRINGS "SIMULATION" "STM32" "ESP32" "RP2040" "CUSTOM")

# Configuration
if(ENABLE_STATISTICS)
    add_compile_definitions(FEATURE_STATISTICS_ENABLED=1)
else()
    add_compile_definitions(FEATURE_STATISTICS_ENABLED=0)
endif()

if(ENABLE_ASSERTS)
    add_compile_definitions(FEATURE_ASSERT_ENABLED=1)
else()
    add_compile_definitions(FEATURE_ASSERT_ENABLED=0)
endif()

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
    )
endif()

if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4)
endif()

# Optimization for embedded
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Os)  # Optimize for size
    endif()
endif()

# =============================================================================
# CORE LIBRARY
# =============================================================================

add_library(sm_framework STATIC
    # Core modules
    src/core/sm_state_machine.c
    src/core/sm_error_handler.c
    src/core/sm_debug.c

    # Platform abstraction (weak symbols - can be overridden)
    src/platform/sm_platform_weak.c

    # Application glue
    src/app/app_main.c
)

target_include_directories(sm_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Platform-specific compilation
if(SM_PLATFORM STREQUAL "STM32")
    message(STATUS "Building for STM32 platform")
    # User must provide their own platform implementation
    # and link with HAL libraries

elseif(SM_PLATFORM STREQUAL "ESP32")
    message(STATUS "Building for ESP32 platform")
    # User must provide their own platform implementation

elseif(SM_PLATFORM STREQUAL "RP2040")
    message(STATUS "Building for RP2040 platform")
    # User must provide their own platform implementation

elseif(SM_PLATFORM STREQUAL "SIMULATION")
    message(STATUS "Building for SIMULATION platform (default weak implementations)")
    # Uses default weak symbol implementations

elseif(SM_PLATFORM STREQUAL "CUSTOM")
    message(STATUS "Building for CUSTOM platform")
    # User must provide their own platform implementation

endif()

# =============================================================================
# EXAMPLES
# =============================================================================

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# =============================================================================
# TESTS
# =============================================================================

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# INSTALL
# =============================================================================

install(TARGETS sm_framework
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/sm_framework
    DESTINATION include
)

# =============================================================================
# STATUS SUMMARY
# =============================================================================

message(STATUS "")
message(STATUS "=== State Machine Framework Configuration ===")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "Platform:       ${SM_PLATFORM}")
message(STATUS "Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler:     ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Statistics:     ${ENABLE_STATISTICS}")
message(STATUS "Asserts:        ${ENABLE_ASSERTS}")
message(STATUS "Examples:       ${BUILD_EXAMPLES}")
message(STATUS "Tests:          ${BUILD_TESTS}")
message(STATUS "==========================================")
message(STATUS "")
