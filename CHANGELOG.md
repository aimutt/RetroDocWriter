# Changelog

All notable user-facing changes to RetroDocWriter are recorded here.
Format follows [Keep a Changelog](https://keepachangelog.com/); versions track
the EXE/installer version (`RetroDocWriter.rc`, `installer\RetroDocWriter.iss`).

## [Unreleased]

### Added
- **Bulleted lists** (**Format > Bulleted List**) with multi-level sub-bullets
  that change shape by depth (• ◦ ▪, cycling). Turn the current paragraph(s)
  into a list from the menu, then build it from the keyboard: **Enter** starts
  the next item, **Tab** at the start of an item makes it a sub-bullet (indent),
  **Shift+Tab** un-indents, and **Backspace** at the start of an item un-indents
  one level — a final Backspace on a top-level bullet leaves the list. Lists
  render and print with a hanging indent and round-trip through RTF.
- **Numbered lists** (**Format > Numbered List**) using legal/multilevel
  numbering (`1.`, `2.`, then `2.1`, `2.2`, then `2.2.1`). Same keyboard nesting
  as bulleted lists (Enter/Tab/Shift+Tab/Backspace); numbers renumber
  automatically as you add, indent, or remove items, and a normal paragraph
  restarts numbering. Toggling Bulleted vs Numbered on a paragraph switches it
  between the two. Renders and prints with a hanging indent and round-trips
  through RTF.
- The **Insert > Image…** dialog now has a **Browse** button (or press **Ctrl+B**)
  that opens the in-app file browser filtered to image files (PNG/JPEG/BMP/GIF).
  Picking an image fills its full path into the dialog's Path field instead of
  making you type it.

### Changed
- Renamed two Format menu items for clarity: **Text Color…** → **Color Text…**
  and **Highlight Color…** → **Highlight…**.

### Fixed
- **Page > Header / Footer…** dialog: choosing a slot's kind was stuck on *Text* —
  *Filename*, *Page #*, and *Date* were unreachable and the Up/Dn format keys
  appeared dead. Cycle the kind with the **Left/Right** arrows now (Space types a
  literal space into the custom-text field).
- You can now scroll all the way to the bottom of the last page when a document
  runs a little past a page boundary; the view no longer snaps back before the
  empty remainder of the page.
- Using the scrollbar (arrows or thumb drag) to move through a document no longer
  moves the text cursor — the caret stays exactly where you left it.
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
- Keyboard shortcuts for three Format features that previously had none:
  **Ctrl+T** Strikethrough, **Ctrl+D** Color Text, **Ctrl+H** Highlight.
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
