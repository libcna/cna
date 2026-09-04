// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "MagnumStateMapping.hpp"

#include <Magnum/GL/CubeMapTexture.h>
#include <Magnum/GL/Framebuffer.h>
#include <Magnum/GL/Renderbuffer.h>
#include <Magnum/GL/Texture.h>

#include <array>
#include <memory>

namespace CNA::Internal::Renderers::Magnum
{
    class MagnumRenderTargetRenderer;
    class MagnumRenderTargetCubeRenderer;

    /**
     * @brief The one record of which render target this renderer currently draws into.
     *
     * Switching destination calls `UnbindAsRenderTarget()` on the outgoing target -- a virtual call
     * that runs its pending MSAA resolve and mipmap regeneration -- so the outgoing target's
     * identity has to be remembered somewhere. It lives in its own allocation, shared between the
     * graphics renderer and every target it created, so that a target destroyed while still bound
     * clears its own slot instead of leaving a dangling pointer for the next switch to dispatch
     * through, and a graphics renderer destroyed first simply takes the record with it.
     */
    struct MagnumBoundTarget
    {
        /** @brief Currently bound single `RenderTarget2D` renderer, or nullptr. */
        IRenderTargetRenderer* renderTarget2D = nullptr;
        /** @brief Currently bound `RenderTargetCube` renderer, or nullptr. */
        IRenderTargetCubeRenderer* renderTargetCube = nullptr;
        /** @brief Currently bound ordered multi-target set; entries past `mrtCount` are unused. */
        std::array<MagnumRenderTargetRenderer*, 4> mrt = {};
        /** @brief Live slots in `mrt`; 0 when no multi-target set is bound. */
        int mrtCount = 0;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int width = 0;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int height = 0;
    };

    /**
     * @brief Whether sampling @p texture needs a vertical coordinate correction.
     *
     * A GL framebuffer's origin is bottom-left, so a render target's colour texture stores the
     * logical image bottom-up: texel row v=0 is the last logical row. Ordinary textures are
     * uploaded top-down and are unaffected, which is why this asks about the resource's nature
     * rather than about the sampler or the currently bound destination.
     *
     * @param texture Sampled source, or nullptr.
     * @return True when @p texture is a render target's colour attachment.
     */
    [[nodiscard]] bool SampledRowOrderIsBottomUp(const ITextureRenderer* texture);

    /** @brief Magnum-backed 2D render target: an off-screen framebuffer with a colour texture. */
    class MagnumRenderTargetRenderer : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates the framebuffer, colour texture and optional depth/multisample storage.
         *
         * @param width            Width in pixels.
         * @param height           Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; `None` allocates no depth storage.
         * @param binding          The creating renderer's shared binding record, held weakly so a
         *                         target outliving its renderer detaches from nothing.
         * @param mipMap           Whether a full mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count, clamped to what the driver supports.
         */
        MagnumRenderTargetRenderer(int width, int height, int depthFormat,
                                  std::weak_ptr<MagnumBoundTarget> binding,
                                  bool mipMap, int multiSampleCount);
        ~MagnumRenderTargetRenderer() override;

        /** @brief Width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Binds this target's resolved colour texture to texture unit 0. */
        void BindGL(int /*unit*/) const override;
        /** @brief Makes this target the destination of subsequent draws. */
        void BindAsRenderTarget() override;
        /** @brief Resolves multisample storage and regenerates mip levels, if any. */
        void UnbindAsRenderTarget() override;
        /**
         * @brief Reads a sub-rectangle of the rendered image back to the CPU.
         *
         * The returned rows are top row first, so the bottom-up storage a rasterized image has in
         * GL memory is normalized here rather than left for the caller to discover.
         *
         * @param level      Mip level to read; only level 0 is rendered into.
         * @param x          Left edge of the requested region, in pixels.
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole region was read; false for an out-of-range level or region.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        /** @brief Applied sample count, or 0 when this target is single-sampled. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Whether a real depth buffer exists on this target.
         *
         * @param depthFormatWasRequested Whether the game asked for one.
         * @return True only when one was requested and really allocated.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthFormat_ != 0;
        }

        /** @brief The resolved colour texture, so a draw path can bind and configure it. */
        [[nodiscard]] Mg::GL::Texture2D& GetColorTexture() const { return *colorTexture_; }
        /** @brief The framebuffer draws are directed into. */
        [[nodiscard]] Mg::GL::Framebuffer& GetFramebuffer() const { return *framebuffer_; }
        /** @brief Mip levels the colour texture allocated. */
        [[nodiscard]] int GetLevelCount() const { return levelCount_; }
        /** @brief Depth renderbuffer, or nullptr when no depth storage was allocated. */
        [[nodiscard]] Mg::GL::Renderbuffer* GetDepthRenderbuffer() const { return depth_.get(); }
        /**
         * @brief Multisample colour storage, or nullptr when this target is single-sampled.
         *
         * Exposed so a multi-target set can attach the same storage this target's own framebuffer
         * uses. Its resolve is unaffected by which framebuffer rendered into it -- the blit reads
         * this renderbuffer either way -- so a set needs no resolve path of its own.
         */
        [[nodiscard]] Mg::GL::Renderbuffer* GetMultisampleColorRenderbuffer() const
        {
            return multisampleColor_.get();
        }
        /** @brief Raw `DepthFormat` ordinal this target was created with. */
        [[nodiscard]] int GetDepthFormat() const { return depthFormat_; }
        /** @brief Resolves multisample colour into the public texture without unbinding. */
        void ResolveColor() const;

    private:
        void DetachFromBinding();

        std::unique_ptr<Mg::GL::Texture2D> colorTexture_;
        std::unique_ptr<Mg::GL::Framebuffer> framebuffer_;
        /// Multisample-only blit destination whose colour attachment is `colorTexture_`.
        std::unique_ptr<Mg::GL::Framebuffer> resolveFramebuffer_;
        std::unique_ptr<Mg::GL::Renderbuffer> multisampleColor_;
        std::unique_ptr<Mg::GL::Renderbuffer> depth_;
        int width_ = 0;
        int height_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int multiSampleCount_ = 0;
        std::weak_ptr<MagnumBoundTarget> binding_;
    };

    /** @brief Magnum-backed cube-map render target: one framebuffer, re-attached per face. */
    class MagnumRenderTargetCubeRenderer : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Creates the shared cube texture and its render framebuffer.
         *
         * @param size             Edge length of each face in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; `None` allocates no depth storage.
         * @param binding          The creating renderer's shared binding record, held weakly.
         * @param mipMap           Whether a full mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count, clamped to what the driver supports.
         */
        MagnumRenderTargetCubeRenderer(int size, int depthFormat,
                                      std::weak_ptr<MagnumBoundTarget> binding,
                                      bool mipMap, int multiSampleCount);
        ~MagnumRenderTargetCubeRenderer() override;

        /** @brief Edge length of each face in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }
        /**
         * @brief Makes one cube face the destination of subsequent draws.
         *
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         */
        void BindAsRenderTargetFace(int face) override;
        /** @brief Resolves the last bound face and regenerates mip levels, if any. */
        void UnbindAsRenderTarget() override;
        /** @brief Applied sample count, or 0 when this target is single-sampled. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Whether a real depth buffer exists on this target.
         *
         * @param depthFormatWasRequested Whether the game asked for one.
         * @return True only when one was requested and really allocated.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthFormat_ != 0;
        }
        /** @brief Binds the shared cube texture to texture unit 0. */
        void BindGL(int /*unit*/) const override;
        /**
         * @brief Uploads CPU pixels into a rendered cube face's mip level.
         *
         * The framebuffer's colour attachment is an ordinary GL cube texture here, so an upload
         * reaches it directly.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was stored; false if nothing was stored.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a rendered cube face's mip level back to the CPU, top row first.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole region was read; false if nothing was read.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief The shared cube texture, so a draw path can bind and configure it. */
        [[nodiscard]] Mg::GL::CubeMapTexture& GetTexture() const { return *texture_; }
        /** @brief Mip levels the cube texture allocated. */
        [[nodiscard]] int GetLevelCount() const { return levelCount_; }
        /** @brief The framebuffer draws are directed into. */
        [[nodiscard]] Mg::GL::Framebuffer& GetFramebuffer() const { return *framebuffer_; }

    private:
        void DetachFromBinding();
        void ResolveFace(int face) const;

        std::unique_ptr<Mg::GL::CubeMapTexture> texture_;
        std::unique_ptr<Mg::GL::Framebuffer> framebuffer_;
        std::unique_ptr<Mg::GL::Framebuffer> resolveFramebuffer_;
        /**
         * One multisample colour renderbuffer per face, not one shared by all six: a renderbuffer
         * is face-agnostic storage, so a single one would hold whichever face was rendered last
         * and a face rebound for a partial update would load that face's samples instead of its
         * own.
         */
        std::array<std::unique_ptr<Mg::GL::Renderbuffer>, 6> multisampleColor_;
        std::unique_ptr<Mg::GL::Renderbuffer> depth_;
        int size_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int multiSampleCount_ = 0;
        int lastFace_ = 0;
        std::weak_ptr<MagnumBoundTarget> binding_;
    };
}
