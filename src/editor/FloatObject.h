#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A floating, paragraph-anchored drawing object — the Word-97 \shp shape
// system. Either an embedded raster image (optionally with a caption) or a
// simple filled/bordered box. Positioned independently of the text flow
// (text wraps around it in Phase 3); anchored to a buffer row so it travels
// with edits above it.
//
// All geometry is in twips (1/1440 inch), matching RTF's \shp* control words.
struct FloatObject
{
    enum class Kind : uint8_t { Image = 0, Box = 1 };

    // Position reference frames (\shpbx* / \shpby*).
    enum class HRef : uint8_t { Page = 0, Margin = 1, Column = 2 };
    enum class VRef : uint8_t { Page = 0, Margin = 1, Paragraph = 2 };

    Kind kind      = Kind::Image;
    int  anchorRow = 0;            // paragraph (buffer row) the shape is tied to

    // Bounding rectangle, twips, interpreted against hRef/vRef.
    int  left = 0, top = 0, right = 0, bottom = 0;
    HRef hRef = HRef::Column;
    VRef vRef = VRef::Paragraph;

    // Wrap + stacking (\shpwrN / \shpwrkN / \shpzN / \shpfblwtxtN).
    int  wrapType  = 3;            // 1=square 2=top&bottom 3=none 4=tight 5=through
    int  wrapSide  = 0;            // 0=both 1=left 2=right 3=largest
    int  zOrder    = 0;
    bool belowText = false;        // true => drawn behind the text

    // Image payload (kind == Image): the original encoded file bytes, emitted
    // to RTF as \pngblip/\jpegblip hex. `imageId` keys the texture in
    // ImageCache; `isPng` selects the blip keyword on save.
    std::vector<uint8_t> imageBytes;
    uint32_t             imageId = 0;
    bool                 isPng   = true;

    // Box payload (kind == Box): palette indices (CharFormat-style; 0xFF = none).
    uint8_t fillColor = 0xFF;
    uint8_t lineColor = 0xFF;

    // Optional caption drawn beneath an image.
    std::string caption;

    // Extra distance (twips) text holds off from every edge of the float
    // — the user-visible "padding" knob in the Insert Image dialog. Stored
    // isotropic: the layout pass applies the same value to all four sides
    // when computing the wrap-exclusion rect. RTF round-trips it as the
    // four \dxText* / \dyText* control words (max is taken on read if a
    // foreign file specifies them asymmetrically).
    int textDistanceTwips = 0;

    int  widthTwips()  const { return right - left; }
    int  heightTwips() const { return bottom - top; }
};

// Monotonic, process-global id source for image textures. Stable per created
// object; preserved across undo snapshots (they copy the field), so the
// ImageCache never has to re-decode an unchanged image.
inline uint32_t NextFloatImageId()
{
    static uint32_t s_next = 1;
    return s_next++;
}
