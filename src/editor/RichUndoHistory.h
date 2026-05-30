#pragma once
#include "FormattedTextBuffer.h"
#include <cstdint>
#include <string>
#include <vector>

// Undo/redo for a FormattedTextBuffer. Mirrors core/editor/UndoHistory's
// API and 200-entry cap, but snapshots both text lines AND the parallel
// CharFormat vectors so style/face/size changes are reversible alongside
// content edits.
//
// RetroEdit continues to use the plain-text UndoHistory in core/. This
// class lives in the RetroDocWriter app only.
struct RichUndoState
{
    std::vector<std::string>                  lines;
    std::vector<std::vector<CharFormat>>      formats;
    std::vector<bool>                         pageBreaks;
    std::vector<uint8_t>                      alignment;   // ParagraphAlign per row
    std::vector<FloatObject>                  floats;      // anchored shapes/images
    int cursorRow = 0;
    int cursorCol = 0;
};

class RichUndoHistory
{
public:
    static constexpr int MAX_DEPTH = 200;

    void PushEdit(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    RichUndoState Undo(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);
    RichUndoState Redo(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);

    void ClearAll();

    // Save-point tracking. Application calls MarkSaved() after a successful
    // Open/Save/New to record that the live state matches what's on disk;
    // IsAtSavedState() then reports whether subsequent undo/redo has
    // returned the buffer to that exact timeline position.
    //
    // Each state pushed onto the undo stack carries a monotonically-unique
    // version tag, and Undo/Redo move m_currentVersion in lockstep with
    // the snapshot they restore. PushEdit always advances to a brand-new
    // version (so an edit on an undone branch can't accidentally collide
    // with a pre-existing version). If the saved version is evicted off
    // the bottom of the undo stack past MAX_DEPTH, no future Undo can
    // bring it back — IsAtSavedState() then correctly stays false.
    void MarkSaved() { m_savedVersion = m_currentVersion; }
    bool IsAtSavedState() const { return m_currentVersion == m_savedVersion; }

private:
    static RichUndoState Snapshot(const FormattedTextBuffer& buf, int row, int col);

    std::vector<RichUndoState> m_undoStack;
    std::vector<RichUndoState> m_redoStack;

    // Parallel to m_undoStack / m_redoStack: each entry is the version
    // identifier of the state held in the corresponding snapshot.
    std::vector<uint32_t> m_undoVersionStack;
    std::vector<uint32_t> m_redoVersionStack;
    uint32_t m_versionCounter = 0;     // monotonic, never decremented
    uint32_t m_currentVersion = 0;     // version of the LIVE buffer
    uint32_t m_savedVersion   = 0;     // version that matches on-disk
};
