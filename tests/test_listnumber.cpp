#include "doctest/doctest.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/CharStyle.h"
#include "editor/ListNumber.h"
#include "editor/RtfReader.h"
#include "editor/RtfWriter.h"
#include "render/FontFace.h"

#include <string>

// Numbered lists ride on the same per-row m_listLevel byte as bullets, with
// kListNumberedFlag distinguishing the kind. Labels are legal/multilevel
// ("1.", "2.1", "2.2.1") computed by ComputeNumberedLabels. These tests cover
// the SDL-free core: the flag accessors, the label resolver, and RTF round-trip.

namespace
{
    FormattedTextBuffer RtfRoundTrip(const FormattedTextBuffer& in)
    {
        std::string rtf = RtfWriter::Write(in, FontFace::EBGaramond, 12);
        FormattedTextBuffer out;
        REQUIRE(RtfReader::Read(rtf, out, nullptr));
        return out;
    }

    // Helper: make row a numbered list item at the given level.
    void MakeNumbered(FormattedTextBuffer& fb, int row, uint8_t level)
    {
        fb.SetListLevel(row, level);
        fb.SetListNumbered(row, true);
    }
}

TEST_CASE("List kind flag: set/clear preserves level, clears on level 0")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a", "b" });

    fb.SetListLevel(0, 2);
    CHECK(fb.ListLevel(0) == 2);
    CHECK_FALSE(fb.ListNumbered(0));

    fb.SetListNumbered(0, true);
    CHECK(fb.ListLevel(0) == 2);          // level intact
    CHECK(fb.ListNumbered(0));

    // SetListLevel keeps the numbered flag while level >= 1...
    fb.SetListLevel(0, 3);
    CHECK(fb.ListLevel(0) == 3);
    CHECK(fb.ListNumbered(0));

    // ...and clears it when leaving the list (level 0).
    fb.SetListLevel(0, 0);
    CHECK(fb.ListLevel(0) == 0);
    CHECK_FALSE(fb.ListNumbered(0));

    // SetListNumbered is a no-op on a non-list row.
    fb.SetListNumbered(1, true);
    CHECK_FALSE(fb.ListNumbered(1));
}

TEST_CASE("ComputeNumberedLabels: top-level decimal with trailing period")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "one", "two", "three" });
    MakeNumbered(fb, 0, 1);
    MakeNumbered(fb, 1, 1);
    MakeNumbered(fb, 2, 1);

    auto labels = ComputeNumberedLabels(fb);
    REQUIRE(labels.size() == 3);
    CHECK(labels[0] == "1.");
    CHECK(labels[1] == "2.");
    CHECK(labels[2] == "3.");
}

TEST_CASE("ComputeNumberedLabels: legal multilevel nesting and continuation")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "i0", "i1", "s0", "s1", "d0", "i2" });
    MakeNumbered(fb, 0, 1);   // 1.
    MakeNumbered(fb, 1, 1);   // 2.
    MakeNumbered(fb, 2, 2);   // 2.1
    MakeNumbered(fb, 3, 2);   // 2.2
    MakeNumbered(fb, 4, 3);   // 2.2.1
    MakeNumbered(fb, 5, 1);   // 3.  (top-level continues past the sub-items)

    auto labels = ComputeNumberedLabels(fb);
    REQUIRE(labels.size() == 6);
    CHECK(labels[0] == "1.");
    CHECK(labels[1] == "2.");
    CHECK(labels[2] == "2.1");
    CHECK(labels[3] == "2.2");
    CHECK(labels[4] == "2.2.1");
    CHECK(labels[5] == "3.");
}

TEST_CASE("ComputeNumberedLabels: sub-counters restart under each parent")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "p1", "c1", "c2", "p2", "c1b" });
    MakeNumbered(fb, 0, 1);   // 1.
    MakeNumbered(fb, 1, 2);   // 1.1
    MakeNumbered(fb, 2, 2);   // 1.2
    MakeNumbered(fb, 3, 1);   // 2.
    MakeNumbered(fb, 4, 2);   // 2.1  (level-2 counter reset under the new parent)

    auto labels = ComputeNumberedLabels(fb);
    CHECK(labels[1] == "1.1");
    CHECK(labels[2] == "1.2");
    CHECK(labels[4] == "2.1");
}

TEST_CASE("ComputeNumberedLabels: a normal paragraph restarts numbering")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "n1", "n2", "plain", "n3" });
    MakeNumbered(fb, 0, 1);
    MakeNumbered(fb, 1, 1);
    // row 2 left as a normal paragraph (level 0)
    MakeNumbered(fb, 3, 1);

    auto labels = ComputeNumberedLabels(fb);
    CHECK(labels[0] == "1.");
    CHECK(labels[1] == "2.");
    CHECK(labels[2] == "");      // not a list item
    CHECK(labels[3] == "1.");    // numbering restarted after the break
}

TEST_CASE("ComputeNumberedLabels: bulleted rows produce no number")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "num", "bul", "num2" });
    MakeNumbered(fb, 0, 1);          // 1.
    fb.SetListLevel(1, 1);           // bulleted (no numbered flag)
    MakeNumbered(fb, 2, 1);          // 2. (bullet between doesn't consume a number)

    auto labels = ComputeNumberedLabels(fb);
    CHECK(labels[0] == "1.");
    CHECK(labels[1] == "");          // bullet → drawn as a glyph, no label
    CHECK(labels[2] == "2.");
}

TEST_CASE("List kind survives InsertNewline (Enter continues a numbered list)")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "hello" });
    MakeNumbered(fb, 0, 2);

    fb.InsertNewline(5, 0);          // Enter at end of the item
    REQUIRE(fb.LineCount() == 2);
    CHECK(fb.ListLevel(1) == 2);
    CHECK(fb.ListNumbered(1));        // new item is still numbered, same level
}

TEST_CASE("List kind survives row-merge (raw byte moves with the row)")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "aa", "bb" });
    MakeNumbered(fb, 0, 1);
    fb.SetListLevel(1, 3);           // bulleted level 3

    fb.Backspace(0, 1);              // join row 1 into row 0 — survivor is row 0
    REQUIRE(fb.LineCount() == 1);
    CHECK(fb.ListLevel(0) == 1);
    CHECK(fb.ListNumbered(0));        // row 0 kept its numbered level-1 state
}

TEST_CASE("Numbered/bulleted kind round-trips through RTF per row")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "num1", "num2", "bul", "plain", "num3" });
    MakeNumbered(fb, 0, 1);
    MakeNumbered(fb, 1, 2);
    fb.SetListLevel(2, 1);           // bulleted
    // row 3 normal
    MakeNumbered(fb, 4, 1);

    FormattedTextBuffer out = RtfRoundTrip(fb);
    REQUIRE(out.LineCount() == 5);

    CHECK(out.ListLevel(0) == 1);
    CHECK(out.ListNumbered(0));
    CHECK(out.ListLevel(1) == 2);
    CHECK(out.ListNumbered(1));
    CHECK(out.ListLevel(2) == 1);
    CHECK_FALSE(out.ListNumbered(2)); // stayed a bullet
    CHECK(out.ListLevel(3) == 0);
    CHECK(out.ListLevel(4) == 1);
    CHECK(out.ListNumbered(4));

    // Labels recompute identically after the round-trip.
    auto labels = ComputeNumberedLabels(out);
    CHECK(labels[0] == "1.");
    CHECK(labels[1] == "1.1");
    CHECK(labels[2] == "");
    CHECK(labels[4] == "1.");
}

TEST_CASE("Numbered RTF round-trip is idempotent")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a", "b", "c" });
    MakeNumbered(fb, 0, 1);
    MakeNumbered(fb, 1, 2);
    MakeNumbered(fb, 2, 1);

    FormattedTextBuffer once  = RtfRoundTrip(fb);
    FormattedTextBuffer twice = RtfRoundTrip(once);
    for (int r = 0; r < 3; ++r)
    {
        CHECK(twice.ListLevel(r) == once.ListLevel(r));
        CHECK(twice.ListNumbered(r) == once.ListNumbered(r));
    }
}
