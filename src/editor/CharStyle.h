#pragma once
#include <cstdint>

// Per-character text style bits. Stored in CharFormat::style. Values
// intentionally match SDL_ttf's TTF_STYLE_* constants so the bitmask can
// be passed straight into TTF_SetFontStyle / GlyphCache without remapping.
namespace CharStyle
{
    enum Bits : uint8_t
    {
        None          = 0x00,
        Bold          = 0x01,
        Italic        = 0x02,
        Underline     = 0x04,
        Strikethrough = 0x08,
    };
}

// Per-paragraph horizontal alignment. A "paragraph" is one TextBuffer row
// (the RTF writer emits \par between rows); word-wrap into multiple visual
// segments happens only at render time. Stored per row in
// FormattedTextBuffer::m_alignment and round-tripped through RTF as
// \ql / \qc / \qr / \qj. Left is the default (RTF's default too).
enum class ParagraphAlign : uint8_t
{
    Left    = 0,
    Center  = 1,
    Right   = 2,
    Justify = 3,
};

// Bulleted-list layout constants, shared by the editor, the WYSIWYG
// renderer, the GDI print path, and the RTF reader/writer so the on-screen
// indent, the printed indent, and the round-tripped \li values all agree.
//   * kListIndentTwips — left indent added per nesting level (0.25").
//   * kMaxListLevel    — deepest level Tab can reach (bullet shapes cycle
//                        every 3 levels, so this caps the UI at a sane depth).
inline constexpr int kListIndentTwips = 360;   // 0.25" per level
inline constexpr int kMaxListLevel    = 6;

// A list paragraph's per-row state is packed into one byte (see
// FormattedTextBuffer::m_listLevel): the low bits hold the nesting level
// (0 = not a list, 1..kMaxListLevel), and kListNumberedFlag marks the item as
// a numbered list item rather than a bulleted one. Stored together so every
// text mutator carries the kind along with the level for free.
inline constexpr uint8_t kListLevelMask    = 0x3F;
inline constexpr uint8_t kListNumberedFlag = 0x40;

// Bullet glyph codepoint for a 1-based list level. Shapes cycle by depth:
// level 1 = • (U+2022), level 2 = ◦ (U+25E6), level 3 = ▪ (U+25AA), repeat.
inline char32_t ListBulletGlyph(int level)
{
    static const char32_t kBullets[3] = { U'•', U'◦', U'▪' };
    if (level < 1) level = 1;
    return kBullets[(level - 1) % 3];
}

// ASCII fallback bullet for fonts that lack the preferred shape: * / o / -
// per cycling depth. Same cycle order as ListBulletGlyph.
inline char32_t ListBulletGlyphAscii(int level)
{
    static const char32_t kFallback[3] = { U'*', U'o', U'-' };
    if (level < 1) level = 1;
    return kFallback[(level - 1) % 3];
}

// Per-character formatting record: one byte each for style bits, font
// face override, and font size override. Stored in FormattedTextBuffer's
// parallel m_formats vector — exactly one CharFormat per character byte
// of the inner TextBuffer.
//
// `face` and `size` are FontFace / FontSize enum indices, with the
// sentinel value `Inherit` (0xFF) meaning "use the document default
// (Application::m_documentFontSettings)", resolved at render time. A run
// pinned to a real index keeps it. Newly typed characters inherit the
// CharFormat of the caret's neighbor (Application::EffectiveTypingFormat),
// so a run continues whatever surrounds it; a no-selection Font dialog pick
// applies once at the caret, and a selection-targeted pick assigns real
// index values across the range.
struct CharFormat
{
    static constexpr uint8_t Inherit = 0xFF;
    uint8_t style     = 0;        // CharStyle bits
    uint8_t face      = Inherit;  // FontFace enum index, or Inherit
    uint8_t size      = Inherit;  // FontSize enum index, or Inherit
    uint8_t color     = Inherit;  // Palette index (0..15) — foreground
    uint8_t highlight = Inherit;  // Palette index (0..15) — background highlight

    bool IsPlain() const
    {
        return style == 0 && face == Inherit && size == Inherit
            && color == Inherit && highlight == Inherit;
    }
};
