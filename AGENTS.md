# AGENTS.md — ERSaveManager

A Windows GUI tool (Win32 API, C11) for managing Elden Ring save files: importing/exporting
face data and character slots, re-signing Steam IDs, and multi-locale UI.

### ⚠️ IMPORTANT: Language Requirements

**All code comments and documentation MUST be written in English.**

---

## Project Layout

```
ERSaveManager/
├── CMakeLists.txt          # Root: sets C++ standard to C23 (C std follows toolchain default)
├── README.md               # Project documentation (English)
├── CHANGELOG.md            # Keep a Changelog format
├── LICENSE                 # MIT License
├── .github/
│   └── workflows/
│       └── release.yml     # CI: build + GitHub Release on v* tags
├── cmake/
│   ├── CustomCompilerOptions.cmake   # /utf-8 flag, strip/LTO/static-CRT options
│   ├── GlobalOptions.cmake           # Visibility presets, export compile commands
│   └── ProjectMacros.cmake           # add_project() macro
├── deps/
│   ├── inih/               # INI file parser (linked but currently unused)
│   └── md5/                # MD5 hash library
└── src/                    # Main application source
    ├── CMakeLists.txt      # add_subdirectory(common/ERSaveManager/Praxis)
    ├── common/             # src/common: Static library: ersave, save_compress, file_dialog, locale_core, config_core
    │   └── CMakeLists.txt
    ├── ERSaveManager/      # src/ERSaveManager: ERSaveManager executable sources
    │   └── CMakeLists.txt
    └── Praxis/             # src/Praxis: Praxis executable sources
        ├── CMakeLists.txt
        ├── backends/
        │   └── er_backend.c
        └── ...
```

---

## Build Commands

This is a **CMake + C project targeting Windows only** (MSVC or MinGW).

### Configure (choose one generator)

```powershell
# MSVC (Visual Studio)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# MinGW / LLVM (single-config)
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

### Build

```powershell
# Build everything
cmake --build build --config Release

# Build specific target
cmake --build build --config Release --target saveman  # ERSaveManager.exe
cmake --build build --config Release --target praxis   # Praxis.exe
```

### Build (Debug)

```powershell
cmake --build build --config Debug
```

### Clean rebuild

```powershell
Remove-Item -Recurse -Force build
cmake -S . -B build ...
cmake --build build --config Release
```

### Notes
- There is **no automated test suite** in this repository. Verification is manual
  (run the resulting `.exe` and exercise the UI).
- No single-test runner command exists.
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set automatically → `build/compile_commands.json`
  is generated for clangd/IDE tooling.

---

## Praxis-Specific Notes

- **Default Hotkeys**:
  - Backup Full Save: `Ctrl+Shift+F5`
  - Restore Full Save: `Ctrl+Shift+F9`
  - Backup Current Slot: `Ctrl+Shift+F6`
  - Restore Current Slot: `Ctrl+Shift+F10`
  - Undo Last Restore: `Ctrl+Shift+Z`
- **Ring Backup Location**: `<tree_root>/.praxis_ring/` (hidden directory).
- **Backend Interface**: Compile-time vtable defined in `src/Praxis/game_backend.h`.

---

## Code Style Guidelines

### Language & Standard
- **Pure C11** (`stdbool.h`, `stdint.h`, fixed-width types).
- The root `CMakeLists.txt` sets `CXX_STANDARD 23`; C code uses whatever the toolchain
  default is (no explicit `CMAKE_C_STANDARD` set at root level).
- All source files are compiled with `/utf-8` (MSVC) to allow UTF-8 literals.

### Includes — Order
Within each `.c` file, includes follow this order (no blank lines between groups unless
already present in the codebase):
1. Own module header (`"config.h"`, `"ersave.h"`, …)
2. Other local headers (`"locale.h"`, `"embedded_face_data.h"`, …)
3. Third-party/dep headers (`<ini.h>`, `<md5.h>`)
4. Standard C headers (`<stdint.h>`, `<wchar.h>`, `<stdio.h>`)
5. Windows SDK headers (`<windows.h>` first, then specific ones like `<shlwapi.h>`)

Headers use `#pragma once` (not include guards).

### Naming Conventions

| Item | Convention | Example |
|------|-----------|---------|
| Functions | `snake_case` | `er_save_data_load`, `handle_splitter_drag` |
| Types / structs | `snake_case_t` | `er_save_data_t`, `config_t` |
| Struct tag | `snake_case_s` | `struct er_save_data_s` | 
| Enum types | `snake_case_e` | `locale_string_index_e` |
| Enum values | `UPPER_SNAKE_CASE` | `STR_APP_TITLE`, `STR_MAX` |
| Macros / `#define` | `UPPER_SNAKE_CASE` | `VERSION_STR`, `SPLITTER_WIDTH` |
| Global variables | `snake_case` | `save_data`, `main_window`, `is_dragging` |
| Local variables | `snake_case` | `bytes_read`, `slot_offset` |
| Constants (`#define`) | `UPPER_SNAKE_CASE` | `DEFAULT_SPLIT_RATIO`, `CONFIG_FILE` |

**Module prefix convention:** public API functions are prefixed with their module:
`er_save_*`, `er_char_data_*`, `er_face_data_*`, `config_*`, `locale_*`.

Static (file-local) helpers have no prefix and are declared `static`.

### Types
- Use `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `int` from `<stdint.h>`.
- Use `bool` from `<stdbool.h>` for boolean returns; `BOOL` only where Win32 API demands it.
- `wchar_t` / `PWSTR` / `LPWSTR` for all user-visible strings (Unicode throughout).
- Struct forward-declarations use opaque typedefs in headers; definition stays in `.c`.

### Formatting
- **4-space indentation** (no tabs).
- Opening brace `{` on the **same line** as the control statement for `if`, `for`, `while`,
  `switch`, and function definitions.
- One blank line between top-level function definitions.
- Short one-liner static functions may be written on a single line when it aids readability
  (`validate_face_data` example in `ersave.c`).
- `switch` cases are indented one level; `case` blocks with local variables use `{ }`.
- No trailing whitespace; files end with a newline.

### Comments
- Block comments use `/* … */` (C-style). Single-line `//` comments are acceptable but
  `/* */` is preferred in this codebase.
- Every public function in headers is documented with a Doxygen-style block:
  ```c
  /**
   * @brief Short description
   * @param name Description
   * @return Description
   */
  ```
- Implementation files begin with a `@file` / `@brief` / `@details` Doxygen block.
- Inline comments explain non-obvious logic; struct members have trailing comments.

### Error Handling
- Functions that can fail return `bool` (success) or a pointer (NULL on failure).
- **Early-return pattern** for error paths — check, clean up (free/close handles), and
  `return NULL` / `return false` immediately. Do not use goto.
- Windows HANDLEs are always closed before returning from any error path.
- `LocalAlloc` / `LocalFree` is used for heap allocation (no `malloc`/`free`).
- Win32 HRESULT values are checked with `SUCCEEDED(hr)`.

### Memory Management
- Use `LocalAlloc(LMEM_FIXED, size)` to allocate; `LocalFree(ptr)` to free.
- `ZeroMemory` / `CopyMemory` (Windows macros) for zeroing/copying buffers.
- `lstrcpyW` / `lstrcpynW` for wide-string copying.
- Always free on every error path before the corresponding `return NULL`.
- Every `CreateFileW` must have a matching `CloseHandle` (including error paths).

### Windows API Patterns
- All string literals in the UI use `L"…"` wide-string literals.
- Message-box calls use `locale_str(STR_…)` for the text (never hard-coded strings).
- COM objects (IFileDialog, IShellItem) are released via `->lpVtbl->Release()`.
- UI controls are created with `CreateWindowW` / `CreateWindowExW`; fonts are set with
  `SendMessage(hwnd, WM_SETFONT, …)`.
- Window procedures return `0` when a message is handled; fall through to
  `DefWindowProcW` for unhandled messages.
- `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` for bulk layout.

### CMake Style
- `if () / endif ()` with a space before `()`.
- `cmake_parse_arguments` used in every macro; options in UPPER_CASE.
- Generator expressions (`$<…>`) used for per-config/per-compiler flags.
- No `add_definitions()` — use `target_compile_definitions()` with `PRIVATE`.

---

## Key Domain Notes

- Save file format is **BND4** (FromSoftware container). Magic `"BND4"` at offset 0.
- Character slots 0–9; face slots 0–14; all slots checked for availability before access.
- MD5 checksums are written to the first 16 bytes of each slot before the slot data.
- Steam IDs are 64-bit integers stored as `uint64_t`; folder names are decimal Steam IDs.
- `UNICODE` / `_UNICODE` preprocessor macros are defined — all Win32 calls use the `W`
  suffix variant.
- Locale strings are indexed via `locale_string_index_t`; always use `locale_str(STR_…)`
  rather than hard-coded text.
