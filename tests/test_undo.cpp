#include "doctest/doctest.h"
#include "editor/TextBuffer.h"
#include "editor/UndoHistory.h"
#include "editor/FormattedTextBuffer.h"
#include "editor/CharStyle.h"
#include "editor/RichUndoHistory.h"

#include <string>
#include <vector>

TEST_CASE("UndoHistory restores text and cursor; redo works")
{
    TextBuffer buf;
    buf.SetLines({ "hello" });

    UndoHistory h;
    CHECK_FALSE(h.CanUndo());

    h.PushEdit(buf, 0, 5);             // snapshot BEFORE the edit
    buf.SetLines({ "hello world" });   // the edit

    REQUIRE(h.CanUndo());
    UndoState u = h.Undo(buf, 0, 11);
    CHECK((u.lines == std::vector<std::string>{ "hello" }));
    CHECK(u.cursorRow == 0);
    CHECK(u.cursorCol == 5);

    buf.SetLines(u.lines);             // caller applies the restored state
    REQUIRE(h.CanRedo());
    UndoState r = h.Redo(buf, 0, 5);
    CHECK((r.lines == std::vector<std::string>{ "hello world" }));
    CHECK(r.cursorCol == 11);
}

TEST_CASE("A new edit clears the redo stack")
{
    TextBuffer buf;
    buf.SetLines({ "a" });

    UndoHistory h;
    h.PushEdit(buf, 0, 1);
    buf.SetLines({ "ab" });
    (void)h.Undo(buf, 0, 2);
    REQUIRE(h.CanRedo());

    h.PushEdit(buf, 0, 1);             // new edit
    CHECK_FALSE(h.CanRedo());
}

TEST_CASE("RichUndoHistory snapshots formatting and tracks the save point")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "abc" });

    RichUndoHistory h;
    h.MarkSaved();
    CHECK(h.IsAtSavedState());

    h.PushEdit(fb, 0, 3);                                  // before edit
    fb.SetStyleInRange(0, 0, 0, 3, CharStyle::Bold, true); // the edit
    CHECK_FALSE(h.IsAtSavedState());                       // diverged from disk

    RichUndoState u = h.Undo(fb, 0, 3);
    CHECK(h.IsAtSavedState());                             // back at the save point
    // The restored snapshot is the pre-edit (unformatted) state.
    bool anyBold = false;
    for (const auto& row : u.formats)
        for (const auto& f : row)
            if (f.style & CharStyle::Bold) anyBold = true;
    CHECK_FALSE(anyBold);
}

TEST_CASE("RichUndoHistory survives beyond MAX_DEPTH; evicted save point stays dirty")
{
    FormattedTextBuffer fb;
    fb.SetLinesPlain({ "x" });

    RichUndoHistory h;
    h.MarkSaved();

    const int n = RichUndoHistory::MAX_DEPTH + 25;
    for (int i = 0; i < n; ++i)
        h.PushEdit(fb, 0, 1);

    int undone = 0;
    while (h.CanUndo()) { (void)h.Undo(fb, 0, 1); ++undone; }
    CHECK(undone <= RichUndoHistory::MAX_DEPTH);   // stack capped
    // The save point was pushed past the cap and evicted, so we can never
    // return to it.
    CHECK_FALSE(h.IsAtSavedState());
}
