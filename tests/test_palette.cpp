#include "doctest/doctest.h"
#include "editor/Palette.h"
#include "render/Color.h"

#include <string>

TEST_CASE("Palette has 16 entries; index 0 is black")
{
    CHECK(Palette::kCount == 16);
    Color black = Palette::ColorAt(0);
    CHECK(black.r == 0);
    CHECK(black.g == 0);
    CHECK(black.b == 0);
}

TEST_CASE("Names are non-empty in range and '?' out of range")
{
    for (int i = 0; i < Palette::kCount; ++i)
    {
        std::string name = Palette::NameAt(static_cast<uint8_t>(i));
        CHECK_FALSE(name.empty());
        CHECK(name != "?");
    }
    CHECK(std::string(Palette::NameAt(99)) == "?");
}

TEST_CASE("NearestIndex maps each palette color back to its own index")
{
    for (int i = 0; i < Palette::kCount; ++i)
        CHECK(Palette::NearestIndex(Palette::ColorAt(static_cast<uint8_t>(i)))
              == static_cast<uint8_t>(i));
}

TEST_CASE("NearestIndex of pure black is index 0")
{
    CHECK(Palette::NearestIndex(Color{ 0, 0, 0, 255 }) == 0);
}
