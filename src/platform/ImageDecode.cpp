#include "platform/ImageDecode.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace ImageDecode
{

SDL_Surface* DecodeToSurface(const uint8_t* data, size_t size)
{
    if (!data || size == 0) return nullptr;

    // SDL_IOFromConstMem keeps a read-only view over the caller's bytes; the
    // stream is closed by IMG_Load_IO (closeio = true) once decoded.
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    if (!io) return nullptr;

    SDL_Surface* surf = IMG_Load_IO(io, true);
    if (!surf) return nullptr;  // unsupported/corrupt (incl. EMF/WMF vector)

    // Normalize to RGBA32 so the texture and (print-path) DIB conversions can
    // assume a single, known pixel layout.
    if (surf->format != SDL_PIXELFORMAT_RGBA32)
    {
        SDL_Surface* conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        return conv;  // may be nullptr if conversion failed
    }
    return surf;
}

SDL_Surface* DecodeToSurface(const std::vector<uint8_t>& bytes)
{
    return DecodeToSurface(bytes.data(), bytes.size());
}

} // namespace ImageDecode
