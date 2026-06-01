#pragma once
#include "FontSettings.h"
#include "ScreenBuffer.h"
#include <SDL3/SDL.h>
#include <memory>

class GlyphCache;

class RetroRenderer
{
public:
    RetroRenderer(SDL_Renderer* renderer, const FontSettings& settings);
    ~RetroRenderer();

    // `smallTextRow` (default -1 = none) names a single grid row whose glyphs
    // are rendered from the smaller font cache (used for the status bar) while
    // staying on the same monospace columns. Every other row uses the normal
    // chrome glyph cache.
    //
    // Convenience: PaintBuffer + Present in one call (existing callers).
    void Render(const ScreenBuffer& buffer, int smallTextRow = -1);

    // Split form used by the WYSIWYG renderer, which needs to draw a
    // proportional overlay AFTER the cell-grid passes but BEFORE the buffer
    // is presented to the window.
    void PaintBuffer(const ScreenBuffer& buffer, int smallTextRow = -1);
    void Present();

    void                SetFontSettings(const FontSettings& settings);
    const FontSettings& GetFontSettings() const { return m_settings; }
    int                 CellWidth()      const;
    int                 CellHeight()     const;
    GlyphCache*         GlyphCachePtr()  const { return m_glyphs.get(); }
    SDL_Renderer*       SdlRenderer()    const { return m_renderer; }

private:
    SDL_Renderer*               m_renderer;
    FontSettings                m_settings;
    std::unique_ptr<GlyphCache> m_glyphs;
    // Smaller cache (~75% of the chrome point size, same face) used to render
    // the status-bar row's glyphs on the same monospace columns.
    std::unique_ptr<GlyphCache> m_smallGlyphs;
};
