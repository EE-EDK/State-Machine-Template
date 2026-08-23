cmake_minimum_required(VERSION 3.15)

project(StateMachineFramework
    VERSION 4.1.0
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

# =============================================================================
# APPLICATION DIMENSIONS -- one value per build, shared by library and apps
#
# SM_STATE_COUNT / SM_EVENT_COUNT are compile-time constants baked into the
# compiled library: they drive SM_Init's initial-state range check,
# SM_PostEvent's accept range, and the SM_Stats_t per-state array (so they
# also change SM_Context_t's layout when SM_FEATURE_STATISTICS is on).
#
# v4.1: they are set ONCE here and propagated PUBLIC to every target that
# links sm_framework, so the library and its applications cannot disagree.
# Before v4.1 the library was compiled with fixed values while each example
# re-#defined its own -- the two silently diverged, and SM_Init would reject
# valid initial states while SM_PostEvent accepted out-of-range events.
# Applications must NOT re-#define these in their sources; set them here (or
# via -DSM_STATE_COUNT=... at configure time) for the whole build.
# =============================================================================
set(SM_STATE_COUNT "4" CACHE STRING "Number of application states (1..255)")
set(SM_EVENT_COUNT "8" CACHE STRING "Number of application events (1..65535)")

set(SM_DIMENSION_DEFS
    SM_STATE_COUNT=${SM_STATE_COUNT}U
    SM_EVENT_COUNT=${SM_EVENT_COUNT}U
)

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

target_compile_definitions(sm_platform_defaults PUBLIC ${SM_DIMENSION_DEFS})

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

# PUBLIC, not PRIVATE: every target that links sm_framework compiles with the
# SAME dimensions as the library itself (see the block near the top of this
# file). SM_Init additionally verifies this at runtime -- a consumer built
# against an installed archive with different values is rejected instead of
# silently misbehaving.
target_compile_definitions(sm_framework PUBLIC ${SM_DIMENSION_DEFS})

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
