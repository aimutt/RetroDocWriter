# RetroDocWriter

A C++20 WYSIWYG document writer styled after the green-phosphor monochrome terminals of the early 1980s — modern dependencies, retro look. RetroDocWriter draws its own text-mode chrome (menu bar, status bar, dialogs, function-key bar) inside an SDL window, then paints a centered US-Letter page over the editor area at the document font's true point size, with proportional text, per-character formatting (bold / italic / underline / strikethrough, face, size, color, highlight), per-paragraph alignment, per-document margins, multi-page navigation via Ctrl+Enter forced page breaks, floating shapes and images with text wrap, multi-column text, rich header/footer bands, RTF load/save, GDI printing, and spell check.

Primary target is **Windows x64**. The architecture is portable; Linux and macOS builds are planned but not yet validated.

## Features

- Centered WYSIWYG page rendered at true point size (DPI-scaled to 96)
- Proportional fonts: EB Garamond (default), Source Serif 4, Source Sans 3, Open Sans
- Monospace fonts also available: Cascadia Mono, IBM Plex Mono, JetBrains Mono, VT323
- Chrome (menus / status bar / dialogs) is fixed at Cascadia Mono 16 pt — picking a different document font in Options > Font does not reshape the UI
- Per-character formatting (bold / italic / underline / strikethrough; face; size; color; highlight)
- Per-paragraph alignment (left / center / right / justify) via the Format menu or Ctrl+L/E/R/J
- Per-document page margins (Page > Margins…)
- Whole-document multi-column text with configurable count and gutter (Page > Columns…)
- Rich header and footer bands with three independent sub-slots each (Left / Center / Right). Each sub-slot can render the document filename, the page number in one of four formats (`Page N of M`, `Page N`, `N`, `N of M`), today's date, or a custom text string. Edited in Page > Header / Footer…
- Floating shapes and images (Word-97 `\shp`) with optional captions, isotropic text-standoff padding, and full text-wrap. Inserted via Insert > Image… (path + caption + padding in one dialog) or Insert > Shape…; selected/moved/resized with the mouse (corner handles); caption editable after the fact via Insert > Caption…
- Multi-page documents with Ctrl+Enter forced page breaks
- Vertical scrollbar with arrow buttons and thumb drag; cursor follows the scroll
- RTF load/save (Save As… `.rtf`; Ctrl+S on a `.txt` with formatting prompts to upgrade). Per-character formatting, alignment, page breaks, floating shapes/images, captions, padding, and columns all round-trip.
- Print to any installed Windows printer with paper-size / orientation / margin / range / copies controls. Printed pages match the on-screen WYSIWYG layout exactly (same line breaks, pagination, float positions, header/footer placement).
- Spell check with built-in 370k-word dictionary, optional misspell highlighting, per-user add/remove
- Theme picker (green phosphor / white office)
- Find with case-insensitive option
- Selection, cut/copy/paste (including Ctrl+V from the system clipboard with UTF-8 support and focused-field routing in multi-field dialogs)
- Undo / redo (one step per edit gesture, including float move/resize)
- Per-document settings (margins, header/footer slot configuration, word wrap, word count visibility) persisted in a central per-user store under `%APPDATA%\RetroDocWriter\documents\` — no sidecar files are dropped next to your `.rtf`. Entries for files that no longer exist are garbage-collected on launch after a 30-day grace window so an accidental delete + undelete keeps the saved settings. Legacy `.retroedit` sidecars from earlier versions are migrated forward automatically on first open.
- Keyboard-first; mouse is a convenience

## Building

Requires CMake 3.24+ and Visual Studio 2022 (MSVC). SDL3 and SDL3_ttf are vendored in the repo — no external package installs needed.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64    # one-time configure
cmake --build build --config Debug                       # Debug build
cmake --build build --config Release                     # Release build
.\build\Debug\RetroDocWriter.exe                         # run
```

The build copies `SDL3.dll`, `SDL3_ttf.dll`, and the `assets/` directory next to the executable, so it runs in place.

## Project layout

```
src/
  main.cpp        entry point
  app/            Cursor, Application orchestrator
  editor/         TextBuffer / FormattedTextBuffer, FileDocument /
                  RichFileDocument, UndoHistory / RichUndoHistory,
                  Selection, WordWrap, WordCount, Dictionary,
                  Dictionary.gen, CharStyle, Palette, Utf8,
                  RtfReader, RtfWriter, FileSettings,
                  DocumentSettingsStore, FloatObject, HeaderFooter
  platform/       Window, AppData, AssetPath, Beep, Print, ImageDecode
  render/         ScreenBuffer, RetroRenderer, GlyphCache, FontFace,
                  FontSettings, Color, Theme, WysiwygRenderer,
                  ImageCache, PlacedSegment
  ui/             RetroUi (menus, dialogs, status bar), Layout, MenuDefs
assets/
  fonts/          Bundled TTFs (monospace + proportional)
  dictionary/     words.txt source for the embedded spell-check list
  themes/         (reserved; themes are currently compiled in)
Docs/
  RetroDocWriter.md   Design notes
SDL/              Vendored SDL3 source
SDL_ttf/          Vendored SDL3_ttf prebuilt binaries
SDL_image/        Vendored SDL3_image prebuilt binaries (raster image decode)
tools/
  gen_dictionary.ps1  Regenerate src/editor/Dictionary.gen.cpp from words.txt
```

## License

MIT — see [LICENSE](LICENSE).

Bundled fonts ship under the SIL Open Font License (see `assets/fonts/OFL-*.txt`). Vendored SDL3, SDL3_ttf, and SDL3_image are under the Zlib license.
