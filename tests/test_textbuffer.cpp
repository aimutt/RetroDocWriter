#include "doctest/doctest.h"
#include "editor/TextBuffer.h"

#include <string>
#include <vector>

TEST_CASE("TextBuffer basics: SetLines / LineCount / Line / LineLength")
{
    TextBuffer b;
    b.SetLines({ "hello", "world!" });
    CHECK(b.LineCount() == 2);
    CHECK(b.Line(0) == "hello");
    CHECK(b.LineLength(0) == 5);
    CHECK(b.LineLength(1) == 6);
}

TEST_CASE("InsertChar inserts at the column")
{
    TextBuffer b;
    b.SetLines({ "ac" });
    b.InsertChar(1, 0, 'b');
    CHECK(b.Line(0) == "abc");
}

TEST_CASE("Backspace erases previous char and joins lines at col 0")
{
    TextBuffer b;
    b.SetLines({ "abc" });
    b.Backspace(2, 0);              // erase 'b'
    CHECK(b.Line(0) == "ac");

    b.SetLines({ "ab", "cd" });
    b.Backspace(0, 1);             // join into previous line
    CHECK(b.LineCount() == 1);
    CHECK(b.Line(0) == "abcd");
}

TEST_CASE("DeleteForward erases char at col and joins at EOL")
{
    TextBuffer b;
    b.SetLines({ "abc" });
    b.DeleteForward(1, 0);         // erase 'b'
    CHECK(b.Line(0) == "ac");

    b.SetLines({ "ab", "cd" });
    b.DeleteForward(2, 0);         // at EOL -> join next line
    CHECK(b.LineCount() == 1);
    CHECK(b.Line(0) == "abcd");
}

TEST_CASE("InsertNewline splits a line at the column")
{
    TextBuffer b;
    b.SetLines({ "abcd" });
    b.InsertNewline(2, 0);
    CHECK(b.LineCount() == 2);
    CHECK(b.Line(0) == "ab");
    CHECK(b.Line(1) == "cd");
}

TEST_CASE("GetText / DeleteRange / InsertText")
{
    TextBuffer b;
    b.SetLines({ "hello" });
    CHECK(b.GetText(0, 1, 0, 4) == "ell");   // end exclusive

    b.DeleteRange(0, 1, 0, 4);
    CHECK(b.Line(0) == "ho");

    b.SetLines({ "ho" });
    int endRow = -1, endCol = -1;
    b.InsertText(1, 0, "X\nY", endRow, endCol);
    CHECK(b.LineCount() == 2);
    CHECK(b.Line(0) == "hX");
    CHECK(b.Line(1) == "Yo");
    CHECK(endRow == 1);
    CHECK(endCol == 1);
}

TEST_CASE("FindNext: forward, wrap-around, case-insensitive, not found")
{
    TextBuffer b;
    b.SetLines({ "hello world" });
    int r = -1, c = -1;

    CHECK(b.FindNext("world", 0, 0, r, c));
    CHECK(r == 0);
    CHECK(c == 6);

    // From just past the only "hello", search wraps around to find it at 0,0.
    r = c = -1;
    CHECK(b.FindNext("hello", 0, 3, r, c));
    CHECK(r == 0);
    CHECK(c == 0);

    r = c = -1;
    CHECK(b.FindNext("WORLD", 0, 0, r, c, /*caseInsensitive=*/true));
    CHECK(r == 0);
    CHECK(c == 6);

    CHECK_FALSE(b.FindNext("zzz", 0, 0, r, c));
}
