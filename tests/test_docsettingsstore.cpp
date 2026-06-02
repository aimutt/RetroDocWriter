#include "doctest/doctest.h"
#include "editor/DocumentSettingsStore.h"

#include <cctype>
#include <string>

// Note: ReadFor/WriteFor/CollectGarbage operate on the real
// %APPDATA%\RetroDocWriter\documents\ store (the path is not injectable), so
// to keep the tests hermetic we only exercise the pure CanonicalizePath here.
// The underlying key=value serialization is covered by the FileSettings tests.

TEST_CASE("CanonicalizePath lowercases and is idempotent")
{
    std::string c = DocumentSettingsStore::CanonicalizePath("C:\\Foo\\Bar.RTF");
    CHECK(c == "c:\\foo\\bar.rtf");

    // No uppercase letters survive.
    for (char ch : c)
        CHECK_FALSE(std::isupper(static_cast<unsigned char>(ch)));

    // Applying it again changes nothing.
    CHECK(DocumentSettingsStore::CanonicalizePath(c) == c);
}

TEST_CASE("CanonicalizePath resolves a relative path to absolute")
{
    std::string c = DocumentSettingsStore::CanonicalizePath("file.rtf");
    CHECK_FALSE(c.empty());
    // An absolute Windows path contains a drive separator.
    CHECK(c.find(':') != std::string::npos);
}
