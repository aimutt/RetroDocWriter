#pragma once
#include "editor/CharStyle.h"
#include "editor/FloatObject.h"
#include "render/PlacedSegment.h"
#include <string>
#include <vector>

class TextBuffer;

enum class PrintOrientation { Portrait, Landscape };

struct PrintMargins
{
    double topIn    = 0.5;
    double bottomIn = 0.5;
    double leftIn   = 0.75;
    double rightIn  = 0.75;
};

struct PrintRequest
{
    std::string       printerName;             // empty = first from EnumeratePrinters()
    int               copies      = 1;
    bool              allPages    = true;
    int               pageFrom    = 1;         // honored only when allPages == false
    int               pageTo      = 1;         // honored only when allPages == false
    PrintOrientation  orientation = PrintOrientation::Portrait;
    PrintMargins      margins;
    std::string       documentName;            // used in the page footer

    // When useDocumentFont is true, the printer uses fontFamily + pointSize
    // instead of the built-in Courier 10pt fallback. fontFile is the bundled
    // TTF path; the printer registers it via AddFontResourceEx before
    // CreateFont so end-users don't need the font installed system-wide.
    bool              useDocumentFont = false;
    std::string       fontFamily;
    std::string       fontFile;
    int               pointSize       = 10;
    bool              bold            = false;

    // Optional per-character formatting parallel to `buffer`. When non-
    // null, the print path switches GDI fonts per-run to honor style /
    // face / size from each CharFormat. Inherit-sentinel face/size falls
    // back to `fontFamily` / `pointSize` (the document defaults set
    // above). When null, every character prints with the single document
    // font (Phase 2 behavior).
    const std::vector<std::vector<CharFormat>>* formats = nullptr;

    // Optional per-row page-break-before flags parallel to `buffer`.
    // When set on row N, the print path advances to a new page before
    // emitting that row's first segment. Null = no forced page breaks.
    const std::vector<bool>* pageBreakBefore = nullptr;

    // Optional per-row paragraph alignment (ParagraphAlign as uint8_t)
    // parallel to `buffer`. The formatted print path offsets / justifies
    // each visual segment to match the on-screen WYSIWYG layout. Null =
    // every paragraph left-aligned.
    const std::vector<uint8_t>* alignment = nullptr;

    // Four independent header/footer slots: file name (left) and page number
    // (right) in the top-margin header and/or bottom-margin footer. All
    // default off; matches the on-screen WysiwygRenderer so print is WYSIWYG.
    bool headerShowFilename   = false;
    bool headerShowPageNumber = false;
    bool footerShowFilename   = false;
    bool footerShowPageNumber = false;

    // Optional floating shapes/images, anchored to buffer rows. The formatted
    // print path draws each at its resolved page position (z-ordered around
    // the text). Null = no floats.
    const std::vector<FloatObject>* floats = nullptr;

    // Optional pre-computed layout from WysiwygRenderer (px at `layoutDpi`).
    // When supplied, the formatted print path renders these exact lines —
    // same breaks, pagination, and float runs as the screen — instead of
    // re-wrapping with GDI metrics. `layoutDpi` is the DPI those pixels are
    // in (the on-screen 96), scaled to the printer's DPI at draw time.
    const std::vector<PlacedSegment>* placedSegments = nullptr;
    int                               layoutDpi      = 96;

    // Optional pre-resolved float rects from the same layout pass (px at
    // `layoutDpi`, content-area-relative — see PlacedFloat). When supplied, the
    // print path draws each float at its resolved rect (scaled to the printer's
    // DPI) instead of recomputing from FloatObject twips, so column-anchored
    // floats print at their column and the printed image occupies exactly the
    // rect the text reflowed around. `floatIndex` indexes `floats` above.
    const std::vector<PlacedFloat>* placedFloats = nullptr;
};

// Returns the installed printers. First entry is the system default (or the
// list is empty if no printers are installed / the API call failed).
std::vector<std::string> EnumeratePrinters();

// Synchronous; returns a user-facing status string suitable for the status bar
// ("Printed N pages." on success or an error description on failure).
std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req);
