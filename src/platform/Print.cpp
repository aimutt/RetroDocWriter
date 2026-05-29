#include "Print.h"
#include "editor/Palette.h"
#include "editor/TextBuffer.h"
#include "platform/ImageDecode.h"
#include "render/FontFace.h"
#include "render/FontSettings.h"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    std::string DefaultPrinterName()
    {
        DWORD len = 0;
        GetDefaultPrinterA(nullptr, &len);
        if (len == 0) return {};
        std::string out(len, '\0');
        if (!GetDefaultPrinterA(out.data(), &len)) return {};
        if (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
    }

    std::string FormatLastError(const char* prefix)
    {
        DWORD err = GetLastError();
        char* msg = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&msg), 0, nullptr);
        std::string out = prefix ? prefix : "Print error";
        if (msg)
        {
            out += ": ";
            out += msg;
            // Strip trailing CRLF FormatMessage tends to append.
            while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
                out.pop_back();
            LocalFree(msg);
        }
        return out;
    }

    // Wrap a single source line into print-line segments, each of length
    // <= charsPerLine. Empty source line produces one empty segment so it
    // still consumes a row on the page. Word-aware: greedily fills up to
    // charsPerLine, then prefers to break at the most recent whitespace
    // inside the segment, falling back to a hard cut when no whitespace fit.
    // Mirrors WysiwygRenderer::WrapLinePx so display and print wrap at the
    // same positions.
    std::vector<std::string> WrapLine(const std::string& src, int charsPerLine)
    {
        std::vector<std::string> out;
        if (charsPerLine <= 0) { out.push_back(src); return out; }
        if (src.empty())       { out.emplace_back();  return out; }

        size_t i = 0;
        const size_t n = src.size();
        while (i < n)
        {
            size_t start   = i;
            size_t lastBrk = std::string::npos;
            size_t taken   = 0;

            while (i < n && taken < static_cast<size_t>(charsPerLine))
            {
                char c = src[i];
                if (c == ' ' || c == '\t') lastBrk = i;
                ++i;
                ++taken;
            }

            if (i < n && lastBrk != std::string::npos && lastBrk > start)
            {
                // Break at the whitespace; skip the space so it doesn't lead
                // the next print line.
                out.emplace_back(src.substr(start, lastBrk - start));
                i = lastBrk + 1;
            }
            else
            {
                // Hard break (no whitespace in the segment, or end of line).
                out.emplace_back(src.substr(start, i - start));
            }
        }
        return out;
    }

    int CountTotalPrintLines(const TextBuffer& buf, int charsPerLine)
    {
        int total = 0;
        int n = buf.LineCount();
        for (int i = 0; i < n; ++i)
        {
            const std::string& line = buf.Line(i);
            if (line.empty())                  total += 1;
            else if (charsPerLine <= 0)        total += 1;
            else                               total += static_cast<int>(
                WrapLine(line, charsPerLine).size());
        }
        return total;
    }

    // ---- Formatted print helpers (used when PrintRequest::formats != nullptr) ----

    struct PrintChar
    {
        char     ch;
        HFONT    font;
        int      advance;
        int      lineHeight;
        uint8_t  style;
        COLORREF color;     // RGB for SetTextColor; default = black
        bool     hasHighlight;
        COLORREF highlight; // only valid when hasHighlight (RGB)
    };

    struct PrintSegment { int startCol; int endCol; int height; };

    // Per-segment alignment geometry for the print path — integer mirror of
    // WysiwygRenderer's ComputeSegAlign so the printed page matches the screen.
    struct PrintSegAlign { int xOffset = 0; double spaceStretch = 0.0; };

    PrintSegAlign ComputePrintSegAlign(int contentW, int trimmedW, int usableW,
                                       ParagraphAlign a, bool isLastSeg, int spaceCount)
    {
        PrintSegAlign out;
        switch (a)
        {
            case ParagraphAlign::Left:
                break;
            case ParagraphAlign::Center:
            {
                int slack = usableW - trimmedW;
                if (slack > 0) out.xOffset = slack / 2;
                break;
            }
            case ParagraphAlign::Right:
            {
                int slack = usableW - trimmedW;
                if (slack > 0) out.xOffset = slack;
                break;
            }
            case ParagraphAlign::Justify:
            {
                int slack = usableW - contentW;
                if (!isLastSeg && spaceCount > 0 && slack > 0)
                    out.spaceStretch = static_cast<double>(slack) / spaceCount;
                break;
            }
        }
        return out;
    }

    // Measure a print segment [startCol,endCol): full advance width, width up
    // to the last non-space glyph, and stretchable-space count.
    void MeasurePrintSegment(const std::vector<PrintChar>& chars,
                             int startCol, int endCol,
                             int& contentW, int& trimmedW, int& spaceCount)
    {
        int cum = 0, lastNonSpaceEnd = 0, spaces = 0;
        for (int c = startCol; c < endCol; ++c)
        {
            cum += chars[c].advance;
            if (chars[c].ch == ' ' || chars[c].ch == '\t') ++spaces;
            else                                            lastNonSpaceEnd = cum;
        }
        contentW   = cum;
        trimmedW   = lastNonSpaceEnd;
        spaceCount = spaces;
    }

    // Stretch-blit an RGBA32 SDL_Surface onto a GDI DC at (x,y,w,h). Converts
    // RGBA bytes to a top-down 32-bpp DIB (0xAARRGGBB DWORDs) for StretchDIBits.
    void BlitSurfaceToDC(HDC hdc, SDL_Surface* surf, int x, int y, int w, int h)
    {
        if (!surf || surf->w <= 0 || surf->h <= 0 || w <= 0 || h <= 0) return;
        const int sw = surf->w, sh = surf->h;
        std::vector<uint32_t> px(static_cast<size_t>(sw) * static_cast<size_t>(sh));
        const uint8_t* base = static_cast<const uint8_t*>(surf->pixels);
        for (int row = 0; row < sh; ++row)
        {
            const uint8_t* sp = base + static_cast<size_t>(row) * surf->pitch;
            uint32_t* dp = px.data() + static_cast<size_t>(row) * sw;
            for (int col = 0; col < sw; ++col)
            {
                uint8_t r = sp[0], g = sp[1], b = sp[2], a = sp[3];
                dp[col] = (static_cast<uint32_t>(a) << 24)
                        | (static_cast<uint32_t>(r) << 16)
                        | (static_cast<uint32_t>(g) << 8)
                        |  static_cast<uint32_t>(b);
                sp += 4;
            }
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = sw;
        bmi.bmiHeader.biHeight      = -sh;   // negative = top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchDIBits(hdc, x, y, w, h, 0, 0, sw, sh,
                      px.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }
}

std::vector<std::string> EnumeratePrinters()
{
    std::vector<std::string> out;

    // Two passes through EnumPrintersA — sizing then fill.
    DWORD needed = 0, returned = 0;
    EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                  nullptr, 2, nullptr, 0, &needed, &returned);
    if (needed > 0)
    {
        std::vector<unsigned char> buf(needed);
        if (EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                          nullptr, 2, buf.data(), needed, &needed, &returned))
        {
            const PRINTER_INFO_2A* info = reinterpret_cast<const PRINTER_INFO_2A*>(buf.data());
            for (DWORD i = 0; i < returned; ++i)
                if (info[i].pPrinterName) out.emplace_back(info[i].pPrinterName);
        }
    }

    // Hoist the default printer to the front.
    std::string def = DefaultPrinterName();
    if (!def.empty())
    {
        auto it = std::find(out.begin(), out.end(), def);
        if (it != out.end())
        {
            std::rotate(out.begin(), it, it + 1);
        }
        else
        {
            out.insert(out.begin(), def);
        }
    }
    return out;
}

// Forward decl: formatted path (per-character font switching).
static std::string PrintDocumentFormatted(const TextBuffer& buffer, const PrintRequest& req);

std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req)
{
    if (req.formats != nullptr)
        return PrintDocumentFormatted(buffer, req);

    // Resolve printer name (default to first installed).
    std::string printerName = req.printerName;
    if (printerName.empty())
    {
        auto list = EnumeratePrinters();
        if (list.empty()) return "No printers installed.";
        printerName = list.front();
    }

    HANDLE hPrinter = nullptr;
    if (!OpenPrinterA(const_cast<char*>(printerName.c_str()), &hPrinter, nullptr))
        return FormatLastError("OpenPrinter failed");

    // Resolve DEVMODE — size, fill, override orientation/copies, merge.
    LONG devSize = DocumentPropertiesA(nullptr, hPrinter,
                                       const_cast<char*>(printerName.c_str()),
                                       nullptr, nullptr, 0);
    if (devSize < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (size) failed");
    }
    std::vector<unsigned char> devBuf(static_cast<size_t>(devSize));
    DEVMODEA* devmode = reinterpret_cast<DEVMODEA*>(devBuf.data());
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, nullptr, DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (read) failed");
    }
    devmode->dmFields    |= DM_ORIENTATION | DM_COPIES;
    devmode->dmOrientation = (req.orientation == PrintOrientation::Landscape)
                             ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    devmode->dmCopies      = static_cast<short>(std::max(1, req.copies));
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, devmode,
                            DM_IN_BUFFER | DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (merge) failed");
    }

    HDC hdc = CreateDCA("WINSPOOL", printerName.c_str(), nullptr, devmode);
    if (!hdc)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("CreateDC failed");
    }

    // Pixel metrics from the device.
    const int horzRes   = GetDeviceCaps(hdc, HORZRES);
    const int vertRes   = GetDeviceCaps(hdc, VERTRES);
    const int dpiX      = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY      = GetDeviceCaps(hdc, LOGPIXELSY);

    auto clampMargin = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 5.0) v = 5.0;
        return v;
    };
    const int marginTopPx    = static_cast<int>(clampMargin(req.margins.topIn)    * dpiY);
    const int marginBottomPx = static_cast<int>(clampMargin(req.margins.bottomIn) * dpiY);
    const int marginLeftPx   = static_cast<int>(clampMargin(req.margins.leftIn)   * dpiX);
    const int marginRightPx  = static_cast<int>(clampMargin(req.margins.rightIn)  * dpiX);

    // Font selection. When the caller passes useDocumentFont (WYSIWYG mode),
    // register the bundled TTF privately so CreateFont can find it by family
    // name even if it isn't installed system-wide. Falls back to Courier 10pt
    // for plain-text-mode prints.
    const char* family    = "Courier New";
    int         pointSize = 10;
    DWORD       weight    = FW_NORMAL;
    if (req.useDocumentFont && !req.fontFamily.empty())
    {
        family    = req.fontFamily.c_str();
        pointSize = std::max(4, req.pointSize);
        if (req.bold) weight = FW_BOLD;
        if (!req.fontFile.empty())
        {
            // Register once per process per font path.
            static std::vector<std::string> registered;
            if (std::find(registered.begin(), registered.end(), req.fontFile)
                == registered.end())
            {
                if (AddFontResourceExA(req.fontFile.c_str(), FR_PRIVATE, 0) > 0)
                    registered.push_back(req.fontFile);
            }
        }
    }

    HFONT hFont = CreateFontA(
        -MulDiv(pointSize, dpiY, 72),  // negative → cell height in points
        0, 0, 0,
        weight, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, family);
    if (!hFont)
    {
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return FormatLastError("CreateFont failed");
    }
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    TEXTMETRICA tm{};
    GetTextMetricsA(hdc, &tm);
    const int lineHeight = tm.tmHeight + tm.tmExternalLeading;

    SIZE charSize{};
    GetTextExtentPoint32A(hdc, "M", 1, &charSize);
    const int charWidth = std::max(1L, charSize.cx);

    // Usable area + per-page line capacity.
    const int usableLeft   = marginLeftPx;
    const int usableTop    = marginTopPx;
    const int usableRight  = horzRes - marginRightPx;
    const int usableBottom = vertRes - marginBottomPx;
    int       usableWidth  = std::max(0, usableRight  - usableLeft);
    int       usableHeight = std::max(0, usableBottom - usableTop);

    // Plain-text (formats == nullptr) print path is monospace-only — chars
    // per line comes from GDI's 'M' measurement. The formatted path
    // (PrintDocumentFormatted) does per-glyph pixel wrap and supports
    // proportional fonts.
    int charsPerLine   = std::max(1, usableWidth / charWidth);
    int linesPerPage   = std::max(1, (usableHeight - lineHeight) / lineHeight); // -1 for footer
    // Reserve the very bottom row for the footer.
    const int footerY = usableTop + linesPerPage * lineHeight;

    // Total pages (consider page range so the footer denominator matches what
    // the user requested).
    const int totalPrintLinesAll = CountTotalPrintLines(buffer, charsPerLine);
    const int totalPagesAll      = std::max(1, (totalPrintLinesAll + linesPerPage - 1) / linesPerPage);
    int firstPage = req.allPages ? 1 : std::max(1, req.pageFrom);
    int lastPage  = req.allPages ? totalPagesAll
                                 : std::min(totalPagesAll, std::max(firstPage, req.pageTo));

    DOCINFOA di{};
    di.cbSize    = sizeof(di);
    std::string docName = req.documentName.empty() ? "RetroEdit Document" : req.documentName;
    di.lpszDocName = docName.c_str();

    if (StartDocA(hdc, &di) <= 0)
    {
        std::string msg = FormatLastError("StartDoc failed");
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return msg;
    }

    // Print loop: walk the buffer, wrap each line, paginate, emit only pages
    // inside [firstPage, lastPage]. NOTE: dmCopies (DEVMODE) tells the driver
    // to spool the document N times — we don't loop here for copies.
    int  currentPage = 1;
    int  lineOnPage  = 0;
    bool pageStarted = false;
    int  pagesEmitted = 0;

    auto emitFooter = [&](int pageNum) {
        // Plain-text path (unused by RDW, which always sends formats). Honor
        // each footer sub-slot using the shared resolver so the band model
        // matches the formatted path.
        if (!req.footer.AnyActive()) return;
        const int usableW = usableRight - usableLeft;
        for (int slotIdx = 0; slotIdx < 3; ++slotIdx)
        {
            std::string s = ResolveHeaderFooterSlot(
                req.footer.slots[static_cast<size_t>(slotIdx)],
                pageNum, totalPagesAll, docName);
            if (s.empty()) continue;
            SIZE sz{};
            GetTextExtentPoint32A(hdc, s.c_str(),
                                  static_cast<int>(s.size()), &sz);
            int x = (slotIdx == 0) ? usableLeft
                  : (slotIdx == 1) ? usableLeft + (usableW - sz.cx) / 2
                                   : usableRight - sz.cx;
            TextOutA(hdc, x, footerY, s.c_str(), static_cast<int>(s.size()));
        }
    };

    auto beginPageIfNeeded = [&]() {
        if (pageStarted) return true;
        if (currentPage < firstPage || currentPage > lastPage) return false;
        if (StartPage(hdc) <= 0) return false;
        // Font selection survives across pages on most drivers but reselecting
        // is the safe, portable choice.
        SelectObject(hdc, hFont);
        pageStarted = true;
        return true;
    };

    auto endPageIfStarted = [&]() {
        if (!pageStarted) return;
        emitFooter(currentPage);
        EndPage(hdc);
        ++pagesEmitted;
        pageStarted = false;
    };

    auto advanceLine = [&]() {
        ++lineOnPage;
        if (lineOnPage >= linesPerPage)
        {
            endPageIfStarted();
            lineOnPage = 0;
            ++currentPage;
        }
    };

    const int lineCount = buffer.LineCount();
    for (int li = 0; li < lineCount; ++li)
    {
        auto segments = WrapLine(buffer.Line(li), charsPerLine);
        for (const auto& seg : segments)
        {
            if (currentPage > lastPage) break;
            if (currentPage >= firstPage && beginPageIfNeeded())
            {
                int y = usableTop + lineOnPage * lineHeight;
                TextOutA(hdc, usableLeft, y,
                         seg.c_str(), static_cast<int>(seg.size()));
            }
            advanceLine();
        }
        if (currentPage > lastPage) break;
    }
    endPageIfStarted();

    EndDoc(hdc);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    DeleteDC(hdc);
    ClosePrinter(hPrinter);

    if (pagesEmitted == 0)
        return "Nothing printed (page range produced no pages).";

    std::string msg = "Printed " + std::to_string(pagesEmitted) + " page";
    if (pagesEmitted != 1) msg += "s";
    msg += ".";
    if (req.copies > 1)
    {
        msg += " (" + std::to_string(req.copies) + " copies)";
    }
    return msg;
}

// ---------------------------------------------------------------------------
// Formatted print path
// ---------------------------------------------------------------------------
//
// When PrintRequest::formats is non-null, the document carries per-character
// CharFormat overrides (style bits + face + size). The single-font path
// above can't honor them, so we use a per-(face, ptSize, styleBits) HFONT
// cache: every printable character is rendered with the GDI font that
// matches its CharFormat. Pixel-based wrap and variable per-segment line
// height mirror WysiwygRenderer's screen layout, so what the user sees on
// screen matches what GDI prints.
static std::string PrintDocumentFormatted(const TextBuffer& buffer,
                                          const PrintRequest& req)
{
    std::string printerName = req.printerName;
    if (printerName.empty())
    {
        auto list = EnumeratePrinters();
        if (list.empty()) return "No printers installed.";
        printerName = list.front();
    }

    HANDLE hPrinter = nullptr;
    if (!OpenPrinterA(const_cast<char*>(printerName.c_str()), &hPrinter, nullptr))
        return FormatLastError("OpenPrinter failed");

    LONG devSize = DocumentPropertiesA(nullptr, hPrinter,
                                       const_cast<char*>(printerName.c_str()),
                                       nullptr, nullptr, 0);
    if (devSize < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (size) failed");
    }
    std::vector<unsigned char> devBuf(static_cast<size_t>(devSize));
    DEVMODEA* devmode = reinterpret_cast<DEVMODEA*>(devBuf.data());
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, nullptr, DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (read) failed");
    }
    devmode->dmFields    |= DM_ORIENTATION | DM_COPIES;
    devmode->dmOrientation = (req.orientation == PrintOrientation::Landscape)
                             ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    devmode->dmCopies      = static_cast<short>(std::max(1, req.copies));
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, devmode,
                            DM_IN_BUFFER | DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (merge) failed");
    }

    HDC hdc = CreateDCA("WINSPOOL", printerName.c_str(), nullptr, devmode);
    if (!hdc)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("CreateDC failed");
    }

    // The user's margins are measured from the *physical* page edge so
    // the printed wrap matches what RetroDocWriter draws on screen
    // (which uses the full 8.5" × 11" page). GDI's device origin (0,0)
    // is the top-left of the printable area, not the physical page, so
    // we offset by PHYSICALOFFSETX/Y when computing where on the device
    // surface the user's margins land. HORZRES/VERTRES (the printable
    // area's pixel size) are kept only as a safety clamp.
    const int physW    = GetDeviceCaps(hdc, PHYSICALWIDTH);
    const int physH    = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    const int physOffX = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    const int physOffY = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    const int horzRes  = GetDeviceCaps(hdc, HORZRES);
    const int vertRes  = GetDeviceCaps(hdc, VERTRES);
    const int dpiX     = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY     = GetDeviceCaps(hdc, LOGPIXELSY);

    auto clampMargin = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 5.0) v = 5.0;
        return v;
    };
    const int marginTopPx    = static_cast<int>(clampMargin(req.margins.topIn)    * dpiY);
    const int marginBottomPx = static_cast<int>(clampMargin(req.margins.bottomIn) * dpiY);
    const int marginLeftPx   = static_cast<int>(clampMargin(req.margins.leftIn)   * dpiX);
    const int marginRightPx  = static_cast<int>(clampMargin(req.margins.rightIn)  * dpiX);

    // Device coordinates of the user's content area. usableLeft can be
    // negative if the user margin is smaller than the printer's hardware
    // margin — clamp to 0 (the leftmost printable column).
    int usableLeft   = std::max(0, marginLeftPx - physOffX);
    int usableTop    = std::max(0, marginTopPx  - physOffY);
    int usableRight  = std::min(horzRes,
                                physW  - marginRightPx  - physOffX);
    int usableBottom = std::min(vertRes,
                                physH  - marginBottomPx - physOffY);
    const int usableWidth  = std::max(1, usableRight  - usableLeft);
    const int usableHeight = std::max(1, usableBottom - usableTop);

    // Register every font face that appears in the document (privately —
    // no system install needed). Always include the default face.
    FontFace defaultFace = FontFace::CascadiaMono;
    for (int i = 0; i < FontFaceCount(); ++i)
    {
        if (req.fontFamily == FontFaceFamily(static_cast<FontFace>(i)))
            { defaultFace = static_cast<FontFace>(i); break; }
    }
    const int defaultPt = std::max(4, req.pointSize);

    auto resolveFace = [&](const CharFormat& f) -> FontFace {
        if (f.face == CharFormat::Inherit) return defaultFace;
        if (f.face >= static_cast<uint8_t>(FontFace::Count_)) return defaultFace;
        return static_cast<FontFace>(f.face);
    };
    auto resolveSize = [&](const CharFormat& f) -> int {
        if (f.size == CharFormat::Inherit) return defaultPt;
        if (f.size >= 4) return defaultPt;
        return FontSizePoints(FontSizeAt(static_cast<int>(f.size)));
    };

    // Walk the document, register each unique face's TTF privately.
    static std::vector<std::string> registered; // process-lifetime set
    {
        std::vector<bool> seen(FontFaceCount(), false);
        // TTF file paths are resolved by Application; PrintRequest only carries
        // the default's file, but every bundled face lives in the same
        // assets/fonts dir, so we rebuild sibling paths from that prefix.
        std::string fontDir;
        if (!req.fontFile.empty())
        {
            auto p = req.fontFile.rfind("fonts");
            if (p != std::string::npos) fontDir = req.fontFile.substr(0, p);
        }
        auto registerPath = [&](const char* file) {
            if (fontDir.empty()) return;
            std::string path = fontDir + file;
            if (std::find(registered.begin(), registered.end(), path) == registered.end())
            {
                if (AddFontResourceExA(path.c_str(), FR_PRIVATE, 0) > 0)
                    registered.push_back(path);
            }
        };
        auto registerFace = [&](FontFace face) {
            int idx = static_cast<int>(face);
            if (idx < 0 || idx >= FontFaceCount() || seen[idx]) return;
            seen[idx] = true;
            // Register the regular TTF *and* its bold sibling: bold runs carry
            // the Bold style bit (not a bold face), and GDI's faux-bold of a
            // privately-registered regular face doesn't take, so a real bold
            // member of the family must be present for CreateFont(FW_BOLD).
            registerPath(FontFaceFile(face));
            registerPath(FontFaceFile(FontFaceBoldVariant(face)));
        };
        registerFace(defaultFace);
        for (const auto& row : *req.formats)
            for (const auto& f : row)
            {
                if (f.face != CharFormat::Inherit
                    && f.face < static_cast<uint8_t>(FontFace::Count_))
                    registerFace(static_cast<FontFace>(f.face));
            }
    }

    // HFONT cache keyed by (face, ptSize, styleBits).
    std::unordered_map<uint64_t, HFONT> fontCache;
    auto fontFor = [&](FontFace face, int ptSize, uint8_t styleBits) -> HFONT {
        uint64_t key = (static_cast<uint64_t>(face) << 24)
                     | (static_cast<uint64_t>(ptSize & 0xFFFF) << 8)
                     | styleBits;
        auto it = fontCache.find(key);
        if (it != fontCache.end()) return it->second;
        DWORD weight    = (styleBits & 0x01) ? FW_BOLD : FW_NORMAL;
        BOOL  italic    = (styleBits & 0x02) ? TRUE : FALSE;
        BOOL  underline = (styleBits & 0x04) ? TRUE : FALSE;
        BOOL  strikeout = (styleBits & 0x08) ? TRUE : FALSE;
        HFONT f = CreateFontA(
            -MulDiv(ptSize, dpiY, 72),
            0, 0, 0, weight, italic, underline, strikeout,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
            FontFaceFamily(face));
        fontCache[key] = f;
        return f;
    };

    HFONT defaultFont = fontFor(defaultFace, defaultPt, 0);
    if (!defaultFont)
    {
        for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return FormatLastError("CreateFont failed");
    }
    HGDIOBJ oldFont = SelectObject(hdc, defaultFont);
    TEXTMETRICA tmDefault{};
    GetTextMetricsA(hdc, &tmDefault);
    const int defaultLineHeight = tmDefault.tmHeight + tmDefault.tmExternalLeading;

    // Pre-pass: build PrintChar per buffer line. Switching SelectObject is
    // amortized — most documents have only a few distinct (face, size,
    // style) combos, so the metric lookups stay cheap.
    std::vector<std::vector<PrintChar>> chars(buffer.LineCount());
    HFONT lastSelected = defaultFont;
    int   lastLineHeight = defaultLineHeight;
    for (int li = 0; li < buffer.LineCount(); ++li)
    {
        const std::string& line = buffer.Line(li);
        chars[li].reserve(line.size());
        for (size_t c = 0; c < line.size(); ++c)
        {
            CharFormat f{};
            if (li < static_cast<int>(req.formats->size())
                && c < (*req.formats)[li].size())
                f = (*req.formats)[li][c];
            FontFace face = resolveFace(f);
            int      pt   = resolveSize(f);
            HFONT    font = fontFor(face, pt, f.style);
            if (font != lastSelected)
            {
                SelectObject(hdc, font);
                TEXTMETRICA tm{};
                GetTextMetricsA(hdc, &tm);
                lastLineHeight = tm.tmHeight + tm.tmExternalLeading;
                lastSelected = font;
            }
            char ch = line[c];
            INT w = 0;
            int adv = 0;
            if (GetCharWidth32A(hdc, static_cast<UINT>(static_cast<unsigned char>(ch)),
                                       static_cast<UINT>(static_cast<unsigned char>(ch)), &w))
                adv = w;
            else
            {
                SIZE sz{};
                GetTextExtentPoint32A(hdc, &ch, 1, &sz);
                adv = sz.cx;
            }
            // Per-char foreground: explicit palette entry → its RGB; Inherit
            // → black on paper regardless of screen theme. Selection isn't a
            // concern in the print pipeline.
            COLORREF cref;
            if (f.color == CharFormat::Inherit || f.color >= Palette::kCount)
                cref = RGB(0, 0, 0);
            else
            {
                Color pc = Palette::ColorAt(f.color);
                cref = RGB(pc.r, pc.g, pc.b);
            }
            bool     hasHl   = false;
            COLORREF hlRef   = RGB(255, 255, 255);
            if (f.highlight != CharFormat::Inherit && f.highlight < Palette::kCount)
            {
                Color ph = Palette::ColorAt(f.highlight);
                hasHl    = true;
                hlRef    = RGB(ph.r, ph.g, ph.b);
            }
            chars[li].push_back({ ch, font, adv, lastLineHeight, f.style,
                                  cref, hasHl, hlRef });
        }
    }

    // Float-aware layout sweep — mirrors WysiwygRenderer's BuildLayoutPass so
    // print wraps text around floats exactly like the screen. Builds the
    // per-segment placement (page, yInPage, xOffset, width), the per-row map
    // (for resolving float draw positions), and the page count in one pass.
    const int lineCount = buffer.LineCount();
    auto twX = [&](int t) { return MulDiv(t, dpiX, 1440); };
    auto twY = [&](int t) { return MulDiv(t, dpiY, 1440); };

    struct Excl { int page, yTop, yBottom, xL, xR; bool reflow; };
    std::vector<Excl> exclusions;
    auto freeRun = [&](int page, int y, int h, int& oL, int& oR) {
        oL = 0; oR = usableWidth;
        std::vector<std::pair<int,int>> bl;
        for (const auto& f : exclusions)
        {
            if (!f.reflow || f.page != page) continue;
            if (y >= f.yBottom || y + h <= f.yTop) continue;
            int a = std::max(0, f.xL), b = std::min(usableWidth, f.xR);
            if (b > a) bl.emplace_back(a, b);
        }
        if (bl.empty()) return;
        std::sort(bl.begin(), bl.end());
        int bestL = 0, bestW = 0, cur = 0;
        for (auto& b : bl) { if (b.first - cur > bestW) { bestW = b.first - cur; bestL = cur; } cur = std::max(cur, b.second); }
        if (usableWidth - cur > bestW) { bestW = usableWidth - cur; bestL = cur; }
        if (bestW <= 0) return;
        oL = bestL; oR = bestL + bestW;
    };
    auto wrapChunk = [&](const std::vector<PrintChar>& cs, int startCol, int availW,
                         int& endExcl, int& nextCol, int& height) {
        int n = static_cast<int>(cs.size());
        if (startCol >= n) { endExcl = n; nextCol = n; height = defaultLineHeight; return; }
        int xAccum = 0, lastBreak = -1;
        for (int c = startCol; c < n; ++c)
        {
            if (xAccum + cs[c].advance > availW && c > startCol)
            {
                int breakAt = (lastBreak > startCol) ? lastBreak : c;
                nextCol = (breakAt == c) ? c : breakAt + 1;
                int hh = 0; for (int k = startCol; k < breakAt; ++k) hh = std::max(hh, cs[k].lineHeight);
                endExcl = breakAt; height = hh > 0 ? hh : defaultLineHeight; return;
            }
            xAccum += cs[c].advance;
            if (cs[c].ch == ' ' || cs[c].ch == '\t') lastBreak = c;
        }
        int hh = 0; for (int k = startCol; k < n; ++k) hh = std::max(hh, cs[k].lineHeight);
        endExcl = n; nextCol = n; height = hh > 0 ? hh : defaultLineHeight;
    };

    std::vector<std::vector<PrintSegment>> segments(lineCount);
    struct PlacedSeg { int li; int s; int page; int yInPage; int xOffset; int width; };
    std::vector<PlacedSeg> placed;
    std::vector<int> rowPage(static_cast<size_t>(std::max(0, lineCount)), 1);
    std::vector<int> rowY   (static_cast<size_t>(std::max(0, lineCount)), 0);
    int totalPages = 1;
    if (req.placedSegments && !req.placedSegments->empty())
    {
        // WYSIWYG path: render the screen's exact layout, scaled from its DPI
        // to the printer's. Page index is 0-based on screen, 1-based here.
        const double sx = static_cast<double>(dpiX) / std::max(1, req.layoutDpi);
        const double sy = static_cast<double>(dpiY) / std::max(1, req.layoutDpi);
        auto scaleX = [&](int v) { return static_cast<int>(v * sx + 0.5); };
        auto scaleY = [&](int v) { return static_cast<int>(v * sy + 0.5); };
        std::vector<bool> rowSeen(static_cast<size_t>(std::max(0, lineCount)), false);
        for (const auto& p : *req.placedSegments)
        {
            if (p.bufferRow < 0 || p.bufferRow >= lineCount) continue;
            int li   = p.bufferRow;
            int sidx = static_cast<int>(segments[li].size());
            int page = p.page + 1;
            segments[li].push_back({ p.startCol, p.endCol, scaleY(p.height) });
            placed.push_back({ li, sidx, page, scaleY(p.yInPage),
                               scaleX(p.xOffset), scaleX(p.width) });
            if (!rowSeen[li]) { rowPage[li] = page; rowY[li] = scaleY(p.yInPage); rowSeen[li] = true; }
            totalPages = std::max(totalPages, page);
        }
    }
    else
    {
        // Fallback: GDI-metric float-aware sweep (used only when no shared
        // layout was supplied).
        int curPage = 1;
        int yInPage = 0;
        for (int li = 0; li < lineCount; ++li)
        {
            if (li > 0 && req.pageBreakBefore
                && li < static_cast<int>(req.pageBreakBefore->size())
                && (*req.pageBreakBefore)[li])
            {
                ++curPage;
                yInPage = 0;
            }
            if (req.floats)
                for (const auto& f : *req.floats)
                {
                    if (f.anchorRow != li) continue;
                    Excl e; e.page = curPage;
                    int yOrigin = (f.vRef == FloatObject::VRef::Margin) ? 0
                                : (f.vRef == FloatObject::VRef::Page)   ? -marginTopPx
                                :  yInPage;
                    e.yTop = yOrigin + twY(f.top);  e.yBottom = yOrigin + twY(f.bottom);
                    int xOrigin = (f.hRef == FloatObject::HRef::Page) ? -marginLeftPx : 0;
                    e.xL = xOrigin + twX(f.left);   e.xR = xOrigin + twX(f.right);
                    e.reflow = (f.wrapType == 1 || f.wrapType == 4 || f.wrapType == 5);
                    exclusions.push_back(e);
                }
            rowPage[li] = curPage; rowY[li] = yInPage;
            const auto& cs = chars[li];
            const int nchars = static_cast<int>(cs.size());
            if (nchars == 0)
            {
                int h = defaultLineHeight;
                if (yInPage + h > usableHeight && yInPage > 0) { ++curPage; yInPage = 0; rowPage[li] = curPage; rowY[li] = yInPage; }
                segments[li].push_back({ 0, 0, h });
                placed.push_back({ li, 0, curPage, yInPage, 0, usableWidth });
                yInPage += h;
                continue;
            }
            int col = 0; bool first = true;
            while (col < nchars)
            {
                int provH = cs[col].lineHeight;
                if (yInPage + provH > usableHeight && yInPage > 0) { ++curPage; yInPage = 0; }
                int xL, xR; freeRun(curPage, yInPage, provH, xL, xR);
                int endExcl, nextCol, h2;
                wrapChunk(cs, col, xR - xL, endExcl, nextCol, h2);
                int sidx = static_cast<int>(segments[li].size());
                segments[li].push_back({ col, endExcl, h2 });
                placed.push_back({ li, sidx, curPage, yInPage, xL, xR - xL });
                if (first) { rowPage[li] = curPage; rowY[li] = yInPage; first = false; }
                yInPage += h2;
                col = nextCol;
            }
        }
        totalPages = std::max(1, curPage);
    }

    const int firstPage = req.allPages ? 1 : std::max(1, req.pageFrom);
    const int lastPage  = req.allPages ? totalPages
                                       : std::min(totalPages, std::max(firstPage, req.pageTo));

    DOCINFOA di{};
    di.cbSize = sizeof(di);
    std::string docName = req.documentName.empty() ? "RetroDocWriter Document"
                                                   : req.documentName;
    di.lpszDocName = docName.c_str();

    if (StartDocA(hdc, &di) <= 0)
    {
        std::string msg = FormatLastError("StartDoc failed");
        SelectObject(hdc, oldFont);
        for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return msg;
    }

    int pagesEmitted = 0;
    int activePage   = -1;

    // Draw one band's three sub-slots (Left / Center / Right) at y, resolving
    // each per the shared HeaderFooter helper so screen and print agree.
    auto emitBand = [&](int pageNum, int y, const HeaderFooterBand& band) {
        if (!band.AnyActive()) return;
        SelectObject(hdc, defaultFont);
        const int usableW = usableRight - usableLeft;
        for (int slotIdx = 0; slotIdx < 3; ++slotIdx)
        {
            std::string s = ResolveHeaderFooterSlot(
                band.slots[static_cast<size_t>(slotIdx)],
                pageNum, totalPages, docName);
            if (s.empty()) continue;
            SIZE sz{};
            GetTextExtentPoint32A(hdc, s.c_str(),
                                  static_cast<int>(s.size()), &sz);
            int x;
            if (slotIdx == 0)            // Left
                x = usableLeft;
            else if (slotIdx == 1)       // Center
                x = usableLeft + (usableW - sz.cx) / 2;
            else                          // Right
                x = usableRight - sz.cx;
            TextOutA(hdc, x, y, s.c_str(), static_cast<int>(s.size()));
        }
    };
    auto emitHeader = [&](int pageNum) {
        if (!req.header.AnyActive()) return;
        int headerY = std::max(0, (usableTop - defaultLineHeight) / 2);
        emitBand(pageNum, headerY, req.header);
    };
    auto emitFooter = [&](int pageNum) {
        if (!req.footer.AnyActive()) return;
        int footerY = usableBottom + std::max(0, (marginBottomPx - defaultLineHeight) / 2);
        if (footerY > vertRes - defaultLineHeight)
            footerY = std::max(usableBottom, vertRes - defaultLineHeight);
        emitBand(pageNum, footerY, req.footer);
    };

    // Draw the floats anchored to `page`, in the requested z-band (below/above
    // text), at their resolved device-pixel rects. Mirrors WysiwygRenderer.
    const int pageLeftDev = usableLeft - marginLeftPx;   // physical page left (approx)
    const int pageTopDev  = usableTop  - marginTopPx;    // physical page top  (approx)
    // Scale factors for the shared layout (same as the placedSegments path):
    // resolved float rects are content-area-relative px at req.layoutDpi.
    const double fsx = static_cast<double>(dpiX) / std::max(1, req.layoutDpi);
    const double fsy = static_cast<double>(dpiY) / std::max(1, req.layoutDpi);
    auto drawFloatsForPage = [&](int page, bool below) {
        if (!req.floats) return;

        // One device-pixel rect per float to draw this pass, paired with its
        // object. Prefer the layout-resolved rects (placedFloats) — they bake
        // in the column x-base and match the rect the text reflowed around;
        // fall back to recomputing from FloatObject twips only when the shared
        // layout wasn't supplied.
        struct DrawItem { const FloatObject* obj; int x, y, w, h; };
        std::vector<DrawItem> list;

        if (req.placedFloats && !req.placedFloats->empty())
        {
            const int nFloats = static_cast<int>(req.floats->size());
            for (const PlacedFloat& pf : *req.placedFloats)
            {
                if (pf.page + 1 != page) continue;
                if (pf.floatIndex < 0 || pf.floatIndex >= nFloats) continue;
                const FloatObject& f = (*req.floats)[static_cast<size_t>(pf.floatIndex)];
                if (f.belowText != below) continue;
                int x = usableLeft + static_cast<int>(pf.xLeft * fsx + 0.5);
                int y = usableTop  + static_cast<int>(pf.yTop  * fsy + 0.5);
                int w = static_cast<int>((pf.xRight  - pf.xLeft) * fsx + 0.5);
                int h = static_cast<int>((pf.yBottom - pf.yTop)  * fsy + 0.5);
                if (w <= 0 || h <= 0) continue;
                list.push_back({ &f, x, y, w, h });
            }
        }
        else
        {
            for (const auto& f : *req.floats)
            {
                if (f.belowText != below) continue;
                int r = std::clamp(f.anchorRow, 0, std::max(0, lineCount - 1));
                if (lineCount <= 0 || rowPage[r] != page) continue;
                int originX = (f.hRef == FloatObject::HRef::Page) ? pageLeftDev : usableLeft;
                int originY = pageTopDev;
                if (f.vRef == FloatObject::VRef::Margin)         originY = usableTop;
                else if (f.vRef == FloatObject::VRef::Paragraph) originY = usableTop + rowY[r];
                int x = originX + MulDiv(f.left, dpiX, 1440);
                int y = originY + MulDiv(f.top,  dpiY, 1440);
                int w = MulDiv(f.right - f.left, dpiX, 1440);
                int h = MulDiv(f.bottom - f.top, dpiY, 1440);
                if (w <= 0 || h <= 0) continue;
                list.push_back({ &f, x, y, w, h });
            }
        }
        std::stable_sort(list.begin(), list.end(),
                         [](const DrawItem& a, const DrawItem& b) { return a.obj->zOrder < b.obj->zOrder; });
        for (const DrawItem& it : list)
        {
            const FloatObject& f = *it.obj;
            int x = it.x, y = it.y, w = it.w, h = it.h;

            if (f.kind == FloatObject::Kind::Image)
            {
                SDL_Surface* surf = ImageDecode::DecodeToSurface(f.imageBytes);
                if (surf) { BlitSurfaceToDC(hdc, surf, x, y, w, h); SDL_DestroySurface(surf); }
                else
                {
                    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, x, y, x + w, y + h);
                    SelectObject(hdc, ob);
                }
                if (!f.caption.empty())
                {
                    SelectObject(hdc, defaultFont);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(80, 80, 80));
                    SIZE cs{};
                    GetTextExtentPoint32A(hdc, f.caption.c_str(), static_cast<int>(f.caption.size()), &cs);
                    TextOutA(hdc, x + (w - cs.cx) / 2, y + h + 2,
                             f.caption.c_str(), static_cast<int>(f.caption.size()));
                }
            }
            else  // Box
            {
                if (f.fillColor != CharFormat::Inherit && f.fillColor < Palette::kCount)
                {
                    Color c = Palette::ColorAt(f.fillColor);
                    HBRUSH br = CreateSolidBrush(RGB(c.r, c.g, c.b));
                    RECT rc{ x, y, x + w, y + h };
                    FillRect(hdc, &rc, br);
                    DeleteObject(br);
                }
                COLORREF lc = RGB(0, 0, 0);
                if (f.lineColor != CharFormat::Inherit && f.lineColor < Palette::kCount)
                {
                    Color c = Palette::ColorAt(f.lineColor);
                    lc = RGB(c.r, c.g, c.b);
                }
                HPEN pen = CreatePen(PS_SOLID, 1, lc);
                HGDIOBJ op = SelectObject(hdc, pen);
                HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, x, y, x + w, y + h);
                SelectObject(hdc, op);
                SelectObject(hdc, ob);
                DeleteObject(pen);
            }
        }
    };

    for (const PlacedSeg& ps : placed)
    {
        if (ps.page < firstPage) continue;
        if (ps.page > lastPage)  break;

        if (activePage != ps.page)
        {
            if (activePage > 0)
            {
                drawFloatsForPage(activePage, /*below=*/false);
                emitFooter(activePage);
                EndPage(hdc);
                ++pagesEmitted;
            }
            if (StartPage(hdc) <= 0) break;
            activePage = ps.page;
            emitHeader(activePage);
            drawFloatsForPage(activePage, /*below=*/true);
        }

        const auto& seg = segments[ps.li][ps.s];
        const auto& lineChars = chars[ps.li];
        int y = usableTop + ps.yInPage;

        // Paragraph alignment: shift the segment (Center/Right) and/or widen
        // its spaces (Justify), mirroring WysiwygRenderer so print matches
        // the on-screen layout.
        ParagraphAlign align = ParagraphAlign::Left;
        if (req.alignment && ps.li < static_cast<int>(req.alignment->size()))
            align = static_cast<ParagraphAlign>((*req.alignment)[ps.li]);
        int segContentW = 0, segTrimmedW = 0, segSpaces = 0;
        MeasurePrintSegment(lineChars, seg.startCol, seg.endCol,
                            segContentW, segTrimmedW, segSpaces);
        PrintSegAlign sa = ComputePrintSegAlign(
            segContentW, segTrimmedW, ps.width, align,
            ps.s + 1 == static_cast<int>(segments[ps.li].size()), segSpaces);
        // Float-determined run inset (ps.xOffset) + alignment offset.
        const int baseX = usableLeft + ps.xOffset + sa.xOffset;

        // Per-column x offset from the segment's left edge (justify stretch
        // baked in after each space). Size = (endCol - startCol + 1).
        std::vector<int> xArr(static_cast<size_t>(seg.endCol - seg.startCol + 1));
        {
            double cum = 0.0;
            for (int c = seg.startCol; c <= seg.endCol; ++c)
            {
                xArr[static_cast<size_t>(c - seg.startCol)] =
                    static_cast<int>(cum + 0.5);
                if (c < seg.endCol)
                {
                    cum += lineChars[c].advance;
                    if (sa.spaceStretch > 0.0
                        && (lineChars[c].ch == ' ' || lineChars[c].ch == '\t'))
                        cum += sa.spaceStretch;
                }
            }
        }
        auto colX = [&](int col) { return baseX + xArr[static_cast<size_t>(col - seg.startCol)]; };

        // Group consecutive chars by (font, color, highlight) so we make
        // one SelectObject / SetTextColor / FillRect + ExtTextOut call per
        // run. ExtTextOut with an explicit dx array reproduces the justify
        // space-stretch within a run. Highlight rect goes underneath.
        int groupStart = seg.startCol;
        while (groupStart < seg.endCol)
        {
            HFONT    groupFont      = lineChars[groupStart].font;
            COLORREF groupColor     = lineChars[groupStart].color;
            bool     groupHasHl     = lineChars[groupStart].hasHighlight;
            COLORREF groupHlColor   = lineChars[groupStart].highlight;
            int groupEnd = groupStart + 1;
            while (groupEnd < seg.endCol
                   && lineChars[groupEnd].font          == groupFont
                   && lineChars[groupEnd].color         == groupColor
                   && lineChars[groupEnd].hasHighlight  == groupHasHl
                   && (!groupHasHl
                       || lineChars[groupEnd].highlight == groupHlColor))
                ++groupEnd;

            int runLeft  = colX(groupStart);
            int runRight = colX(groupEnd);

            if (groupHasHl && runRight > runLeft)
            {
                HBRUSH brush = CreateSolidBrush(groupHlColor);
                if (brush)
                {
                    RECT bg{ runLeft, y, runRight,
                             y + lineChars[groupStart].lineHeight };
                    FillRect(hdc, &bg, brush);
                    DeleteObject(brush);
                }
            }

            SelectObject(hdc, groupFont);
            SetTextColor(hdc, groupColor);
            std::string  text;
            std::vector<INT> dx;
            text.reserve(static_cast<size_t>(groupEnd - groupStart));
            dx.reserve(static_cast<size_t>(groupEnd - groupStart));
            for (int c = groupStart; c < groupEnd; ++c)
            {
                text.push_back(lineChars[c].ch);
                dx.push_back(xArr[static_cast<size_t>(c + 1 - seg.startCol)]
                           - xArr[static_cast<size_t>(c - seg.startCol)]);
            }
            // Transparent background so ExtTextOut doesn't over-paint the
            // highlight rect we just laid down.
            SetBkMode(hdc, TRANSPARENT);
            ExtTextOutA(hdc, runLeft, y, 0, nullptr,
                        text.c_str(), static_cast<UINT>(text.size()), dx.data());
            groupStart = groupEnd;
        }
    }
    if (activePage > 0)
    {
        drawFloatsForPage(activePage, /*below=*/false);
        emitFooter(activePage);
        EndPage(hdc);
        ++pagesEmitted;
    }

    EndDoc(hdc);
    SelectObject(hdc, oldFont);
    for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
    DeleteDC(hdc);
    ClosePrinter(hPrinter);

    if (pagesEmitted == 0)
        return "Nothing printed (page range produced no pages).";

    std::string msg = "Printed " + std::to_string(pagesEmitted) + " page";
    if (pagesEmitted != 1) msg += "s";
    msg += ".";
    if (req.copies > 1)
        msg += " (" + std::to_string(req.copies) + " copies)";
    return msg;
}
