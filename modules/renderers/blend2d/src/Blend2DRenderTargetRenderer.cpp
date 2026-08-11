#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::Blend2D
{
    Blend2DRenderTargetRenderer::Blend2DRenderTargetRenderer(Blend2DRenderer& owner, int width,
                                                              int height, bool preserveContents)
        : owner_(owner), surface_(width, height), preserveContents_(preserveContents)
    {
    }

    Blend2DRenderTargetRenderer::~Blend2DRenderTargetRenderer()
    {
        if (owner_.GetActiveRenderTargetEXT() == this)
            owner_.SetActiveRenderTargetEXT(nullptr);
    }

    void Blend2DRenderTargetRenderer::BindAsRenderTarget()
    {
        owner_.SetActiveRenderTargetEXT(this);
    }

    void Blend2DRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (owner_.GetActiveRenderTargetEXT() == this)
            owner_.SetActiveRenderTargetEXT(nullptr);
    }

    bool Blend2DRenderTargetRenderer::GetData(int level, int x, int y, int w, int h, void* data,
                                              int dataLength) const
    {
        if (level != 0) return false;
        if (w <= 0 || h <= 0) return false;
        // 64-bit product: dataLength is caller-supplied and must not overflow int before the
        // comparison runs; ReadPixelsRgba independently bounds-checks x/y/w/h against the surface.
        if (dataLength < 0 ||
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4 > dataLength)
        {
            return false;
        }
        return surface_.ReadPixelsRgba(x, y, w, h, static_cast<std::uint8_t*>(data));
    }
} // namespace CNA::Internal::Renderers::Blend2D
