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
