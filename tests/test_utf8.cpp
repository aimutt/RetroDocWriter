#include "doctest/doctest.h"
#include "editor/Utf8.h"

#include <string>

TEST_CASE("Utf8CodepointSize classifies lead bytes")
{
    CHECK(Utf8CodepointSize(static_cast<unsigned char>('A')) == 1);
    CHECK(Utf8CodepointSize(0xC3) == 2);   // 110xxxxx
    CHECK(Utf8CodepointSize(0xE2) == 3);   // 1110xxxx
    CHECK(Utf8CodepointSize(0xF0) == 4);   // 11110xxx
    CHECK(Utf8CodepointSize(0x80) == 1);   // stray continuation -> 1
    CHECK(Utf8CodepointSize(0xFF) == 1);   // malformed -> 1
}

TEST_CASE("Utf8IsContinuationByte")
{
    CHECK(Utf8IsContinuationByte(0x80));
    CHECK(Utf8IsContinuationByte(0xBF));
    CHECK_FALSE(Utf8IsContinuationByte(static_cast<unsigned char>('A')));
    CHECK_FALSE(Utf8IsContinuationByte(0xC3));
}

TEST_CASE("Utf8DecodeAt decodes 1/2/3/4-byte sequences")
{
    size_t next = 0;

    std::string ascii = "A";
    CHECK(Utf8DecodeAt(ascii, 0, next) == U'A');
    CHECK(next == 1);

    std::string two = "\xC3\xA9";          // U+00E9 é
    CHECK(Utf8DecodeAt(two, 0, next) == 0x00E9);
    CHECK(next == 2);

    std::string three = "\xE2\x82\xAC";    // U+20AC €
    CHECK(Utf8DecodeAt(three, 0, next) == 0x20AC);
    CHECK(next == 3);

    std::string four = "\xF0\x9F\x98\x80"; // U+1F600 😀
    CHECK(Utf8DecodeAt(four, 0, next) == 0x1F600);
    CHECK(next == 4);
}

TEST_CASE("Utf8DecodeAt is defensive on malformed/truncated input")
{
    size_t next = 0;

    // Truncated 2-byte sequence (lead byte only) falls back to 1 byte.
    std::string truncated = "\xC3";
    char32_t cp = Utf8DecodeAt(truncated, 0, next);
    CHECK(cp == 0xC3);
    CHECK(next == 1);

    // Lead byte followed by a non-continuation byte.
    std::string bad = "\xC3""Z";
    cp = Utf8DecodeAt(bad, 0, next);
    CHECK(cp == 0xC3);
    CHECK(next == 1);

    // Past end of string.
    std::string s = "x";
    cp = Utf8DecodeAt(s, 5, next);
    CHECK(next == s.size());
}

TEST_CASE("Utf8LeadByteOffset walks back to the lead byte")
{
    std::string line = "a\xC3\xA9" "b";   // 'a', é (bytes 1..2), 'b'
    CHECK(Utf8LeadByteOffset(line, 0) == 0);   // already a lead byte
    CHECK(Utf8LeadByteOffset(line, 1) == 1);   // é lead byte
    CHECK(Utf8LeadByteOffset(line, 2) == 1);   // continuation -> back to 1
    CHECK(Utf8LeadByteOffset(line, 3) == 3);   // 'b'
}
