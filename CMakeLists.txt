cmake_minimum_required(VERSION 3.15)

project(StateMachineFramework
    VERSION 3.0.0
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

# Platform selection (default to simulation)
set(SM_PLATFORM "SIMULATION" CACHE STRING "Target platform (SIMULATION, STM32, ESP32, RP2040, CUSTOM)")
set_property(CACHE SM_PLATFORM PROPERTY STRINGS "SIMULATION" "STM32" "ESP32" "RP2040" "CUSTOM")

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

# =============================================================================
# CORE LIBRARY
#
# Includes platform weak defaults directly. On ELF targets (ARM, Linux),
# weak symbols allow user overrides via strong definitions in separate .o
# files. On PE/COFF targets (MinGW/Windows), SM_WEAK is defined as empty
# (see sm_platform.h) so the defaults are strong -- users override by
# compiling the framework as part of their project (add_subdirectory) and
# excluding sm_platform_weak.c, or by not linking sm_platform_defaults
# (see below).
# =============================================================================

# Platform defaults as a separate static library so they can be excluded
# when the user provides their own platform implementation.
add_library(sm_platform_defaults STATIC
    src/platform/sm_platform_weak.c
)

target_include_directories(sm_platform_defaults PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(sm_platform_defaults PRIVATE
    SM_STATE_COUNT=4U
    SM_EVENT_COUNT=8U
)

# Core framework (without platform layer)
add_library(sm_framework STATIC
    src/core/sm_engine.c
    src/core/sm_error.c
    src/core/sm_debug.c
    src/app/app_main.c
)

target_include_directories(sm_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The library requires SM_STATE_COUNT and SM_EVENT_COUNT at compile time
# (they size arrays in sm_types.h). Provide defaults for library compilation.
# Examples and user projects override these with their own values via
# #define in their source before #include "sm_framework/sm_framework.h".
target_compile_definitions(sm_framework PRIVATE
    SM_STATE_COUNT=4U
    SM_EVENT_COUNT=8U
)

# Link platform defaults so downstream targets get them automatically.
# Users who provide their own platform can link sm_framework without
# sm_platform_defaults.
target_link_libraries(sm_framework PUBLIC
    sm_platform_defaults
)


# Platform-specific compilation
if(SM_PLATFORM STREQUAL "STM32")
    message(STATUS "Building for STM32 platform")
elseif(SM_PLATFORM STREQUAL "ESP32")
    message(STATUS "Building for ESP32 platform")
elseif(SM_PLATFORM STREQUAL "RP2040")
    message(STATUS "Building for RP2040 platform")
elseif(SM_PLATFORM STREQUAL "SIMULATION")
    message(STATUS "Building for SIMULATION platform (default weak implementations)")
elseif(SM_PLATFORM STREQUAL "CUSTOM")
    message(STATUS "Building for CUSTOM platform")
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
message(STATUS "Examples:       ${BUILD_EXAMPLES}")
message(STATUS "Tests:          ${BUILD_TESTS}")
message(STATUS "==========================================")
message(STATUS "")
