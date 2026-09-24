// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_opengl4_modern_graphics.md GL4-0012: OpenGL4's texture and render-target resources.
//
// The public members the renderer core (OpenGL4Renderer.cpp, OpenGL4StockDraw.cpp,
// OpenGL4SpriteBatch.cpp) relies on keep the signatures of the GL4-0012 contract. Everything else
// here -- the format tables, the scoped GL-state guards and each class's private state -- belongs
// to the implementation files OpenGL4Textures.cpp, OpenGL4RenderTargets.cpp and
// OpenGL4Formats.cpp, which port EasyGL's texture, cube, volume, render-target and surface-format
// layer to desktop OpenGL 4.1 core. Desktop only: no ES/WebGL branch and no context-loss recovery.

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL4
{
    /**
     * @brief Per-renderer cache of the runtime format probes (owned by the renderer as
     *        `formatSupport_`).
     *
     * Whether a colour format is renderable is a property of the live context, not of the API
     * version, so each answer is probed once -- by attaching a 1x1 image of the real format to a
     * framebuffer and asking GL whether it is complete -- and remembered, because
     * `ClassifyRenderTargetFormatEXT` is a query a caller may make every frame.
     */
    struct OpenGL4FormatSupport
    {
        /** @brief Whether an RGBA16F colour attachment is framebuffer-complete; empty until probed. */
        std::optional<bool> halfFloatRenderable;
        /** @brief Whether an RGBA32F colour attachment is framebuffer-complete; empty until probed. */
        std::optional<bool> fullFloatRenderable;
        /**
         * @brief Renderability of the normalized non-Color targets; empty until probed.
         *
         * Index 0 is Rgba1010102 (RGB10_A2), 1 is Rg32 (RG16), 2 is Rgba64 (RGBA16).
         */
        std::array<std::optional<bool>, 3> normalizedRenderable{};
    };

    /**
     * @brief Describes the surface formats this context stores and renders to, for the renderer's
     *        startup capability log.
     *
     * Runs (and caches) the same render-target probes `ClassifyRenderTargetFormatEXT` uses, so it
     * needs the renderer's context current.
     *
     * @param support The renderer's probe cache.
     * @return Text of the form "texture SurfaceFormat: Color + ...; render-target SurfaceFormat: ...".
     */
    [[nodiscard]] std::string DescribeOpenGL4SurfaceFormatSupport(OpenGL4FormatSupport& support);

    /**
     * @brief Format tables and GL-state helpers shared by the OpenGL4 resource implementations.
     *
     * Internal to the renderer family: nothing outside modules/renderers/opengl4 includes these.
     */
    namespace Detail
    {
        /**
         * @brief Restores the active texture unit's binding of one target when it leaves scope.
         *
         * Every upload, readback and allocation binds the resource on whatever unit is currently
         * active and puts that unit's previous binding back, so no texture binding leaks into a
         * later draw.
         */
        class ScopedTextureBinding
        {
        public:
            /**
             * @brief Remembers the active unit's current binding of @p target, then binds @p texture.
             *
             * @param target GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP or GL_TEXTURE_3D.
             * @param texture Texture name to bind.
             */
            ScopedTextureBinding(GLenum target, GLuint texture)
                : target_(target)
            {
                const GLenum query = target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP
                                   : target == GL_TEXTURE_3D       ? GL_TEXTURE_BINDING_3D
                                                                   : GL_TEXTURE_BINDING_2D;
                glGetIntegerv(query, &previous_);
                glBindTexture(target, texture);
            }

            /** @brief Rebinds the texture that was bound before construction. */
            ~ScopedTextureBinding() { glBindTexture(target_, static_cast<GLuint>(previous_)); }

            ScopedTextureBinding(const ScopedTextureBinding&) = delete;
            ScopedTextureBinding& operator=(const ScopedTextureBinding&) = delete;

        private:
            GLenum target_;
            GLint previous_ = 0;
        };

        /**
         * @brief Sets up an unpack (CPU-to-GL) transfer and restores the defaults afterwards.
         *
         * Sets GL_UNPACK_ALIGNMENT, and unbinds a GL_PIXEL_UNPACK_BUFFER for the duration so a
         * client pointer (or a null allocation) is never reinterpreted as a buffer offset.
         */
        class ScopedUnpackState
        {
        public:
            /**
             * @brief Applies @p alignment and unbinds any pixel-unpack buffer.
             *
             * @param alignment GL_UNPACK_ALIGNMENT for the transfer (1, 2, 4 or 8).
             */
            explicit ScopedUnpackState(GLint alignment)
            {
                glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousBuffer_);
                if (previousBuffer_ != 0)
                    GL4::gl4_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            }

            /** @brief Restores GL_UNPACK_ALIGNMENT to 4 and rebinds the previous unpack buffer. */
            ~ScopedUnpackState()
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                if (previousBuffer_ != 0)
                    GL4::gl4_glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(previousBuffer_));
            }

            ScopedUnpackState(const ScopedUnpackState&) = delete;
            ScopedUnpackState& operator=(const ScopedUnpackState&) = delete;

        private:
            GLint previousBuffer_ = 0;
        };

        /** @brief The pack (GL-to-CPU) counterpart of @ref ScopedUnpackState. */
        class ScopedPackState
        {
        public:
            /**
             * @brief Applies @p alignment and unbinds any pixel-pack buffer.
             *
             * @param alignment GL_PACK_ALIGNMENT for the transfer (1, 2, 4 or 8).
             */
            explicit ScopedPackState(GLint alignment)
            {
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousBuffer_);
                if (previousBuffer_ != 0)
                    GL4::gl4_glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            }

            /** @brief Restores GL_PACK_ALIGNMENT to 4 and rebinds the previous pack buffer. */
            ~ScopedPackState()
            {
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                if (previousBuffer_ != 0)
                    GL4::gl4_glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousBuffer_));
            }

            ScopedPackState(const ScopedPackState&) = delete;
            ScopedPackState& operator=(const ScopedPackState&) = delete;

        private:
            GLint previousBuffer_ = 0;
        };

        /** @brief Restores the READ and DRAW framebuffer bindings when it leaves scope. */
        class ScopedFramebufferBindings
        {
        public:
            /** @brief Remembers the current READ and DRAW framebuffer bindings. */
            ScopedFramebufferBindings()
            {
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_);
            }

            /** @brief Rebinds the remembered READ and DRAW framebuffers. */
            ~ScopedFramebufferBindings()
            {
                GL4::gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_));
                GL4::gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_));
            }

            ScopedFramebufferBindings(const ScopedFramebufferBindings&) = delete;
            ScopedFramebufferBindings& operator=(const ScopedFramebufferBindings&) = delete;

        private:
            GLint read_ = 0;
            GLint draw_ = 0;
        };

        /** @brief Restores the GL_RENDERBUFFER binding when it leaves scope. */
        class ScopedRenderbufferBinding
        {
        public:
            /** @brief Remembers the current renderbuffer binding. */
            ScopedRenderbufferBinding() { glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_); }

            /** @brief Rebinds the remembered renderbuffer. */
            ~ScopedRenderbufferBinding()
            {
                GL4::gl4_glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_));
            }

            ScopedRenderbufferBinding(const ScopedRenderbufferBinding&) = delete;
            ScopedRenderbufferBinding& operator=(const ScopedRenderbufferBinding&) = delete;

        private:
            GLint previous_ = 0;
        };

        /**
         * @brief GL storage and transfer layout of one uncompressed plain-texture SurfaceFormat.
         *
         * The transfer layout is what GL is handed and hands back; for the formats GL stores with
         * more channels than XNA (Alpha8, Single, Vector2, HalfSingle, HalfVector2, Rg32,
         * NormalizedByte2) it is the expanded one, see @ref ExpandTexelsForTransfer.
         */
        struct TextureTransferFormat
        {
            /** @brief Sized internal format of the GL storage. */
            GLenum internalFormat = GL_RGBA8;
            /** @brief Pixel format of the transfer. */
            GLenum pixelFormat = GL_RGBA;
            /** @brief Pixel type of the transfer. */
            GLenum pixelType = GL_UNSIGNED_BYTE;
            /** @brief Bytes per texel of the transfer layout. */
            int transferBytesPerTexel = 4;
        };

        /**
         * @brief Maps an uncompressed SurfaceFormat to the storage a plain texture uses.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @param texture2D True for Texture2D, whose set also holds NormalizedByte2/4 and stores any
         *        other unlisted ordinal as Color; false for TextureCube/Texture3D, whose narrower
         *        set refuses everything unlisted.
         * @param out Receives the mapping.
         * @return False when the format has no uncompressed mapping for that texture kind.
         */
        [[nodiscard]] bool MapTextureTransferFormat(int surfaceFormat, bool texture2D,
                                                    TextureTransferFormat& out);

        /**
         * @brief Converts tightly packed declared-format texels into the GL transfer layout.
         *
         * Bgra5551/Bgra4444 are rotated one channel width left (XNA keeps alpha in the high bits,
         * GL in the low ones). The one- and two-channel formats are widened to four channels with
         * Direct3D 9's expansion of the missing ones (1.0, or 127 for signed bytes); Alpha8 becomes
         * (0, 0, 0, A).
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @param source Declared-format texels.
         * @param texelCount Number of texels in @p source.
         * @param scratch Storage for the converted texels when a conversion is needed.
         * @return @p source when the layouts coincide, otherwise `scratch.data()`.
         */
        [[nodiscard]] const void* ExpandTexelsForTransfer(int surfaceFormat, const void* source,
                                                          std::size_t texelCount,
                                                          std::vector<std::uint8_t>& scratch);

        /**
         * @brief Inverse of @ref ExpandTexelsForTransfer: keeps the channels XNA stores.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @param transfer Texels in the GL transfer layout.
         * @param texelCount Number of texels to convert.
         * @param destination Receives tightly packed declared-format texels.
         */
        void CollapseTransferTexels(int surfaceFormat, const std::uint8_t* transfer,
                                    std::size_t texelCount, std::uint8_t* destination);

        /**
         * @brief Whether @p surfaceFormat is one of the three classic S3TC formats.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return True for Dxt1, Dxt3 and Dxt5.
         */
        [[nodiscard]] bool IsDxtFormat(int surfaceFormat);

        /**
         * @brief Bytes of one 4x4 block: Dxt1 carries colour only, Dxt3/Dxt5 add an alpha block.
         *
         * @param surfaceFormat Dxt1, Dxt3 or Dxt5 ordinal.
         * @return 8 or 16.
         */
        [[nodiscard]] std::size_t DxtBlockBytes(int surfaceFormat);

        /**
         * @brief Bytes of the complete block stream of a @p width x @p height image.
         *
         * @param surfaceFormat Dxt1, Dxt3 or Dxt5 ordinal.
         * @param width Image width in texels.
         * @param height Image height in texels.
         * @return Padded block-row bytes times block rows.
         */
        [[nodiscard]] std::size_t DxtImageBytes(int surfaceFormat, int width, int height);

        /**
         * @brief GL compressed internal format of a DXT SurfaceFormat.
         *
         * @param surfaceFormat Dxt1, Dxt3 or Dxt5 ordinal.
         * @return The matching GL_COMPRESSED_RGBA_S3TC_DXTn_EXT token.
         */
        [[nodiscard]] GLenum DxtInternalFormat(int surfaceFormat);

        /**
         * @brief Decodes a DXT block stream to tightly packed RGBA8, as the GPU would sample it.
         *
         * @param surfaceFormat Dxt1, Dxt3 or Dxt5 ordinal.
         * @param blocks Block stream.
         * @param byteCount Bytes available in @p blocks.
         * @param width Image width in texels.
         * @param height Image height in texels.
         * @return `width * height * 4` bytes.
         */
        [[nodiscard]] std::vector<std::uint8_t> DecodeDxtBlocks(int surfaceFormat,
                                                                const std::uint8_t* blocks,
                                                                std::size_t byteCount,
                                                                int width, int height);

        /**
         * @brief Whether the context stores S3TC blocks natively.
         *
         * A missing extension selects the renderer-local RGBA decode path; it never changes the
         * logical XNA SurfaceFormat. Answered once per process, as EasyGL does, the first time it
         * is asked with a context current.
         *
         * @return True when GL_EXT_texture_compression_s3tc (or ANGLE's DXT5 variant) is advertised.
         */
        [[nodiscard]] bool ContextHasS3tc();

        /**
         * @brief Colour storage of one render-target SurfaceFormat.
         *
         * The identical triple is needed in three places -- the texture's per-level storage, the
         * multisample colour renderbuffer and the renderability probe -- so it is data.
         */
        struct RenderTargetColorStorage
        {
            /** @brief Sized internal format. */
            GLenum internalFormat = GL_RGBA8;
            /** @brief Transfer pixel format. */
            GLenum pixelFormat = GL_RGBA;
            /** @brief Transfer pixel type. */
            GLenum pixelType = GL_UNSIGNED_BYTE;
            /** @brief Needs a float-renderable colour buffer. */
            bool isFloat = false;
            /** @brief 32 bits per channel (as opposed to 16-bit half float). */
            bool isFullFloat = false;
            /** @brief Bytes per texel of the public (declared-format) transfer. */
            int bytesPerPixel = 4;
        };

        /**
         * @brief Maps a SurfaceFormat to render-target colour storage.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @param out Receives the storage.
         * @return False for a format CNA does not allocate as a render target.
         */
        [[nodiscard]] bool MapRenderTargetColorFormat(int surfaceFormat,
                                                      RenderTargetColorStorage& out);

        /**
         * @brief Maps a DepthFormat ordinal to renderbuffer storage and attachment point.
         *
         * @param depthFormat Raw DepthFormat ordinal.
         * @param internalFormat Receives GL_DEPTH_COMPONENT16/24 or GL_DEPTH24_STENCIL8.
         * @param attachment Receives GL_DEPTH_ATTACHMENT or GL_DEPTH_STENCIL_ATTACHMENT.
         * @return False for DepthFormat.None: no depth/stencil storage at all.
         */
        [[nodiscard]] bool MapDepthFormat(int depthFormat, GLenum& internalFormat,
                                          GLenum& attachment);

        /**
         * @brief Gives a one- or two-channel render target Direct3D 9's sampled expansion.
         *
         * Single/HalfSingle sample as (R, 1, 1, 1) and Vector2/HalfVector2/Rg32 as (R, G, 1, 1),
         * where GL would answer 0 for the missing colour channels. Writes the texture currently
         * bound to @p target.
         *
         * @param target GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP.
         * @param surfaceFormat SurfaceFormat ordinal of the target.
         */
        void ApplyRenderTargetChannelSwizzle(GLenum target, int surfaceFormat);

        /**
         * @brief Chooses the sample count a render target of @p storage really gets.
         *
         * The largest count GL reports for the format (glGetInternalformativ/GL_SAMPLES) that does
         * not exceed the request; when that query is unavailable, the request as already clamped
         * to GL_MAX_SAMPLES. A normalized 16-bit format additionally has to prove that its
         * multisample resolve works, or the target stays single-sample.
         *
         * @param storage Colour storage of the target.
         * @param requested Requested (already GL_MAX_SAMPLES-clamped) count.
         * @param cubeTarget Whether the resolve destination is a cube face.
         * @return Applied count; 0 means single-sample.
         */
        [[nodiscard]] int ClampRenderTargetSamples(const RenderTargetColorStorage& storage,
                                                   int requested, bool cubeTarget);

        /**
         * @brief Reads a rectangle of the bound READ framebuffer's colour attachment 0 in the
         *        target's declared format.
         *
         * @param surfaceFormat SurfaceFormat ordinal of the target.
         * @param storage Its colour storage.
         * @param x Left edge in framebuffer (bottom-up) coordinates.
         * @param y Bottom edge in framebuffer (bottom-up) coordinates.
         * @param width Rectangle width.
         * @param height Rectangle height.
         * @param data Receives `width * height * bytesPerPixel` bytes, bottom row first.
         * @return True when GL reported no error.
         */
        [[nodiscard]] bool ReadRenderTargetPixels(int surfaceFormat,
                                                  const RenderTargetColorStorage& storage,
                                                  int x, int y, int width, int height, void* data);

        /**
         * @brief Names a glCheckFramebufferStatus result for a diagnostic.
         *
         * @param status The status.
         * @return Its GL token name, or its hexadecimal value when unknown.
         */
        [[nodiscard]] std::string FramebufferStatusName(GLenum status);

        /**
         * @brief Full mip-chain length of a @p width x @p height x @p depth image.
         *
         * XNA requests D3D9's complete chain, so the largest dimension decides.
         *
         * @param width Width in texels.
         * @param height Height in texels.
         * @param depth Depth in texels (1 for a 2D image).
         * @return Number of levels down to 1x1x1.
         */
        [[nodiscard]] int CalculateMipLevels(int width, int height, int depth);

        /**
         * @brief Probes (once) whether a float colour attachment is framebuffer-complete.
         *
         * @param support The renderer's probe cache.
         * @param fullFloat True for RGBA32F, false for RGBA16F.
         * @return Whether the attachment is complete.
         */
        [[nodiscard]] bool ProbeFloatRenderTargetSupport(OpenGL4FormatSupport& support,
                                                         bool fullFloat);

        /**
         * @brief Probes (once) whether a normalized non-Color target format is renderable.
         *
         * @param support The renderer's probe cache.
         * @param surfaceFormat Rgba1010102, Rg32 or Rgba64 ordinal.
         * @return Whether a 1x1 attachment of the format is complete; false for other ordinals.
         */
        [[nodiscard]] bool ProbeNormalizedRenderTargetSupport(OpenGL4FormatSupport& support,
                                                              int surfaceFormat);
    }

    /** @brief `OpenGL4`-backed plain `Texture2D`. */
    class OpenGL4TextureRenderer final : public ITextureRenderer,
                                         public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates the texture, uploading level 0 from @p data in its declared surface format.
         *
         * Every declared mip level is given storage (none is generated), and
         * GL_TEXTURE_MAX_LEVEL is clamped to the declared chain so a mipmap-carrying filter never
         * samples an incomplete texture as black.
         *
         * @param data Width, height, level-0 bytes, declared level count and SurfaceFormat ordinal.
         * @throws std::invalid_argument When a DXT level 0 carries too few block bytes.
         */
        explicit OpenGL4TextureRenderer(const ImageData& data);
        /** @brief Deletes the GL texture. */
        ~OpenGL4TextureRenderer() override;

        OpenGL4TextureRenderer(const OpenGL4TextureRenderer&) = delete;
        OpenGL4TextureRenderer& operator=(const OpenGL4TextureRenderer&) = delete;

        /** @brief Level-0 width in texels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Level-0 height in texels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /**
         * @brief Makes @p unit the active texture unit and binds this texture to it.
         *
         * @param unit Texture unit index; it is left active.
         */
        void BindGL(int unit) const override;

        /**
         * @brief Replaces level 0 with declared-format bytes (raw blocks for a DXT texture).
         *
         * @param data Level-0 texels, tightly packed.
         * @param stride Unused; the source is tightly packed.
         */
        void UpdatePixels(const uint8_t* data, int stride) override;

        /**
         * @brief Replaces one mip level with declared-format bytes (raw blocks for DXT).
         *
         * @param level Mip level.
         * @param data Level texels, tightly packed.
         * @param levelW Level width.
         * @param levelH Level height.
         */
        void UpdatePixelsLevel(int level, const uint8_t* data, int levelW, int levelH) override;

        /**
         * @brief Reports whether exact DXT bytes exist for a declared mip level.
         *
         * @param level Mip level.
         * @return True when caller- or content-authored blocks are retained for @p level.
         */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;

        /**
         * @brief Reads exact DXT block rows; uncompressed readback is left to the shared layer's
         *        CPU copy and refused here.
         *
         * @param level Mip level.
         * @param x Left edge in texels, block aligned.
         * @param y Top edge in texels, block aligned.
         * @param w Width in texels, block aligned or reaching the level edge.
         * @param h Height in texels, block aligned or reaching the level edge.
         * @param data Receives the tightly packed block rows of the region.
         * @param dataLength Size of @p data in bytes.
         * @return True once the complete region was copied.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Accepts the shared CPU pixel buffer and keeps nothing: OpenGL4 has no
         *        context-loss restoration that would need it.
         *
         * @param pixels Ignored.
         */
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /**
         * @brief The raw SurfaceFormat ordinal this texture was created with.
         *
         * @return The ordinal.
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

        /** @brief The GL texture name. */
        CNAEXT [[nodiscard]] unsigned int GLHandle() const { return texture_; }

    private:
        void UploadLevel(int level, int levelWidth, int levelHeight, const void* pixels);
        void AllocateDeclaredLevels();

        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        int surfaceFormat_ = 0;
        /// Declared level count; GL_TEXTURE_MAX_LEVEL is clamped to mipLevels_ - 1.
        int mipLevels_ = 1;
        /// DXT only: whether this texture stores the blocks natively (S3TC) or decoded RGBA8.
        bool nativeBlocks_ = false;
        /// DXT only: the exact block stream of each declared level; empty = never authored.
        std::vector<std::vector<std::uint8_t>> compressedLevels_;
    };

    /** @brief `OpenGL4`-backed plain (non-render-target) `TextureCube`. */
    class OpenGL4TextureCubeRenderer final : public ITextureCubeRenderer,
                                             public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates a cube texture of the declared format with every face and level zeroed.
         *
         * @param size Face edge length in texels.
         * @param mipMap Whether a full mip chain is allocated.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @throws std::runtime_error For a format outside the cube format set.
         */
        OpenGL4TextureCubeRenderer(int size, bool mipMap, int surfaceFormat);
        /** @brief Deletes the GL texture. */
        ~OpenGL4TextureCubeRenderer() override;

        OpenGL4TextureCubeRenderer(const OpenGL4TextureCubeRenderer&) = delete;
        OpenGL4TextureCubeRenderer& operator=(const OpenGL4TextureCubeRenderer&) = delete;

        /**
         * @brief Makes @p unit the active texture unit and binds this cube map to it.
         *
         * @param unit Texture unit index; it is left active.
         */
        void BindGL(int unit) const override;

        /**
         * @brief Uploads Color texels into one face; refused for every other format.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Region width.
         * @param h Region height.
         * @param data Tightly packed Color texels.
         * @param dataLength Available source bytes.
         * @return True once the whole region was uploaded with no GL error.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Uploads exact DXT blocks into one face, or decodes them when S3TC is absent.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge in texels, block aligned.
         * @param y Top edge in texels, block aligned.
         * @param w Width in texels, block aligned or reaching the level edge.
         * @param h Height in texels, block aligned or reaching the level edge.
         * @param data Exact block payload of the region.
         * @param dataLength Payload size in bytes.
         * @return True once the complete payload was stored.
         */
        [[nodiscard]] bool SetCompressedDataEXT(int face, int level, int x, int y, int w, int h,
                                                const void* data, int dataLength) override;

        /**
         * @brief Reads the exact DXT blocks retained for a block-aligned face region.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge in texels, block aligned.
         * @param y Top edge in texels, block aligned.
         * @param w Width in texels, block aligned or reaching the level edge.
         * @param h Height in texels, block aligned or reaching the level edge.
         * @param data Receives the tightly packed block rows of the region.
         * @param dataLength Available destination bytes.
         * @return True once the complete payload was copied.
         */
        [[nodiscard]] bool GetCompressedDataEXT(int face, int level, int x, int y, int w, int h,
                                                void* data, int dataLength) const override;

        /**
         * @brief Uploads exact uncompressed declared-format texels into one face.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Region width.
         * @param h Region height.
         * @param data Tightly packed declared-format texels.
         * @param dataLength Available source bytes.
         * @return True once the complete region was uploaded with no GL error.
         */
        [[nodiscard]] bool SetDataBytesEXT(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength) override;

        /**
         * @brief Reads Color texels (a DXT cube decodes its retained blocks to RGBA8).
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Region width.
         * @param h Region height.
         * @param data Receives tightly packed RGBA8 texels.
         * @param dataLength Available destination bytes.
         * @return True once the complete region was written; false for other formats.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Reads exact uncompressed declared-format texels from one face.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Region width.
         * @param h Region height.
         * @param data Receives tightly packed declared-format texels.
         * @param dataLength Available destination bytes.
         * @return True once the complete region was written.
         */
        [[nodiscard]] bool GetDataBytesEXT(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const override;

        /**
         * @brief Accepts one face's shared CPU pixels and keeps nothing (no context-loss
         *        restoration on OpenGL4).
         *
         * @param face Ignored.
         * @param pixels Ignored.
         */
        void ShareCpuPixels(int face, std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /** @brief Returns the cube edge length in texels. */
        [[nodiscard]] int GetSizeEXT() const noexcept override { return size_; }

        /**
         * @brief Returns the declared SurfaceFormat ordinal.
         *
         * @return The ordinal supplied at construction.
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

    private:
        [[nodiscard]] std::size_t LevelIndex(int face, int level) const
        {
            return static_cast<std::size_t>(face * levelCount_ + level);
        }

        unsigned int texture_ = 0;
        int size_ = 0;
        int surfaceFormat_ = 0;
        int levelCount_ = 1;
        bool nativeBlocks_ = false;
        /// DXT only: exact face-major block streams, the source of DXT readback.
        std::vector<std::vector<std::uint8_t>> compressedLevels_;
    };

    /** @brief `OpenGL4`-backed plain (non-render-target) `Texture3D` (volume texture). */
    class OpenGL4Texture3DRenderer final : public ITexture3DRenderer,
                                           public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates a zeroed volume texture of the declared format.
         *
         * @param w Width in texels.
         * @param h Height in texels.
         * @param depth Depth in texels.
         * @param mipMap Whether a full mip chain is allocated.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @throws std::runtime_error For a format outside the volume format set.
         */
        OpenGL4Texture3DRenderer(int w, int h, int depth, bool mipMap, int surfaceFormat);
        /** @brief Deletes the GL texture. */
        ~OpenGL4Texture3DRenderer() override;

        OpenGL4Texture3DRenderer(const OpenGL4Texture3DRenderer&) = delete;
        OpenGL4Texture3DRenderer& operator=(const OpenGL4Texture3DRenderer&) = delete;

        /**
         * @brief Makes @p unit the active texture unit and binds this volume texture to it.
         *
         * @param unit Texture unit index; it is left active.
         */
        void BindGL(int unit) const override;

        /**
         * @brief Uploads Color voxels into a box; refused for every other format.
         *
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Tightly packed Color voxels, slice by slice.
         * @param dataLength Available source bytes.
         * @return True once the whole box was uploaded with no GL error.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Uploads exact declared-format voxels into a box.
         *
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Tightly packed declared-format voxels.
         * @param dataLength Available source bytes.
         * @return True once the complete box was uploaded with no GL error.
         */
        [[nodiscard]] bool SetDataBytesEXT(int level, int x, int y, int z, int w, int h, int depth,
                                           const void* data, int dataLength) override;

        /**
         * @brief Reads Color voxels from a box; refused for every other format.
         *
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Receives tightly packed Color voxels.
         * @param dataLength Available destination bytes.
         * @return True once the complete box was written.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /**
         * @brief Reads exact declared-format voxels from a box.
         *
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Receives tightly packed declared-format voxels.
         * @param dataLength Available destination bytes.
         * @return True once the complete box was written.
         */
        [[nodiscard]] bool GetDataBytesEXT(int level, int x, int y, int z, int w, int h, int depth,
                                           void* data, int dataLength) const override;

        /**
         * @brief Reports the volume's level-0 extent.
         *
         * @param width Receives the width.
         * @param height Receives the height.
         * @param depth Receives the depth.
         */
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override
        {
            width = width_;
            height = height_;
            depth = depth_;
        }

        /**
         * @brief Returns the declared SurfaceFormat ordinal.
         *
         * @return The ordinal supplied at construction.
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

    private:
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
        int surfaceFormat_ = 0;
    };

    /**
     * @brief `OpenGL4`-backed `RenderTarget2D`: an FBO whose colour attachment is also the texture
     * the target is later sampled as.
     */
    class OpenGL4RenderTargetRenderer final : public IRenderTargetRenderer,
                                              public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates the target's colour, depth/stencil and (optional) multisample storage.
         *
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat Raw DepthFormat ordinal.
         * @param binding The renderer's bound-target record, cleared by this target on destruction.
         * @param mipMap Whether a mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count; 0/1 disables MSAA.
         * @param surfaceFormat SurfaceFormat ordinal of the colour storage.
         * @throws std::runtime_error When GL reports the assembled framebuffer incomplete.
         */
        OpenGL4RenderTargetRenderer(int w, int h, int depthFormat,
                                    std::weak_ptr<OpenGL4BoundTarget> binding,
                                    bool mipMap, int multiSampleCount, int surfaceFormat);
        /** @brief Detaches from the bound-target record, then deletes every GL object. */
        ~OpenGL4RenderTargetRenderer() override;

        OpenGL4RenderTargetRenderer(const OpenGL4RenderTargetRenderer&) = delete;
        OpenGL4RenderTargetRenderer& operator=(const OpenGL4RenderTargetRenderer&) = delete;

        /** @brief Width in pixels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Height in pixels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /**
         * @brief Makes @p unit the active texture unit and binds the colour texture to it.
         *
         * @param unit Texture unit index; it is left active.
         */
        void BindGL(int unit) const override;
        /** @brief Binds this target's framebuffer as GL_FRAMEBUFFER. */
        void BindAsRenderTarget() override;
        /**
         * @brief Resolves the multisample colour (if any), regenerates the mip chain (if any) and
         *        leaves framebuffer 0 bound.
         */
        void UnbindAsRenderTarget() override;
        /** @brief The colour texture's GL name. */
        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTexture_; }
        /** @brief Applied (clamped) sample count; 0 when single-sample. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Uploads a complete level-zero image in the target's declared format.
         *
         * @param data Source pixels in top-row-first order.
         * @param stride Source row pitch in bytes (0 = tightly packed).
         */
        void UpdatePixels(const uint8_t* data, int stride) override;

        /**
         * @brief Uploads a complete mip image in the target's declared format.
         *
         * @param level Destination mip level.
         * @param data Source pixels in top-row-first order.
         * @param levelW Expected mip width.
         * @param levelH Expected mip height.
         */
        void UpdatePixelsLevel(int level, const uint8_t* data, int levelW, int levelH) override;

        /**
         * @brief Reports every allocated mip as directly readable.
         *
         * @param level Mip level to query.
         * @return True when the level belongs to this target's allocated chain.
         */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override
        {
            return level >= 0 && level < levelCount_;
        }

        /**
         * @brief Reads a rectangle of one mip level back, top row first, in the declared format.
         *
         * A multisampled target that is still the active producer is resolved first.
         *
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Receives `w * h * bytesPerPixel` bytes.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole rectangle was read; every invalid request throws instead.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief The raw SurfaceFormat ordinal the colour storage was created with.
         *
         * @return The ordinal (Color when an unmapped ordinal reached the constructor).
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

        /**
         * @brief Whether this target has depth storage.
         *
         * @param depthFormatWasRequested Whether the public DepthFormat asks for depth.
         * @return True when depth was requested and this target allocated some.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthFormat_ != 0;
        }

        /** @brief CNAEXT. Depth precision of this target, from the DepthFormat it was created with. */
        [[nodiscard]] int DepthBufferBitsEXT() const override
        {
            return OpenGL4DepthBufferBits(depthFormat_);
        }

        /** @brief Raw DepthFormat ordinal this target was created with. */
        [[nodiscard]] int GetDepthFormatEXT() const noexcept { return depthFormat_; }

        /**
         * @brief Attaches this target's colour storage to @p framebuffer (bound as GL_FRAMEBUFFER).
         *
         * @param framebuffer The multi-target FBO.
         * @param attachment GL_COLOR_ATTACHMENTi.
         */
        void AttachColorToMRT(unsigned int framebuffer, unsigned int attachment) const;

        /**
         * @brief Attaches this target's depth/stencil storage (if any) to @p framebuffer.
         *
         * @param framebuffer The multi-target FBO, bound as GL_FRAMEBUFFER.
         */
        void AttachDepthToMRT(unsigned int framebuffer) const;

    private:
        void CreateResources();
        void DestroyResources() noexcept;
        void UploadPixelsLevel(int level, const uint8_t* data, int levelW, int levelH, int stride);
        void ResolveColor() const;
        void DetachFromBinding();

        unsigned int colorTexture_ = 0;
        unsigned int fbo_ = 0;         ///< Render FBO (colour = colorTexture_, or msaaColorRbo_).
        unsigned int resolveFbo_ = 0;  ///< MSAA only: blit destination (colour = colorTexture_).
        unsigned int depthRbo_ = 0;
        unsigned int msaaColorRbo_ = 0;
        int width_ = 0;
        int height_ = 0;
        int depthFormat_ = 0;
        int multiSampleCount_ = 0;
        int surfaceFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        std::weak_ptr<OpenGL4BoundTarget> binding_;
    };

    /** @brief `OpenGL4`-backed `RenderTargetCube`. */
    class OpenGL4RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer,
                                                  public OpenGL4ContextResource
    {
    public:
        /**
         * @brief Creates the cube's colour, depth/stencil and (optional) multisample storage.
         *
         * @param size Face edge length.
         * @param depthFormat Raw DepthFormat ordinal.
         * @param binding The renderer's bound-target record, cleared by this target on destruction.
         * @param mipMap Whether a mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count; 0/1 disables MSAA.
         * @param surfaceFormat SurfaceFormat ordinal of the colour storage.
         * @throws std::runtime_error When GL reports the assembled framebuffer incomplete.
         */
        OpenGL4RenderTargetCubeRenderer(int size, int depthFormat,
                                        std::weak_ptr<OpenGL4BoundTarget> binding,
                                        bool mipMap, int multiSampleCount, int surfaceFormat);
        /** @brief Detaches from the bound-target record, then deletes every GL object. */
        ~OpenGL4RenderTargetCubeRenderer() override;

        OpenGL4RenderTargetCubeRenderer(const OpenGL4RenderTargetCubeRenderer&) = delete;
        OpenGL4RenderTargetCubeRenderer& operator=(const OpenGL4RenderTargetCubeRenderer&) = delete;

        /** @brief Face edge length in pixels. */
        [[nodiscard]] int GetSize() const override { return size_; }
        /**
         * @brief Binds this cube's framebuffer with @p face as its colour destination.
         *
         * @param face Cube face ordinal (0=+X .. 5=-Z).
         */
        void BindAsRenderTargetFace(int face) override;
        /** @brief Finalizes the most recently bound face (see @ref UnbindMRTFace). */
        void UnbindAsRenderTarget() override;
        /** @brief The cube texture's GL name. */
        [[nodiscard]] unsigned int GetGLHandle() const override { return cubeTexture_; }
        /** @brief Applied (clamped) sample count; 0 when single-sample. */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Makes @p unit the active texture unit and binds the cube texture to it.
         *
         * @param unit Texture unit index; it is left active.
         */
        void BindGL(int unit) const override;

        /**
         * @brief Uploads declared-format texels into a rendered cube face's mip level.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge of the destination rectangle.
         * @param y Top edge of the destination rectangle.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Source texels in the target's declared format, top row first.
         * @param dataLength Available source bytes.
         * @return True once the whole region was uploaded with no GL error.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Uploads exact declared-format texels into a rendered cube face.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Source texels in the target's declared format, top row first.
         * @param dataLength Available source bytes.
         * @return True once the complete region was uploaded.
         */
        [[nodiscard]] bool SetDataBytesEXT(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength) override;

        /**
         * @brief Reads a rendered cube face's mip level back, top row first, in the declared format.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Receives tightly packed declared-format rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True once the whole region was read.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Reads exact declared-format texels from a rendered cube face.
         *
         * @param face Cube face index.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Receives tightly packed declared-format rows, top row first.
         * @param dataLength Available destination bytes.
         * @return True once the complete region was returned.
         */
        [[nodiscard]] bool GetDataBytesEXT(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const override;

        /**
         * @brief Returns the SurfaceFormat ordinal this cube target stores.
         *
         * @return The ordinal (Color when an unmapped ordinal reached the constructor).
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

        /** @brief CNAEXT. Depth precision of this cube target, from its own DepthFormat. */
        [[nodiscard]] int DepthBufferBitsEXT() const override
        {
            return OpenGL4DepthBufferBits(depthFormat_);
        }

        /** @brief Raw DepthFormat ordinal this target was created with. */
        [[nodiscard]] int GetDepthFormatEXT() const noexcept { return depthFormat_; }

        /**
         * @brief Attaches one face's colour storage to @p framebuffer (bound as GL_FRAMEBUFFER).
         *
         * @param framebuffer The multi-target FBO.
         * @param attachment GL_COLOR_ATTACHMENTi.
         * @param face Cube face ordinal.
         */
        void AttachColorToMRT(unsigned int framebuffer, unsigned int attachment, int face) const;

        /**
         * @brief Attaches the cube's depth/stencil storage (if any) to @p framebuffer.
         *
         * @param framebuffer The multi-target FBO, bound as GL_FRAMEBUFFER.
         */
        void AttachDepthToMRT(unsigned int framebuffer) const;

        /**
         * @brief Finalizes one face that was bound as an MRT slot (MSAA resolve, mip regeneration).
         *
         * @param face Cube face ordinal.
         */
        void UnbindMRTFace(int face);

    private:
        void CreateResources();
        void DestroyResources() noexcept;
        void ResolveFace(int face);
        void DetachFromBinding();

        unsigned int cubeTexture_ = 0;
        unsigned int fbo_ = 0;         ///< Render FBO (colour = a cubeTexture_ face, or msaaColorRbos_[face]).
        unsigned int resolveFbo_ = 0;  ///< MSAA only: blit destination, re-attached per face.
        unsigned int depthRbo_ = 0;
        /// One multisample colour renderbuffer PER FACE, so a PreserveContents face rebound for a
        /// partial update finds its own samples rather than whichever face was rendered last.
        std::array<unsigned int, 6> msaaColorRbos_{};
        int size_ = 0;
        int depthFormat_ = 0;
        int multiSampleCount_ = 0;
        int surfaceFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int lastFace_ = 0;  ///< Most recently bound face, used by UnbindAsRenderTarget's resolve.
        std::weak_ptr<OpenGL4BoundTarget> binding_;
    };
}
