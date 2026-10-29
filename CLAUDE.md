# Zoominator: notes for Claude

OBS Studio plugin (C++17, Qt6, libobs) that records attractive vertical Reels: zoom, 3D tilt, click halo,
focus spotlight, frame and camera ring. Canvas is usually 1080x1920 (also 1920x1080). Read `ARCHITECTURE.md`
first for threads, files and where things go.

## Working with the owner

- The owner speaks Persian and is a programmer. Reply in Persian; keep code, commit messages and PR text in English.
- Explain first when he asks "why" or "tell me first", and build only after he agrees.
- Say plainly that a change is untested in real OBS unless he reports he tested it. Never claim a visual result
  you have not seen.
- Windows only for him. He records videos of the output; analyzing his mp4 (ffmpeg + numpy, frame by frame) is the
  best way to diagnose visual bugs.

## Workflow

- Work on a branch from `origin/main`, never on `main`. One small change per PR; refactors must not change behavior.
- Build: run the `Dispatch` workflow on the branch (`gh workflow run Dispatch --ref <branch> -f job=build`).
  The build is the only compile check (no local Windows toolchain). If the run fails before any step starts, it is
  a billing/quota problem on the GitHub account, not the code.
- Open the PR with the REST API (`gh api repos/AliCT27/zoom-obs/pulls ...`); GraphQL (`gh pr create`) is
  blocked. Then tell him which PR to merge.
- After merging he runs Dispatch on `main` and copies the `zoominator` folder (DLL plus `data`) from the artifact into
  `C:\ProgramData\obs-studio\plugins\` (hidden folder; the OBS 33 layout). His OBS itself is installed (not portable)
  on `E:\obs-studio`, which does not matter for this path. Confirmed on OBS 33.0.0: no Legacy tag. The old
  `obs-plugins\64bit` + `data\obs-plugins` layout inside the OBS folder is Legacy in OBS 33 and stops loading in OBS 34.
  He knows this: after a finished change just say which PR to merge. Do not repeat the Dispatch/copy steps, and do not
  say whether `data` is needed. Mention only something unusual (for example a log line he must send back).
- The repo is `AliCT27/zoom-obs` (it was renamed from `zoominator-private`, then `zoominator-custom`; old names redirect
  for git but the API proxy refuses the redirect, so always use the new name). A new session may need `add_repo` with
  `access: push` for the new name before `gh api` works.
- Never push to or open PRs against the upstream (mmlTools) repo. Only this repo. No tokens or secrets in chat or
  files; the repo is public.
- A stop-hook may warn about "unpushed commits" right after a push; check `git ls-remote origin <branch>` first.

## Code rules that matter

- Never change the scene graph from the graphics thread (`videoTick`). Publish a `pending*` value; the main thread
  (`onTick`) applies it.
- Simple settings (a switch or a number) go in the table at the top of `zoominator-settings.cpp`; settings with
  special rules are written by hand in `loadSettings`/`saveSettings`. Every new setting also needs its dialog
  control and both locale files (`en-US.ini`, `fa-IR.ini`).
- Shader math (`data/*.effect`) must match its CPU twin (for example the focus beam profile in `beamExponent`).
- Shaders are compiled only when OBS loads them, so the Windows build cannot catch a shader error and a broken one
  silently removes the feature. After editing a `.effect`, strip the `technique` block, make the uniforms plain
  globals and run `glslangValidator -D -e <PixelShaderName> -S frag -V file.hlsl -o /dev/null` (apt: glslang-tools).
  Also assert that every text replacement in a patch script actually matched.
- Halo size and position are read from the scene item's own transform, not from values published by another thread
  (that caused one-frame glitches).
- Per-platform code stays behind `#ifdef` inside `zoominator-screen.cpp` and `zoominator-input.cpp`.
