#include "doctest/doctest.h"
#include "editor/TextBuffer.h"
#include "editor/WordCount.h"

TEST_CASE("CountWords across lines and whitespace runs")
{
    TextBuffer b;

    b.SetLines({ "" });
    CHECK(CountWords(b) == 0);

    b.SetLines({ "   " });
    CHECK(CountWords(b) == 0);

    b.SetLines({ "hello" });
    CHECK(CountWords(b) == 1);

    b.SetLines({ "hello world" });
    CHECK(CountWords(b) == 2);

    b.SetLines({ "  a   b  " });        // leading/trailing/multiple spaces
    CHECK(CountWords(b) == 2);

    b.SetLines({ "one", "two three", "" });
    CHECK(CountWords(b) == 3);
}
