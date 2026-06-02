#include "doctest/doctest.h"
#include "render/ScreenBuffer.h"
#include "render/Color.h"

TEST_CASE("Dimensions and default cells")
{
    ScreenBuffer sb(10, 3);
    CHECK(sb.Columns() == 10);
    CHECK(sb.Rows() == 3);
    CHECK(sb.At(0, 0).character == U' ');   // default-constructed cell
}

TEST_CASE("PutChar writes a cell; WriteText writes a run")
{
    ScreenBuffer sb(10, 2);
    Color fg{ 1, 2, 3, 255 }, bg{ 4, 5, 6, 255 };

    sb.PutChar(2, 1, U'X', fg, bg);
    CHECK(sb.At(2, 1).character == U'X');
    CHECK(sb.At(2, 1).foreground.r == 1);
    CHECK(sb.At(2, 1).background.g == 5);

    sb.WriteText(0, 0, "Hi", fg, bg);
    CHECK(sb.At(0, 0).character == U'H');
    CHECK(sb.At(1, 0).character == U'i');
}

TEST_CASE("Clear fills every cell")
{
    ScreenBuffer sb(4, 2);
    ScreenCell fill;
    fill.character = U'#';
    sb.Clear(fill);
    for (int y = 0; y < sb.Rows(); ++y)
        for (int x = 0; x < sb.Columns(); ++x)
            CHECK(sb.At(x, y).character == U'#');
}

TEST_CASE("Out-of-bounds PutChar is ignored; WriteText clips at the right edge")
{
    ScreenBuffer sb(10, 2);
    Color fg{}, bg{};

    sb.PutChar(100, 100, U'Z', fg, bg);   // must be a safe no-op (no crash)
    sb.PutChar(-1, 0, U'Z', fg, bg);

    sb.WriteText(8, 0, "ABCDE", fg, bg);  // only cols 8,9 fit
    CHECK(sb.At(8, 0).character == U'A');
    CHECK(sb.At(9, 0).character == U'B');
    // 'C','D','E' were clipped; col 9 is the last writable cell.
    CHECK(sb.At(9, 1).character == U' ');  // untouched cell stays default
}
