#include "doctest/doctest.h"
#include "editor/Dictionary.h"
#include "test_util.h"

#include <string>

TEST_CASE("Built-in word list loaded and common words present")
{
    Dictionary d;
    CHECK(d.BuiltinCount() > 1000);          // the embedded list populated
    CHECK(d.Contains("apple"));
    CHECK(d.Contains("house"));
    CHECK(d.Contains("APPLE"));              // lookups are case-insensitive
    CHECK_FALSE(d.Contains("zzqqxxnotaword"));
}

TEST_CASE("User overlay add/remove behavior")
{
    Dictionary d;

    // Add a word not in the built-in list.
    REQUIRE(d.AddWord("zzqqxxnotaword"));
    CHECK(d.Contains("zzqqxxnotaword"));
    CHECK(d.AddWord("zzqqxxnotaword") == false);   // already present

    // Remove it again.
    REQUIRE(d.RemoveWord("zzqqxxnotaword"));
    CHECK_FALSE(d.Contains("zzqqxxnotaword"));

    // Removing a built-in word overrides it.
    REQUIRE(d.Contains("apple"));
    REQUIRE(d.RemoveWord("apple"));
    CHECK_FALSE(d.Contains("apple"));
    CHECK(d.RemoveWord("apple") == false);         // already removed

    // Added words are stored lowercased (use a non-dictionary gibberish word).
    REQUIRE(d.AddWord("ZzQqVvCaseword"));
    CHECK(d.Contains("zzqqvvcaseword"));
    CHECK(d.Contains("ZZQQVVCASEWORD"));
}

TEST_CASE("User overlay round-trips through a file")
{
    TempDir dir;
    std::string path = dir.file("user_dictionary.txt");

    {
        Dictionary d;
        REQUIRE(d.AddWord("customword1"));
        REQUIRE(d.RemoveWord("apple"));            // override a built-in
        REQUIRE(d.SaveUserOverlay(path));
    }

    Dictionary d2;
    REQUIRE(d2.LoadUserOverlay(path));
    CHECK(d2.Contains("customword1"));
    CHECK_FALSE(d2.Contains("apple"));             // removal persisted
}
