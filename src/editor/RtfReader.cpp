#include "RtfReader.h"
#include "editor/CharStyle.h"
#include "editor/Palette.h"
#include "render/FontFace.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
    // Map a body-encountered point size to the closest FontSize enum index.
    // FontSize has four discrete values (Small=12, Medium=16, Large=20,
    // ExtraLarge=24); anything in between snaps to the nearest bucket.
    uint8_t MapPointsToSizeIdx(int pt)
    {
        if (pt <= 13) return 0; // Small
        if (pt <= 17) return 1; // Medium
        if (pt <= 22) return 2; // Large
        return 3;               // ExtraLarge
    }

    // Single-pass RTF parser. We don't build an AST — we walk the byte
    // stream and emit characters with the current style/face/size as we go.
    //
    // State that matters:
    //   - braceDepth, skipFromDepth, styleStack, curStyle: as before
    //   - fontTblOpenDepth: brace depth where \fonttbl opened (0 = not in)
    //   - fontTblNames: map of \fN index -> family name parsed from
    //     {\f0 Cascadia Mono;}{\f1 JetBrains Mono;}... groups
    //   - curFaceIdx: body-level \fN state. -1 means "no \fN seen since
    //     start of body" (= Inherit). \fN sets it to N; the resolved face
    //     comes from fontTblNames + FontFaceFromFamilyName.
    //   - curHalfPt:   body-level \fsN state. 0 = "no body \fs seen" (=
    //     Inherit). headerFsHalfPoints captures the pre-body \fs so the
    //     resolver can fold "matches doc default" back to Inherit.
    struct Parser
    {
        const std::string& src;
        FormattedTextBuffer& out;
        size_t pos = 0;
        int braceDepth   = 0;
        int skipFromDepth = 0;
        uint8_t curStyle = 0;
        std::vector<uint8_t> styleStack;
        std::vector<std::string>            lines;
        std::vector<std::vector<CharFormat>> formatRows;

        // Header capture
        int headerFsHalfPoints = 0;
        int defaultFontIdx     = 0;     // \deffN value (defaults to 0)

        // Font table parsing
        int fontTblOpenDepth = 0;
        int captureFontIdx   = -1;      // -1 = not capturing a name
        std::string captureFontName;
        std::unordered_map<int, std::string> fontTblNames;

        // Body run state
        int curFaceIdx = -1;            // -1 = Inherit
        int curHalfPt  = 0;             // 0  = Inherit
        int curColorIdx = -1;           // -1 = Inherit, else palette index
        int curHighlightIdx = -1;       // -1 = Inherit, else palette index

        // Color table parsing — sibling of fonttbl with its own depth marker.
        // readerColorMap maps RTF \cfN index → palette index (or -1 for the
        // RTF "auto" sentinel that we treat as Inherit).
        int colorTblOpenDepth = 0;
        int colorR = 0, colorG = 0, colorB = 0;
        bool colorEntryStarted = false;
        std::vector<int> readerColorMap;

        // Page-break parsing. \page is a paragraph-level control that
        // forces the *next* paragraph onto a new page. We can't always
        // know "the next paragraph" at parse time — so we set a pending
        // flag, and when the next newline opens a new line, we transfer
        // the flag to that line's page-break-before slot.
        bool pendingPageBreak = false;
        std::vector<bool> pageBreakBefore;   // mirrors lines

        // Paragraph alignment. \ql/\qc/\qr/\qj set curAlignment; \pard resets
        // it to Left. Alignment is sticky in RTF, so a new paragraph inherits
        // the current value until reset. lineAlignment mirrors `lines`.
        uint8_t curAlignment = static_cast<uint8_t>(ParagraphAlign::Left);
        std::vector<uint8_t> lineAlignment;

        // Whole-document columns (\cols / \colsx). Last value seen wins.
        int docCols       = 1;
        int docColsGutter = 720;

        // Shape (\shp) parsing. While inShape, body emission is suppressed and
        // bytes route through handleShapeByte (pict hex / caption / discard).
        // Depths record the brace level of the {\shp}, {\pict}, {\shptxt} and
        // {\shprslt} groups so the matching `}` can finalize/close each.
        bool inShape = false;
        int  shpBraceDepth     = 0;
        int  pictBraceDepth    = 0;
        int  shptxtBraceDepth  = 0;
        int  shprsltBraceDepth = 0;   // fallback rendering — skipped entirely
        FloatObject curShape;
        std::string pictHex;
        std::vector<FloatObject> floats;

        Parser(const std::string& s, FormattedTextBuffer& o) : src(s), out(o)
        {
            lines.emplace_back();
            formatRows.emplace_back();
            pageBreakBefore.push_back(false);
            lineAlignment.push_back(static_cast<uint8_t>(ParagraphAlign::Left));
        }

        // Route a literal byte while inside a shape: pict hex (only at the
        // pict group's own brace level, so nested {\*\blipuid …} hex is not
        // mistaken for image data), caption text, or discard.
        void handleShapeByte(unsigned char b)
        {
            if (shprsltBraceDepth > 0) return;
            if (pictBraceDepth > 0)
            {
                if (braceDepth == pictBraceDepth && HexDigit(static_cast<char>(b)) >= 0)
                    pictHex.push_back(static_cast<char>(b));
                return;
            }
            if (shptxtBraceDepth > 0)
            {
                if (b == '\n')      curShape.caption.push_back(' ');
                else if (b != '\r') curShape.caption.push_back(static_cast<char>(b));
                return;
            }
            // structural bytes (property names/values) — discard
        }

        static void DecodeHexInto(std::vector<uint8_t>& out, const std::string& hex)
        {
            int hi = -1;
            for (char c : hex)
            {
                int d = HexDigit(c);
                if (d < 0) continue;
                if (hi < 0) hi = d;
                else { out.push_back(static_cast<uint8_t>((hi << 4) | d)); hi = -1; }
            }
        }

        bool skipping() const { return skipFromDepth > 0; }

        // Resolve curFaceIdx → CharFormat::face byte. Inherit when
        //   - no body \fN seen yet
        //   - curFaceIdx matches the document-default index (\deffN, ~ 0)
        //     so chars at the doc default round-trip without becoming
        //     "locked-in" explicit overrides.
        uint8_t ResolveFaceForEmit() const
        {
            if (curFaceIdx < 0) return CharFormat::Inherit;
            if (curFaceIdx == defaultFontIdx) return CharFormat::Inherit;
            auto it = fontTblNames.find(curFaceIdx);
            if (it == fontTblNames.end()) return CharFormat::Inherit;
            FontFace face;
            if (!FontFaceFromFamilyName(it->second.c_str(), face))
                return CharFormat::Inherit;
            return static_cast<uint8_t>(face);
        }

        // Resolve curHalfPt → CharFormat::size byte. Inherit when
        //   - no body \fsN seen yet (== 0)
        //   - body \fsN matches the header \fs (the doc default)
        uint8_t ResolveSizeForEmit() const
        {
            if (curHalfPt <= 0) return CharFormat::Inherit;
            if (headerFsHalfPoints > 0 && curHalfPt == headerFsHalfPoints)
                return CharFormat::Inherit;
            return MapPointsToSizeIdx(curHalfPt / 2);
        }

        // Resolve curColorIdx → CharFormat::color byte. -1 = Inherit.
        uint8_t ResolveColorForEmit() const
        {
            if (curColorIdx < 0) return CharFormat::Inherit;
            if (curColorIdx >= Palette::kCount) return CharFormat::Inherit;
            return static_cast<uint8_t>(curColorIdx);
        }

        uint8_t ResolveHighlightForEmit() const
        {
            if (curHighlightIdx < 0) return CharFormat::Inherit;
            if (curHighlightIdx >= Palette::kCount) return CharFormat::Inherit;
            return static_cast<uint8_t>(curHighlightIdx);
        }

        void emitByte(unsigned char b)
        {
            // Inside a shape, bytes never reach the document body — they feed
            // the pict/caption capture (or are discarded). Takes precedence
            // over the skip check so the \* destinations nested in a shape
            // (e.g. \*\blipuid) still route here rather than vanishing early.
            if (inShape) { handleShapeByte(b); return; }
            if (skipping()) return;
            if (b == '\n')
            {
                lines.emplace_back();
                formatRows.emplace_back();
                pageBreakBefore.push_back(pendingPageBreak);
                pendingPageBreak = false;
                // New paragraph inherits the current (sticky) alignment.
                lineAlignment.push_back(curAlignment);
            }
            else if (b == '\r' || b == 0)
            {
                // RFC: ignore \r in RTF body; \0 is invalid.
            }
            else
            {
                lines.back().push_back(static_cast<char>(b));
                CharFormat f;
                f.style = curStyle;
                f.face  = ResolveFaceForEmit();
                f.size  = ResolveSizeForEmit();
                f.color = ResolveColorForEmit();
                f.highlight = ResolveHighlightForEmit();
                formatRows.back().push_back(f);
            }
        }

        static int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool ReadOptionalParam(int& outVal)
        {
            size_t start = pos;
            bool neg = false;
            if (pos < src.size() && src[pos] == '-') { neg = true; ++pos; }
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos])))
            {
                pos = start;
                return false;
            }
            int v = 0;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
            {
                v = v * 10 + (src[pos] - '0');
                ++pos;
            }
            outVal = neg ? -v : v;
            return true;
        }

        void ConsumeControlWordDelimiter()
        {
            if (pos < src.size() && src[pos] == ' ')
                ++pos;
        }

        bool IsDestinationKeyword(const std::string& w) const
        {
            // Note: \colortbl is NOT in this list — it's handled specially
            // so the parser can capture \red\green\blue triples instead of
            // dropping the contents.
            return w == "fonttbl" || w == "filetbl"
                || w == "stylesheet" || w == "listtable" || w == "rsidtbl"
                || w == "info" || w == "pict" || w == "header" || w == "footer"
                || w == "headerl" || w == "headerr" || w == "footerl" || w == "footerr"
                || w == "themedata" || w == "datastore" || w == "object"
                || w == "field" || w == "shppict" || w == "nonshppict"
                || w == "bkmkstart" || w == "bkmkend" || w == "xe" || w == "tc";
        }

        // True iff we haven't emitted any body text yet (so \fs / \deff
        // seen now refers to document-level defaults).
        bool InHeader() const
        {
            return lines.size() == 1 && lines.back().empty();
        }

        // Shape control words. Returns true if `word` was consumed by shape
        // parsing (so the generic body handlers below are skipped).
        bool HandleShapeWord(const std::string& word, bool hasParam, int param)
        {
            if (word == "shp")
            {
                inShape = true;
                shpBraceDepth = braceDepth;
                curShape = FloatObject{};
                curShape.anchorRow = static_cast<int>(lines.size()) - 1;
                pictHex.clear();
                pictBraceDepth = shptxtBraceDepth = shprsltBraceDepth = 0;
                return true;
            }
            if (!inShape) return false;

            // \*\shpinst — cancel the \* skip so the instance is parsed.
            if (word == "shpinst")
            {
                if (skipFromDepth == braceDepth) skipFromDepth = 0;
                return true;
            }
            if (word == "shprslt") { shprsltBraceDepth = braceDepth; return true; }
            if (word == "shptxt")  { shptxtBraceDepth  = braceDepth; return true; }
            if (shprsltBraceDepth == 0)
            {
                if (word == "shpleft"   && hasParam) { curShape.left   = param; return true; }
                if (word == "shptop"    && hasParam) { curShape.top    = param; return true; }
                if (word == "shpright"  && hasParam) { curShape.right  = param; return true; }
                if (word == "shpbottom" && hasParam) { curShape.bottom = param; return true; }
                if (word == "shpbxpage")   { curShape.hRef = FloatObject::HRef::Page;      return true; }
                if (word == "shpbxmargin") { curShape.hRef = FloatObject::HRef::Margin;    return true; }
                if (word == "shpbxcolumn") { curShape.hRef = FloatObject::HRef::Column;    return true; }
                if (word == "shpbypage")   { curShape.vRef = FloatObject::VRef::Page;      return true; }
                if (word == "shpbymargin") { curShape.vRef = FloatObject::VRef::Margin;    return true; }
                if (word == "shpbypara")   { curShape.vRef = FloatObject::VRef::Paragraph; return true; }
                if (word == "shpwr"      && hasParam) { curShape.wrapType  = param;       return true; }
                if (word == "shpwrk"     && hasParam) { curShape.wrapSide  = param;       return true; }
                if (word == "shpz"       && hasParam) { curShape.zOrder    = param;       return true; }
                if (word == "shpfblwtxt" && hasParam) { curShape.belowText = (param != 0); return true; }
                if (word == "pict")     { pictBraceDepth = braceDepth; pictHex.clear(); return true; }
                if (word == "pngblip")  { curShape.isPng = true;  return true; }
                if (word == "jpegblip") { curShape.isPng = false; return true; }
            }
            // Any other control word inside a shape (\sp/\sn/\sv/\picw/…) is
            // consumed and ignored so it never reaches the body handlers.
            return true;
        }

        void HandleControlWord(const std::string& word, bool hasParam, int param)
        {
            if (HandleShapeWord(word, hasParam, param)) return;
            if (word == "par" || word == "line")
            {
                emitByte('\n');
                return;
            }
            if (word == "page")
            {
                // Forces the next paragraph onto a new page. If the
                // current line is non-empty, also start a new line so
                // the break sits before fresh text. Otherwise just flag
                // the existing line (which is currently the "next" one
                // pending text).
                if (!lines.back().empty())
                    emitByte('\n');
                pendingPageBreak = true;
                // If we just opened a new line, the pendingPageBreak set
                // above was already transferred into pageBreakBefore.back()
                // — promote it now so the flag actually lives on the new
                // line rather than waiting for a subsequent \n.
                if (!lines.back().empty() == false && !pageBreakBefore.empty())
                {
                    pageBreakBefore.back() = true;
                    pendingPageBreak = false;
                }
                return;
            }
            auto setBit = [&](uint8_t bit, bool on) {
                if (skipping()) return;
                if (on) curStyle |=  bit;
                else    curStyle &= ~bit;
            };
            if (word == "b")      { setBit(CharStyle::Bold,          !(hasParam && param == 0)); return; }
            if (word == "i")      { setBit(CharStyle::Italic,        !(hasParam && param == 0)); return; }
            if (word == "strike") { setBit(CharStyle::Strikethrough, !(hasParam && param == 0)); return; }
            if (word == "ul")     { setBit(CharStyle::Underline,     !(hasParam && param == 0)); return; }
            if (word == "ulnone") { setBit(CharStyle::Underline, false); return; }

            // Whole-document multi-column layout.
            if (word == "cols"  && hasParam) { docCols = param; return; }
            if (word == "colsx" && hasParam) { docColsGutter = param; return; }

            // Paragraph alignment. Applies to the current (in-progress) line;
            // sticky for following paragraphs until \pard resets it to Left.
            auto setAlign = [&](ParagraphAlign a) {
                if (skipping()) return;
                curAlignment = static_cast<uint8_t>(a);
                if (!lineAlignment.empty()) lineAlignment.back() = curAlignment;
            };
            if (word == "ql")   { setAlign(ParagraphAlign::Left);    return; }
            if (word == "qc")   { setAlign(ParagraphAlign::Center);  return; }
            if (word == "qr")   { setAlign(ParagraphAlign::Right);   return; }
            if (word == "qj")   { setAlign(ParagraphAlign::Justify); return; }
            // \pard resets paragraph properties to their defaults (alignment
            // back to Left). Character props are unaffected (that's \plain).
            if (word == "pard") { setAlign(ParagraphAlign::Left);    return; }

            if (word == "deff" && hasParam)
            {
                defaultFontIdx = param;
                return;
            }

            if (word == "fs" && hasParam)
            {
                // The FIRST \fs encountered is the document-default size.
                // Any subsequent \fs — even if seen before the first body
                // byte (the writer emits one right before a heading at
                // the document start) — is a body-run override.
                // Using "no body byte yet" as the header test, as we did
                // originally, corrupts headerFsHalfPoints when a doc
                // opens with a heading: the heading's \fs overwrites the
                // real header size and the editor inherits the heading
                // size as the global chrome cell size.
                if (headerFsHalfPoints == 0)
                    headerFsHalfPoints = param;
                else
                    curHalfPt = param;
                return;
            }

            if (word == "f" && hasParam)
            {
                // Inside \fonttbl: this introduces a font-table entry; we
                // begin capturing the literal text bytes that follow as the
                // entry's family name until ';' or '}'.
                if (fontTblOpenDepth > 0)
                {
                    captureFontIdx = param;
                    captureFontName.clear();
                    return;
                }
                // In the body: \fN switches the active face for following
                // text. The renderer falls back to doc default when the
                // index is unmapped (e.g., font missing from the table).
                curFaceIdx = param;
                return;
            }

            // Color table contents (\red, \green, \blue accumulate into a
            // pending triple; the next ';' literal commits it).
            if (colorTblOpenDepth > 0)
            {
                if (word == "red"   && hasParam) { colorR = param; colorEntryStarted = true; return; }
                if (word == "green" && hasParam) { colorG = param; colorEntryStarted = true; return; }
                if (word == "blue"  && hasParam) { colorB = param; colorEntryStarted = true; return; }
            }

            // \colortbl: start capturing palette entries. Unlike fonttbl we
            // do NOT add skipFromDepth — we want the parser to process
            // literal ';' inside the group as the entry-terminator.
            if (word == "colortbl")
            {
                colorTblOpenDepth = braceDepth;
                return;
            }

            // \cfN — switch active foreground for the body run. Index into
            // readerColorMap (0 = auto = Inherit; 1+ = palette).
            if (word == "cf" && hasParam && colorTblOpenDepth == 0)
            {
                if (param >= 0 && param < static_cast<int>(readerColorMap.size()))
                    curColorIdx = readerColorMap[param];
                else
                    curColorIdx = -1;
                return;
            }

            // \highlightN — same shape as \cfN but writes to the background
            // highlight slot. Word also writes \highlightN; LibreOffice
            // emits \chcbpat or \cb depending on settings — those would
            // need separate handling and are deferred.
            if (word == "highlight" && hasParam && colorTblOpenDepth == 0)
            {
                if (param >= 0 && param < static_cast<int>(readerColorMap.size()))
                    curHighlightIdx = readerColorMap[param];
                else
                    curHighlightIdx = -1;
                return;
            }

            if (IsDestinationKeyword(word))
            {
                if (skipFromDepth == 0)
                    skipFromDepth = braceDepth;
                if (word == "fonttbl")
                    fontTblOpenDepth = braceDepth;
                return;
            }
        }

        // While inside \fonttbl and capturing a font name, drain literal
        // text bytes into captureFontName until we hit ';' or any
        // structural character; finalize on terminator.
        bool FeedFontTableCapture()
        {
            if (captureFontIdx < 0) return false;
            char peek = src[pos];
            if (peek == ';' || peek == '}' || peek == '{' || peek == '\\')
            {
                // Strip trailing whitespace from the captured name.
                while (!captureFontName.empty()
                       && (captureFontName.back() == ' '
                           || captureFontName.back() == '\t'))
                    captureFontName.pop_back();
                fontTblNames[captureFontIdx] = std::move(captureFontName);
                captureFontIdx = -1;
                captureFontName.clear();
                return false; // let normal parsing handle the char
            }
            ++pos;
            // Skip leading whitespace.
            if (!(captureFontName.empty() && (peek == ' ' || peek == '\t')))
                captureFontName.push_back(peek);
            return true;
        }

        void ParseOne()
        {
            // While inside the \colortbl group, intercept literal bytes
            // here so they don't reach the document body. ';' commits the
            // current \red\green\blue triple (or an empty "auto" entry).
            // '\', '{', '}' fall through so braces and control words still
            // close / open the group correctly. Everything else (spaces,
            // newlines) is silently dropped.
            if (colorTblOpenDepth > 0 && pos < src.size())
            {
                char peek = src[pos];
                if (peek == ';')
                {
                    if (colorEntryStarted)
                    {
                        Color rgb{
                            static_cast<uint8_t>(std::clamp(colorR, 0, 255)),
                            static_cast<uint8_t>(std::clamp(colorG, 0, 255)),
                            static_cast<uint8_t>(std::clamp(colorB, 0, 255)),
                            255
                        };
                        readerColorMap.push_back(
                            static_cast<int>(Palette::NearestIndex(rgb)));
                    }
                    else
                    {
                        // Empty entry — RTF "auto" → Inherit in our model.
                        readerColorMap.push_back(-1);
                    }
                    colorR = colorG = colorB = 0;
                    colorEntryStarted = false;
                    ++pos;
                    return;
                }
                if (peek != '\\' && peek != '{' && peek != '}')
                {
                    ++pos;
                    return;
                }
                // Fall through for \ { } so the parser still sees them.
            }

            if (FeedFontTableCapture()) return;

            char ch = src[pos++];
            if (ch == '{')
            {
                ++braceDepth;
                styleStack.push_back(curStyle);
                return;
            }
            if (ch == '}')
            {
                if (skipFromDepth > 0 && braceDepth == skipFromDepth)
                    skipFromDepth = 0;
                if (fontTblOpenDepth > 0 && braceDepth == fontTblOpenDepth)
                    fontTblOpenDepth = 0;
                if (colorTblOpenDepth > 0 && braceDepth == colorTblOpenDepth)
                    colorTblOpenDepth = 0;
                if (inShape)
                {
                    if (pictBraceDepth > 0 && braceDepth == pictBraceDepth)
                    {
                        DecodeHexInto(curShape.imageBytes, pictHex);
                        pictHex.clear();
                        pictBraceDepth = 0;
                    }
                    if (shptxtBraceDepth > 0 && braceDepth == shptxtBraceDepth)
                        shptxtBraceDepth = 0;
                    if (shprsltBraceDepth > 0 && braceDepth == shprsltBraceDepth)
                        shprsltBraceDepth = 0;
                    if (shpBraceDepth > 0 && braceDepth == shpBraceDepth)
                    {
                        // Finalize: image if it carried a \pict, else a box.
                        curShape.kind = curShape.imageBytes.empty()
                                      ? FloatObject::Kind::Box
                                      : FloatObject::Kind::Image;
                        if (curShape.kind == FloatObject::Kind::Image)
                            curShape.imageId = NextFloatImageId();
                        if (curShape.anchorRow < 0) curShape.anchorRow = 0;
                        // Trim trailing whitespace the caption capture may have left.
                        while (!curShape.caption.empty()
                               && (curShape.caption.back() == ' '
                                || curShape.caption.back() == '\t'))
                            curShape.caption.pop_back();
                        floats.push_back(std::move(curShape));
                        inShape = false;
                        shpBraceDepth = 0;
                    }
                }
                if (!styleStack.empty())
                {
                    curStyle = styleStack.back();
                    styleStack.pop_back();
                }
                if (braceDepth > 0) --braceDepth;
                return;
            }
            if (ch == '\\')
            {
                if (pos >= src.size()) return;
                char ch2 = src[pos];
                if (!std::isalpha(static_cast<unsigned char>(ch2)))
                {
                    ++pos;
                    if (ch2 == '\\') { emitByte('\\'); return; }
                    if (ch2 == '{')  { emitByte('{');  return; }
                    if (ch2 == '}')  { emitByte('}');  return; }
                    if (ch2 == '\n' || ch2 == '\r')
                    {
                        emitByte('\n');
                        return;
                    }
                    if (ch2 == '\'')
                    {
                        if (pos + 1 < src.size())
                        {
                            int hi = HexDigit(src[pos]);
                            int lo = HexDigit(src[pos + 1]);
                            pos += 2;
                            if (hi >= 0 && lo >= 0)
                                emitByte(static_cast<unsigned char>((hi << 4) | lo));
                        }
                        return;
                    }
                    if (ch2 == '*')
                    {
                        if (skipFromDepth == 0)
                            skipFromDepth = braceDepth;
                        return;
                    }
                    return;
                }
                std::string word;
                while (pos < src.size() && std::isalpha(static_cast<unsigned char>(src[pos])))
                {
                    word.push_back(src[pos]);
                    ++pos;
                }
                int param = 0;
                bool hasParam = ReadOptionalParam(param);
                ConsumeControlWordDelimiter();
                HandleControlWord(word, hasParam, param);
                return;
            }
            if (ch == '\n' || ch == '\r')
            {
                return;
            }
            emitByte(static_cast<unsigned char>(ch));
        }

        void Run()
        {
            while (pos < src.size())
                ParseOne();
        }

        void Finalize()
        {
            if (lines.size() > 1 && lines.back().empty() && formatRows.back().empty())
            {
                lines.pop_back();
                formatRows.pop_back();
                if (!pageBreakBefore.empty()) pageBreakBefore.pop_back();
                if (!lineAlignment.empty()) lineAlignment.pop_back();
            }
            // Make pageBreakBefore + lineAlignment exactly line-count sized.
            pageBreakBefore.resize(lines.size(), false);
            lineAlignment.resize(lines.size(),
                                 static_cast<uint8_t>(ParagraphAlign::Left));
            // Clamp shape anchors into range (a popped trailing line could
            // leave an anchor one past the end), then install them. Always
            // SetFloats — even when empty — so stale shapes from a prior load
            // on the same buffer are cleared.
            int lastRow = static_cast<int>(lines.size()) - 1;
            for (auto& f : floats)
                if (f.anchorRow > lastRow) f.anchorRow = lastRow;
            out.SetLines(std::move(lines), std::move(formatRows),
                         std::move(pageBreakBefore), std::move(lineAlignment));
            out.SetFloats(std::move(floats));
            out.SetColumns(docCols, docColsGutter);
        }
    };
}

namespace RtfReader
{

bool Read(const std::string& rtf, FormattedTextBuffer& out, Header* outHeader)
{
    if (rtf.empty()) return false;
    Parser p(rtf, out);
    p.Run();
    p.Finalize();
    if (outHeader)
    {
        outHeader->pointSize = p.headerFsHalfPoints > 0 ? p.headerFsHalfPoints / 2 : 0;
        // Family name of the default \fN entry (index 0 unless \deff
        // overrode it). Used by Application::OpenFile to restore the
        // document's saved font face.
        auto it = p.fontTblNames.find(p.defaultFontIdx);
        if (it != p.fontTblNames.end()) outHeader->fontFamily = it->second;
        else                            outHeader->fontFamily.clear();
    }
    return true;
}

bool ReadFile(const std::string& path, FormattedTextBuffer& out, Header* outHeader)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    std::stringstream ss;
    ss << file.rdbuf();
    return Read(ss.str(), out, outHeader);
}

} // namespace RtfReader
