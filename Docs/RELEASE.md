# Releasing RetroDocWriter

This is the repeatable process for publishing a new downloadable build.

Each release ships a **version-stamped installer** (e.g.
`RetroDocWriter-Setup-0.2.0.exe`). New versions are written *beside* the older
ones, never over them, so a user who dislikes a new build can uninstall it and
reinstall an older version from that version's own setup `.exe` (see
[Reverting to an older version](#reverting-to-an-older-version)).

## Prerequisites (one-time)

- Visual Studio 2022 (MSVC) + CMake 3.24+ (already required to build the app).
- [Inno Setup 6](https://jrsoftware.org/isdl.php) (free). Installs `ISCC.exe`,
  typically at `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`.

## Steps

### 0. Bump the version (four places, must agree)

For a release `X.Y.Z`:

- `installer\RetroDocWriter.iss` — `#define AppVersion "X.Y.Z"`
- `RetroDocWriter.rc` — `FILEVERSION`, `PRODUCTVERSION` (`X,Y,Z,0`) and the
  `FileVersion` / `ProductVersion` strings (`"X.Y.Z.0"`)
- `CHANGELOG.md` — rename `## [Unreleased]` to `## [X.Y.Z] — <date>` and add a
  fresh empty `## [Unreleased]` above it
- `RELEASE-NOTES.txt` — refresh the "What's new" summary and the version/date
  header (this file is bundled into the install folder and shown by the
  optional post-install "View release notes" checkbox)

Leave the `AppId` GUID in `RetroDocWriter.iss` **unchanged** — it must stay the
same across all releases so a new version overwrites the old one in place (no
side-by-side installs) and the installer can detect an existing install.

### 1. Build Release (and Debug)

Per the project rule, both configs must build clean (the unit-test suite runs
as a post-build step and fails the build on any failed assertion):

```powershell
cmake --build build --config Debug
cmake --build build --config Release
```

Release leaves `RetroDocWriter.exe` plus `SDL3.dll`, `SDL3_ttf.dll`, and
`SDL3_image.dll` in `build\Release\` (the installer pulls assets from the clean
repo `assets\` tree, not from `build\Release\assets`).

### 2. Build the installer

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RetroDocWriter.iss
```

Output: `dist\RetroDocWriter-Setup-X.Y.Z.exe`. This does **not** touch any
older `dist\RetroDocWriter-Setup-*.exe` files — keep them.

### 3. Commit & push

Commit the version bump, changelog, release notes, and installer-script
changes. Follow the repo's usual flow: work on `dev`, open a PR to `main`.
(The setup `.exe` itself does not need to be committed — it's published as a
GitHub Release asset in the next step.)

### 4. Publish a GitHub Release

On https://github.com/aimutt/RetroDocWriter:

- **Releases → Draft a new release.**
- Tag `vX.Y.Z` (e.g. `v0.2.0`), title `RetroDocWriter vX.Y.Z`.
- Paste the release notes (from `RELEASE-NOTES.txt` / the changelog).
- Upload `dist\RetroDocWriter-Setup-X.Y.Z.exe` as the asset.
- Publish.

**Leave every earlier release and its installer asset in place** so older
versions stay downloadable for reverting.

CLI equivalent:

```powershell
gh release create v0.2.0 dist\RetroDocWriter-Setup-0.2.0.exe `
    --title "RetroDocWriter v0.2.0" --notes-file RELEASE-NOTES.txt
```

### 5. Update the website Download link

The setup filename now carries the version, so the old fixed URL
(`releases/latest/download/RetroDocWriter-Setup.exe`) no longer resolves.
Update the aimutt.com "Download" button to point at the new asset, e.g.
`.../releases/latest/download/RetroDocWriter-Setup-X.Y.Z.exe`, or link to the
Releases page.

## What the installer does

- Shows `LICENSE.txt` (the freeware EULA) on a page the user must accept;
  `THIRD-PARTY-NOTICES.txt` ships alongside it.
- If a previous RetroDocWriter is already installed, prompts: *"RetroDocWriter
  &lt;old&gt; is already installed. Update to version X.Y.Z?"* — **Yes**
  overwrites the existing install in the same folder, **No** aborts.
- Installs the EXE + 3 SDL DLLs + `assets\` + `RELEASE-NOTES.txt` into
  `Program Files\RetroDocWriter`.
- Offers an optional post-install checkbox to view the release notes.
- Writes a clean uninstaller (`unins000.exe`) into the install folder.

## Reverting to an older version

Because each release keeps its own installer:

1. Uninstall the current build — Windows **Settings → Apps**, or run
   `unins000.exe` in the install folder. The uninstaller removes only what it
   installed; **your `.rtf` documents are never touched** (they live wherever
   you saved them, not in the install folder).
2. Download the desired older `RetroDocWriter-Setup-X.Y.Z.exe` from its GitHub
   Release and run it. With no current install present, it installs cleanly
   with no update prompt.

## Notes

- **Unsigned binaries.** The EXE and installer are not code-signed, so Windows
  SmartScreen warns ("unknown publisher" / "Windows protected your PC"). Users
  click *More info → Run anyway*. Removing the warning requires a paid
  Authenticode code-signing certificate.
- **First-run defaults vs. existing users.** New default settings (theme,
  spell-check, margins) only apply on a machine with no
  `%APPDATA%\RetroDocWriter\config.ini` yet. Anyone who ran an earlier version
  keeps their saved preferences across the update.
