#include "doctest/doctest.h"
#include "editor/FileSettings.h"
#include "test_util.h"

#include <string>

TEST_CASE("Typed set/get round-trip through a file")
{
    TempDir dir;
    std::string path = dir.file("settings.ini");

    {
        FileSettings s;
        s.SetString("name", "RetroDoc");
        s.SetInt("count", 42);
        s.SetBool("flag", true);
        REQUIRE(s.Save(path));
    }

    FileSettings s;
    REQUIRE(s.Load(path));
    CHECK(s.Has("name"));
    CHECK(s.GetString("name") == "RetroDoc");
    CHECK(s.GetInt("count") == 42);
    CHECK(s.GetBool("flag") == true);

    CHECK_FALSE(s.Has("missing"));
    CHECK(s.GetString("missing", "default") == "default");
    CHECK(s.GetInt("missing", -1) == -1);
    CHECK(s.GetBool("missing", true) == true);
}

TEST_CASE("Unknown keys survive a load/save round-trip")
{
    TempDir dir;
    std::string path = dir.file("legacy.ini");
    WriteTextFile(path, "# a comment\nfuture_key=keepme\nknown=1\n");

    FileSettings s;
    REQUIRE(s.Load(path));
    CHECK(s.GetString("future_key") == "keepme");
    CHECK(s.GetInt("known") == 1);

    std::string path2 = dir.file("legacy2.ini");
    REQUIRE(s.Save(path2));

    FileSettings reloaded;
    REQUIRE(reloaded.Load(path2));
    CHECK(reloaded.GetString("future_key") == "keepme");
}

TEST_CASE("SidecarPath")
{
    CHECK(FileSettings::SidecarPath("doc.rtf") == "doc.rtf.retroedit");
    CHECK(FileSettings::SidecarPath("").empty());
}
