#include "doctest/doctest.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/CharStyle.h"
#include "editor/FloatObject.h"

#include <vector>

TEST_CASE("Default formatted buffer is plain")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "hello" });
    CHECK_FALSE(fb.HasAnyFormatting());
    for (int c = 0; c < fb.LineLength(0); ++c)
        CHECK(fb.FormatAt(0, c).IsPlain());
}

TEST_CASE("Style/size/face/color/highlight range setters")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "hello world" });

    fb.SetStyleInRange(0, 0, 0, 5, CharStyle::Bold, true);   // "hello"
    CHECK((fb.StyleAt(0, 0) & CharStyle::Bold) != 0);
    CHECK((fb.StyleAt(0, 6) & CharStyle::Bold) == 0);
    CHECK(fb.AllInRangeHaveStyle(0, 0, 0, 5, CharStyle::Bold));
    CHECK_FALSE(fb.AllInRangeHaveStyle(0, 0, 0, 11, CharStyle::Bold));

    fb.SetSizeInRange(0, 0, 0, 1, 2);
    CHECK(fb.SizeAt(0, 0) == 2);
    fb.SetFaceInRange(0, 0, 0, 1, 3);
    CHECK(fb.FaceAt(0, 0) == 3);
    fb.SetColorInRange(0, 0, 0, 1, 5);
    CHECK(fb.FormatAt(0, 0).color == 5);
    fb.SetHighlightInRange(0, 0, 0, 1, 7);
    CHECK(fb.FormatAt(0, 0).highlight == 7);

    CHECK(fb.HasAnyFormatting());
}

TEST_CASE("Format vector stays length-locked with the text")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "ab" });
    fb.InsertChar(1, 0, 'X', CharFormat{});
    CHECK(fb.LineLength(0) == 3);
    CHECK(fb.Line(0) == "aXb");
    // FormatAt must be valid for every column (no out-of-range / desync).
    for (int c = 0; c < fb.LineLength(0); ++c)
        (void)fb.FormatAt(0, c);

    fb.InsertNewline(1, 0);
    CHECK(fb.LineCount() == 2);
    for (int row = 0; row < fb.LineCount(); ++row)
        for (int c = 0; c < fb.LineLength(row); ++c)
            (void)fb.FormatAt(row, c);
}

TEST_CASE("Per-paragraph alignment and page breaks")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a", "b", "c" });

    CHECK(fb.Alignment(0) == ParagraphAlign::Left);
    fb.SetAlignment(1, ParagraphAlign::Center);
    CHECK(fb.Alignment(1) == ParagraphAlign::Center);
    CHECK(fb.Alignment(0) == ParagraphAlign::Left);
    CHECK(fb.HasAnyFormatting());

    fb.SetLinesPlain({ "a", "b" });            // resets paragraph state
    CHECK(fb.Alignment(0) == ParagraphAlign::Left);
    CHECK_FALSE(fb.PageBreakBefore(1));
    fb.SetPageBreakBefore(1, true);
    CHECK(fb.PageBreakBefore(1));
}

TEST_CASE("SetColumns clamps count and gutter")
{
    FormattedTextBuffer fb;
    fb.SetColumns(3, 360);
    CHECK(fb.ColumnCount() == 3);
    CHECK(fb.ColumnGutterTwips() == 360);

    fb.SetColumns(99, -5);
    CHECK(fb.ColumnCount() == 12);             // clamped to max
    CHECK(fb.ColumnGutterTwips() == 0);        // clamped to >= 0

    fb.SetColumns(0, 100);
    CHECK(fb.ColumnCount() == 1);              // clamped to min
}

TEST_CASE("FlattenAllStyles removes character formatting")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "hello" });
    fb.SetStyleInRange(0, 0, 0, 5, CharStyle::Italic, true);
    CHECK(fb.HasAnyFormatting());

    fb.FlattenAllStyles();
    CHECK_FALSE(fb.HasAnyFormatting());
    for (int c = 0; c < fb.LineLength(0); ++c)
        CHECK(fb.FormatAt(0, c).IsPlain());
}

TEST_CASE("Float anchors shift when rows are inserted above them")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a", "b", "c" });

    std::vector<FloatObject> floats(1);
    floats[0].anchorRow = 2;                   // anchored to "c"
    fb.SetFloats(floats);

    fb.InsertNewline(0, 0);                     // inserts a row above row 0's text
    REQUIRE(fb.Floats().size() == 1);
    CHECK(fb.Floats()[0].anchorRow == 3);       // travelled down with its paragraph
}
