# Upload handoff for Codex App

## Request

Publish this prepared repository to **https://github.com/SilverCirno/starfox64-esp32p4** as a **public** repository. Use an English repository description and keep the README in English.

Suggested description:

> Native Star Fox 64 port for ESP32-P4 with TGX software rendering, ST7102 MIPI display and touch controls. ROM supplied separately.

Do not add verification narratives or a transparency-effects discussion to the introduction.

## Working directory

`X:\ESP\starfox64-public`

This is an independent copy. Do not edit, build, commit, clean, move or delete anything in the original project at `X:\ESP\starfox64`.

## What is included

- Port integration source, display/input/audio interfaces, and build tools.
- Pinned upstream dependencies represented as Git submodules.
- TGX local source patch, license texts and upstream notices.
- Resource layout metadata only: names, addresses, dimensions and counts.
- Application firmware, bootloader and partition table under `firmware/`, with SHA-256 checksums.

## What must never be uploaded

- Any ROM (`.z64`, `.n64`, `.v64` or ROM ZIP).
- `assets/local/` or extracted DMA segments, models, textures and game audio.
- Full-device flash backups, `artifacts/`, build outputs or local logs.
- A merged firmware image containing the ROM partition.
- The original project's Git history or upstream repositories flattened into this repository.

Use Git push on the prepared repository. Do not recursively upload every filesystem file: local submodule worktrees and ignored build directories are not repository payload. Preserve `.gitmodules` and Git submodule entries (mode `160000`).

## Upload procedure

1. Inspect `git status`, `git ls-files --stage` and the committed tree. Confirm that only the intended firmware binaries are tracked, no ROM/local assets or backups are present, and the upstream directories remain Git links.
2. Check whether `SilverCirno/starfox64-esp32p4` already exists. If absent, create an empty public repository using normal GitHub authorization. Never extract or print authentication tokens.
3. Set `origin` to `https://github.com/SilverCirno/starfox64-esp32p4.git` and push `main`. Do not force-push or replace an existing repository/history without asking the user.
4. Do not create releases, upload additional binary bundles or flash a board unless requested.
5. Verify the remote README, tracked files and submodule commit IDs, then provide the GitHub URL.

The current local source preparation is complete. Compile and package details are in README.md. A ROM is needed at runtime, not to compile the application.
