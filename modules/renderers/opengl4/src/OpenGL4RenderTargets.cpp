// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0012: OpenGL4's render targets -- RenderTarget2D and
// RenderTargetCube in every colour format EasyGL renders into, with per-target MSAA, resolve and
// mip regeneration on unbind, exact-format upload/readback and the REMED-GFX-168 self-detaching
// binding record -- ported from EasyGL's render-target layer to desktop OpenGL 4.1 core.
//
// The one deliberate difference in mechanics: every operation that binds something for its own
// purposes (creation, readback, the mip regeneration's texture bind) puts the previous READ/DRAW
// framebuffer, renderbuffer, texture-unit and pixel-store state back, where EasyGL leaves the
// default framebuffer or texture unit 0 bound. The bind/unbind entry points themselves keep
// EasyGL's contract: BindAs* binds this target's framebuffer, Unbind* leaves framebuffer 0 bound.

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Resources.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL4
{
    using namespace CNA::Internal::Renderers::OpenGL4::GL4;

    namespace
    {
        [[nodiscard]] GLenum CubeFaceTarget(const int face)
        {
            return static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
        }

        /// Reverses the row order of a tightly packed image in place: a framebuffer stores the
        /// logical image bottom-up, the public transfers are top-row-first.
        void ReverseRows(void* pixels, const std::size_t rowBytes, const int rows)
        {
            auto* bytes = static_cast<std::uint8_t*>(pixels);
            std::vector<std::uint8_t> row(rowBytes);
            for (int topRow = 0; topRow < rows / 2; ++topRow)
            {
                std::uint8_t* top = bytes + static_cast<std::size_t>(topRow) * rowBytes;
                std::uint8_t* bottom = bytes + static_cast<std::size_t>(rows - 1 - topRow) * rowBytes;
                std::copy_n(top, rowBytes, row.data());
                std::copy_n(bottom, rowBytes, top);
                std::copy_n(row.data(), rowBytes, bottom);
            }
        }

        /// Copies top-row-first source rows into bottom-up order for an upload into a rendered
        /// image, so upload, rendering, sampling and readback all agree on one orientation.
        [[nodiscard]] std::vector<std::uint8_t> ToBottomUp(const std::uint8_t* source,
                                                           const std::size_t sourceStride,
                                                           const std::size_t rowBytes,
                                                           const int rows)
        {
            std::vector<std::uint8_t> bottomUp(rowBytes * static_cast<std::size_t>(rows));
            for (int row = 0; row < rows; ++row)
            {
                std::copy_n(source + static_cast<std::size_t>(row) * sourceStride, rowBytes,
                            bottomUp.data() + static_cast<std::size_t>(rows - 1 - row) * rowBytes);
            }
            return bottomUp;
        }

        /**
         * plans/plan_modern.md MOD-119: a driver can accept every individual call and still refuse
         * the assembled framebuffer -- a colour format that is sampleable but not renderable, a
         * sample count the depth attachment cannot match, a size beyond a limit. The format and the
         * GL status make that a one-glance diagnosis instead of a target that renders nowhere.
         */
        [[noreturn]] void ThrowIncompleteFramebuffer(const char* what, const int width,
                                                     const int height, const int surfaceFormat,
                                                     const int depthFormat, const int samples,
                                                     const GLenum status)
        {
            throw std::runtime_error(
                std::string("OpenGL4: ") + what + " " + std::to_string(width) + "x" +
                std::to_string(height) + " (SurfaceFormat ordinal " +
                std::to_string(surfaceFormat) + ", DepthFormat ordinal " +
                std::to_string(depthFormat) + ", samples " + std::to_string(samples) +
                ") is not framebuffer-complete: " + Detail::FramebufferStatusName(status));
        }

        /// Clamps a request to GL_MAX_SAMPLES and then to what the colour format really offers.
        [[nodiscard]] int ApplyRenderTargetSampleCount(const Detail::RenderTargetColorStorage& storage,
                                                       int requested, const bool cubeTarget)
        {
            if (requested <= 0)
                return requested;
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            if (maxSamples > 0 && requested > static_cast<int>(maxSamples))
                requested = static_cast<int>(maxSamples);
            return Detail::ClampRenderTargetSamples(storage, requested, cubeTarget);
        }
    }

    // --- OpenGL4RenderTargetRenderer -------------------------------------------------------------

    OpenGL4RenderTargetRenderer::OpenGL4RenderTargetRenderer(
        const int w, const int h, const int depthFormat, std::weak_ptr<OpenGL4BoundTarget> binding,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
        : width_(w), height_(h), depthFormat_(depthFormat), multiSampleCount_(multiSampleCount),
          surfaceFormat_(surfaceFormat), mipMap_(mipMap), binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? Detail::CalculateMipLevels(w, h, 1) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetRenderer::~OpenGL4RenderTargetRenderer()
    {
        // REMED-GFX-168: detach first, before any GL name is released, so there is no instant at
        // which the binding record names this object while its storage is partly gone.
        DetachFromBinding();
        DestroyResources();
    }

    /**
     * REMED-GFX-168: leaves the shared binding record naming nothing that is about to be freed.
     *
     * Deliberately does NOT run this target's pending finalization (resolve, mip regeneration):
     * both write into storage this destructor destroys, and every route to that content needs the
     * live wrapper. The other slots of a bound MRT set are live targets whose finalization is still
     * observable, so only this target's own slot is cleared.
     */
    void OpenGL4RenderTargetRenderer::DetachFromBinding()
    {
        const auto binding = binding_.lock();
        if (!binding)
            return;   // The renderer went first; there is nothing to detach from.
        if (binding->rt2D == this)
        {
            binding->rt2D = nullptr;
            binding->width = 0;
            binding->height = 0;
        }
        bool anySlotLeft = false;
        for (int i = 0; i < binding->mrtCount; ++i)
        {
            auto& slot = binding->mrt[static_cast<std::size_t>(i)];
            if (slot.rt2D == this)
                slot = {};
            else if (!slot.IsEmpty())
                anySlotLeft = true;
        }
        // Every slot of the set has died: a positive count would keep reporting an extent no live
        // destination has.
        if (binding->mrtCount > 0 && !anySlotLeft)
        {
            binding->mrtCount = 0;
            binding->mrtFramebuffer = 0;
            binding->width = 0;
            binding->height = 0;
        }
    }

    void OpenGL4RenderTargetRenderer::CreateResources()
    {
        // MOD-115: an unmapped ordinal can only arrive by bypassing CreateRenderTarget2DEXT's
        // refusal; fall back to Color rather than leave the storage undefined.
        Detail::RenderTargetColorStorage colorStorage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, colorStorage))
        {
            (void)Detail::MapRenderTargetColorFormat(0, colorStorage);
            surfaceFormat_ = 0;
        }

        Detail::ScopedFramebufferBindings framebuffers;
        Detail::ScopedRenderbufferBinding renderbuffer;
        try
        {
            glGenTextures(1, &colorTexture_);
            {
                Detail::ScopedTextureBinding texture(GL_TEXTURE_2D, colorTexture_);
                // Storage for every mip level, not just level 0: the chain is regenerated from
                // level 0 when the target is unbound, and glGenerateMipmap needs the levels to exist
                // (Task 336).
                {
                    Detail::ScopedUnpackState unpack(1);
                    for (int level = 0; level < levelCount_; ++level)
                    {
                        glTexImage2D(GL_TEXTURE_2D, level,
                                     static_cast<GLint>(colorStorage.internalFormat),
                                     std::max(1, width_ >> level), std::max(1, height_ >> level),
                                     0, colorStorage.pixelFormat, colorStorage.pixelType, nullptr);
                    }
                }
                Detail::ApplyRenderTargetChannelSwizzle(GL_TEXTURE_2D, surfaceFormat_);
                // REMED-GFX-174: clamp GL_TEXTURE_MAX_LEVEL to the real chain. The sampler object a
                // draw binds overrides the texture's own filter, so the level range -- not the
                // MinFilter below -- is what keeps the target complete under every TextureFilter.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }

            multiSampleCount_ = ApplyRenderTargetSampleCount(colorStorage, multiSampleCount_, false);

            gl4_glGenFramebuffers(1, &fbo_);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            if (multiSampleCount_ > 0)
            {
                // Render into a multisample colour renderbuffer; colorTexture_ is only the
                // single-sample resolve destination. MOD-115: the renderbuffer carries the target's
                // own format -- the resolve blit needs compatible formats, and an RGBA8 multisample
                // side would clamp exactly the values a float target exists to keep.
                gl4_glGenRenderbuffers(1, &msaaColorRbo_);
                gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRbo_);
                gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                     colorStorage.internalFormat, width_, height_);
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_RENDERBUFFER, msaaColorRbo_);

                gl4_glGenFramebuffers(1, &resolveFbo_);
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
                gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                           colorTexture_, 0);
                const GLenum resolveStatus = gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (resolveStatus != GL_FRAMEBUFFER_COMPLETE)
                {
                    ThrowIncompleteFramebuffer("render target resolve framebuffer", width_, height_,
                                               surfaceFormat_, depthFormat_, multiSampleCount_,
                                               resolveStatus);
                }
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            }
            else
            {
                gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                           colorTexture_, 0);
            }

            GLenum depthInternalFormat = 0;
            GLenum depthAttachment = 0;
            if (Detail::MapDepthFormat(depthFormat_, depthInternalFormat, depthAttachment))
            {
                gl4_glGenRenderbuffers(1, &depthRbo_);
                gl4_glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
                if (multiSampleCount_ > 0)
                {
                    gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                         depthInternalFormat, width_, height_);
                }
                else
                {
                    gl4_glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, width_, height_);
                }
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER,
                                              depthRbo_);
            }

            const GLenum status = gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                ThrowIncompleteFramebuffer("render target", width_, height_, surfaceFormat_,
                                           depthFormat_, multiSampleCount_, status);
            }
        }
        catch (...)
        {
            DestroyResources();
            throw;
        }
    }

    void OpenGL4RenderTargetRenderer::DestroyResources() noexcept
    {
        if (fbo_ != 0)
            gl4_glDeleteFramebuffers(1, &fbo_);
        if (resolveFbo_ != 0)
            gl4_glDeleteFramebuffers(1, &resolveFbo_);
        if (msaaColorRbo_ != 0)
            gl4_glDeleteRenderbuffers(1, &msaaColorRbo_);
        if (depthRbo_ != 0)
            gl4_glDeleteRenderbuffers(1, &depthRbo_);
        if (colorTexture_ != 0)
            glDeleteTextures(1, &colorTexture_);
        fbo_ = 0;
        resolveFbo_ = 0;
        msaaColorRbo_ = 0;
        depthRbo_ = 0;
        colorTexture_ = 0;
    }

    void OpenGL4RenderTargetRenderer::BindGL(const int unit) const
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
    }

    void OpenGL4RenderTargetRenderer::BindAsRenderTarget()
    {
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    }

    void OpenGL4RenderTargetRenderer::ResolveColor() const
    {
        if (multiSampleCount_ <= 0)
            return;
        const ScopedScissorTestDisabled fullSurface;
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        gl4_glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    void OpenGL4RenderTargetRenderer::UnbindAsRenderTarget()
    {
        // Resolve the multisample colour into colorTexture_ before the mip chain is regenerated
        // from it -- FNA3D's OPENGL_ResolveTarget order: resolve, then mipmap.
        if (multiSampleCount_ > 0)
            ResolveColor();
        // Leave the default framebuffer bound (the unbind contract), and do it before the mip
        // regeneration so level 0 is no longer attached to the draw framebuffer it reads from.
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (levelCount_ > 1)
        {
            Detail::ScopedTextureBinding texture(GL_TEXTURE_2D, colorTexture_);
            gl4_glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    bool OpenGL4RenderTargetRenderer::GetData(const int level, const int x, const int y,
                                              const int w, const int h,
                                              void* data, const int dataLength) const
    {
        // MOD-108: a float target's texels are 2, 4, 8 or 16 bytes wide; reading one back as RGBA8
        // would clamp exactly the above-1.0 values the format was chosen to keep.
        Detail::RenderTargetColorStorage colorStorage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, colorStorage))
            (void)Detail::MapRenderTargetColorFormat(0, colorStorage);

        if (data == nullptr || level < 0 || w <= 0 || h <= 0 ||
            static_cast<std::int64_t>(dataLength) <
                static_cast<std::int64_t>(w) * h * colorStorage.bytesPerPixel)
            throw std::invalid_argument(
                "OpenGL4RenderTargetRenderer::GetData: invalid destination or range.");
        if (level >= levelCount_)
            throw std::out_of_range("OpenGL4RenderTargetRenderer::GetData: mip level out of bounds.");

        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        if (x < 0 || y < 0 || x + w > levelWidth || y + h > levelHeight)
            throw std::out_of_range(
                "OpenGL4RenderTargetRenderer::GetData: rectangle out of bounds.");

        Detail::ScopedFramebufferBindings framebuffers;

        // REMED-GFX-164: GetData is legal while this target is still the active producer. Its
        // public texture is the single-sample resolve destination, so resolve exactly when this
        // multisample attachment is active; the draw binding comes back afterwards so rendering
        // may continue. An idle target was already resolved when it was unbound.
        if (multiSampleCount_ > 0)
        {
            const auto binding = binding_.lock();
            bool active = binding && binding->rt2D == this;
            if (binding)
            {
                for (int i = 0; i < binding->mrtCount; ++i)
                    active = active || binding->mrt[static_cast<std::size_t>(i)].rt2D == this;
            }
            if (active)
                ResolveColor();
        }

        GLuint mipFramebuffer = 0;
        if (level == 0)
        {
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, multiSampleCount_ > 0 ? resolveFbo_ : fbo_);
        }
        else
        {
            gl4_glGenFramebuffers(1, &mipFramebuffer);
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, mipFramebuffer);
            gl4_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       colorTexture_, level);
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        // The framebuffer origin is bottom-left: map the requested rectangle into bottom-up rows.
        const bool read = Detail::ReadRenderTargetPixels(surfaceFormat_, colorStorage, x,
                                                         levelHeight - y - h, w, h, data);
        if (mipFramebuffer != 0)
            gl4_glDeleteFramebuffers(1, &mipFramebuffer);
        if (!read)
            throw std::runtime_error(
                "OpenGL4RenderTargetRenderer::GetData: GL rejected the exact-format readback.");

        ReverseRows(data, static_cast<std::size_t>(w) *
                              static_cast<std::size_t>(colorStorage.bytesPerPixel), h);
        return true;
    }

    void OpenGL4RenderTargetRenderer::AttachColorToMRT(const unsigned int framebuffer,
                                                       const unsigned int attachment) const
    {
        // The caller has @p framebuffer bound; binding it as the draw framebuffer again is a no-op
        // that keeps the attachment on the right object whatever the READ binding is.
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        if (multiSampleCount_ > 0)
        {
            gl4_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
                                          msaaColorRbo_);
        }
        else
        {
            gl4_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D,
                                       colorTexture_, 0);
        }
    }

    void OpenGL4RenderTargetRenderer::AttachDepthToMRT(const unsigned int framebuffer) const
    {
        GLenum ignoredFormat = 0;
        GLenum attachment = 0;
        if (Detail::MapDepthFormat(depthFormat_, ignoredFormat, attachment))
        {
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            gl4_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
                                          depthRbo_);
        }
    }

    void OpenGL4RenderTargetRenderer::UploadPixelsLevel(const int level, const uint8_t* data,
                                                        const int levelW, const int levelH,
                                                        const int stride)
    {
        if (data == nullptr || level < 0 || level >= levelCount_ ||
            levelW != std::max(1, width_ >> level) || levelH != std::max(1, height_ >> level))
            throw std::invalid_argument(
                "OpenGL4RenderTargetRenderer::UpdatePixelsLevel: invalid mip upload.");

        Detail::RenderTargetColorStorage storage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, storage))
            throw std::runtime_error(
                "OpenGL4RenderTargetRenderer::UpdatePixelsLevel: unsupported SurfaceFormat.");
        const int rowBytes = levelW * storage.bytesPerPixel;
        if (stride > 0 && stride < rowBytes)
            throw std::invalid_argument(
                "OpenGL4RenderTargetRenderer::UpdatePixelsLevel: stride is smaller than one row.");
        const int sourceStride = stride > 0 ? stride : rowBytes;

        // A rendered attachment is exposed top-row-first by reversing readback rows and flipping
        // its sampling coordinate. Store a public SetData image in that same bottom-up texel
        // orientation, so upload, rendering, sampling and GetData all agree.
        const std::vector<std::uint8_t> bottomUp =
            ToBottomUp(data, static_cast<std::size_t>(sourceStride),
                       static_cast<std::size_t>(rowBytes), levelH);
        DrainGlErrors();
        bool uploaded = false;
        {
            Detail::ScopedTextureBinding texture(GL_TEXTURE_2D, colorTexture_);
            Detail::ScopedUnpackState unpack(1);
            glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, levelW, levelH, storage.pixelFormat,
                            storage.pixelType, bottomUp.data());
            uploaded = GlOperationSucceeded();
        }
        if (!uploaded)
            throw std::runtime_error(
                "OpenGL4RenderTargetRenderer::UpdatePixelsLevel: GL rejected the upload.");
    }

    void OpenGL4RenderTargetRenderer::UpdatePixels(const uint8_t* data, const int stride)
    {
        UploadPixelsLevel(0, data, width_, height_, stride);
    }

    void OpenGL4RenderTargetRenderer::UpdatePixelsLevel(const int level, const uint8_t* data,
                                                        const int levelW, const int levelH)
    {
        Detail::RenderTargetColorStorage storage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, storage))
            throw std::runtime_error(
                "OpenGL4RenderTargetRenderer::UpdatePixelsLevel: unsupported SurfaceFormat.");
        UploadPixelsLevel(level, data, levelW, levelH, levelW * storage.bytesPerPixel);
    }

    // --- OpenGL4RenderTargetCubeRenderer ---------------------------------------------------------

    OpenGL4RenderTargetCubeRenderer::OpenGL4RenderTargetCubeRenderer(
        const int size, const int depthFormat, std::weak_ptr<OpenGL4BoundTarget> binding,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
        : size_(size), depthFormat_(depthFormat), multiSampleCount_(multiSampleCount),
          surfaceFormat_(surfaceFormat), mipMap_(mipMap), binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? Detail::CalculateMipLevels(size, size, 1) : 1;
        CreateResources();
    }

    OpenGL4RenderTargetCubeRenderer::~OpenGL4RenderTargetCubeRenderer()
    {
        DetachFromBinding();
        DestroyResources();
    }

    /**
     * REMED-GFX-168: the cube counterpart of the 2D detach. A cube is recorded as ONE binding
     * whatever face is active, so detaching the resource detaches every face at once; in an MRT
     * set the same cube may occupy several slots as distinct faces, and all of them are cleared
     * while unrelated live attachments remain available for finalization.
     */
    void OpenGL4RenderTargetCubeRenderer::DetachFromBinding()
    {
        const auto binding = binding_.lock();
        if (!binding)
            return;
        if (binding->cube == this)
        {
            binding->cube = nullptr;
            binding->width = 0;
            binding->height = 0;
        }
        bool anySlotLeft = false;
        for (int i = 0; i < binding->mrtCount; ++i)
        {
            auto& slot = binding->mrt[static_cast<std::size_t>(i)];
            if (slot.cube == this)
                slot = {};
            else if (!slot.IsEmpty())
                anySlotLeft = true;
        }
        if (binding->mrtCount > 0 && !anySlotLeft)
        {
            binding->mrtCount = 0;
            binding->mrtFramebuffer = 0;
            binding->width = 0;
            binding->height = 0;
        }
    }

    void OpenGL4RenderTargetCubeRenderer::CreateResources()
    {
        // MOD-107: the same storage description the 2D targets use, so a float cube face and a
        // float 2D target cannot end up with different GL formats.
        Detail::RenderTargetColorStorage cubeStorage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, cubeStorage))
        {
            (void)Detail::MapRenderTargetColorFormat(0, cubeStorage);
            surfaceFormat_ = 0;
        }

        Detail::ScopedFramebufferBindings framebuffers;
        Detail::ScopedRenderbufferBinding renderbuffer;
        try
        {
            glGenTextures(1, &cubeTexture_);
            {
                Detail::ScopedTextureBinding texture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
                // Storage for all six faces and every level (Task 336, as for the 2D target).
                {
                    Detail::ScopedUnpackState unpack(1);
                    for (int face = 0; face < 6; ++face)
                    {
                        for (int level = 0; level < levelCount_; ++level)
                        {
                            const int levelSize = std::max(1, size_ >> level);
                            glTexImage2D(CubeFaceTarget(face), level,
                                         static_cast<GLint>(cubeStorage.internalFormat),
                                         levelSize, levelSize, 0, cubeStorage.pixelFormat,
                                         cubeStorage.pixelType, nullptr);
                        }
                    }
                }
                Detail::ApplyRenderTargetChannelSwizzle(GL_TEXTURE_CUBE_MAP, surfaceFormat_);
                // REMED-GFX-174: see the 2D target's identical clamp.
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount_ - 1);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }

            multiSampleCount_ = ApplyRenderTargetSampleCount(cubeStorage, multiSampleCount_, true);

            gl4_glGenFramebuffers(1, &fbo_);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
            if (multiSampleCount_ > 0)
            {
                // REMED-GFX-141: one multisample colour renderbuffer PER FACE. A renderbuffer
                // carries no face identity, so a single shared one left a PreserveContents face
                // rebound for a partial update holding whichever face was rendered last.
                for (auto& rbo : msaaColorRbos_)
                {
                    gl4_glGenRenderbuffers(1, &rbo);
                    gl4_glBindRenderbuffer(GL_RENDERBUFFER, rbo);
                    gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                         cubeStorage.internalFormat, size_, size_);
                }
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_RENDERBUFFER, msaaColorRbos_[0]);
                gl4_glGenFramebuffers(1, &resolveFbo_);
            }
            else
            {
                // Face 0 is attached only so this FBO is complete -- and checkable -- the moment it
                // exists; BindAsRenderTargetFace re-attaches the face actually being bound.
                gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CubeFaceTarget(0),
                                           cubeTexture_, 0);
            }

            GLenum depthInternalFormat = 0;
            GLenum depthAttachment = 0;
            if (Detail::MapDepthFormat(depthFormat_, depthInternalFormat, depthAttachment))
            {
                gl4_glGenRenderbuffers(1, &depthRbo_);
                gl4_glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
                if (multiSampleCount_ > 0)
                {
                    gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount_,
                                                         depthInternalFormat, size_, size_);
                }
                else
                {
                    gl4_glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, size_, size_);
                }
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER,
                                              depthRbo_);
            }

            const GLenum status = gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                ThrowIncompleteFramebuffer("cube render target", size_, size_, surfaceFormat_,
                                           depthFormat_, multiSampleCount_, status);
            }
        }
        catch (...)
        {
            DestroyResources();
            throw;
        }
    }

    void OpenGL4RenderTargetCubeRenderer::DestroyResources() noexcept
    {
        if (fbo_ != 0)
            gl4_glDeleteFramebuffers(1, &fbo_);
        if (resolveFbo_ != 0)
            gl4_glDeleteFramebuffers(1, &resolveFbo_);
        for (auto& rbo : msaaColorRbos_)
        {
            if (rbo != 0)
                gl4_glDeleteRenderbuffers(1, &rbo);
            rbo = 0;
        }
        if (depthRbo_ != 0)
            gl4_glDeleteRenderbuffers(1, &depthRbo_);
        if (cubeTexture_ != 0)
            glDeleteTextures(1, &cubeTexture_);
        fbo_ = 0;
        resolveFbo_ = 0;
        depthRbo_ = 0;
        cubeTexture_ = 0;
    }

    void OpenGL4RenderTargetCubeRenderer::BindAsRenderTargetFace(const int face)
    {
        lastFace_ = face;
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (multiSampleCount_ == 0)
        {
            // fbo_'s colour attachment IS cubeTexture_: re-attach the requested face directly.
            gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CubeFaceTarget(face),
                                       cubeTexture_, 0);
        }
        else
        {
            // REMED-GFX-141: this face's OWN multisample renderbuffer. GL has no load action, so
            // a face's samples survive until something draws over them -- all PreserveContents
            // needs; a DiscardContents face is still wiped by the Clear GraphicsDevice issues.
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                          msaaColorRbos_[static_cast<std::size_t>(face)]);
        }
    }

    void OpenGL4RenderTargetCubeRenderer::AttachColorToMRT(const unsigned int framebuffer,
                                                           const unsigned int attachment,
                                                           const int face) const
    {
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        if (multiSampleCount_ > 0)
        {
            gl4_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
                                          msaaColorRbos_[static_cast<std::size_t>(face)]);
        }
        else
        {
            gl4_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, CubeFaceTarget(face),
                                       cubeTexture_, 0);
        }
    }

    void OpenGL4RenderTargetCubeRenderer::AttachDepthToMRT(const unsigned int framebuffer) const
    {
        GLenum ignoredFormat = 0;
        GLenum attachment = 0;
        if (Detail::MapDepthFormat(depthFormat_, ignoredFormat, attachment))
        {
            gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            gl4_glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
                                          depthRbo_);
        }
    }

    void OpenGL4RenderTargetCubeRenderer::ResolveFace(const int face)
    {
        if (multiSampleCount_ <= 0)
            return;
        // While an MRT set is bound its transient FBO owns the live colour attachment: point this
        // cube's private source FBO at the requested face's samples before resolving them.
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                      msaaColorRbos_[static_cast<std::size_t>(face)]);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
        gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CubeFaceTarget(face),
                                   cubeTexture_, 0);
        const ScopedScissorTestDisabled fullSurface;
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        gl4_glBlitFramebuffer(0, 0, size_, size_, 0, 0, size_, size_, GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
    }

    void OpenGL4RenderTargetCubeRenderer::UnbindMRTFace(const int face)
    {
        if (multiSampleCount_ > 0)
            ResolveFace(face);
        // Detach the face from the draw framebuffer before generating its mip chain: keeping a
        // texture attached while it is glGenerateMipmap's source is a feedback hazard (Mesa GLES
        // left RGBA16 cube levels unchanged), and the unbind contract leaves framebuffer 0 bound.
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // Regenerate the chain for all six faces from their just-rendered (and possibly
        // just-resolved) level 0.
        if (levelCount_ > 1)
        {
            Detail::ScopedTextureBinding texture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
            gl4_glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }
    }

    void OpenGL4RenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        UnbindMRTFace(lastFace_);
    }

    void OpenGL4RenderTargetCubeRenderer::BindGL(const int unit) const
    {
        gl4_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
    }

    bool OpenGL4RenderTargetCubeRenderer::SetData(const int face, const int level, const int x,
                                                  const int y, const int w, const int h,
                                                  const void* data, const int dataLength)
    {
        return SetDataBytesEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool OpenGL4RenderTargetCubeRenderer::SetDataBytesEXT(const int face, const int level,
                                                          const int x, const int y,
                                                          const int w, const int h,
                                                          const void* data, const int dataLength)
    {
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;
        Detail::RenderTargetColorStorage storage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, storage))
            return false;
        const int rowBytes = w * storage.bytesPerPixel;
        if (dataLength < rowBytes * h)
            return false;

        // A rendered face uses the same bottom-up storage convention as a 2D target: map both the
        // row order and a partial rectangle's Y before the upload, so SetData/GetData stay exact
        // even after the same face has also been rasterized.
        const std::vector<std::uint8_t> bottomUp =
            ToBottomUp(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(rowBytes),
                       static_cast<std::size_t>(rowBytes), h);
        DrainGlErrors();
        Detail::ScopedTextureBinding texture(GL_TEXTURE_CUBE_MAP, cubeTexture_);
        Detail::ScopedUnpackState unpack(1);
        glTexSubImage2D(CubeFaceTarget(face), level, x, levelSize - y - h, w, h,
                        storage.pixelFormat, storage.pixelType, bottomUp.data());
        return GlOperationSucceeded();
    }

    bool OpenGL4RenderTargetCubeRenderer::GetData(const int face, const int level, const int x,
                                                  const int y, const int w, const int h,
                                                  void* data, const int dataLength) const
    {
        return GetDataBytesEXT(face, level, x, y, w, h, data, dataLength);
    }

    bool OpenGL4RenderTargetCubeRenderer::GetDataBytesEXT(const int face, const int level,
                                                          const int x, const int y,
                                                          const int w, const int h,
                                                          void* data, const int dataLength) const
    {
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0)
            return false;
        if (level < 0 || level >= levelCount_)
            return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;
        Detail::RenderTargetColorStorage storage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat_, storage))
            return false;
        const int rowBytes = w * storage.bytesPerPixel;
        if (dataLength < rowBytes * h)
            return false;

        // REMED-GFX-134: attach the requested face/level to a temporary framebuffer and read it.
        // Rendered content is stored bottom-up, so the rectangle is mapped into bottom-up
        // coordinates and the rows are flipped back: the public result is top-row-first. The most
        // recently bound face of a multisample cube was already resolved when it was unbound.
        Detail::ScopedFramebufferBindings framebuffers;
        GLuint readFramebuffer = 0;
        gl4_glGenFramebuffers(1, &readFramebuffer);
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        gl4_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, CubeFaceTarget(face),
                                   cubeTexture_, level);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        const bool complete =
            gl4_glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        const bool read = complete && Detail::ReadRenderTargetPixels(
                                          surfaceFormat_, storage, x, levelSize - y - h, w, h, data);
        gl4_glDeleteFramebuffers(1, &readFramebuffer);
        if (read)
            ReverseRows(data, static_cast<std::size_t>(rowBytes), h);
        return read;
    }
}
