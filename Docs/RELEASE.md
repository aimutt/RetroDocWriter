# Releasing RetroDocWriter

This is the repeatable process for publishing a new downloadable build. The
website's "Download" button (on aimutt.com) links to a fixed GitHub Releases URL,
so each release just needs to upload the installer under the expected filename.

## Prerequisites (one-time)

- Visual Studio 2022 (MSVC) + CMake 3.24+ (already required to build the app).
- [Inno Setup 6](https://jrsoftware.org/isdl.php) (free). Installs `ISCC.exe`,
  typically at `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`.

## Steps

1. **Build Release** (and Debug, per the project rule that both must build clean):

   ```powershell
   cmake --build build --config Release
   ```

   This leaves `RetroDocWriter.exe` plus `SDL3.dll`, `SDL3_ttf.dll`, and
   `SDL3_image.dll` in `build\Release\` (the POST_BUILD step also copies `assets\`
   there, but the installer pulls assets from the clean repo `assets\` tree).

2. **Build the installer:**

   ```powershell
   & "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RetroDocWriter.iss
   ```

   Output: `dist\RetroDocWriter-Setup.exe`.

3. **Publish a GitHub Release** for the repo:
   - Tag it (e.g. `v0.1.0`).
   - Upload `dist\RetroDocWriter-Setup.exe` as a release asset, named **exactly**
     `RetroDocWriter-Setup.exe` — the website link depends on this name.

   ```powershell
   gh release create v0.1.0 dist\RetroDocWriter-Setup.exe `
       --title "RetroDocWriter v0.1.0" --notes "First public release."
   ```

4. **Done.** The website button
   (`github.com/aimutt/RetroDocWriter/releases/latest/download/RetroDocWriter-Setup.exe`)
   now serves the new build automatically — no website edit needed.

## Versioning

When bumping the version, update it in three places so they agree:
- `installer\RetroDocWriter.iss` (`AppVersion`)
- `RetroDocWriter.rc` (`FILEVERSION`, `PRODUCTVERSION`, and the string values)
- the GitHub Release tag

## Notes

- **Unsigned binaries.** The EXE and installer are not code-signed, so Windows
  SmartScreen warns ("unknown publisher" / "Windows protected your PC"). Users
  click *More info -> Run anyway*. Removing the warning requires a paid
  Authenticode code-signing certificate.
- **License.** The installer shows `LICENSE.txt` on a page the user must accept
  before installing. `THIRD-PARTY-NOTICES.txt` ships alongside it.
