# Changelog

All notable user-facing changes to RetroDocWriter are recorded here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versions track
the EXE/installer version (`RetroDocWriter.rc`, `installer\RetroDocWriter.iss`).

## [Unreleased]

### Fixed
- Dragging a floating image (or shape) into a different paragraph now re-anchors
  it to that paragraph on drop, so the text there wraps around it. Previously an
  image moved into an earlier paragraph would just hover over and cover that
  paragraph's text instead of reflowing it.
- Typed text now inherits the formatting of the character before the cursor
  instead of "sticking" to the last font size/style used. Bumping a heading to
  a larger size no longer carries that size to text typed elsewhere. Inheritance
  covers bold/italic/underline/strikethrough, font face, size, color, and
  highlight. Explicitly picking a font/color/highlight/style with no selection
  still starts a new run in that choice at the cursor.
- Typing on a blank line now continues the nearest text's font instead of
  snapping back to the document default, and the status-bar font indicator
  matches what will actually be typed.
- The status-bar font indicator now always shows the face *and* point size
  together (it shifts beside the Ln/Col readout when space is tight instead of
  disappearing).

### Added
- Status bar shows the current font face and point size (e.g. `EB Garamond 12 pt`)
  for the next character to be typed.
- **Browse button** on the Open and Save As dialogs opens an in-app file/folder
  browser (keyboard + mouse). Open browses to a file to open; Save As browses to
  a folder to save into. Reachable by mouse or Ctrl+B.
- The Open and Save As dialogs now show the target directory (`Dir: …`) above the
  file-name field, so it's clear where a typed name opens from / saves to. The
  name resolves against that directory (a full path you type still wins), and the
  directory follows the in-app browser.
- **Save As now warns before overwriting an existing file.** A "File Exists"
  confirmation appears when the target name already exists: Overwrite, Rename
  (back to Save As), or Cancel. Plain Ctrl+S to an already-named document is
  unaffected.

### Changed
- The default document font is now Source Sans 3 12 pt (was EB Garamond 12 pt).
  Existing documents keep the font they were saved with.
- Status-bar text now renders at a smaller font size than the rest of the chrome,
  reducing its visual weight (the function-key bar and menus are unchanged).
- The SDL-free logic (editor model, RTF I/O, settings, screen buffer) is now built as
  a `RetroDocCore` static library shared by the app and the test suite. No behavior change.

### Tests
- Added a doctest-based unit-test suite (70 cases / 385 assertions) covering the SDL-free
  core. Tests build and run on every `cmake --build` (Debug and Release) and **fail the
  build** if any assertion fails.

## [0.1.0] — 2026-05-31

- Initial public release: WYSIWYG green-screen document writer with
  per-character formatting, paragraph alignment, multi-page/multi-column layout,
  floating shapes/images with text wrap, headers/footers, RTF load/save, GDI
  printing, spell check, themes, and a one-click Windows installer.
