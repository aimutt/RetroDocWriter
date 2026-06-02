#include "doctest/doctest.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/CharStyle.h"
#include "editor/RtfReader.h"
#include "editor/RtfWriter.h"
#include "render/FontFace.h"

#include <string>

namespace
{
    FormattedTextBuffer RoundTrip(const FormattedTextBuffer& in,
                                  RtfReader::Header* hdr = nullptr)
    {
        std::string rtf = RtfWriter::Write(in, FontFace::EBGaramond, 12);
        FormattedTextBuffer out;
        REQUIRE(RtfReader::Read(rtf, out, hdr));
        return out;
    }
}

TEST_CASE("Plain text round-trips through RTF")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "Hello World" });
    FormattedTextBuffer out = RoundTrip(fb);
    CHECK(out.LineCount() == 1);
    CHECK(out.Line(0) == "Hello World");
}

TEST_CASE("Bold/italic runs survive the round-trip")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "Hello World" });
    fb.SetStyleInRange(0, 0, 0, 5, CharStyle::Bold, true);     // "Hello"
    fb.SetStyleInRange(0, 6, 0, 11, CharStyle::Italic, true);  // "World"

    FormattedTextBuffer out = RoundTrip(fb);
    CHECK(out.Line(0) == "Hello World");
    CHECK((out.StyleAt(0, 0) & CharStyle::Bold) != 0);
    CHECK((out.StyleAt(0, 6) & CharStyle::Italic) != 0);
    CHECK((out.StyleAt(0, 0) & CharStyle::Italic) == 0);
}

TEST_CASE("Multiple paragraphs round-trip")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "one", "two", "three" });
    FormattedTextBuffer out = RoundTrip(fb);
    REQUIRE(out.LineCount() == 3);
    CHECK(out.Line(0) == "one");
    CHECK(out.Line(1) == "two");
    CHECK(out.Line(2) == "three");
}

TEST_CASE("Brace and backslash literals are escaped and restored")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "a{b}\\c" });
    std::string rtf = RtfWriter::Write(fb, FontFace::EBGaramond, 12);
    // Literal braces/backslash must be escaped in the emitted RTF.
    CHECK(rtf.find("\\{") != std::string::npos);
    CHECK(rtf.find("\\}") != std::string::npos);

    FormattedTextBuffer out = RoundTrip(fb);
    CHECK(out.Line(0) == "a{b}\\c");
}

TEST_CASE("write -> read -> write is byte-stable")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "Stable", "Document" });
    fb.SetStyleInRange(0, 0, 0, 6, CharStyle::Underline, true);

    std::string rtf1 = RtfWriter::Write(fb, FontFace::EBGaramond, 14);
    FormattedTextBuffer mid;
    REQUIRE(RtfReader::Read(rtf1, mid));
    std::string rtf2 = RtfWriter::Write(mid, FontFace::EBGaramond, 14);
    CHECK(rtf1 == rtf2);
}

TEST_CASE("Header point size parses back from the document")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "x" });
    std::string rtf = RtfWriter::Write(fb, FontFace::EBGaramond, 18);

    FormattedTextBuffer out;
    RtfReader::Header hdr;
    REQUIRE(RtfReader::Read(rtf, out, &hdr));
    CHECK(hdr.pointSize == 18);
    CHECK_FALSE(hdr.fontFamily.empty());
}
