# Changelog

All notable user-facing changes to RetroDocWriter are recorded here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versions track
the EXE/installer version (`RetroDocWriter.rc`, `installer\RetroDocWriter.iss`).

## [Unreleased]

### Fixed
- Typed text now inherits the formatting of the character before the cursor
  instead of "sticking" to the last font size/style used. Bumping a heading to
  a larger size no longer carries that size to text typed elsewhere. Inheritance
  covers bold/italic/underline/strikethrough, font face, size, color, and
  highlight. Explicitly picking a font/color/highlight/style with no selection
  still starts a new run in that choice at the cursor.

### Added
- Status bar shows the current font face and point size (e.g. `EB Garamond 12 pt`)
  for the next character to be typed.

### Changed
- Status-bar text now renders at a smaller font size than the rest of the chrome,
  reducing its visual weight (the function-key bar and menus are unchanged).

## [0.1.0] — 2026-05-31

- Initial public release: WYSIWYG green-screen document writer with
  per-character formatting, paragraph alignment, multi-page/multi-column layout,
  floating shapes/images with text wrap, headers/footers, RTF load/save, GDI
  printing, spell check, themes, and a one-click Windows installer.
