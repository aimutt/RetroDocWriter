#include "doctest/doctest.h"
#include "platform/DirListing.h"
#include "test_util.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    bool HasEntry(const std::vector<DirEntry>& v, const std::string& name)
    {
        return std::any_of(v.begin(), v.end(),
            [&](const DirEntry& e) { return e.name == name; });
    }
    int IndexOf(const std::vector<DirEntry>& v, const std::string& name)
    {
        for (int i = 0; i < static_cast<int>(v.size()); ++i)
            if (v[i].name == name) return i;
        return -1;
    }
}

TEST_CASE("ListDirectory: dirs first, case-insensitive sort, extension filter")
{
    TempDir dir;
    dir.makeSubdir("Beta");
    dir.makeSubdir("alpha");
    WriteTextFile(dir.file("z.rtf"), "x");
    WriteTextFile(dir.file("a.txt"), "x");
    WriteTextFile(dir.file("note.md"), "x");     // filtered out
    WriteTextFile(dir.file("img.png"), "x");     // filtered out

    auto entries = ListDirectory(dir.str(), { ".rtf", ".txt" }, /*dirsOnly=*/false);

    // ".." present (this temp dir is not a filesystem root) and first.
    REQUIRE_FALSE(entries.empty());
    CHECK(entries.front().name == "..");

    // Filtered files only.
    CHECK(HasEntry(entries, "a.txt"));
    CHECK(HasEntry(entries, "z.rtf"));
    CHECK_FALSE(HasEntry(entries, "note.md"));
    CHECK_FALSE(HasEntry(entries, "img.png"));

    // Directories sort before files; case-insensitive alpha within each group.
    CHECK(IndexOf(entries, "alpha") < IndexOf(entries, "Beta"));
    CHECK(IndexOf(entries, "Beta")  < IndexOf(entries, "a.txt"));
    CHECK(IndexOf(entries, "a.txt") < IndexOf(entries, "z.rtf"));

    auto a = IndexOf(entries, "alpha");
    CHECK(entries[a].isDir);
    auto f = IndexOf(entries, "a.txt");
    CHECK_FALSE(entries[f].isDir);
}

TEST_CASE("ListDirectory dirsOnly omits files")
{
    TempDir dir;
    dir.makeSubdir("sub");
    WriteTextFile(dir.file("a.rtf"), "x");

    auto entries = ListDirectory(dir.str(), { ".rtf", ".txt" }, /*dirsOnly=*/true);
    CHECK(HasEntry(entries, "sub"));
    CHECK_FALSE(HasEntry(entries, "a.rtf"));
}

TEST_CASE("ParentDirectory / JoinPath / IsDirectory")
{
    TempDir dir;
    dir.makeSubdir("child");

    CHECK(IsDirectory(dir.str()));
    CHECK(IsDirectory(JoinPath(dir.str(), "child")));
    CHECK_FALSE(IsDirectory(JoinPath(dir.str(), "nope")));

    std::string child = JoinPath(dir.str(), "child");
    CHECK(ParentDirectory(child) == dir.path.string());
}

TEST_CASE("A filesystem root has no parent-link entry and is its own parent")
{
    // Use the temp dir's root (e.g. "C:\\") so the test is environment-agnostic.
    std::string root = TempDir{}.path.root_path().string();
    auto entries = ListDirectory(root, {}, /*dirsOnly=*/true);
    CHECK_FALSE(HasEntry(entries, ".."));
    CHECK(ParentDirectory(root) == root);
}
