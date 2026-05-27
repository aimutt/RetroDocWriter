#include "WysiwygRenderer.h"
#include "render/GlyphCache.h"
#include "render/FontSettings.h"
#include "editor/CharStyle.h"
#include "editor/Palette.h"
#include "editor/TextBuffer.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/Utf8.h"
#include "platform/AssetPath.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // US Letter, hardcoded for Phase 1.
    constexpr double kPaperWidthIn  = 8.5;
    constexpr double kPaperHeightIn = 11.0;
    constexpr int    kPageGapPx     = 16; // empty space between stacked page rects

    void FillRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_FRect rect{ static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h) };
        SDL_RenderFillRect(r, &rect);
    }

    void StrokeRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_FRect rect{ static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h) };
        SDL_RenderRect(r, &rect);
    }

    // Resolve a CharFormat's face/size with fallback to document defaults.
    inline FontFace ResolveFace(const CharFormat& f, FontFace defaultFace) {
        if (f.face == CharFormat::Inherit) return defaultFace;
        if (f.face >= static_cast<uint8_t>(FontFace::Count_)) return defaultFace;
        return static_cast<FontFace>(f.face);
    }
    inline int ResolveSize(const CharFormat& f, int defaultPointSize) {
        if (f.size == CharFormat::Inherit) return defaultPointSize;
        if (f.size >= 4) return defaultPointSize; // FontSize enum has 4 values
        return FontSizePoints(FontSizeAt(static_cast<int>(f.size)));
    }
    // Per-char foreground color. Inherit-sentinel falls back to the theme's
    // normal-text color so unstyled text reflows visually when the theme
    // changes; explicitly-colored chars stay pinned to their palette entry.
    inline Color ResolveColor(const CharFormat& f, Color themeNormal) {
        if (f.color == CharFormat::Inherit) return themeNormal;
        if (f.color >= Palette::kCount)     return themeNormal;
        return Palette::ColorAt(f.color);
    }

    // Per-segment horizontal-alignment geometry. Shared by Draw, HitTest, and
    // ComputeVisualLayout so the painted glyphs, the click→column map, and
    // arrow-key navigation all agree.
    //   xOffset      pixels to shift the whole segment right (Center/Right)
    //   spaceStretch extra width added after each space char (Justify only)
    struct SegAlign { int xOffset = 0; double spaceStretch = 0.0; };

    // contentW   = full advance width of the segment's glyphs
    // trimmedW   = width up to the last non-space glyph (Center/Right ignore
    //              trailing whitespace so the visible text centers)
    // spaceCount = number of stretchable space glyphs in the segment
    // isLastSeg  = true for a paragraph's final visual segment (Justify leaves
    //              the last line flush-left, matching word processors)
    inline SegAlign ComputeSegAlign(double contentW, double trimmedW, int usableW,
                                    ParagraphAlign a, bool isLastSeg, int spaceCount)
    {
        SegAlign out;
        switch (a)
        {
            case ParagraphAlign::Left:
                break;
            case ParagraphAlign::Center:
            {
                double slack = usableW - trimmedW;
                if (slack > 0) out.xOffset = static_cast<int>(slack / 2.0 + 0.5);
                break;
            }
            case ParagraphAlign::Right:
            {
                double slack = usableW - trimmedW;
                if (slack > 0) out.xOffset = static_cast<int>(slack + 0.5);
                break;
            }
            case ParagraphAlign::Justify:
            {
                double slack = usableW - contentW;
                if (!isLastSeg && spaceCount > 0 && slack > 0.0)
                    out.spaceStretch = slack / spaceCount;
                break;
            }
        }
        return out;
    }
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

GlyphCache* WysiwygRenderer::CacheFor(FontFace face, int pointSize, int dpi)
{
    if (dpi != m_lastDpi)
    {
        // DPI change invalidates every cache — the pxSize used at
        // construction depends on dpi. Drop everything and rebuild lazily.
        m_caches.clear();
        m_lastDpi = dpi;
    }
    pointSize = std::max(1, pointSize);
    uint32_t key = MakeCacheKey(face, pointSize);
    auto it = m_caches.find(key);
    if (it != m_caches.end()) return it->second.get();

    // Pixel size at the renderer's effective dpi (typically 72 on screen).
    // Matches CreateFont's -MulDiv(point, dpi, 72) so the per-cell metrics
    // align with what the print path will produce.
    int pxSize = std::max(1, (pointSize * dpi + 36) / 72);
    auto cache = std::make_unique<GlyphCache>(m_sdl, face, pxSize);
    GlyphCache* raw = cache.get();
    m_caches[key] = std::move(cache);
    return raw;
}

// ---------------------------------------------------------------------------
// Wrap + layout helpers (pixel-based)
// ---------------------------------------------------------------------------

namespace
{
    // Per-character resolved render data — captured once per Draw call to
    // avoid repeatedly walking the CharFormat / cache lookup per glyph.
    struct CharRender
    {
        char        ch;
        char32_t    cp;            // decoded codepoint at the lead byte; U' ' on continuation bytes
        int         advance;       // integer pixel advance used for drawing
        // Sub-pixel-precision advance, used ONLY for wrap-budget math so
        // the on-screen wrap column matches LibreOffice / print (which
        // both lay out at high enough resolution that integer rounding
        // is negligible). At dpi=72 with small ptSize, the integer
        // `advance` is truncated by 0.5+ pixels per char, which adds up
        // to a few extra chars per line vs. the "true" font metric.
        double      advanceSubpx;
        int         lineHeight;  // px (LineHeight of the cache used)
        GlyphCache* cache;
        uint8_t     style;
        Color       color;       // resolved per-char foreground (theme default if Inherit)
        bool        hasHighlight;
        Color       highlight;   // background highlight (only valid when hasHighlight)
    };

    // Per-segment metadata after pixel-based wrap. `startCol` is the
    // source column where this visual segment begins; `endCol` is exclusive.
    // `height` is max LineHeight among chars in the segment (or the empty-
    // line fallback when the segment has no chars).
    //
    // page/yInPage are filled by the layout pass's pagination so every
    // consumer reads one shared placement instead of re-paginating.
    // xOffset/width describe the segment's horizontal run within the column:
    // for a normal line xOffset=0, width=usableW; beside a wrapping float the
    // run shrinks/shifts so text flows around it.
    struct LineSegment
    {
        int startCol;
        int endCol;
        int height;
        int page    = 0;
        int yInPage = 0;
        int xOffset = 0;
        int width   = 0;
    };

    // Measure a wrapped segment [segLo, segHi) for alignment: full sub-pixel
    // content width, the width up to the last non-space glyph (for Center/
    // Right trimming), and the count of stretchable space glyphs (Justify).
    inline void MeasureSegment(const std::vector<CharRender>& chars,
                               int segLo, int segHi,
                               double& contentW, double& trimmedW, int& spaceCount)
    {
        double cum = 0.0;
        double lastNonSpaceEnd = 0.0;
        int    spaces = 0;
        for (int c = segLo; c < segHi; ++c)
        {
            cum += chars[c].advanceSubpx;
            if (chars[c].ch == ' ' || chars[c].ch == '\t') ++spaces;
            else                                            lastNonSpaceEnd = cum;
        }
        contentW   = cum;
        trimmedW   = lastNonSpaceEnd;
        spaceCount = spaces;
    }

    // A float resolved into layout (page-content) coordinates. [yTop,yBottom)
    // is the image/box DRAW rect; [xLeft,xRight) its horizontal span — both
    // measured from the content top / usableX (so they already include the
    // active column's x-base). `exclBottom` is yBottom plus any caption band,
    // used for the text-reflow exclusion. `reflow` is true only for wrap types
    // that push text aside (1 square / 4 tight / 5 through). `obj` points at
    // the source FloatObject so the draw paths can read its image/box/caption.
    struct ResolvedFloat
    {
        const FloatObject* obj = nullptr;
        int  page = 0;
        int  yTop = 0, yBottom = 0;
        int  exclBottom = 0;
        int  xLeft = 0, xRight = 0;
        bool reflow = false;
    };

    // Widest free horizontal run within [colLeft,colRight] at vertical band
    // [y,y+h) on `page`, after removing the spans of reflowing floats that
    // overlap the band. With no overlap this returns the whole range, so a
    // float-free
    // document wraps exactly as before.
    void FreeRun(const std::vector<ResolvedFloat>& floats, int page, int y, int h,
                 int colLeft, int colRight, int& outL, int& outR)
    {
        outL = colLeft; outR = colRight;
        // Collect blocked x-intervals (clamped to the column range).
        std::vector<std::pair<int,int>> blocked;
        for (const auto& f : floats)
        {
            if (!f.reflow || f.page != page) continue;
            if (y >= f.exclBottom || y + h <= f.yTop) continue;  // no vertical overlap
            int bl = std::max(colLeft, f.xLeft);
            int br = std::min(colRight, f.xRight);
            if (br > bl) blocked.emplace_back(bl, br);
        }
        if (blocked.empty()) return;
        std::sort(blocked.begin(), blocked.end());
        // Widest gap between blocked spans across [colLeft, colRight].
        int bestL = colLeft, bestW = 0, cursor = colLeft;
        for (auto& b : blocked)
        {
            if (b.first - cursor > bestW) { bestW = b.first - cursor; bestL = cursor; }
            cursor = std::max(cursor, b.second);
        }
        if (colRight - cursor > bestW) { bestW = colRight - cursor; bestL = cursor; }
        if (bestW <= 0) return;            // fully blocked → fall back to whole column
        outL = bestL; outR = bestL + bestW;
    }

    struct ChunkResult { int endExcl; int nextCol; int height; };

    // Wrap the next visual segment of `chars` starting at `startCol` into
    // `availW` pixels. Mirrors WrapLinePixels' break decision (greedy, break at
    // last whitespace, consume it) but returns just the first break so the
    // layout sweep can re-query the available width per segment. Always places
    // at least one character, so a narrow run never stalls.
    ChunkResult WrapChunk(const std::vector<CharRender>& chars, int startCol,
                          double availW, int emptyLineHeight)
    {
        const int n = static_cast<int>(chars.size());
        if (startCol >= n) return { n, n, emptyLineHeight };
        double xAccum = 0.0;
        int    lastBreak = -1;
        for (int c = startCol; c < n; ++c)
        {
            if (xAccum + chars[c].advanceSubpx > availW && c > startCol)
            {
                int breakAt = (lastBreak > startCol) ? lastBreak : c;
                int nextCol = (breakAt == c) ? c : breakAt + 1;
                int hh = 0;
                for (int k = startCol; k < breakAt; ++k)
                    if (chars[k].lineHeight > hh) hh = chars[k].lineHeight;
                return { breakAt, nextCol, hh > 0 ? hh : emptyLineHeight };
            }
            xAccum += chars[c].advanceSubpx;
            if (chars[c].ch == ' ' || chars[c].ch == '\t') lastBreak = c;
        }
        int hh = 0;
        for (int k = startCol; k < n; ++k)
            if (chars[k].lineHeight > hh) hh = chars[k].lineHeight;
        return { n, n, hh > 0 ? hh : emptyLineHeight };
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WysiwygRenderer::WysiwygRenderer(SDL_Renderer* sdl, const Theme& theme)
    : m_sdl(sdl), m_theme(theme), m_imageCache(sdl)
{
}

WysiwygRenderer::~WysiwygRenderer()
{
    for (auto& kv : m_hiRes)
    {
        if (kv.second.font)
            TTF_CloseFont(static_cast<TTF_Font*>(kv.second.font));
    }
}

double WysiwygRenderer::SubpxAdvance(FontFace face, int pxSize,
                                     unsigned int codepoint)
{
    // High-precision per-em advance: open the font at a large reference
    // size (1000 px), read the integer advance there, scale back down to
    // the requested rendered pixel height. At kRef=1000 a single-pixel
    // rounding error is 0.1 % — negligible — so the resulting sub-pixel
    // advance at small ptSizes is accurate to a fraction of a pixel.
    // `pxSize` must be the actual rendered pixel height of the glyph
    // (point size scaled by the screen DPI), NOT the raw point size —
    // otherwise the wrap budget undershoots the rendered line width
    // whenever the renderer's effective DPI != 72.
    constexpr float kRef = 1000.0f;
    int key = static_cast<int>(face);
    auto it = m_hiRes.find(key);
    TTF_Font* hires = nullptr;
    if (it != m_hiRes.end())
    {
        hires = static_cast<TTF_Font*>(it->second.font);
    }
    else
    {
        std::string path = ResolveAssetPath(FontFaceFile(face));
        hires = TTF_OpenFont(path.c_str(), kRef);
        HiResEntry e; e.font = hires;
        m_hiRes[key] = e;
    }
    if (!hires) return pxSize / 2.0;
    int minx, maxx, miny, maxy, adv;
    if (TTF_GetGlyphMetrics(hires, static_cast<Uint32>(codepoint),
                            &minx, &maxx, &miny, &maxy, &adv) && adv > 0)
    {
        return (static_cast<double>(adv) / static_cast<double>(kRef))
             * static_cast<double>(pxSize);
    }
    return pxSize / 2.0;
}

int WysiwygRenderer::ComputeCharsPerLine(FontFace face, int ptSize,
                                          double leftMarginIn, double rightMarginIn)
{
    double usableIn = kPaperWidthIn - leftMarginIn - rightMarginIn;
    if (usableIn <= 0.0) return 1;

    // Open the font at the actual rendering ptSize and read the integer
    // glyph advance. Matches what GlyphCache produces at draw time so the
    // chars-per-line budget agrees with the renderer's actual layout. This
    // is now used only by the Print path and cursor-navigation helper —
    // the screen renderer wraps pixel-by-pixel based on per-char advance.
    std::string path = ResolveAssetPath(FontFaceFile(face));
    TTF_Font* font = TTF_OpenFont(path.c_str(), static_cast<float>(ptSize));
    int advancePx = 0;
    if (font)
    {
        int minx, maxx, miny, maxy, adv;
        if (TTF_GetGlyphMetrics(font, U'M', &minx, &maxx, &miny, &maxy, &adv)
            && adv > 0)
        {
            advancePx = adv;
        }
        TTF_CloseFont(font);
    }
    if (advancePx <= 0)
        advancePx = std::max(1, ptSize / 2);

    double charIn = advancePx / 72.0;
    if (charIn <= 0.0) return 1;
    int cpl = static_cast<int>(usableIn / charIn);
    return std::max(1, cpl);
}

// ---------------------------------------------------------------------------
// Pre-pass: build the per-character render data for an entire document.
// Returns one vector<CharRender> per buffer line.
// ---------------------------------------------------------------------------

namespace
{
    // Captures the layout state shared between Draw and ClampScrollForCursor
    // so both produce the exact same pagination.
    struct LayoutPass
    {
        std::vector<std::vector<CharRender>>  chars;     // [line][col]
        std::vector<std::vector<LineSegment>> segments;  // [line][segIdx]
        std::vector<ResolvedFloat>            floats;    // resolved during the sweep (draw + exclusion)
        int defaultLineHeight = 16;
        int totalPages        = 1;
    };

    // Geometry the placement sweep needs (all pixels except dpi).
    struct LayoutGeom
    {
        int dpi         = 96;
        int usableW     = 1;   // full content width (all columns + gutters)
        int usableH     = 1;
        int mTop        = 0;
        int mLeft       = 0;
        int columnCount = 1;
        int gutter      = 0;   // px between columns
    };
}

static void BuildLayoutPass(LayoutPass& out,
                            const TextBuffer& buf,
                            const FormattedTextBuffer* fmt,
                            FontFace defaultFace, int defaultPointSize,
                            Color themeNormalText,
                            const LayoutGeom& geom,
                            const std::vector<FloatObject>* floatObjs,
                            std::function<GlyphCache*(FontFace, int)> cacheFor,
                            std::function<double(FontFace, int, unsigned int)> subpxFor,
                            const std::vector<WysiwygRenderer::MisspelledSpan>* misspells = nullptr,
                            Color misspelledColor = Color{})
{
    int n = buf.LineCount();
    out.chars.clear();
    out.segments.clear();
    out.floats.clear();
    out.chars.resize(n);
    out.segments.resize(n);

    GlyphCache* defaultCache = cacheFor(defaultFace, defaultPointSize);
    out.defaultLineHeight = defaultCache ? defaultCache->LineHeight() : defaultPointSize;

    for (int li = 0; li < n; ++li)
    {
        const std::string& line = buf.Line(li);
        out.chars[li].reserve(line.size());
        for (size_t c = 0; c < line.size(); ++c)
        {
            CharFormat f = fmt ? fmt->FormatAt(li, static_cast<int>(c)) : CharFormat{};
            FontFace face = ResolveFace(f, defaultFace);
            int     ptSz = ResolveSize(f, defaultPointSize);
            GlyphCache* cache = cacheFor(face, ptSz);
            CharRender cr;
            cr.ch          = line[c];
            cr.style       = f.style;
            cr.cache       = cache;
            cr.lineHeight  = cache ? cache->LineHeight() : ptSz;
            // UTF-8 walk: lead byte holds the full codepoint + full advance.
            // Continuation bytes get zero advance and U' ' so the glyph-draw
            // path skips them (cr.cp == U' ' → cp > U' ' is false). Byte
            // indices stay 1:1 with CharRender entries so cursor/selection
            // logic elsewhere is unaffected.
            if (Utf8IsContinuationByte(static_cast<unsigned char>(line[c])))
            {
                cr.cp           = U' ';
                cr.advance      = 0;
                cr.advanceSubpx = 0.0;
            }
            else
            {
                size_t next = c;
                cr.cp = Utf8DecodeAt(line, c, next);
                cr.advance     = cache ? cache->GlyphAdvance(cr.cp, f.style) : ptSz / 2;
                cr.advanceSubpx = subpxFor
                    ? subpxFor(face, ptSz, static_cast<unsigned int>(cr.cp))
                    : static_cast<double>(cr.advance);
            }
            cr.color       = ResolveColor(f, themeNormalText);
            // Misspell override — applied AFTER ResolveColor so explicit
            // per-char colors are kept on misspelled chars too (they just
            // get retinted). The per-char selection path in Draw still
            // wins at glyph-paint time, so selected text doesn't inherit
            // the misspell tint.
            if (misspells)
            {
                const int col = static_cast<int>(c);
                for (const auto& s : *misspells)
                {
                    if (s.row == li && col >= s.col && col < s.col + s.len)
                    {
                        cr.color = misspelledColor;
                        break;
                    }
                }
            }
            // Highlight: only set when format.highlight is an explicit
            // palette index. Inherit/OOB → no highlight rect drawn.
            cr.hasHighlight = false;
            if (f.highlight != CharFormat::Inherit && f.highlight < Palette::kCount)
            {
                cr.hasHighlight = true;
                cr.highlight    = Palette::ColorAt(f.highlight);
            }
            out.chars[li].push_back(cr);
        }
    }

    // --- Placement sweep -------------------------------------------------
    // Thread page / column / yInPage, flowing text down a column then to the
    // next column and finally to the next page. Floats are resolved when the
    // sweep reaches their anchor row (against the active column's x-base);
    // reflowing floats (wrap 1/4/5) then narrow the available run for the
    // following lines. With one column and no reflowing floats this reproduces
    // the old single-column wrap/pagination exactly.
    auto tw2px = [&](int tw) { return (tw * geom.dpi) / 1440; };
    const int nCols = std::max(1, geom.columnCount);
    const int colW  = std::max(1, (geom.usableW - (nCols - 1) * geom.gutter) / nCols);
    auto colLeftOf  = [&](int c) { return c * (colW + geom.gutter); };

    int curPage = 0;
    int curCol  = 0;
    int yInPage = 0;
    out.totalPages = 1;
    // Advance to the next column (then next page) when the current one fills.
    auto advanceColumnIfFull = [&](int h) {
        if (yInPage + h > geom.usableH && yInPage > 0)
        {
            if (++curCol >= nCols) { curCol = 0; ++curPage; }
            yInPage = 0;
        }
    };

    for (int li = 0; li < n; ++li)
    {
        if (li > 0 && fmt && fmt->PageBreakBefore(li))
        {
            ++curPage;
            curCol  = 0;
            yInPage = 0;
        }

        // Resolve floats anchored to this row into layout coordinates, against
        // the active column for column-relative floats.
        if (floatObjs)
        {
            const int activeColLeft = colLeftOf(curCol);
            for (const auto& f : *floatObjs)
            {
                if (f.anchorRow != li) continue;
                ResolvedFloat rf;
                rf.obj  = &f;
                rf.page = curPage;
                int yOrigin = (f.vRef == FloatObject::VRef::Margin) ? 0
                            : (f.vRef == FloatObject::VRef::Page)   ? -geom.mTop
                            :  yInPage;                              // Paragraph
                rf.yTop    = yOrigin + tw2px(f.top);
                rf.yBottom = yOrigin + tw2px(f.bottom);
                // Caption is drawn just below the picture; extend the reflow
                // exclusion (not the draw box) so text never lands on it.
                rf.exclBottom = rf.yBottom;
                if (f.kind == FloatObject::Kind::Image && !f.caption.empty())
                    rf.exclBottom += out.defaultLineHeight + 2;
                int xOrigin = (f.hRef == FloatObject::HRef::Page)   ? -geom.mLeft
                            : (f.hRef == FloatObject::HRef::Column)  ? activeColLeft
                            :  0;                                    // Margin
                rf.xLeft   = xOrigin + tw2px(f.left);
                rf.xRight  = xOrigin + tw2px(f.right);
                rf.reflow  = (f.wrapType == 1 || f.wrapType == 4 || f.wrapType == 5);
                out.floats.push_back(rf);
            }
        }

        const auto& chars = out.chars[li];
        const int nchars = static_cast<int>(chars.size());

        if (nchars == 0)
        {
            // Empty paragraph: one zero-length, full-column segment.
            int h = out.defaultLineHeight;
            advanceColumnIfFull(h);
            out.segments[li].push_back({ 0, 0, h, curPage, yInPage, colLeftOf(curCol), colW });
            yInPage += h;
            out.totalPages = std::max(out.totalPages, curPage + 1);
            continue;
        }

        int col = 0;
        while (col < nchars)
        {
            // Provisional height from the first char of the chunk drives the
            // exclusion band; segments are single-font in the common case.
            int provH = chars[col].lineHeight;
            advanceColumnIfFull(provH);

            int cl = colLeftOf(curCol);
            int xL, xR;
            FreeRun(out.floats, curPage, yInPage, provH, cl, cl + colW, xL, xR);
            int availW = xR - xL;

            ChunkResult ck = WrapChunk(chars, col, static_cast<double>(availW),
                                       out.defaultLineHeight);
            out.segments[li].push_back({ col, ck.endExcl, ck.height,
                                         curPage, yInPage, xL, availW });
            yInPage += ck.height;
            out.totalPages = std::max(out.totalPages, curPage + 1);
            col = ck.nextCol;
        }
    }
}

// ---------------------------------------------------------------------------
// ClampScrollForCursor
// ---------------------------------------------------------------------------

std::vector<WysiwygRenderer::VisualLine>
WysiwygRenderer::ComputeVisualLayout(const DrawContext& ctx)
{
    std::vector<VisualLine> out;
    if (!ctx.buffer) return out;
    const int dpi = std::max(48, ctx.screenDpi);

    const int pageW   = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH   = static_cast<int>(kPaperHeightIn * dpi);
    const int mLeft   = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight  = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int mTop    = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int usableW = std::max(1, pageW - mLeft - mRight);
    const int usableH = std::max(1, pageH - mTop  - mBottom);

    LayoutPass pass;
    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px, cp);
                    });

    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        const auto& chars = pass.chars[li];
        const auto& segs  = pass.segments[li];
        ParagraphAlign align = ctx.formatted ? ctx.formatted->Alignment(li)
                                             : ParagraphAlign::Left;
        for (size_t s = 0; s < segs.size(); ++s)
        {
            const auto& seg = segs[s];
            VisualLine vl;
            vl.bufferRow = li;
            vl.startCol  = seg.startCol;
            vl.endCol    = seg.endCol;
            vl.charXs.reserve(static_cast<size_t>(seg.endCol - seg.startCol + 1));

            // charXs are segment-relative, so the Center/Right offset shifts
            // every entry equally and cancels in column-matching navigation —
            // only the Justify space-stretch changes intra-segment spacing, so
            // bake just that in to keep Up/Down accurate on justified lines.
            double segContentW = 0.0, segTrimmedW = 0.0;
            int    segSpaces = 0;
            MeasureSegment(chars, seg.startCol, seg.endCol,
                           segContentW, segTrimmedW, segSpaces);
            SegAlign sa = ComputeSegAlign(segContentW, segTrimmedW, seg.width, align,
                                          s + 1 == segs.size(), segSpaces);

            // Sub-pixel accumulator rounded to int — matches the draw
            // loop's glyph positions, so cursor-arrow navigation lands
            // exactly under the rendered chars.
            double cum = 0.0;
            vl.charXs.push_back(0);
            for (int c = seg.startCol; c < seg.endCol; ++c)
            {
                cum += chars[c].advanceSubpx;
                if (sa.spaceStretch > 0.0
                    && (chars[c].ch == ' ' || chars[c].ch == '\t'))
                    cum += sa.spaceStretch;
                vl.charXs.push_back(static_cast<int>(cum + 0.5));
            }
            out.push_back(std::move(vl));
        }
    }
    return out;
}

std::vector<PlacedSegment>
WysiwygRenderer::ComputePlacedSegments(const DrawContext& ctx)
{
    std::vector<PlacedSegment> out;
    if (!ctx.buffer) return out;
    const int dpi = std::max(48, ctx.screenDpi);

    const int pageW   = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH   = static_cast<int>(kPaperHeightIn * dpi);
    const int mLeft   = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight  = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int mTop    = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int usableW = std::max(1, pageW - mLeft - mRight);
    const int usableH = std::max(1, pageH - mTop  - mBottom);

    LayoutPass pass;
    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px, cp);
                    });

    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
        for (const auto& s : pass.segments[li])
            out.push_back(PlacedSegment{ li, s.startCol, s.endCol, s.page,
                                         s.yInPage, s.xOffset, s.width, s.height });
    return out;
}

WysiwygRenderer::FloatHit WysiwygRenderer::HitTestFloat(const DrawContext& ctx, int px, int py)
{
    FloatHit miss;
    if (!ctx.buffer || !ctx.formatted || ctx.formatted->Floats().empty()) return miss;
    const int dpi = std::max(48, ctx.screenDpi);

    const int pageW  = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH  = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop   = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom= static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft  = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW = std::max(1, pageW - mLeft - mRight);
    const int usableH = std::max(1, pageH - mTop  - mBottom);
    const int pageStride = pageH + kPageGapPx;

    LayoutPass pass;
    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom,
                    &ctx.formatted->Floats(),
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px2 = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px2, cp);
                    });

    int pageX = ctx.editorAreaPxX + (ctx.editorAreaPxW - pageW) / 2;
    if (pageX < ctx.editorAreaPxX) pageX = ctx.editorAreaPxX;
    const int viewport = ctx.viewportTopPx;
    const FloatObject* base = &ctx.formatted->Floats()[0];

    // Topmost (highest-z) float first.
    std::vector<const ResolvedFloat*> ordered;
    for (const auto& rf : pass.floats) if (rf.obj) ordered.push_back(&rf);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const ResolvedFloat* a, const ResolvedFloat* b) {
                         return a->obj->zOrder > b->obj->zOrder;
                     });

    const int kHandle = 6;  // half-size of a corner grab square (px)
    for (const ResolvedFloat* rf : ordered)
    {
        int x = pageX + mLeft + rf->xLeft;
        int y = ctx.editorAreaPxY - viewport + rf->page * pageStride + mTop + rf->yTop;
        int w = rf->xRight - rf->xLeft;
        int h = rf->yBottom - rf->yTop;
        if (w <= 0 || h <= 0) continue;
        auto nearPt = [&](int cx, int cy) {
            int adx = px > cx ? px - cx : cx - px;
            int ady = py > cy ? py - cy : cy - py;
            return adx <= kHandle && ady <= kHandle;
        };
        FloatHandle hnd = FloatHandle::None;
        if      (nearPt(x,     y))     hnd = FloatHandle::TopLeft;
        else if (nearPt(x + w, y))     hnd = FloatHandle::TopRight;
        else if (nearPt(x,     y + h)) hnd = FloatHandle::BottomLeft;
        else if (nearPt(x + w, y + h)) hnd = FloatHandle::BottomRight;
        else if (px >= x && px <= x + w && py >= y && py <= y + h) hnd = FloatHandle::Body;
        if (hnd != FloatHandle::None)
            return FloatHit{ static_cast<int>(rf->obj - base), hnd };
    }
    return miss;
}

int WysiwygRenderer::ClampScrollForCursor(const DrawContext& ctx)
{
    if (!ctx.buffer) return ctx.viewportTopPx;
    const int dpi = std::max(48, ctx.screenDpi);

    LayoutPass pass;
    const int pageW    = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH    = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop     = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom  = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft    = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight   = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW  = std::max(1, pageW - mLeft - mRight);
    const int usableH  = std::max(1, pageH - mTop  - mBottom);
    const int pageStride = pageH + kPageGapPx;

    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px, cp);
                    });

    int row = std::clamp(ctx.cursorRow, 0, std::max(0, ctx.buffer->LineCount() - 1));

    // Find the cursor's visual segment from the stored placement (handles
    // columns + floats). The segment that contains the cursor column, or the
    // row's last segment.
    int cursorY    = 0;
    int cursorPage = 0;
    int cursorH    = pass.defaultLineHeight;
    {
        const auto& segs = pass.segments[row];
        for (size_t s = 0; s < segs.size(); ++s)
        {
            bool isLast = (s + 1 == segs.size());
            if (ctx.cursorCol <= segs[s].endCol || isLast)
            {
                cursorPage = segs[s].page;
                cursorY    = segs[s].yInPage;
                cursorH    = segs[s].height;
                break;
            }
        }
    }

    int cursorAbsY = cursorPage * pageStride + mTop + cursorY;

    if (ctx.editorAreaPxH >= pageH)
        return cursorPage * pageStride;

    int viewport = ctx.viewportTopPx;
    // Scrolling UP: snap so the viewport's top sits at the page boundary
    // (cursorPage * pageStride) when the cursor is on the page's first
    // content row — otherwise cursorAbsY == cursorPage * pageStride + mTop
    // would put the cursor at the very top of the editor area and hide the
    // page's top margin. Subtracting mTop generalizes this so any upward
    // scroll leaves an mTop-pixel margin above the cursor row, which lands
    // exactly on the page boundary when cursorY == 0.
    if (cursorAbsY < viewport)
        viewport = std::max(0, cursorAbsY - mTop);
    if (cursorAbsY + cursorH > viewport + ctx.editorAreaPxH)
        viewport = cursorAbsY + cursorH - ctx.editorAreaPxH;
    if (viewport < 0) viewport = 0;
    return viewport;
}

// ---------------------------------------------------------------------------
// RowAtViewportTop — used by the WYSIWYG scrollbar's "cursor follows scroll"
// behavior. Walks the same pagination as ClampScrollForCursor / Draw and
// returns the first buffer row whose document-space top is >= viewportTopPx
// (i.e., the topmost row that is visible at or below the viewport's top).
// ---------------------------------------------------------------------------

int WysiwygRenderer::RowAtViewportTop(const DrawContext& ctx, int viewportTopPx)
{
    if (!ctx.buffer || ctx.buffer->LineCount() == 0) return 0;
    const int dpi = std::max(48, ctx.screenDpi);

    LayoutPass pass;
    const int pageH    = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop     = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom  = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft    = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight   = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int pageW    = static_cast<int>(kPaperWidthIn  * dpi);
    const int usableW  = std::max(1, pageW - mLeft - mRight);
    const int usableH  = std::max(1, pageH - mTop  - mBottom);
    const int pageStride = pageH + kPageGapPx;

    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px, cp);
                    });

    int lastRow = ctx.buffer->LineCount() - 1;

    // A buffer row's "top" is the absolute Y of its first segment, read from
    // the stored placement (correct across columns + floats).
    for (int li = 0; li <= lastRow; ++li)
    {
        const auto& segs = pass.segments[li];
        if (segs.empty()) continue;
        int absY = segs.front().page * pageStride + mTop + segs.front().yInPage;
        if (absY >= viewportTopPx)
            return li;
    }
    return lastRow;
}

// ---------------------------------------------------------------------------
// HitTest — reverse of the Draw layout. Mirrors the geometry/pagination walk
// of RowAtViewportTop + Draw's Pass B so a screen pixel maps to the (row,col)
// directly under the glyph the user clicked.
// ---------------------------------------------------------------------------

bool WysiwygRenderer::HitTest(const DrawContext& ctx, int px, int py,
                              int& outRow, int& outCol)
{
    if (!ctx.buffer || ctx.buffer->LineCount() == 0) return false;
    const int dpi = std::max(48, ctx.screenDpi);

    const int pageW    = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH    = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop     = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom  = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft    = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight   = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW  = std::max(1, pageW - mLeft - mRight);
    const int usableH  = std::max(1, pageH - mTop  - mBottom);
    const int pageStride = pageH + kPageGapPx;

    LayoutPass pass;
    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px2 = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px2, cp);
                    });

    int pageX = ctx.editorAreaPxX + (ctx.editorAreaPxW - pageW) / 2;
    if (pageX < ctx.editorAreaPxX) pageX = ctx.editorAreaPxX;
    const int viewport = ctx.viewportTopPx;
    const int usableX  = pageX + mLeft;

    // Maps a click x to a source column within [segLo, segHi] using the same
    // sub-pixel prefix-sum the draw loop builds (including the per-segment
    // alignment offset baked into segUsableX and the Justify space-stretch).
    // Returns the column boundary nearest the click (segHi == end-of-segment).
    auto columnForX = [&](const std::vector<CharRender>& chars,
                          int segLo, int segHi, int clickX,
                          int segUsableX, double spaceStretch) -> int {
        int rel = clickX - segUsableX;
        if (rel <= 0) return segLo;
        double cum = 0.0;
        for (int c = segLo; c < segHi; ++c)
        {
            int x0 = static_cast<int>(cum + 0.5);
            double cumNext = cum + chars[c].advanceSubpx;
            if (spaceStretch > 0.0 && (chars[c].ch == ' ' || chars[c].ch == '\t'))
                cumNext += spaceStretch;
            int x1 = static_cast<int>(cumNext + 0.5);
            if (rel < (x0 + x1) / 2) return c;
            cum = cumNext;
        }
        return segHi;
    };

    // Pick the segment closest to the click, with vertical distance dominating
    // and horizontal as the tie-breaker — so a click inside a column lands on
    // that column's line (column flow makes screen-Y non-monotonic, so the old
    // "last segment above the click" rule no longer suffices).
    int  bestLi = 0, bestS = 0;
    bool any = false;
    long long bestScore = 0;
    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        const auto& segs = pass.segments[li];
        for (size_t s = 0; s < segs.size(); ++s)
        {
            const auto& seg = segs[s];
            int textY = ctx.editorAreaPxY - viewport + seg.page * pageStride
                      + mTop + seg.yInPage;
            int segTop = textY, segBot = textY + seg.height;
            int segL = usableX + seg.xOffset, segR = segL + seg.width;
            long long dy = (py < segTop) ? (segTop - py)
                         : (py >= segBot) ? (py - segBot) : 0;
            long long dx = (px < segL) ? (segL - px)
                         : (px > segR)  ? (px - segR) : 0;
            long long score = dy * 100000LL + dx;
            if (!any || score < bestScore)
            {
                bestScore = score; bestLi = li; bestS = static_cast<int>(s);
                any = true;
            }
        }
    }

    int bestRow = bestLi, bestCol = 0;
    if (any)
    {
        const auto& chars = pass.chars[bestLi];
        const auto& segs  = pass.segments[bestLi];
        const auto& seg   = segs[static_cast<size_t>(bestS)];
        ParagraphAlign align = ctx.formatted ? ctx.formatted->Alignment(bestLi)
                                             : ParagraphAlign::Left;
        double segContentW = 0.0, segTrimmedW = 0.0;
        int    segSpaces = 0;
        MeasureSegment(chars, seg.startCol, seg.endCol, segContentW, segTrimmedW, segSpaces);
        SegAlign sa = ComputeSegAlign(segContentW, segTrimmedW, seg.width, align,
                                      static_cast<size_t>(bestS) + 1 == segs.size(), segSpaces);
        bestCol = columnForX(chars, seg.startCol, seg.endCol, px,
                             usableX + seg.xOffset + sa.xOffset, sa.spaceStretch);
    }

    // Snap off a UTF-8 continuation byte onto the codepoint's lead byte.
    const std::string& line = ctx.buffer->Line(bestRow);
    while (bestCol > 0 && bestCol < static_cast<int>(line.size())
           && Utf8IsContinuationByte(static_cast<unsigned char>(line[bestCol])))
        --bestCol;

    outRow = bestRow;
    outCol = bestCol;
    return true;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void WysiwygRenderer::Draw(const DrawContext& ctx)
{
    if (!m_sdl || !ctx.buffer) return;
    const int dpi    = std::max(48, ctx.screenDpi);
    const int pageW  = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH  = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop   = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft  = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW = std::max(1, pageW - mLeft - mRight);
    const int usableH = std::max(1, pageH - mTop  - mBottom);

    // Default cache must exist (for empty-line height + fallback advance).
    GlyphCache* defaultCache = CacheFor(ctx.face, ctx.pointSize, dpi);
    if (!defaultCache || !defaultCache->IsValid()) return;

    LayoutPass pass;
    LayoutGeom geom{ dpi, usableW, usableH, mTop, mLeft,
                     ctx.columnCount, (ctx.columnGutterTwips * dpi) / 1440 };
    const std::vector<FloatObject>* floatObjs = ctx.formatted ? &ctx.formatted->Floats() : nullptr;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, m_theme.normalText, geom, floatObjs,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); },
                    [&](FontFace f, int p, unsigned int cp) {
                        int px = std::max(1, (p * dpi + 36) / 72);
                        return SubpxAdvance(f, px, cp);
                    },
                    ctx.misspelledSpans.empty() ? nullptr : &ctx.misspelledSpans,
                    m_theme.misspelledText);

    int pageX = ctx.editorAreaPxX + (ctx.editorAreaPxW - pageW) / 2;
    if (pageX < ctx.editorAreaPxX) pageX = ctx.editorAreaPxX;
    const int viewport = ctx.viewportTopPx;
    const int pageStride = pageH + kPageGapPx;

    SDL_Rect oldClip{}, newClip{
        ctx.editorAreaPxX, ctx.editorAreaPxY,
        ctx.editorAreaPxW, ctx.editorAreaPxH
    };
    SDL_GetRenderClipRect(m_sdl, &oldClip);
    bool hadClip = (oldClip.w > 0 && oldClip.h > 0);
    SDL_SetRenderClipRect(m_sdl, &newClip);

    FillRect(m_sdl, ctx.editorAreaPxX, ctx.editorAreaPxY,
             ctx.editorAreaPxW, ctx.editorAreaPxH, m_theme.background);

    // Page count comes straight from the layout sweep (which knows columns +
    // floats); no re-pagination here.
    const int totalPages = pass.totalPages;

    // Cache the layout metrics so the editor's WYSIWYG scrollbar can size
    // its thumb and step the viewport without re-running the layout pass.
    m_lastTotalDocumentPx  = totalPages * pageStride;
    m_lastDefaultLineHeight = pass.defaultLineHeight;

    Color paper = m_theme.background;
    paper.r = static_cast<uint8_t>(std::min(255, paper.r + 18));
    paper.g = static_cast<uint8_t>(std::min(255, paper.g + 32));
    paper.b = static_cast<uint8_t>(std::min(255, paper.b + 18));

    // Muted margin-guide color: midpoint of dimText and paper, so the rect
    // is visible but no longer competes for attention with the text.
    Color mutedMargin;
    mutedMargin.r = static_cast<uint8_t>((m_theme.dimText.r + paper.r) / 2);
    mutedMargin.g = static_cast<uint8_t>((m_theme.dimText.g + paper.g) / 2);
    mutedMargin.b = static_cast<uint8_t>((m_theme.dimText.b + paper.b) / 2);
    mutedMargin.a = 255;

    // Footer string helpers — render with the document's default cache (one
    // glyph per codepoint, UTF-8 decoded). strWidth measures for right-align.
    auto strWidth = [&](const std::string& s) -> int {
        int w = 0; size_t i = 0;
        while (i < s.size())
        {
            size_t next; char32_t cp = Utf8DecodeAt(s, i, next);
            w += defaultCache->GlyphAdvance(cp, 0);
            i = next;
        }
        return w;
    };
    auto drawStr = [&](int x, int y, const std::string& s, Color col) {
        size_t i = 0;
        while (i < s.size())
        {
            size_t next; char32_t cp = Utf8DecodeAt(s, i, next);
            if (cp > U' ') defaultCache->DrawGlyphAt(cp, x, y, col, 0);
            x += defaultCache->GlyphAdvance(cp, 0);
            i = next;
        }
    };

    // Draw a float from its layout-resolved rect (content/usableX coords →
    // screen). The layout already placed it against the right page + column,
    // so this works for single- and multi-column docs alike.
    auto drawFloat = [&](const ResolvedFloat& rf) {
        const FloatObject& f = *rf.obj;
        int x = pageX + mLeft + rf.xLeft;
        int y = ctx.editorAreaPxY - viewport + rf.page * pageStride + mTop + rf.yTop;
        int w = rf.xRight - rf.xLeft;
        int h = rf.yBottom - rf.yTop;
        if (w <= 0 || h <= 0) return;
        if (f.kind == FloatObject::Kind::Image)
        {
            if (SDL_Texture* tex = m_imageCache.GetTexture(f.imageId, f.imageBytes))
            {
                SDL_FRect dst{ static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(w), static_cast<float>(h) };
                SDL_RenderTexture(m_sdl, tex, nullptr, &dst);
            }
            else
            {
                // Undecodable (e.g. a vector WMF): a dim placeholder.
                FillRect  (m_sdl, x, y, w, h, mutedMargin);
                StrokeRect(m_sdl, x, y, w, h, m_theme.border);
            }
            if (!f.caption.empty())
            {
                int cw = strWidth(f.caption);
                drawStr(x + (w - cw) / 2, y + h + 2, f.caption, m_theme.dimText);
            }
        }
        else  // Box
        {
            if (f.fillColor != CharFormat::Inherit && f.fillColor < Palette::kCount)
                FillRect(m_sdl, x, y, w, h, Palette::ColorAt(f.fillColor));
            Color line = (f.lineColor != CharFormat::Inherit && f.lineColor < Palette::kCount)
                       ? Palette::ColorAt(f.lineColor) : m_theme.border;
            StrokeRect(m_sdl, x, y, w, h, line);
        }
        // Selection: outline + corner grab handles on the authored float.
        if (ctx.selectedFloat >= 0 && rf.obj && ctx.formatted
            && ctx.selectedFloat < static_cast<int>(ctx.formatted->Floats().size())
            && rf.obj == &ctx.formatted->Floats()[static_cast<size_t>(ctx.selectedFloat)])
        {
            StrokeRect(m_sdl, x - 1, y - 1, w + 2, h + 2, m_theme.brightText);
            const int hs = 5;  // grab-square size
            int cx[2] = { x, x + w }, cy[2] = { y, y + h };
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    FillRect(m_sdl, cx[i] - hs / 2, cy[j] - hs / 2, hs, hs, m_theme.brightText);
        }
    };

    // Partition the resolved floats into below-text (drawn under the body) and
    // above-text (over it), each ordered by z. Drawn around Pass B below.
    std::vector<const ResolvedFloat*> belowFloats, aboveFloats;
    for (const auto& rf : pass.floats)
    {
        if (!rf.obj) continue;
        (rf.obj->belowText ? belowFloats : aboveFloats).push_back(&rf);
    }
    auto byZ = [](const ResolvedFloat* a, const ResolvedFloat* b) {
        return a->obj->zOrder < b->obj->zOrder;
    };
    std::stable_sort(belowFloats.begin(), belowFloats.end(), byZ);
    std::stable_sort(aboveFloats.begin(), aboveFloats.end(), byZ);

    for (int p = 0; p < totalPages; ++p)
    {
        int pageTopY = ctx.editorAreaPxY - viewport + p * pageStride;
        if (pageTopY + pageH < ctx.editorAreaPxY) continue;
        if (pageTopY > ctx.editorAreaPxY + ctx.editorAreaPxH) break;

        FillRect  (m_sdl, pageX, pageTopY, pageW, pageH, paper);
        StrokeRect(m_sdl, pageX, pageTopY, pageW, pageH, m_theme.border);
        if (ctx.showMargins)
            StrokeRect(m_sdl, pageX + mLeft, pageTopY + mTop, usableW, usableH,
                       mutedMargin);

        // Optional header (top margin) and footer (bottom margin): file name
        // left, "Page N of M" right, each slot drawn only if its flag is set.
        // Both sit in the margins, so they never steal from the text area —
        // pagination is unaffected. Mirrors Print.cpp.
        {
            const int lineH   = defaultCache->LineHeight();
            std::string pageStr = "Page " + std::to_string(p + 1)
                                + " of "  + std::to_string(totalPages);
            // Draw one band's slots at the given y.
            auto drawBand = [&](int y, bool showName, bool showPage) {
                if (showName && !ctx.documentName.empty())
                    drawStr(pageX + mLeft, y, ctx.documentName, m_theme.dimText);
                if (showPage)
                {
                    int rightX = pageX + pageW - mRight - strWidth(pageStr);
                    drawStr(rightX, y, pageStr, m_theme.dimText);
                }
            };
            if (ctx.headerShowFilename || ctx.headerShowPageNumber)
            {
                int headerY = pageTopY + std::max(0, (mTop - lineH) / 2);
                drawBand(headerY, ctx.headerShowFilename, ctx.headerShowPageNumber);
            }
            if (ctx.footerShowFilename || ctx.footerShowPageNumber)
            {
                int bandTop = pageTopY + pageH - mBottom;
                int footerY = bandTop + std::max(0, (mBottom - lineH) / 2);
                drawBand(footerY, ctx.footerShowFilename, ctx.footerShowPageNumber);
            }
        }
    }

    // Floats that sit behind the text (drawn now, over the page rects).
    for (const ResolvedFloat* f : belowFloats) drawFloat(*f);

    // Selection range, normalized.
    int sr = 0, sc = 0, er = 0, ec = 0;
    if (ctx.selActive)
    {
        sr = ctx.selAnchorRow; sc = ctx.selAnchorCol;
        er = ctx.cursorRow;    ec = ctx.cursorCol;
        if (sr > er || (sr == er && sc > ec)) { std::swap(sr, er); std::swap(sc, ec); }
    }

    // Pass B: paint text segment-by-segment, using the placement (page,
    // yInPage, xOffset, width) the layout sweep computed.
    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        const auto& chars = pass.chars[li];
        const auto& segs  = pass.segments[li];
        for (size_t s = 0; s < segs.size(); ++s)
        {
            int h = segs[s].height;
            int pageTopY = ctx.editorAreaPxY - viewport + segs[s].page * pageStride;
            int textY    = pageTopY + mTop + segs[s].yInPage;
            // Segment's float-determined run; alignment shifts within it.
            int usableX  = pageX + mLeft + segs[s].xOffset;
            int segW     = segs[s].width;

            // Cull lines fully outside the editor area. Cull each segment
            // individually (NOT an early break) — with multi-column flow the
            // segments are not in monotonic screen-Y order, so a segment below
            // the viewport may be followed by a visible one in the next column.
            if (textY + h < ctx.editorAreaPxY) continue;
            if (textY > ctx.editorAreaPxY + ctx.editorAreaPxH) continue;

            int segLo = segs[s].startCol;
            int segHi = segs[s].endCol;

            // Paragraph alignment: shift the segment (Center/Right) and/or
            // widen its spaces (Justify). Same math the hit-test and arrow
            // navigation use, so they all agree with what's painted.
            ParagraphAlign align = ctx.formatted ? ctx.formatted->Alignment(li)
                                                  : ParagraphAlign::Left;
            double segContentW = 0.0, segTrimmedW = 0.0;
            int    segSpaces   = 0;
            MeasureSegment(chars, segLo, segHi, segContentW, segTrimmedW, segSpaces);
            SegAlign sa = ComputeSegAlign(segContentW, segTrimmedW, segW, align,
                                          s + 1 == segs.size(), segSpaces);
            usableX += sa.xOffset;

            // Sub-pixel positioning: accumulate each character's sub-pixel
            // advance as a double, then round to the nearest integer pixel
            // when emitting a glyph. This makes the cumulative line width
            // match what the wrap budget assumed (so lines fill out to the
            // right margin), while glyph emit positions stay integer-aligned.
            // xOfCol[c - segLo] is the integer pixel x offset (from the
            // segment's left edge) at column c. Size = (segHi - segLo + 1):
            // the trailing entry is the x just past the last glyph,
            // i.e. the cursor position when the cursor sits at end-of-seg.
            // For Justify, sa.spaceStretch widens the gap after each space.
            std::vector<int> xOfCol(static_cast<size_t>(segHi - segLo + 1));
            {
                double cum = 0.0;
                for (int c = segLo; c <= segHi; ++c)
                {
                    xOfCol[static_cast<size_t>(c - segLo)] =
                        static_cast<int>(cum + 0.5);
                    if (c < segHi)
                    {
                        cum += chars[c].advanceSubpx;
                        if (sa.spaceStretch > 0.0
                            && (chars[c].ch == ' ' || chars[c].ch == '\t'))
                            cum += sa.spaceStretch;
                    }
                }
            }
            auto xAt = [&](int col) -> int {
                int idx = std::clamp(col - segLo, 0, static_cast<int>(xOfCol.size()) - 1);
                return usableX + xOfCol[static_cast<size_t>(idx)];
            };

            // Per-char highlight rects (drawn before the selection rect so
            // the selection's reverse-video visually wins where they overlap).
            {
                int hRunStart = -1;
                Color hRunColor{};
                auto flushHighlight = [&](int rightX) {
                    if (hRunStart < 0) return;
                    int leftX = xAt(hRunStart);
                    int width = rightX - leftX;
                    if (width > 0)
                        FillRect(m_sdl, leftX, textY, width, h, hRunColor);
                    hRunStart = -1;
                };
                for (int c = segLo; c < segHi; ++c)
                {
                    const CharRender& cr = chars[c];
                    if (cr.hasHighlight)
                    {
                        if (hRunStart < 0)
                        {
                            hRunStart = c;
                            hRunColor = cr.highlight;
                        }
                        else if (!(cr.highlight.r == hRunColor.r
                                && cr.highlight.g == hRunColor.g
                                && cr.highlight.b == hRunColor.b
                                && cr.highlight.a == hRunColor.a))
                        {
                            flushHighlight(xAt(c));
                            hRunStart = c;
                            hRunColor = cr.highlight;
                        }
                    }
                    else if (hRunStart >= 0)
                    {
                        flushHighlight(xAt(c));
                    }
                }
                flushHighlight(xAt(segHi));
            }

            if (ctx.selActive && li >= sr && li <= er)
            {
                int selLo = (li == sr) ? sc : 0;
                int selHi = (li == er) ? ec : static_cast<int>(chars.size());
                int hiLo = std::max(selLo, segLo);
                int hiHi = std::min(selHi, segHi);
                if (hiHi > hiLo)
                {
                    int xLo = xAt(hiLo);
                    int xHi = xAt(hiHi);
                    FillRect(m_sdl, xLo, textY, std::max(2, xHi - xLo), h,
                             m_theme.reverseBackground);
                }
            }

            // Glyphs.
            for (int c = segLo; c < segHi; ++c)
            {
                const CharRender& cr = chars[c];
                // cr.cp is the decoded codepoint on the lead byte and
                // U' ' on continuation bytes — so the cp > U' ' check
                // below naturally skips continuation bytes.
                char32_t cp = cr.cp;

                // Per-char foreground: from CharFormat.color (or theme
                // default when Inherit). Selection's reverse-video wins.
                Color fg = cr.color;
                if (ctx.selActive)
                {
                    int colLo = (li == sr) ? sc : 0;
                    int colHi = (li == er) ? ec : static_cast<int>(chars.size());
                    if (li >= sr && li <= er && c >= colLo && c < colHi)
                        fg = m_theme.reverseForeground;
                }

                if (cp > U' ' && cr.cache)
                    cr.cache->DrawGlyphAt(cp, xAt(c), textY, fg, cr.style);
            }

            // Cursor.
            if (ctx.cursorVisible && li == ctx.cursorRow
                && ctx.cursorCol >= segLo && ctx.cursorCol <= segHi)
            {
                int cx = xAt(ctx.cursorCol);
                // Snap the cursor to the right margin when the user is at
                // the very end of the buffer line on the line's last
                // visual segment AND typing one more character would wrap.
                // Without the snap, the cursor can sit up to ~one char
                // width inside the margin (the leftover sub-pixel space
                // below one full advance). Snapping puts the cursor right
                // at the margin so "cursor on the margin" is the clear
                // visual cue that the next keystroke will wrap.
                bool atEndOfBufferLine =
                    ctx.cursorCol == static_cast<int>(chars.size());
                bool isLastSegment = (s + 1 == segs.size());
                // Only meaningful for left-aligned paragraphs — for Center/
                // Right/Justify the segment is offset, so usableX + usableW
                // is not the live right margin and the snap would misfire.
                if (atEndOfBufferLine && isLastSegment
                    && align == ParagraphAlign::Left)
                {
                    double advNext = (segHi > segLo)
                        ? chars[segHi - 1].advanceSubpx
                        : SubpxAdvance(ctx.face,
                                       std::max(1, (ctx.pointSize * dpi + 36) / 72),
                                       static_cast<unsigned int>('M'));
                    int margin = usableX + segW;
                    if (cx + static_cast<int>(advNext + 0.5) > margin)
                        cx = margin - 2;
                }
                // On an empty line, the segment's height is the document
                // default's LineHeight. Override with the "insert" font so
                // the cursor previews the size of the next-typed char —
                // otherwise picking a smaller font after writing in a
                // larger one leaves the cursor at the larger height.
                int cursorPxH = h;
                if (chars.empty()
                    && (ctx.insertFace != ctx.face || ctx.insertPointSize != ctx.pointSize))
                {
                    GlyphCache* insertCache = CacheFor(ctx.insertFace, ctx.insertPointSize, dpi);
                    if (insertCache) cursorPxH = insertCache->LineHeight();
                }
                FillRect(m_sdl, cx, textY, 2, cursorPxH, m_theme.brightText);
            }
        }
    }

    // Floats that sit in front of the text (drawn last).
    for (const ResolvedFloat* f : aboveFloats) drawFloat(*f);

    if (hadClip)
        SDL_SetRenderClipRect(m_sdl, &oldClip);
    else
        SDL_SetRenderClipRect(m_sdl, nullptr);
}
