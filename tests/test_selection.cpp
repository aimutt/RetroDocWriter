#include "doctest/doctest.h"
#include "editor/Selection.h"

TEST_CASE("Activate / Clear / IsEmpty")
{
    Selection sel;
    CHECK_FALSE(sel.active);
    sel.Activate(1, 2);
    CHECK(sel.active);
    CHECK(sel.IsEmpty(1, 2));        // cursor still at anchor
    CHECK_FALSE(sel.IsEmpty(1, 3));
    sel.Clear();
    CHECK_FALSE(sel.active);
}

TEST_CASE("GetRange normalizes regardless of drag direction")
{
    Selection sel;
    sel.Activate(1, 2);

    int sr, sc, er, ec;
    sel.GetRange(3, 4, sr, sc, er, ec);   // cursor after anchor
    CHECK(sr == 1); CHECK(sc == 2);
    CHECK(er == 3); CHECK(ec == 4);

    sel.Activate(3, 4);
    sel.GetRange(1, 2, sr, sc, er, ec);   // cursor before anchor
    CHECK(sr == 1); CHECK(sc == 2);
    CHECK(er == 3); CHECK(ec == 4);
}

TEST_CASE("ContainsCell respects inclusive start, exclusive end")
{
    Selection sel;
    sel.Activate(0, 1);                   // anchor (0,1), cursor (0,4) => [1,4)

    CHECK(sel.ContainsCell(1, 0, 0, 4));  // start is inclusive
    CHECK(sel.ContainsCell(2, 0, 0, 4));
    CHECK(sel.ContainsCell(3, 0, 0, 4));
    CHECK_FALSE(sel.ContainsCell(4, 0, 0, 4)); // end is exclusive
    CHECK_FALSE(sel.ContainsCell(0, 0, 0, 4)); // before start
}

TEST_CASE("ContainsCell is false for an empty or inactive selection")
{
    Selection sel;
    CHECK_FALSE(sel.ContainsCell(0, 0, 0, 0));   // inactive
    sel.Activate(2, 2);
    CHECK_FALSE(sel.ContainsCell(2, 2, 2, 2));   // empty (cursor == anchor)
}
