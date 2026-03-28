# CLAUDE.md — State Machine Framework

## Project Summary
Production-ready, modular state machine framework for embedded C systems. Platform-agnostic with weak-symbol HAL abstraction. Version 2.0.0, 100% complete.

## Directory Structure
```
state-machine-template/
├── include/sm_framework/   # Public API headers
│   ├── sm_framework.h      # Main include (umbrella header)
│   ├── sm_state_machine.h  # State machine API
│   ├── sm_error_handler.h  # 3-tier error handling API
│   ├── sm_debug.h          # Multi-interface debug system
│   ├── sm_platform.h       # Platform HAL interface (5 functions to implement)
│   ├── sm_types.h          # Type definitions and enums
│   └── sm_config.h         # Configuration defaults (overridable via app_config.h)
├── src/
│   ├── core/               # Platform-independent logic
│   │   ├── sm_state_machine.c
│   │   ├── sm_error_handler.c
│   │   └── sm_debug.c
│   ├── platform/
│   │   └── sm_platform_weak.c  # Default weak implementations
│   └── app/
│       └── app_main.c      # Application glue (init + task loop)
├── examples/
│   ├── basic_example.c
│   └── simulation_example.c
├── config/
│   └── app_config_template.h   # User config template
├── CMakeLists.txt           # Build system (cmake 3.15+)
├── App_Config_Template.h    # Legacy config template
├── app_main.c / app_main.h  # Root-level app entry (legacy convenience copies)
├── main.c                   # Minimal main() example
├── Quick-Guide.md           # Quick reference guide
└── README.md                # Full documentation
```

## Build Commands
```bash
# Standard build
mkdir build && cd build && cmake .. && make

# With options
cmake .. -DSM_PLATFORM=SIMULATION -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug

# Run examples
./examples/basic_example
./examples/simulation_example

# Use as library in another project
add_subdirectory(path/to/state-machine-template)
target_link_libraries(your_target sm_framework)
```

## Key Architecture
- **10 states:** INIT, IDLE, ACTIVE, PROCESSING, COMMUNICATING, MONITORING, CALIBRATING, DIAGNOSTICS, RECOVERY, CRITICAL_ERROR
- **3-tier errors:** MINOR (auto-recover), NORMAL (RECOVERY state, 3 retries), CRITICAL (system lock)
- **Thread-safe:** Event posting is ISR-safe via critical sections
- **Platform HAL:** Override 5 weak functions: `Platform_GetTimeMs`, `Platform_EnterCritical`, `Platform_ExitCritical`, `Platform_UART_Init`, `Platform_UART_Send`
- **Memory:** ~1.5 KB RAM, ~6-8 KB Flash

## Conventions
- C99 standard
- No heap allocations in framework core
- All config via `#define` in app_config.h (copy from template)
- Doxygen comments on public APIs
- `volatile` on ISR-shared data
- `extern "C"` guards for C++ compatibility

## What NOT to Do
- Do not block in state callbacks (no delay/infinite loops)
- Do not modify state machine context directly (use API)
- Do not post multiple events rapidly (one pending at a time)
- Do not leave all debug messages enabled in production

## TODO
- [ ] C99 standard
- [ ] No heap allocations in framework core
- [ ] All config via `#define` in app_config.h (copy from template)
- [ ] Doxygen comments on public APIs
- [ ] Do not block in state callbacks (no delay/infinite loops)
- [ ] Do not modify state machine context directly (use API)
- [ ] Do not post multiple events rapidly (one pending at a time)
- [ ] Do not leave all debug messages enabled in production
