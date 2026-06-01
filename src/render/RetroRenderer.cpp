#include "RetroRenderer.h"
#include "GlyphCache.h"

RetroRenderer::RetroRenderer(SDL_Renderer* renderer, const FontSettings& settings)
    : m_renderer(renderer)
{
    SetFontSettings(settings);
}

RetroRenderer::~RetroRenderer() = default;

void RetroRenderer::SetFontSettings(const FontSettings& settings)
{
    m_settings = settings;

    // Scale the nominal point size to the screen's effective pixel size,
    // matching what RetroDocWriter's WysiwygRenderer does. SDL_ttf's
    // TTF_OpenFont treats its ptsize argument as a target pixel height
    // assuming 72 DPI, so on a 96-DPI Windows display "16 pt" passed
    // raw is rendered at only ~16 px — about 75 % of the size a user
    // expects. Multiplying by dpi/72 (with +36 = half-72 for round-to-
    // nearest) brings the on-screen glyph height to true point size.
    constexpr int kScreenDpi = 96;   // matches WysiwygRenderer's hardcoded dpi
    const int pointSize = FontSizePoints(settings.size);
    const int pxSize    = std::max(1, (pointSize * kScreenDpi + 36) / 72);
    m_glyphs = std::make_unique<GlyphCache>(m_renderer, settings.face, pxSize);

    // Smaller cache for the status-bar row — ~75% of the chrome size, same
    // face and the same point->pixel conversion as above.
    const int smallPoint = std::max(8, pointSize * 3 / 4);   // 16pt -> 12pt
    const int smallPx    = std::max(1, (smallPoint * kScreenDpi + 36) / 72);
    m_smallGlyphs = std::make_unique<GlyphCache>(m_renderer, settings.face, smallPx);
}

int RetroRenderer::CellWidth() const
{
    return m_glyphs ? m_glyphs->CellWidth() : 0;
}

int RetroRenderer::CellHeight() const
{
    return m_glyphs ? m_glyphs->CellHeight() : 0;
}

void RetroRenderer::Render(const ScreenBuffer& buffer, int smallTextRow)
{
    PaintBuffer(buffer, smallTextRow);
    Present();
}

void RetroRenderer::PaintBuffer(const ScreenBuffer& buffer, int smallTextRow)
{
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    const int cw = CellWidth();
    const int ch = CellHeight();
    if (cw <= 0 || ch <= 0) return;

    // Pass 1: cell backgrounds
    for (int row = 0; row < buffer.Rows(); ++row)
    {
        for (int col = 0; col < buffer.Columns(); ++col)
        {
            const ScreenCell& cell = buffer.At(col, row);
            Color bg = cell.reverseVideo ? cell.foreground : cell.background;

            SDL_FRect rect{
                static_cast<float>(col * cw),
                static_cast<float>(row * ch),
                static_cast<float>(cw),
                static_cast<float>(ch)
            };
            SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(m_renderer, &rect);
        }
    }

    // Pass 2: glyphs (tinted by foreground colour)
    if (m_glyphs && m_glyphs->IsValid())
    {
        const bool smallValid = (m_smallGlyphs && m_smallGlyphs->IsValid());
        const int  scw = smallValid ? m_smallGlyphs->CellWidth()  : 0;
        const int  sch = smallValid ? m_smallGlyphs->CellHeight() : 0;

        for (int row = 0; row < buffer.Rows(); ++row)
        {
            const bool small = smallValid && row == smallTextRow;
            for (int col = 0; col < buffer.Columns(); ++col)
            {
                const ScreenCell& cell = buffer.At(col, row);
                if (cell.character <= U' ') continue;

                Color fg = cell.reverseVideo ? cell.background : cell.foreground;
                if (small)
                    // Smaller glyph, centred both axes inside the full cell so
                    // it stays on the original monospace column.
                    m_smallGlyphs->DrawGlyph(cell.character,
                                             col * cw + (cw - scw) / 2,
                                             row * ch + (ch - sch) / 2, fg);
                else
                    m_glyphs->DrawGlyph(cell.character, col * cw, row * ch, fg);
            }
        }
    }
}

void RetroRenderer::Present()
{
    SDL_RenderPresent(m_renderer);
}
