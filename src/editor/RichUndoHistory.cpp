#include "RichUndoHistory.h"

RichUndoState RichUndoHistory::Snapshot(const FormattedTextBuffer& buf,
                                        int row, int col)
{
    RichUndoState s;
    s.cursorRow = row;
    s.cursorCol = col;
    int n = buf.LineCount();
    s.lines.reserve(static_cast<size_t>(n));
    s.formats.reserve(static_cast<size_t>(n));
    s.pageBreaks.reserve(static_cast<size_t>(n));
    s.alignment.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        s.lines.push_back(buf.Line(i));
        std::vector<CharFormat> row_fmt;
        int len = buf.LineLength(i);
        row_fmt.reserve(static_cast<size_t>(len));
        for (int c = 0; c < len; ++c)
            row_fmt.push_back(buf.FormatAt(i, c));
        s.formats.push_back(std::move(row_fmt));
        s.pageBreaks.push_back(buf.PageBreakBefore(i));
        s.alignment.push_back(static_cast<uint8_t>(buf.Alignment(i)));
    }
    s.floats = buf.Floats();
    return s;
}

void RichUndoHistory::PushEdit(const FormattedTextBuffer& buf,
                               int cursorRow, int cursorCol)
{
    // Snapshot the LIVE pre-edit state, tagged with the version it
    // represents (the version currently held in m_currentVersion).
    m_redoStack.clear();
    m_redoVersionStack.clear();
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    m_undoVersionStack.push_back(m_currentVersion);
    if (static_cast<int>(m_undoStack.size()) > MAX_DEPTH)
    {
        m_undoStack.erase(m_undoStack.begin());
        m_undoVersionStack.erase(m_undoVersionStack.begin());
    }
    // Live state moves to a brand-new version on the timeline.
    m_currentVersion = ++m_versionCounter;
}

RichUndoState RichUndoHistory::Undo(const FormattedTextBuffer& buf,
                                    int cursorRow, int cursorCol)
{
    // Save the live state into the redo stack tagged with its current
    // version, then pop the undo snapshot and adopt ITS version as the
    // new live one.
    m_redoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    m_redoVersionStack.push_back(m_currentVersion);
    RichUndoState s = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_currentVersion = m_undoVersionStack.back();
    m_undoVersionStack.pop_back();
    return s;
}

RichUndoState RichUndoHistory::Redo(const FormattedTextBuffer& buf,
                                    int cursorRow, int cursorCol)
{
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    m_undoVersionStack.push_back(m_currentVersion);
    RichUndoState s = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_currentVersion = m_redoVersionStack.back();
    m_redoVersionStack.pop_back();
    return s;
}

void RichUndoHistory::ClearAll()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoVersionStack.clear();
    m_redoVersionStack.clear();
    m_versionCounter = 0;
    m_currentVersion = 0;
    m_savedVersion   = 0;
}
