#include "render/ImageCache.h"
#include "platform/ImageDecode.h"

#include <SDL3/SDL.h>

ImageCache::ImageCache(SDL_Renderer* renderer) : m_renderer(renderer) {}

ImageCache::~ImageCache() { Clear(); }

SDL_Texture* ImageCache::GetTexture(uint32_t id, const std::vector<uint8_t>& bytes,
                                    int* outW, int* outH)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end())
    {
        Entry e;  // null texture / 0×0 by default — also caches decode misses
        if (SDL_Surface* surf = ImageDecode::DecodeToSurface(bytes))
        {
            e.w   = surf->w;
            e.h   = surf->h;
            e.tex = SDL_CreateTextureFromSurface(m_renderer, surf);
            SDL_DestroySurface(surf);
        }
        it = m_entries.emplace(id, e).first;
    }
    if (outW) *outW = it->second.w;
    if (outH) *outH = it->second.h;
    return it->second.tex;
}

void ImageCache::Invalidate(uint32_t id)
{
    auto it = m_entries.find(id);
    if (it != m_entries.end())
    {
        if (it->second.tex) SDL_DestroyTexture(it->second.tex);
        m_entries.erase(it);
    }
}

void ImageCache::Clear()
{
    for (auto& kv : m_entries)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    m_entries.clear();
}
