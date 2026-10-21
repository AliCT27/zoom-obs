# Review checklist

Use this when reviewing a change here. Most bugs in this plugin were visual or timing problems that the compiler
cannot catch, so review for behavior as well as syntax.

## Always

- Does the PR do one thing? Refactors must be pure moves/renames; check by diffing the moved lines.
- Does it build on all platforms (Dispatch workflow)? A failure before any step runs is a billing issue, not code.
- Is it stated whether the change was tested in real OBS? Untested must be said so.
- Does it say whether `data/` (shaders, locale) must be copied, or only `obs-plugins`?

## Threads

- Nothing in `videoTick` creates, removes or reconfigures sources, filters or scene items.
- Values passed from the graphics thread are guarded by `inputMutex` or are atomics, and a stale value cannot be
  used for a new action (reset it when the owning state starts).

## Settings

- New setting: field in `zoominator-controller.hpp`, default + load + save (table row or by hand), dialog control
  (read and write), and strings in both locale files.
- Loading an old config file must still work: missing keys keep their defaults; values are clamped.
- Save then load round-trips the same values.

## Visual behavior

- Zoom in, zoom out, back-to-back clicks, and not-zoomed states all look right (no one-frame flashes, size jumps
  or missing halo). Check frame by frame on a recording when in doubt.
- With 3D on, the frame/screen never shows black bars at rest or while zoomed.
- Both canvases work: 1080x1920 and 1920x1080.
- Shader and CPU math still agree.

## Safety

- No secrets, tokens, personal paths or upstream pushes.
- Nothing is deleted from the user's scene: original transforms are restored after zoom (see recovery).
