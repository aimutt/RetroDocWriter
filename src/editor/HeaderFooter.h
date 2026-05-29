#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

// Per-document header/footer slot model.
//
// The page header (top margin) and page footer (bottom margin) each have
// three independent sub-slots laid out left / center / right. Each sub-slot
// emits one of a small set of resolved strings — empty, the user's literal
// text, the document basename, the current page number in one of four
// formats, or today's date — at render and print time.
//
// The renderer (WysiwygRenderer + Print.cpp) consumes a HeaderFooterBand
// pair via DrawContext / PrintRequest; the dialog UI (RetroUi) edits a
// shared snapshot through EditorUiState.

enum class HeaderFooterSlotKind : uint8_t
{
    None       = 0,   // slot stays blank
    CustomText = 1,   // emit `text` verbatim
    Filename   = 2,   // emit the document's basename
    PageNumber = 3,   // emit a format from PageNumberFormat
    Date       = 4,   // emit today's date as YYYY-MM-DD
};

enum class PageNumberFormat : uint8_t
{
    PageNofM = 0,     // "Page 3 of 12"
    PageN    = 1,     // "Page 3"
    N        = 2,     // "3"
    NofM     = 3,     // "3 of 12"
};

struct HeaderFooterSlot
{
    HeaderFooterSlotKind kind = HeaderFooterSlotKind::None;
    PageNumberFormat     fmt  = PageNumberFormat::PageNofM;
    std::string          text;
};

struct HeaderFooterBand
{
    // Indexed 0=Left, 1=Center, 2=Right.
    std::array<HeaderFooterSlot, 3> slots;

    bool AnyActive() const
    {
        for (const auto& s : slots)
            if (s.kind != HeaderFooterSlotKind::None) return true;
        return false;
    }
};

// Resolve one sub-slot to its display string at draw/print time. `pageNum`
// is 1-based; `totalPages` is the document's total page count from the
// layout pass. `documentName` is the basename used for Filename slots.
// Returns empty for None or when its inputs would render blank.
inline std::string ResolveHeaderFooterSlot(const HeaderFooterSlot& slot,
                                           int pageNum, int totalPages,
                                           const std::string& documentName)
{
    switch (slot.kind)
    {
        case HeaderFooterSlotKind::None:
            return {};
        case HeaderFooterSlotKind::CustomText:
            return slot.text;
        case HeaderFooterSlotKind::Filename:
            return documentName;
        case HeaderFooterSlotKind::PageNumber:
            switch (slot.fmt)
            {
                case PageNumberFormat::PageNofM:
                    return "Page " + std::to_string(pageNum)
                         + " of "  + std::to_string(totalPages);
                case PageNumberFormat::PageN:
                    return "Page " + std::to_string(pageNum);
                case PageNumberFormat::N:
                    return std::to_string(pageNum);
                case PageNumberFormat::NofM:
                    return std::to_string(pageNum)
                         + " of " + std::to_string(totalPages);
            }
            return {};
        case HeaderFooterSlotKind::Date:
        {
            auto now = std::chrono::system_clock::to_time_t(
                          std::chrono::system_clock::now());
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            return buf;
        }
    }
    return {};
}
