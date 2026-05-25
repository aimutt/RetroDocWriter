# RetroDocWriter

A C++20 WYSIWYG document writer styled after the green-phosphor monochrome terminals of the early 1980s — modern dependencies, retro look. RetroDocWriter draws its own text-mode chrome (menu bar, status bar, dialogs, function-key bar) inside an SDL window, then paints a centered US-Letter page over the editor area at the document font's true point size, with proportional text, per-character formatting (bold / italic / underline / strikethrough, face, size, color, highlight), per-document margins, multi-page navigation via Ctrl+Enter forced page breaks, RTF load/save, GDI printing, and spell check.

Primary target is **Windows x64**. The architecture is portable; Linux and macOS builds are planned but not yet validated.

## Features

- Centered WYSIWYG page rendered at true point size (DPI-scaled to 96)
- Proportional fonts: EB Garamond (default), Source Serif 4, Source Sans 3, Open Sans
- Monospace fonts also available: Cascadia Mono, IBM Plex Mono, JetBrains Mono, VT323
- Chrome (menus / status bar / dialogs) is fixed at Cascadia Mono 16 pt — picking a different document font in Options > Font does not reshape the UI
- Per-character formatting (bold / italic / underline / strikethrough; face; size; color; highlight)
- Per-document page margins (Page > Margins…)
- Multi-page documents with Ctrl+Enter forced page breaks
- Vertical scrollbar with arrow buttons and thumb drag; cursor follows the scroll
- RTF load/save (Save As… `.rtf`; Ctrl+S on a `.txt` with formatting prompts to upgrade)
- Print to any installed Windows printer with paper-size / orientation / margin / range / copies controls
- Spell check with built-in 370k-word dictionary, optional misspell highlighting, per-user add/remove
- Theme picker (green phosphor / white office)
- Find with case-insensitive option
- Selection, cut/copy/paste (including Ctrl+V from the system clipboard with UTF-8 support)
- Undo / redo
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
                  RtfReader, RtfWriter, FileSettings
  platform/       Window, AppData, AssetPath, Beep, Print
  render/         ScreenBuffer, RetroRenderer, GlyphCache, FontFace,
                  FontSettings, Color, Theme, WysiwygRenderer
  ui/             RetroUi (menus, dialogs, status bar), Layout, MenuDefs
assets/
  fonts/          Bundled TTFs (monospace + proportional)
  dictionary/     words.txt source for the embedded spell-check list
  themes/         (reserved; themes are currently compiled in)
Docs/
  RetroDocWriter.md   Design notes
SDL/              Vendored SDL3 source
SDL_ttf/          Vendored SDL3_ttf prebuilt binaries
tools/
  gen_dictionary.ps1  Regenerate src/editor/Dictionary.gen.cpp from words.txt
```

## License

MIT — see [LICENSE](LICENSE).

Bundled fonts ship under the SIL Open Font License (see `assets/fonts/OFL-*.txt`). Vendored SDL3 and SDL3_ttf are under the Zlib license.
