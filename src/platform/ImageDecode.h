#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct SDL_Surface;

// Decodes encoded image bytes (PNG/JPEG/BMP/GIF/… — whatever the vendored
// SDL_image supports) into an SDL_Surface. SDL_image is raster-only, so vector
// formats embedded in RTF \pict (EMF/WMF) are NOT supported and decode to
// nullptr — callers treat that as "image can't be shown" rather than an error.
namespace ImageDecode
{
    // Returns a newly-allocated RGBA32 SDL_Surface, or nullptr on failure /
    // unsupported format. The caller owns the surface (SDL_DestroySurface).
    SDL_Surface* DecodeToSurface(const uint8_t* data, size_t size);
    SDL_Surface* DecodeToSurface(const std::vector<uint8_t>& bytes);
}
