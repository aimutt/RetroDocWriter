#include "doctest/doctest.h"
#include "editor/WordWrap.h"

#include <vector>

TEST_CASE("ComputeWrapStarts breaks after the last fitting space")
{
    // "aaaa bbbb cccc": spaces at index 4 and 9; width 10 breaks at 9.
    auto starts = ComputeWrapStarts("aaaa bbbb cccc", 10);
    CHECK((starts == std::vector<int>{ 0, 10 }));
}

TEST_CASE("ComputeWrapStarts hard-cuts when no space fits")
{
    auto starts = ComputeWrapStarts("aaaaaaaaaaaa", 5);   // 12 chars
    CHECK((starts == std::vector<int>{ 0, 5, 10 }));
}

TEST_CASE("ComputeWrapStarts with non-positive width yields a single segment")
{
    auto starts = ComputeWrapStarts("anything", 0);
    CHECK((starts == std::vector<int>{ 0 }));
}

TEST_CASE("CountWrapRows")
{
    CHECK(CountWrapRows("", 10) == 1);
    CHECK(CountWrapRows("short", 10) == 1);
    CHECK(CountWrapRows("aaaaaaaaaaaa", 5) == 3);
}

TEST_CASE("WrapSegmentForColumn picks the largest start <= col")
{
    std::vector<int> starts{ 0, 10 };
    CHECK(WrapSegmentForColumn(starts, 0) == 0);
    CHECK(WrapSegmentForColumn(starts, 9) == 0);
    CHECK(WrapSegmentForColumn(starts, 10) == 1);
    CHECK(WrapSegmentForColumn(starts, 12) == 1);
}
