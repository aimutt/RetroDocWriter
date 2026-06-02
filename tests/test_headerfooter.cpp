#include "doctest/doctest.h"
#include "editor/HeaderFooter.h"

#include <cctype>
#include <string>

TEST_CASE("ResolveHeaderFooterSlot: None / CustomText / Filename")
{
    HeaderFooterSlot s;
    CHECK(ResolveHeaderFooterSlot(s, 1, 1, "doc.rtf").empty());   // None

    s.kind = HeaderFooterSlotKind::CustomText;
    s.text = "My Header";
    CHECK(ResolveHeaderFooterSlot(s, 1, 1, "doc.rtf") == "My Header");

    s.kind = HeaderFooterSlotKind::Filename;
    CHECK(ResolveHeaderFooterSlot(s, 1, 1, "doc.rtf") == "doc.rtf");
}

TEST_CASE("ResolveHeaderFooterSlot: all four page-number formats")
{
    HeaderFooterSlot s;
    s.kind = HeaderFooterSlotKind::PageNumber;

    s.fmt = PageNumberFormat::PageNofM;
    CHECK(ResolveHeaderFooterSlot(s, 3, 12, "") == "Page 3 of 12");
    s.fmt = PageNumberFormat::PageN;
    CHECK(ResolveHeaderFooterSlot(s, 3, 12, "") == "Page 3");
    s.fmt = PageNumberFormat::N;
    CHECK(ResolveHeaderFooterSlot(s, 3, 12, "") == "3");
    s.fmt = PageNumberFormat::NofM;
    CHECK(ResolveHeaderFooterSlot(s, 3, 12, "") == "3 of 12");
}

TEST_CASE("ResolveHeaderFooterSlot: date has YYYY-MM-DD shape")
{
    HeaderFooterSlot s;
    s.kind = HeaderFooterSlotKind::Date;
    std::string d = ResolveHeaderFooterSlot(s, 1, 1, "");
    REQUIRE(d.size() == 10);
    CHECK(d[4] == '-');
    CHECK(d[7] == '-');
    for (int i : { 0, 1, 2, 3, 5, 6, 8, 9 })
        CHECK(std::isdigit(static_cast<unsigned char>(d[i])));
}

TEST_CASE("HeaderFooterBand::AnyActive")
{
    HeaderFooterBand band;
    CHECK_FALSE(band.AnyActive());
    band.slots[1].kind = HeaderFooterSlotKind::PageNumber;
    CHECK(band.AnyActive());
}
