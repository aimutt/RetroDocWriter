#pragma once

// One laid-out visual line, in page-content pixel coordinates at a stated DPI
// (0,0 = the top-left of a page's content area, i.e. just inside the top/left
// margins). Produced by WysiwygRenderer::ComputePlacedSegments so the print
// path can render the *exact* same wrap, pagination, and float-avoidance the
// screen shows — the single source of layout truth, the WYSIWYG guarantee.
struct PlacedSegment
{
    int bufferRow = 0;   // source row in the TextBuffer
    int startCol  = 0;   // byte range of this visual line: [startCol, endCol)
    int endCol    = 0;
    int page      = 0;   // 0-based page index
    int yInPage   = 0;   // top y within the page content area (px)
    int xOffset   = 0;   // inset from the column's left edge (px) — float run
    int width     = 0;   // available text width for this line (px)
    int height    = 0;   // line height (px)
};

// One float (image/box) at the position the layout pass resolved for it, in the
// same page-content pixel coordinates as PlacedSegment (0,0 = inside the
// top/left margins) at the same stated DPI. Produced by
// WysiwygRenderer::ComputePlacedFloats so the print path draws each float at the
// exact rect the text reflowed around — including the column x-base for
// column-anchored floats, which the print path cannot otherwise recover.
// `floatIndex` indexes the document's FloatObject vector (for the image bytes /
// caption / kind / colors / z-order / below-text flag).
struct PlacedFloat
{
    int floatIndex = 0;
    int page       = 0;   // 0-based page index
    int xLeft      = 0;   // content-area-relative px (includes column x-base)
    int yTop       = 0;
    int xRight     = 0;
    int yBottom    = 0;
};
