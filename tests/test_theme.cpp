#include "doctest/doctest.h"
#include "render/Theme.h"

#include <string>

TEST_CASE("ThemeCount and preset distinctness")
{
    CHECK(ThemeCount() == 2);
    Theme green = MakeTheme(ThemeName::Green);
    Theme white = MakeTheme(ThemeName::White);
    // The two presets must differ at least in background.
    bool bgDiffers = green.background.r != white.background.r
                  || green.background.g != white.background.g
                  || green.background.b != white.background.b;
    CHECK(bgDiffers);
}

TEST_CASE("ParseThemeName round-trips and falls back to Green")
{
    CHECK(ParseThemeName("green") == ThemeName::Green);
    CHECK(ParseThemeName("white") == ThemeName::White);
    CHECK(ParseThemeName("nonsense") == ThemeName::Green);   // safe fallback

    CHECK(ParseThemeName(ThemeNameKey(ThemeName::Green)) == ThemeName::Green);
    CHECK(ParseThemeName(ThemeNameKey(ThemeName::White)) == ThemeName::White);
}

TEST_CASE("Display names are non-empty")
{
    CHECK_FALSE(std::string(ThemeDisplayName(ThemeName::Green)).empty());
    CHECK_FALSE(std::string(ThemeDisplayName(ThemeName::White)).empty());
}
