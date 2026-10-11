# Zoominator – Smart Scene Zoom & Follow for OBS Studio

[![Watch the demo](https://img.youtube.com/vi/R3KQnsnzIAM/0.jpg)](https://www.youtube.com/watch?v=R3KQnsnzIAM)

Zoominator is an OBS Studio plugin that dynamically **zooms and pans the entire scene** to follow your mouse, keeping the focus exactly where the action happens.

It operates at the **scene level**, meaning it works with any source automatically no per-source setup, no dock, no complexity.

---

## What It Does

- **Scene-wide Zoom & Pan**  
  Smoothly transforms the entire scene based on mouse position.

- **Smart Clamping**  
  Ensures the canvas is always fully covered no black edges, even with cropped sources.

- **Mouse-Driven Focus**  
  Choose where the zoom is anchored: the canvas center, the cursor position at
  trigger time (static), or the cursor position with continuous following.

- **Ultrawide Edge Tracking**
  Optionally keeps the cursor centered until the captured scene reaches an edge.

- **Idle Freeze**
  Pauses mouse following after a configurable idle timeout and resumes on movement.

- **Click Highlight (Halo)**  
  Optional click-only visual feedback using a configurable halo.

- **Flexible Activation**  
  Toggle or hold behavior with customizable key combinations.

- **Wheel Zoom (Windows and X11)**
  With Mouse X2 in toggle mode and no modifiers, hold the button and scroll to
  change zoom. Release the button to keep the selected level, or click it
  without scrolling to reset. The plugin consumes wheel events during the
  gesture so the application below the pointer does not scroll.

---

- **Automatic Trigger (new in 2.1)**
  Nothing to press: zoom in on a click and/or while typing, then zoom back out
  after a configurable pause. Plain typing counts; shortcuts held with
  Ctrl/Alt/Win are ignored. On Windows, clicks and keys while OBS itself has
  focus (Start/Stop Recording, this dialog) are ignored too.

- **Animation Styles (new in 2.1)**
  Smooth, Ease out, Expo, or Punch (a slight overshoot that feels snappy in
  short videos).

- **Focus (new in 2.2)**
  While zoomed, dims everything except a soft spotlight around the cursor so
  the viewer's eye stays on what you are clicking. Adjustable strength, radius
  and edge softness (Advanced tab). Sources that are not ticked on the Sources
  tab, such as a webcam or logo, stay above the dimming and are not darkened.
  Off by default.

- **Focus styles (new in 2.3)**
  Choose how the area outside the spotlight is treated: Dim, Blur, or
  Blur + Dim. Blur uses a small shader (`data/focus_blur.effect`), so the
  plugin's `data` folder must be installed next to the DLL; if the shader
  cannot be loaded the plugin falls back to Dim and logs a warning. Blur is
  applied only while zooming and removed from the source afterwards, so it never
  shows up in your filter lists.

- **Pause / Resume hotkey (new in 2.2)**
  One key that switches all zooming off and on, for when you are talking and
  do not want the view to move. Works while OBS is in the background.
  Set it on the Target tab.

- **Portrait Cover (now functional)**
  On a canvas taller than wide, the capture is scaled up while zoomed so it
  fills the canvas with no gaps, and the view stays centred on the cursor.
  Zoom Factor is applied on top of this fill. Use Zoom Factor 1.0 to just fill.

- **Reels preset (new in 2.1)**
  One button in the dialog that fills in settings for vertical short-form video.

---

## Recording vertical videos (Reels / Shorts / TikTok)

1. Set the OBS canvas to **1080x1920** (Settings -> Video).
2. Add your display capture and fit it to the canvas width. Add your webcam
   below or above it.
3. Open **Tools -> Zoominator -> Sources** and tick **only the screen capture**.
   Unticked sources (webcam, logo, watermark) stay fixed.
4. Click **Reels preset**, pick your monitor on the Target tab, and Apply.
5. Click or type while recording: the view pushes in, follows the cursor, and
   glides back after a pause.

---

## Installation

### Windows
1. Download the latest release
2. Extract the archive and move the zoominator.dll file into your OBS Studio directory:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit
   ```
3. Restart OBS

### macOS
1. Download the `.pkg` or `.dmg` from releases
2. Install and restart OBS

### Linux (X11)
1. Build from source or install via package (if available)
2. Copy plugin files into:
   ```
   ~/.config/obs-studio/plugins/
   ```
3. Restart OBS

---

## Build from Source

### Requirements
- OBS Studio development libraries
- CMake (3.20+ recommended)
- C++17 compatible compiler
- Qt6

### Steps
```bash
git clone https://github.com/mmlTools/zoominator.git
cd zoominator
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## Compatibility Notes

- **Windows:** Full support (global input, smooth tracking, and Mouse X2 + wheel zoom)
- **macOS:** Requires Accessibility permissions for input tracking
- **Linux (X11):** Supported via XInput2
- **Wheel zoom:** Supported on Windows and Linux X11. Zoom-in and zoom-out sensitivity, minimum and
  maximum zoom, and animation durations are configurable in the Advanced tab.
- **Wayland:** Native sessions are detected and X11 hooks are disabled. The
  Global Shortcuts portal can support hotkeys, but Wayland currently has no
  standard passive global cursor-position portal, so full mouse tracking still
  requires compositor-specific input capture support.

---

## Use Cases

- Tutorials & live coding  
- Product demos  
- Gameplay & analysis  
- Vertical / short-form content  

---

## Notes

Zoominator behaves like a **virtual camera system inside OBS**, applying transformations at the scene level for maximum flexibility and reliability.

## Clang format fix

```bash
git add --chmod=+x build-aux/.run-format.zsh build-aux/run-clang-format build-aux/run-gersemi build-aux/run-swift-format build-aux/.functions/*
git add --renormalize .
git commit -m "Make build scripts executable and normalize line endings"
```

```gitattributes
.gitattributes
*.sh text eol=lf
*.zsh text eol=lf
.github/scripts/** text eol=lf
.functions/* text eol=lf
run-clang-format text eol=lf
run-gersemi text eol=lf
run-swift-format text eol=lf
.run-format.zsh text eol=lf
```

```bash
git add --renormalize .
git commit -m "Normalize line endings for Unix scripts"
git push
```
