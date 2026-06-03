#include "doctest/doctest.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/CharStyle.h"
#include "editor/RtfReader.h"
#include "editor/RtfWriter.h"
#include "render/FontFace.h"

#include <string>

// Bulleted-list level is a per-paragraph property (FormattedTextBuffer::
// m_listLevel) maintained in lock-step with the text in every mutator,
// round-tripped through RTF (\ilvl + \pn bullet block), and snapshotted by
// undo/redo. These tests exercise the SDL-free core that the GUI links.

namespace
{
    FormattedTextBuffer RtfRoundTrip(const FormattedTextBuffer& in)
    {
        std::string rtf = RtfWriter::Write(in, FontFace::EBGaramond, 12);
        FormattedTextBuffer out;
        REQUIRE(RtfReader::Read(rtf, out, nullptr));
        return out;
    }
}

TEST_CASE("List levels: getter/setter and HasAnyFormatting")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a", "b", "c" });
    CHECK(fb.ListLevel(0) == 0);
    CHECK_FALSE(fb.HasAnyFormatting());

    fb.SetListLevel(1, 2);
    CHECK(fb.ListLevel(1) == 2);
    CHECK(fb.ListLevel(0) == 0);
    CHECK(fb.HasAnyFormatting());

    // SetLinesPlain resets list state back to none.
    fb.SetLinesPlain({ "x", "y" });
    CHECK(fb.ListLevel(0) == 0);
    CHECK(fb.ListLevel(1) == 0);
    CHECK_FALSE(fb.HasAnyFormatting());
}

TEST_CASE("List levels: out-of-range access is safe")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "only" });
    CHECK(fb.ListLevel(-1) == 0);
    CHECK(fb.ListLevel(99) == 0);
    fb.SetListLevel(99, 3);          // no-op, must not crash
    CHECK(fb.ListLevel(0) == 0);
}

TEST_CASE("List levels: InsertNewline inherits the split row's level")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "hello" });
    fb.SetListLevel(0, 2);

    // Split mid-paragraph: both halves stay at level 2 (Enter in a bullet
    // makes another bullet at the same level).
    fb.InsertNewline(2, 0);
    REQUIRE(fb.LineCount() == 2);
    CHECK(fb.ListLevel(0) == 2);
    CHECK(fb.ListLevel(1) == 2);
}

TEST_CASE("List levels: InsertText (multi-line paste) inherits the level")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "start" });
    fb.SetListLevel(0, 1);

    int er = 0, ec = 0;
    fb.InsertText(5, 0, "\none\ntwo", CharFormat{}, er, ec);
    REQUIRE(fb.LineCount() == 3);
    CHECK(fb.ListLevel(0) == 1);
    CHECK(fb.ListLevel(1) == 1);
    CHECK(fb.ListLevel(2) == 1);
}

TEST_CASE("List levels: Backspace row-merge keeps the surviving row's level")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "aa", "bb" });
    fb.SetListLevel(0, 1);
    fb.SetListLevel(1, 3);

    // Join row 1 into row 0 (caret at col 0 of row 1). Row 0 (the survivor)
    // keeps its own level; the disappearing row's level is dropped.
    fb.Backspace(0, 1);
    REQUIRE(fb.LineCount() == 1);
    CHECK(fb.ListLevel(0) == 1);
}

TEST_CASE("List levels: DeleteForward row-merge keeps the surviving row's level")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "aa", "bb" });
    fb.SetListLevel(0, 2);
    fb.SetListLevel(1, 1);

    // Delete at end of row 0 joins row 1 up; row 0 survives with its level.
    fb.DeleteForward(2, 0);
    REQUIRE(fb.LineCount() == 1);
    CHECK(fb.ListLevel(0) == 2);
}

TEST_CASE("List levels: DeleteRange erases the right slice")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "r0", "r1", "r2", "r3" });
    fb.SetListLevel(0, 1);
    fb.SetListLevel(1, 2);
    fb.SetListLevel(2, 3);
    fb.SetListLevel(3, 4);

    // Delete across rows 1..2 (down to start of row 3 here selecting r1+r2):
    // rows 1 and 2 vanish, row 0 keeps level 1, former row 3 keeps level 4.
    fb.DeleteRange(0, 2, 3, 0);
    REQUIRE(fb.LineCount() == 1);
    CHECK(fb.ListLevel(0) == 1);   // row 0 survives unchanged
}

TEST_CASE("List levels: FlattenAllStyles clears bullets")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "item" });
    fb.SetListLevel(0, 2);
    CHECK(fb.HasAnyFormatting());

    fb.FlattenAllStyles();
    CHECK(fb.ListLevel(0) == 0);
    CHECK_FALSE(fb.HasAnyFormatting());
}

TEST_CASE("List levels: RTF round-trip preserves per-row levels")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "top", "sub", "deeper", "plain", "back" });
    fb.SetListLevel(0, 1);
    fb.SetListLevel(1, 2);
    fb.SetListLevel(2, 3);
    // row 3 stays 0 (left the list)
    fb.SetListLevel(4, 1);

    FormattedTextBuffer out = RtfRoundTrip(fb);
    REQUIRE(out.LineCount() == 5);
    CHECK(out.Line(0) == "top");
    CHECK(out.Line(3) == "plain");
    CHECK(out.ListLevel(0) == 1);
    CHECK(out.ListLevel(1) == 2);
    CHECK(out.ListLevel(2) == 3);
    CHECK(out.ListLevel(3) == 0);
    CHECK(out.ListLevel(4) == 1);
}

TEST_CASE("List levels: RTF round-trip is idempotent for every level 1..6")
{
    for (uint8_t lvl = 1; lvl <= kMaxListLevel; ++lvl)
    {
        FormattedTextBuffer fb;
        fb.SetLinesPlain({ "before", "item", "after" });
        fb.SetListLevel(1, lvl);

        FormattedTextBuffer once = RtfRoundTrip(fb);
        CHECK(once.ListLevel(0) == 0);
        CHECK(once.ListLevel(1) == lvl);
        CHECK(once.ListLevel(2) == 0);

        // Save -> load -> save -> load must land on the same levels.
        FormattedTextBuffer twice = RtfRoundTrip(once);
        CHECK(twice.ListLevel(0) == 0);
        CHECK(twice.ListLevel(1) == lvl);
        CHECK(twice.ListLevel(2) == 0);
    }
}

TEST_CASE("List levels: a plain \\li indent is not mistaken for a bullet")
{
    // External RTF that indents a paragraph with \li but carries no \ilvl/\pn
    // must read back as a normal (non-list) paragraph.
    const std::string rtf =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}"
        "\\li720 indented but not a list\\par}";
    FormattedTextBuffer out;
    REQUIRE(RtfReader::Read(rtf, out, nullptr));
    REQUIRE(out.LineCount() >= 1);
    CHECK(out.ListLevel(0) == 0);
}
