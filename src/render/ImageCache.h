#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

// Lazily decodes image bytes into GPU textures, keyed by a stable object id
// (a FloatObject's image id). Textures are created on first request and freed
// in the destructor / Clear / Invalidate. SDL-aware (render layer); mirrors
// GlyphCache's ownership model. A decode failure (e.g. a vector EMF/WMF) is
// cached as a null texture so we don't re-attempt every frame.
class ImageCache
{
public:
    explicit ImageCache(SDL_Renderer* renderer);
    ~ImageCache();

    ImageCache(const ImageCache&)            = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // Returns the cached texture for `id`, decoding `bytes` on first request.
    // Returns nullptr when the bytes can't be decoded. Pixel dimensions are
    // reported via the out params when non-null (0×0 on failure).
    SDL_Texture* GetTexture(uint32_t id, const std::vector<uint8_t>& bytes,
                            int* outW = nullptr, int* outH = nullptr);

    void Invalidate(uint32_t id);
    void Clear();

private:
    struct Entry { SDL_Texture* tex = nullptr; int w = 0; int h = 0; };

    SDL_Renderer*                          m_renderer;
    std::unordered_map<uint32_t, Entry>    m_entries;
};
