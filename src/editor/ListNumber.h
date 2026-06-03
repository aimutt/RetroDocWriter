#pragma once
#include "editor/CharStyle.h"
#include "editor/FormattedTextBuffer.h"
#include <algorithm>
#include <string>
#include <vector>

// Shared numbered-list label resolver (SDL-free).
//
// A numbered list paragraph shows a "legal / multilevel" marker: 1., 2., then
// 2.1, 2.2, then 2.2.1, … — the top level carries a trailing period, deeper
// levels are dotted ordinal paths with no trailing period. Sub-counters restart
// under each parent, and a non-list paragraph (level 0) breaks/restarts the run.
//
// The marker depends on every preceding paragraph, so it can't be derived from a
// single row in isolation. ComputeNumberedLabels does one forward pass over the
// whole buffer and returns the marker string for every row (empty for rows that
// are not numbered list items — bulleted rows draw a glyph instead). Both the
// on-screen renderer (WysiwygRenderer) and the print path (Print.cpp) call this,
// guaranteeing the printed numbers match the screen.

inline std::vector<std::string>
ComputeNumberedLabels(const FormattedTextBuffer& buf)
{
    const int n = buf.LineCount();
    std::vector<std::string> labels(static_cast<size_t>(std::max(0, n)));

    // counters[k] = current ordinal at nesting level k (1-based). Index 0 unused.
    int counters[kMaxListLevel + 2] = { 0 };

    for (int row = 0; row < n; ++row)
    {
        const int level = buf.ListLevel(row);   // masked nesting level (0 = none)
        if (level <= 0)
        {
            // A normal paragraph breaks the list: numbering restarts after it.
            for (int k = 0; k <= kMaxListLevel + 1; ++k) counters[k] = 0;
            continue;
        }
        if (!buf.ListNumbered(row))
            continue;                            // bulleted item: glyph, no number

        const int lvl = (level > kMaxListLevel) ? kMaxListLevel : level;
        ++counters[lvl];
        for (int k = lvl + 1; k <= kMaxListLevel + 1; ++k) counters[k] = 0;

        std::string label;
        for (int k = 1; k <= lvl; ++k)
        {
            if (k > 1) label += '.';
            label += std::to_string(counters[k] > 0 ? counters[k] : 1);
        }
        if (lvl == 1) label += '.';              // "1." at top, "2.1" deeper
        labels[static_cast<size_t>(row)] = std::move(label);
    }
    return labels;
}
