# State Machine Framework — Project Mandates

Extends the root `ENGINEERING-PROJECTS/GEMINI.md`. This file defines project-specific mandates for the State Machine Framework template.

## 1. Project Identity
- **Name:** State Machine Framework v2.0
- **Type:** Reusable embedded C library/template
- **Language:** C99 (CMake build system, GCC toolchain)
- **Status:** 100% complete — production-ready template

## 2. Project-Specific Mandates

### Code Quality
- **C99 strict:** No C++ features, no compiler extensions (`CMAKE_C_EXTENSIONS OFF`).
- **No heap in core:** The framework core (`src/core/`) must never call `malloc`/`free`. All buffers are statically sized via config defines.
- **Thread safety:** All shared state accessed from ISR context must use `Platform_EnterCritical()`/`Platform_ExitCritical()`. Event queue uses `volatile` qualifiers.
- **Weak symbol pattern:** Platform implementations use `__attribute__((weak))` so users override only what they need.

### Configuration
- **Single source of truth:** All tunable parameters live in `include/sm_framework/sm_config.h` with defaults. Users override via `app_config.h` (copied from `config/app_config_template.h`).
- **No magic numbers:** Every constant must be a named `#define` with units in the comment.

### Build System
- **CMake 3.15+** is the only supported build system.
- **Platform selection:** Via `-DSM_PLATFORM=` (SIMULATION, STM32, ESP32, RP2040, CUSTOM).
- **Examples:** Built by default (`BUILD_EXAMPLES=ON`). Must always compile cleanly.

### Validation
- **Examples must run:** `basic_example` and `simulation_example` must produce expected output after any change.
- **Zero warnings:** Build with `-Wall -Wextra -Wpedantic` must produce zero warnings.

## 3. Key Files
| File | Purpose |
|------|---------|
| `include/sm_framework/sm_framework.h` | Public umbrella header |
| `src/core/sm_state_machine.c` | Core state machine logic |
| `src/platform/sm_platform_weak.c` | Default platform HAL |
| `config/app_config_template.h` | User configuration template |
| `CMakeLists.txt` | Build configuration |
| `README.md` | Full documentation and API reference |
| `Quick-Guide.md` | Quick start guide |

## TODO
- [ ] None identified
