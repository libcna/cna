#pragma once

#include "CanvasTextureBackend.hpp"

namespace CNA::Internal::Backends::Canvas
{
    /**
     * @brief Off-screen render target backed by the same private-canvas mechanism as
     * CanvasTextureBackend (Design decision 3), plus Bind/UnbindAsRenderTarget() to switch which
     * `CanvasRenderingContext2D` subsequent Clear()/Draw() calls target (`Module['cnaCurrentCtx']`).
     */
    class CanvasRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        CanvasRenderTargetBackend(int w, int h);
        ~CanvasRenderTargetBackend() override;

        [[nodiscard]] int GetWidth() const override { return texture_.GetWidth(); }
        [[nodiscard]] int GetHeight() const override { return texture_.GetHeight(); }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override { texture_.UpdatePixels(rgba, stride); }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        // plan_canvas.md CANVAS-23: no Canvas2D target -- main canvas or any off-screen one --
        // ever has a real depth/stencil buffer, regardless of what DepthFormat was requested at
        // construction (same reasoning/precedent as SDL_RENDERER's Task 708 override).
        [[nodiscard]] bool HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const override { return false; }

        [[nodiscard]] int GetCanvasId() const { return texture_.GetCanvasId(); }

    private:
        CanvasTextureBackend texture_;
    };
}
