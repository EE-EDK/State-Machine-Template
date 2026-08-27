# State Machine Framework — Project Mandates

Extends the root `ENGINEERING-PROJECTS/GEMINI.md`. This file defines project-specific mandates for the State Machine Framework template.

## 1. Project Identity
- **Name:** State Machine Framework v3.0
- **Type:** Reusable embedded C library/template
- **Language:** C99 (CMake build system, GCC toolchain)
- **Status:** Complete — production-ready template; maintenance doc/README parity with `CLAUDE.md`

## 2. Project-Specific Mandates

### Code Quality
- **C99 strict:** No C++ features, no compiler extensions (`CMAKE_C_EXTENSIONS OFF`).
- **No heap in core:** The framework core (`src/core/`) must never call `malloc`/`free`. All buffers are statically sized via config defines.
- **ISR vs task:** `SM_PostEvent` is ISR-safe (critical sections). `SM_Process`, defer/recall, and runtime transition edits are **not** ISR-safe. Do not call `SM_Process` recursively from callbacks.
- **Critical sections:** ISR-visible shared state uses `SM_Platform_EnterCritical()` / `SM_Platform_ExitCritical()` (must nest). Event queue indices use `volatile` where appropriate.
- **Weak symbol pattern:** Platform implementations use `__attribute__((weak))` so users override only what they need.

### Configuration
- **Single source of truth:** All tunable parameters live in `include/sm_framework/sm_config.h` with defaults. Users override via `app_config.h` (copied from `config/sm_config_template.h`).
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
| `src/core/sm_engine.c` | Core RTC engine (process, queue, time/defer, transitions) |
| `src/core/sm_error.c` | Per-instance error handler |
| `src/core/sm_debug.c` | Debug output (global subsystem when enabled) |
| `src/platform/sm_platform_weak.c` | Default platform HAL |
| `config/sm_config_template.h` | User configuration template |
| `CMakeLists.txt` | Build configuration |
| `README.md` | Full documentation and API reference |
| `Quick-Guide.md` | Quick start + integration reminders |
| `docs_dev/findings.md` | **Historical** pre-v3 audit — not current defect list |

## TODO
- [ ] None identified

## Conversation History Archive

Past AI conversations (217 total) are archived at the workspace root: `.claude/conversation-history/`. Search `index.json` by keyword or browse `index.md` for topic-grouped context on prior decisions, approaches, and project history.


## Auto-Commit & Push Mandate

After completing each task, automatically commit all relevant changes with a descriptive message and push to `origin main`. Report what was committed. This is standing authorization — no confirmation needed.

## graphify

This project has a graphify knowledge graph at graphify-out/.

Rules:
- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- If graphify-out/wiki/index.md exists, navigate it instead of reading raw files
- After modifying code files in this session, run `python3 -c "from graphify.watch import _rebuild_code; from pathlib import Path; _rebuild_code(Path('.'))"` to keep the graph current
