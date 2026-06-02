#include "doctest/doctest.h"
#include "editor/RichFileDocument.h"
#include "editor/CharStyle.h"
#include "render/FontFace.h"
#include "test_util.h"

#include <string>

TEST_CASE("WithDefaultExtension")
{
    CHECK(RichFileDocument::WithDefaultExtension("foo") == "foo.rtf");
    CHECK(RichFileDocument::WithDefaultExtension("foo.txt") == "foo.txt");
    CHECK(RichFileDocument::WithDefaultExtension("bar.rtf") == "bar.rtf");
    CHECK(RichFileDocument::WithDefaultExtension("foo", ".txt") == "foo.txt");
    CHECK(RichFileDocument::WithDefaultExtension("foo.rtf", ".txt") == "foo.rtf");
}

TEST_CASE("IsRtfPath is case-insensitive on the extension")
{
    CHECK(RichFileDocument::IsRtfPath("a.rtf"));
    CHECK(RichFileDocument::IsRtfPath("a.RTF"));
    CHECK_FALSE(RichFileDocument::IsRtfPath("a.txt"));
    CHECK_FALSE(RichFileDocument::IsRtfPath("a"));
}

TEST_CASE("Save/Load round-trip preserves formatting for .rtf")
{
    TempDir dir;
    std::string path = dir.file("doc.rtf");

    RichFileDocument doc;
    doc.Buffer().SetLinesPlain({ "Hello", "There" });
    doc.Buffer().SetStyleInRange(0, 0, 0, 5, CharStyle::Bold, true);
    REQUIRE(doc.SaveAs(path, FontFace::EBGaramond, 12));

    RichFileDocument loaded;
    REQUIRE(loaded.Load(path));
    REQUIRE(loaded.Buffer().LineCount() == 2);
    CHECK(loaded.Buffer().Line(0) == "Hello");
    CHECK(loaded.Buffer().Line(1) == "There");
    CHECK((loaded.Buffer().StyleAt(0, 0) & CharStyle::Bold) != 0);
}

TEST_CASE("Save/Load .txt keeps text but carries no formatting")
{
    TempDir dir;
    std::string path = dir.file("doc.txt");

    RichFileDocument doc;
    doc.Buffer().SetLinesPlain({ "plain text here" });
    REQUIRE(doc.SaveAs(path, FontFace::EBGaramond, 12));

    RichFileDocument loaded;
    REQUIRE(loaded.Load(path));
    CHECK(loaded.Buffer().Line(0) == "plain text here");
    for (int c = 0; c < loaded.Buffer().LineLength(0); ++c)
        CHECK(loaded.Buffer().FormatAt(0, c).IsPlain());
    CHECK_FALSE(loaded.Buffer().HasAnyFormatting());
}
