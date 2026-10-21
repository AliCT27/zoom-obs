# Zoominator architecture

C++17, Qt6, libobs + obs-frontend-api, built with CMake. One class,
`ZoominatorController` (a singleton), does the work; its implementation is split
over several files so each can be read on its own.

## Threads

- **Graphics thread** (`videoTick`, a libobs tick callback): runs the zoom state
  machine and writes scene item transforms. It must not create, remove or
  reconfigure sources. It publishes what the main thread should do (marker and
  focus positions) under `inputMutex`.
- **Main/Qt thread** (`onTick`, a 16 ms timer): owns everything that changes the
  scene graph: overlays, filters, capturing and restoring items, saving settings.
- **Input hooks** (Windows/macOS/Linux): call into the controller, which only sets
  flags and timestamps.
- **Caret worker** (`caret-locator`, Windows): polls the focused app for its text
  caret while the user types.

## Files (src/)

| File | Job |
| --- | --- |
| `plugin-main.cpp` | OBS module entry; registers the blur filter and starts the controller |
| `zoominator-controller.hpp` | The class: settings, state, every method declaration |
| `zoominator-controller.cpp` | Lifecycle, wheel zoom, easing, `applyZoomToScene`, `onTick`, `videoTick` |
| `zoominator-settings.cpp` | Defaults, `loadSettings`/`saveSettings`, enum <-> string |
| `zoominator-recovery.cpp` | Remembers original transforms to undo a crash mid-zoom |
| `zoominator-sceneitems.cpp` | Read/apply/capture/restore scene item transforms |
| `zoominator-screen.cpp` | Which monitor/window is captured; maps the cursor to scene pixels (per platform) |
| `zoominator-marker.cpp` | Click halo |
| `zoominator-focus.cpp` | Focus dim, idle blur, and attaching/detaching the blur filter |
| `zoominator-tilt.cpp` | 3D tilt filter and pose |
| `zoominator-input.cpp` | Global keyboard/mouse hooks (per platform) and install/uninstall |
| `zoominator-triggers.cpp` | What a trigger does: start/stop zoom, automatic mode, pause, follow toggle |
| `zoominator-hotkeys.cpp` | Key sequences -> virtual keys; builds trigger state from settings |
| `zoominator-dialog.cpp/.hpp` | The settings window |
| `zoominator-internal.hpp` | Shared includes and small helpers for the files above |
| `focus-blur-filter.cpp/.hpp` | Self-contained OBS filter (shader in `data/focus_blur.effect`) |
| `caret-locator.cpp/.hpp` | Self-contained text caret finder (Windows) |

## Adding a feature

1. **Setting:** add the field in `zoominator-controller.hpp`, its default and
   load/save in `zoominator-settings.cpp` (a row in the `kScalarSettings` table for a plain switch or
   number; by hand in `loadSettings`/`saveSettings` if it needs special rules), the control in `zoominator-dialog.cpp`
   (apply and load sections), and text in `data/locale/en-US.ini` and `fa-IR.ini`.
2. **Behavior that changes the view:** `applyZoomToScene` (graphics thread). If it
   needs scene graph changes, publish a `pending*` value and act on it in `onTick`.
3. **New trigger or input:** the hook in `zoominator-input.cpp` records it,
   `zoominator-triggers.cpp` decides what it does.
4. **Something self-contained** (a filter, a locator): give it its own file pair
   with a small interface, like `focus-blur-filter` and `caret-locator`.

## Rules worth keeping

- Never mutate the scene graph from the graphics thread.
- Anything per-platform goes behind `#ifdef` inside the per-platform files
  (`screen`, `input`), not in the core.
- Files under `data/` (locale, shader) must be installed next to the plugin.
- Build all three platforms (the Dispatch workflow) before merging; the Windows
  and macOS code cannot be compiled locally on Linux.
