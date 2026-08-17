#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::PixiJs
{
    /**
     * @brief Off-screen render target backed by a real `PIXI.RenderTexture` (Design decision 8),
     * plus Bind/UnbindAsRenderTarget() to switch which container/RenderTexture pair subsequent
     * Clear()/Draw() calls target (`Module['cnaPixiActiveContainer']`/
     * `Module['cnaPixiActiveRenderTexture']`).
     *
     * Deliberately does NOT reuse PixiJsTextureRenderer's buffer-backed upload path: a
     * `PIXI.RenderTexture` is a GPU-framebuffer-backed resource, not a `PIXI.BufferResource` one --
     * they are registered under the same `Module['cnaPixiTextures']` id space (so
     * PixiJsSpriteBatchRenderer can sample either uniformly by id) but created differently.
     */
    class PixiJsRenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        /** @brief Creates a new @p w x @p h render target backed by its own PIXI.RenderTexture. */
        PixiJsRenderTargetRenderer(int w, int h);
        /** @brief Destroys the underlying PIXI.RenderTexture. */
        ~PixiJsRenderTargetRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        /**
         * @brief Direct CPU pixel upload into a bound PixiJS RenderTexture.
         *
         * Not yet implemented: a `PIXI.RenderTexture` has no simple synchronous CPU-buffer upload
         * path the way a `PIXI.BufferResource`-backed plain texture does (Design decision 8) --
         * doing this correctly needs a re-upload-via-sprite-draw trick that has not been designed
         * yet. Throws rather than silently no-opping.
         *
         * @throws std::runtime_error always, until this is implemented.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads this target's rendered pixels back as tightly packed RGBA8 rows.
         *
         * @param level      Mip level; PixiJS render textures have no mip chain in this v1 scope.
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false when this target's texture
         *         is unavailable, which is always the case outside an Emscripten build.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the target, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /// plan_pixijs.md PIXIJS-34: no PIXI.RenderTexture in this renderer's v1 scope carries a
        /// real depth/stencil attachment, regardless of what DepthFormat was requested at
        /// construction (same reasoning/precedent as CANVAS-23).
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }

        /** @brief Id into `Module['cnaPixiTextures']` for this target's backing PIXI.RenderTexture. */
        [[nodiscard]] int GetPixiTextureId() const { return id_; }

    private:
        int id_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
