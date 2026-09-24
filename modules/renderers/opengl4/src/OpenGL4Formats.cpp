// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0012: OpenGL4's surface-format layer -- the storage
// tables every texture and render target allocates from, the S3TC decode fallback, the runtime
// renderability probes, and the OpenGL4Renderer members that create texture/render-target
// resources and classify formats. A desktop OpenGL 4.1 core port of EasyGL's equivalents: the
// ES 2.0/WebGL generation's refusals and extension checks collapse to their desktop answers
// (sized formats, texture swizzle and 16-bit normalized storage are all core).

#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::OpenGL4
{
    using namespace CNA::Internal::Renderers::OpenGL4::GL4;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace Detail
    {
        namespace
        {
            constexpr std::uint16_t kHalfOne = 0x3c00u;
            constexpr std::uint16_t kUnorm16One = 65535u;
            constexpr std::int8_t kSnorm8One = 127;

            /// Widens texels of @p sourceChannels channels to four, filling the missing ones.
            template <typename Channel>
            void WidenToFourChannels(const std::uint8_t* source, std::size_t texelCount,
                                     std::size_t sourceChannels, Channel fill,
                                     std::vector<std::uint8_t>& scratch)
            {
                constexpr std::size_t channelBytes = sizeof(Channel);
                scratch.resize(texelCount * 4u * channelBytes);
                const std::array<Channel, 4> filled{fill, fill, fill, fill};
                for (std::size_t texel = 0; texel < texelCount; ++texel)
                {
                    std::uint8_t* out = scratch.data() + texel * 4u * channelBytes;
                    std::memcpy(out, filled.data(), 4u * channelBytes);
                    std::memcpy(out, source + texel * sourceChannels * channelBytes,
                                sourceChannels * channelBytes);
                }
            }

            /// Keeps the first @p keptChannels channels of each four-channel transfer texel.
            void NarrowFromFourChannels(const std::uint8_t* transfer, std::size_t texelCount,
                                        std::size_t keptChannels, std::size_t channelBytes,
                                        std::uint8_t* destination)
            {
                for (std::size_t texel = 0; texel < texelCount; ++texel)
                {
                    std::memcpy(destination + texel * keptChannels * channelBytes,
                                transfer + texel * 4u * channelBytes,
                                keptChannels * channelBytes);
                }
            }

            /// Rotates every 16-bit texel left by @p rotate bits (right when @p rotate is negative).
            void RotatePacked16(const std::uint8_t* source, std::size_t texelCount, int rotate,
                                std::uint8_t* destination)
            {
                const int left = rotate >= 0 ? rotate : 16 + rotate;
                for (std::size_t texel = 0; texel < texelCount; ++texel)
                {
                    std::uint16_t value = 0;
                    std::memcpy(&value, source + texel * 2u, 2u);
                    value = static_cast<std::uint16_t>((value << left) | (value >> (16 - left)));
                    std::memcpy(destination + texel * 2u, &value, 2u);
                }
            }

            /// Whether @p storage is one of the two 16-bit normalized layouts whose multisample
            /// resolve is proven before it is used.
            [[nodiscard]] bool IsNormalized16(const RenderTargetColorStorage& storage)
            {
                return !storage.isFloat &&
                       (storage.internalFormat == GL_RG16 || storage.internalFormat == GL_RGBA16);
            }

            /**
             * Whether GL's multisample resolve of a normalized 16-bit format really works: clear a
             * 4x4 multisample buffer to a known colour, blit it into a single-sample image of the
             * same format and read one texel back. EasyGL found Mesa GLES leaving such a resolve
             * unwritten; the same measurement costs nothing on a driver that gets it right.
             */
            [[nodiscard]] bool ProbeNormalized16MsaaResolve(const RenderTargetColorStorage& storage,
                                                            int samples, bool cubeTarget)
            {
                ScopedFramebufferBindings framebuffers;
                ScopedRenderbufferBinding renderbuffer;
                GLboolean previousMask[4]{};
                glGetBooleanv(GL_COLOR_WRITEMASK, previousMask);
                const bool scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;

                GLuint resolveTexture = 0;
                GLuint multisampleColor = 0;
                std::array<GLuint, 2> framebufferNames{};
                const GLenum resolveTarget = cubeTarget ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                glGenTextures(1, &resolveTexture);
                {
                    ScopedTextureBinding binding(resolveTarget, resolveTexture);
                    ScopedUnpackState unpack(1);
                    const int images = cubeTarget ? 6 : 1;
                    for (int face = 0; face < images; ++face)
                    {
                        const GLenum imageTarget = cubeTarget
                            ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                            : static_cast<GLenum>(GL_TEXTURE_2D);
                        glTexImage2D(imageTarget, 0, static_cast<GLint>(storage.internalFormat),
                                     4, 4, 0, storage.pixelFormat, storage.pixelType, nullptr);
                    }
                }
                gl4_glGenRenderbuffers(1, &multisampleColor);
                gl4_glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
                gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                                     storage.internalFormat, 4, 4);
                gl4_glGenFramebuffers(2, framebufferNames.data());
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, framebufferNames[0]);
                gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                              GL_RENDERBUFFER, multisampleColor);
                const bool multisampleComplete =
                    gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, framebufferNames[1]);
                gl4_glFramebufferTexture2D(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                    cubeTarget ? static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X)
                               : static_cast<GLenum>(GL_TEXTURE_2D),
                    resolveTexture, 0);
                const bool resolveComplete =
                    gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

                bool succeeded = multisampleComplete && resolveComplete;
                std::array<std::uint8_t, 4> pixel{};
                if (succeeded)
                {
                    DrainGlErrors();
                    if (scissorWasEnabled)
                        glDisable(GL_SCISSOR_TEST);
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebufferNames[0]);
                    const GLfloat clear[4]{0.25f, 0.5f, 0.75f, 1.0f};
                    gl4_glClearBufferfv(GL_COLOR, 0, clear);
                    gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferNames[0]);
                    gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebufferNames[1]);
                    gl4_glBlitFramebuffer(0, 0, 4, 4, 0, 0, 4, 4, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, framebufferNames[1]);
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    {
                        ScopedPackState pack(4);
                        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
                    }
                    succeeded = GlOperationSucceeded() && pixel[0] > 32u && pixel[1] > 96u;
                }

                glColorMask(previousMask[0], previousMask[1], previousMask[2], previousMask[3]);
                if (scissorWasEnabled)
                    glEnable(GL_SCISSOR_TEST);
                gl4_glDeleteFramebuffers(2, framebufferNames.data());
                gl4_glDeleteRenderbuffers(1, &multisampleColor);
                glDeleteTextures(1, &resolveTexture);
                DrainGlErrors();
                return succeeded;
            }

            /**
             * MOD-117: probed, not inferred. Creates a 1x1 attachment of the real format and asks
             * GL whether the framebuffer is complete -- the same question a render target's
             * creation asks for real, so the probe cannot be optimistic about something that then
             * fails. Leaves every binding exactly as it found it: this runs on demand, possibly
             * mid-frame with a render target bound.
             */
            [[nodiscard]] bool ProbeColorAttachment(const RenderTargetColorStorage& storage)
            {
                ScopedFramebufferBindings framebuffers;
                GLuint texture = 0;
                GLuint framebuffer = 0;
                glGenTextures(1, &texture);
                {
                    ScopedTextureBinding binding(GL_TEXTURE_2D, texture);
                    ScopedUnpackState unpack(1);
                    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(storage.internalFormat),
                                 1, 1, 0, storage.pixelFormat, storage.pixelType, nullptr);
                }
                gl4_glGenFramebuffers(1, &framebuffer);
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                gl4_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                           texture, 0);
                const bool complete =
                    gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                gl4_glDeleteFramebuffers(1, &framebuffer);
                glDeleteTextures(1, &texture);
                // A refused format leaves a GL error queued behind it; drain it so the next real
                // call is not blamed for this one.
                DrainGlErrors();
                return complete;
            }
        }

        bool MapTextureTransferFormat(const int surfaceFormat, const bool texture2D,
                                      TextureTransferFormat& out)
        {
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Color:
                out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                return true;
            // REMED-GFX-244: Bgr565 is handed to GL as it stands (XNA packs R:11 G:5 B:0, exactly
            // what GL_UNSIGNED_SHORT_5_6_5 reads); the two with alpha are rotated one channel
            // width on the way (see ExpandTexelsForTransfer) -- a pure bit permutation.
            case SurfaceFormat::Bgr565:
                out = {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2};
                return true;
            case SurfaceFormat::Bgra5551:
                out = {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2};
                return true;
            case SurfaceFormat::Bgra4444:
                out = {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2};
                return true;
            // The two signed-normalized byte formats differ only in channel count; NormalizedByte2
            // is what a content pipeline picks for a 2D displacement map. Texture2D only.
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                if (!texture2D)
                    return false;
                out = {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4};
                return true;
            case SurfaceFormat::Rgba1010102:
                out = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4};
                return true;
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
                out = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 8};
                return true;
            case SurfaceFormat::Alpha8:
                out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                return true;
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
                out = {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
                return true;
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
                return true;
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
                return false;
            default:
                // Texture2D stores any other ordinal the shared layer let through as Color bytes,
                // exactly as EasyGL's plain RGBA upload does; cubes and volumes refuse it.
                if (!texture2D)
                    return false;
                out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
                return true;
            }
        }

        const void* ExpandTexelsForTransfer(const int surfaceFormat, const void* source,
                                            const std::size_t texelCount,
                                            std::vector<std::uint8_t>& scratch)
        {
            if (source == nullptr)
                return nullptr;
            const auto* bytes = static_cast<const std::uint8_t*>(source);
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
                scratch.resize(texelCount * 2u);
                RotatePacked16(bytes, texelCount,
                               static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Bgra5551
                                   ? 1 : 4,
                               scratch.data());
                return scratch.data();
            case SurfaceFormat::NormalizedByte2:
                WidenToFourChannels<std::int8_t>(bytes, texelCount, 2u, kSnorm8One, scratch);
                return scratch.data();
            case SurfaceFormat::Rg32:
                WidenToFourChannels<std::uint16_t>(bytes, texelCount, 2u, kUnorm16One, scratch);
                return scratch.data();
            case SurfaceFormat::Alpha8:
                scratch.assign(texelCount * 4u, 0u);
                for (std::size_t texel = 0; texel < texelCount; ++texel)
                    scratch[texel * 4u + 3u] = bytes[texel];
                return scratch.data();
            case SurfaceFormat::Single:
                WidenToFourChannels<float>(bytes, texelCount, 1u, 1.0f, scratch);
                return scratch.data();
            case SurfaceFormat::Vector2:
                WidenToFourChannels<float>(bytes, texelCount, 2u, 1.0f, scratch);
                return scratch.data();
            case SurfaceFormat::HalfSingle:
                WidenToFourChannels<std::uint16_t>(bytes, texelCount, 1u, kHalfOne, scratch);
                return scratch.data();
            case SurfaceFormat::HalfVector2:
                WidenToFourChannels<std::uint16_t>(bytes, texelCount, 2u, kHalfOne, scratch);
                return scratch.data();
            default:
                return source;
            }
        }

        void CollapseTransferTexels(const int surfaceFormat, const std::uint8_t* transfer,
                                    const std::size_t texelCount, std::uint8_t* destination)
        {
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Bgra5551:
                RotatePacked16(transfer, texelCount, -1, destination);
                return;
            case SurfaceFormat::Bgra4444:
                RotatePacked16(transfer, texelCount, -4, destination);
                return;
            case SurfaceFormat::NormalizedByte2:
                NarrowFromFourChannels(transfer, texelCount, 2u, 1u, destination);
                return;
            case SurfaceFormat::Rg32:
                NarrowFromFourChannels(transfer, texelCount, 2u, 2u, destination);
                return;
            case SurfaceFormat::Alpha8:
                for (std::size_t texel = 0; texel < texelCount; ++texel)
                    destination[texel] = transfer[texel * 4u + 3u];
                return;
            case SurfaceFormat::Single:
                NarrowFromFourChannels(transfer, texelCount, 1u, 4u, destination);
                return;
            case SurfaceFormat::Vector2:
                NarrowFromFourChannels(transfer, texelCount, 2u, 4u, destination);
                return;
            case SurfaceFormat::HalfSingle:
                NarrowFromFourChannels(transfer, texelCount, 1u, 2u, destination);
                return;
            case SurfaceFormat::HalfVector2:
                NarrowFromFourChannels(transfer, texelCount, 2u, 2u, destination);
                return;
            default:
            {
                TextureTransferFormat transferFormat{};
                if (!MapTextureTransferFormat(surfaceFormat, true, transferFormat))
                    return;
                std::memcpy(destination, transfer,
                            texelCount *
                                static_cast<std::size_t>(transferFormat.transferBytesPerTexel));
                return;
            }
            }
        }

        bool IsDxtFormat(const int surfaceFormat)
        {
            const auto format = static_cast<SurfaceFormat>(surfaceFormat);
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

        std::size_t DxtBlockBytes(const int surfaceFormat)
        {
            return static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Dxt1 ? 8u : 16u;
        }

        std::size_t DxtImageBytes(const int surfaceFormat, const int width, const int height)
        {
            return static_cast<std::size_t>((width + 3) / 4) *
                   static_cast<std::size_t>((height + 3) / 4) * DxtBlockBytes(surfaceFormat);
        }

        GLenum DxtInternalFormat(const int surfaceFormat)
        {
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Dxt1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            case SurfaceFormat::Dxt3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
            default:                  return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            }
        }

        std::vector<std::uint8_t> DecodeDxtBlocks(const int surfaceFormat,
                                                  const std::uint8_t* blocks,
                                                  const std::size_t byteCount,
                                                  const int width, const int height)
        {
            using CNA::Internal::Graphics::DxtUtil;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Dxt1: return DxtUtil::DecompressDxt1(blocks, byteCount, width, height);
            case SurfaceFormat::Dxt3: return DxtUtil::DecompressDxt3(blocks, byteCount, width, height);
            default:                  return DxtUtil::DecompressDxt5(blocks, byteCount, width, height);
            }
        }

        bool ContextHasS3tc()
        {
            // -1: not yet known; 0: absent; 1: present. Only a real answer is cached: the extension
            // list is reachable once the renderer has resolved glGetStringi.
            static std::atomic<int> known{-1};
            const int cached = known.load(std::memory_order_acquire);
            if (cached >= 0)
                return cached == 1;
            if (gl4_glGetStringi == nullptr)
                return false;

            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            bool present = false;
            for (GLint index = 0; index < count && !present; ++index)
            {
                const auto* name = reinterpret_cast<const char*>(
                    gl4_glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
                if (name == nullptr)
                    continue;
                present = std::strcmp(name, "GL_EXT_texture_compression_s3tc") == 0 ||
                          std::strcmp(name, "GL_ANGLE_texture_compression_dxt5") == 0;
            }
            known.store(present ? 1 : 0, std::memory_order_release);
            return present;
        }

        bool MapRenderTargetColorFormat(const int surfaceFormat, RenderTargetColorStorage& out)
        {
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Color:
                out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false, 4};
                return true;
            case SurfaceFormat::Rgba1010102:
                out = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, false, false, 4};
                return true;
            case SurfaceFormat::Rg32:
                out = {GL_RG16, GL_RG, GL_UNSIGNED_SHORT, false, false, 4};
                return true;
            case SurfaceFormat::Rgba64:
                out = {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, false, false, 8};
                return true;
            case SurfaceFormat::Single:
                out = {GL_R32F, GL_RED, GL_FLOAT, true, true, 4};
                return true;
            case SurfaceFormat::Vector2:
                out = {GL_RG32F, GL_RG, GL_FLOAT, true, true, 8};
                return true;
            case SurfaceFormat::Vector4:
                out = {GL_RGBA32F, GL_RGBA, GL_FLOAT, true, true, 16};
                return true;
            case SurfaceFormat::HalfSingle:
                out = {GL_R16F, GL_RED, GL_HALF_FLOAT, true, false, 2};
                return true;
            case SurfaceFormat::HalfVector2:
                out = {GL_RG16F, GL_RG, GL_HALF_FLOAT, true, false, 4};
                return true;
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                // HdrBlendable is XNA's "float format for HDR data"; on Windows it was RGBA16F,
                // and CNA makes that equivalence explicit.
                out = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, true, false, 8};
                return true;
            default:
                return false;
            }
        }

        bool MapDepthFormat(const int depthFormat, GLenum& internalFormat, GLenum& attachment)
        {
            switch (static_cast<DepthFormat>(depthFormat))
            {
            case DepthFormat::Depth16:
                internalFormat = GL_DEPTH_COMPONENT16;
                attachment = GL_DEPTH_ATTACHMENT;
                return true;
            case DepthFormat::Depth24:
                internalFormat = GL_DEPTH_COMPONENT24;
                attachment = GL_DEPTH_ATTACHMENT;
                return true;
            case DepthFormat::Depth24Stencil8:
                internalFormat = GL_DEPTH24_STENCIL8;
                attachment = GL_DEPTH_STENCIL_ATTACHMENT;
                return true;
            case DepthFormat::None:
            default:
                return false;
            }
        }

        void ApplyRenderTargetChannelSwizzle(const GLenum target, const int surfaceFormat)
        {
            const auto format = static_cast<SurfaceFormat>(surfaceFormat);
            const bool oneChannel = format == SurfaceFormat::Single ||
                                    format == SurfaceFormat::HalfSingle;
            const bool twoChannel = format == SurfaceFormat::Vector2 ||
                                    format == SurfaceFormat::HalfVector2 ||
                                    format == SurfaceFormat::Rg32;
            if (!oneChannel && !twoChannel)
                return;
            if (oneChannel)
                glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_ONE);
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_ONE);
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        }

        int ClampRenderTargetSamples(const RenderTargetColorStorage& storage, const int requested,
                                     const bool cubeTarget)
        {
            if (requested <= 1)
                return 0;

            // glGetInternalformativ is GL 4.2 (ARB_internalformat_query). Where it is missing, or
            // the context refuses the query, the request as already clamped to GL_MAX_SAMPLES is
            // what glRenderbufferStorageMultisample is guaranteed to accept.
            int applied = requested;
            if (gl4_glGetInternalformativ != nullptr)
            {
                DrainGlErrors();
                std::array<GLint, 16> supported{};
                gl4_glGetInternalformativ(GL_RENDERBUFFER, storage.internalFormat, GL_SAMPLES,
                                          static_cast<GLsizei>(supported.size()),
                                          supported.data());
                if (GlOperationSucceeded())
                {
                    applied = 0;
                    for (const GLint count : supported)
                    {
                        if (count > 1 && count <= requested)
                            applied = std::max(applied, static_cast<int>(count));
                    }
                }
                DrainGlErrors();
            }

            if (IsNormalized16(storage) && applied > 0)
            {
                if (!ProbeNormalized16MsaaResolve(storage, applied, cubeTarget))
                    return 0;
            }
            return applied;
        }

        bool ReadRenderTargetPixels(const int surfaceFormat,
                                    const RenderTargetColorStorage& storage,
                                    const int x, const int y, const int width, const int height,
                                    void* data)
        {
            DrainGlErrors();
            if (static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Rg32)
            {
                // Read the four normalized channels and keep the two XNA's Rg32 stores, without
                // narrowing either to 8 bits (the ReadPixels shape EasyGL uses on every profile).
                std::vector<std::uint16_t> expanded(
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
                {
                    ScopedPackState pack(8);
                    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_SHORT, expanded.data());
                }
                if (!GlOperationSucceeded())
                    return false;
                auto* destination = static_cast<std::uint8_t*>(data);
                const std::size_t texels =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                for (std::size_t index = 0; index < texels; ++index)
                {
                    std::memcpy(destination + index * 4u, &expanded[index * 4u], 2u);
                    std::memcpy(destination + index * 4u + 2u, &expanded[index * 4u + 1u], 2u);
                }
                return true;
            }

            {
                ScopedPackState pack(std::min(8, storage.bytesPerPixel));
                glReadPixels(x, y, width, height, storage.pixelFormat, storage.pixelType, data);
            }
            return GlOperationSucceeded();
        }

        std::string FramebufferStatusName(const GLenum status)
        {
            switch (status)
            {
            case GL_FRAMEBUFFER_COMPLETE: return "GL_FRAMEBUFFER_COMPLETE";
            case GL_FRAMEBUFFER_UNDEFINED: return "GL_FRAMEBUFFER_UNDEFINED";
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
                return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
                return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
            case GL_FRAMEBUFFER_UNSUPPORTED: return "GL_FRAMEBUFFER_UNSUPPORTED";
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
                return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
            case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
                return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
            default:
            {
                std::ostringstream os;
                os << "0x" << std::hex << std::uppercase << status;
                return os.str();
            }
            }
        }

        int CalculateMipLevels(int width, int height, int depth)
        {
            int levels = 1;
            while (width > 1 || height > 1 || depth > 1)
            {
                width = std::max(1, width / 2);
                height = std::max(1, height / 2);
                depth = std::max(1, depth / 2);
                ++levels;
            }
            return levels;
        }

        bool ProbeFloatRenderTargetSupport(OpenGL4FormatSupport& support, const bool fullFloat)
        {
            auto& cache = fullFloat ? support.fullFloatRenderable : support.halfFloatRenderable;
            if (cache.has_value())
                return *cache;
            RenderTargetColorStorage storage{};
            (void)MapRenderTargetColorFormat(
                static_cast<int>(fullFloat ? SurfaceFormat::Vector4 : SurfaceFormat::HdrBlendable),
                storage);
            cache = ProbeColorAttachment(storage);
            return *cache;
        }

        bool ProbeNormalizedRenderTargetSupport(OpenGL4FormatSupport& support,
                                                const int surfaceFormat)
        {
            std::size_t cacheIndex = 0;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Rgba1010102: cacheIndex = 0; break;
            case SurfaceFormat::Rg32:        cacheIndex = 1; break;
            case SurfaceFormat::Rgba64:      cacheIndex = 2; break;
            default:                         return false;
            }
            auto& cache = support.normalizedRenderable[cacheIndex];
            if (cache.has_value())
                return *cache;
            RenderTargetColorStorage storage{};
            if (!MapRenderTargetColorFormat(surfaceFormat, storage))
            {
                cache = false;
                return false;
            }
            cache = ProbeColorAttachment(storage);
            return *cache;
        }
    }

    std::string DescribeOpenGL4SurfaceFormatSupport(OpenGL4FormatSupport& support)
    {
        std::ostringstream os;
        os << "texture SurfaceFormat: Color"
              " + NormalizedByte4/2 (RGBA8_SNORM)"
              " + Bgr565/Bgra5551/Bgra4444"
              " + Rgba1010102 + Alpha8"
              " + Single/Vector2/Vector4"
              " + HalfSingle/HalfVector2/HalfVector4/HdrBlendable"
              " + Rg32/Rgba64"
           << (Detail::ContextHasS3tc() ? " + Dxt1/Dxt3/Dxt5 (S3TC blocks)"
                                        : " + Dxt1/Dxt3/Dxt5 (decoded, no S3TC extension)")
           << "; render-target SurfaceFormat: Color"
           << (Detail::ProbeNormalizedRenderTargetSupport(
                   support, static_cast<int>(SurfaceFormat::Rgba1010102))
                   ? " + Rgba1010102 (RGB10_A2)" : "")
           << (Detail::ProbeNormalizedRenderTargetSupport(
                   support, static_cast<int>(SurfaceFormat::Rg32))
                   ? " + Rg32 (RG16 UNORM)" : "")
           << (Detail::ProbeNormalizedRenderTargetSupport(
                   support, static_cast<int>(SurfaceFormat::Rgba64))
                   ? " + Rgba64 (RGBA16 UNORM)" : "")
           << (Detail::ProbeFloatRenderTargetSupport(support, false) ? " + half-float (RGBA16F)" : "")
           << (Detail::ProbeFloatRenderTargetSupport(support, true) ? " + float (RGBA32F)" : "");
        return os.str();
    }

    // --- OpenGL4Renderer: resource creation ------------------------------------------------------
    //
    // Each creator first makes the renderer's context current on the calling thread: ContentManager
    // may construct graphics resources on a loading thread.

    namespace
    {
        /// GL4-0021: a resource remembers the context its names were created in.
        template <typename Resource>
        std::unique_ptr<Resource> OwnedByContext(std::unique_ptr<Resource> resource,
                                                 const std::shared_ptr<PlatformGlContextOwner>& context)
        {
            resource->AttachOwningContext(context);
            return resource;
        }
    }

    std::unique_ptr<ITextureRenderer> OpenGL4Renderer::CreateTexture(const ImageData& data)
    {
        EnsureCallingThreadContext();
        return OwnedByContext(std::make_unique<OpenGL4TextureRenderer>(data), platformContext_);
    }

    std::unique_ptr<ITexture3DRenderer> OpenGL4Renderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        EnsureCallingThreadContext();
        return OwnedByContext(std::make_unique<OpenGL4Texture3DRenderer>(w, h, depth, mipMap, surfaceFormat), platformContext_);
    }

    std::unique_ptr<ITextureCubeRenderer> OpenGL4Renderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        EnsureCallingThreadContext();
        return OwnedByContext(std::make_unique<OpenGL4TextureCubeRenderer>(size, mipMap, surfaceFormat), platformContext_);
    }

    std::unique_ptr<IRenderTargetRenderer> OpenGL4Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        EnsureCallingThreadContext();
        // REMED-GFX-136: deliberately unused. A GL framebuffer object's colour attachment IS the
        // texture and binding an FBO never touches its contents, so a target is preserved by
        // construction; the only thing that clears one is the explicit Clear GraphicsDevice issues
        // for a DiscardContents target.
        (void)preserveContents;
        // REMED-GFX-168: `bound_` is handed over weakly so the target can clear its own slot when
        // it is destroyed while still bound, without keeping the binding record alive past the
        // renderer.
        return OwnedByContext(std::make_unique<OpenGL4RenderTargetRenderer>(
            w, h, depthFormat, std::weak_ptr<OpenGL4BoundTarget>(bound_), mipMap,
            multiSampleCount, static_cast<int>(SurfaceFormat::Color)), platformContext_);
    }

    std::unique_ptr<IRenderTargetRenderer> OpenGL4Renderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount,
        int surfaceFormat)
    {
        EnsureCallingThreadContext();
        (void)preserveContents;   // REMED-GFX-136: see CreateRenderTarget2D.
        // plans/plan_modern.md MOD-115: refuse rather than substitute; a renderer that has
        // implemented formats owes an honest answer, and
        // GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() is the way to ask in advance.
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) == RendererFormatVerdict::Unsupported)
        {
            throw std::runtime_error(
                "OpenGL4: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as a render target on this GL context. Query "
                "GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() first.");
        }
        return OwnedByContext(std::make_unique<OpenGL4RenderTargetRenderer>(
            w, h, depthFormat, std::weak_ptr<OpenGL4BoundTarget>(bound_), mipMap,
            multiSampleCount, surfaceFormat), platformContext_);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenGL4Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        EnsureCallingThreadContext();
        // REMED-GFX-136: deliberately unused, exactly as for CreateRenderTarget2D. Each face owns
        // its own multisample colour renderbuffer (REMED-GFX-141), so binding one still touches
        // nothing and there is no load action to carry a usage decision.
        (void)preserveContents;
        return OwnedByContext(std::make_unique<OpenGL4RenderTargetCubeRenderer>(
            size, depthFormat, std::weak_ptr<OpenGL4BoundTarget>(bound_), mipMap,
            multiSampleCount, static_cast<int>(SurfaceFormat::Color)), platformContext_);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenGL4Renderer::CreateRenderTargetCubeEXT(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount,
        int surfaceFormat)
    {
        EnsureCallingThreadContext();
        (void)preserveContents;   // REMED-GFX-136: see CreateRenderTargetCube.
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) == RendererFormatVerdict::Unsupported)
        {
            throw std::runtime_error(
                "OpenGL4: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as a cube render target on this GL context. Query "
                "GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() first.");
        }
        return OwnedByContext(std::make_unique<OpenGL4RenderTargetCubeRenderer>(
            size, depthFormat, std::weak_ptr<OpenGL4BoundTarget>(bound_), mipMap,
            multiSampleCount, surfaceFormat), platformContext_);
    }

    // --- OpenGL4Renderer: format classification --------------------------------------------------

    RendererFormatVerdict OpenGL4Renderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Alpha8:
        // RGBA8_SNORM storage for both signed-normalized byte formats.
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
        // REMED-GFX-244: the packed 16-bit formats GraphicsProfile.Reach permits.
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        // REMED-GFX-244: block-compressed content is supported whether or not the driver has
        // S3TC; it decides only whether the blocks stay compressed.
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
        // Desktop core has 16-bit normalized storage without an extension.
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return RendererFormatVerdict::Supported;
        default:
            return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict OpenGL4Renderer::ClassifyTextureCubeFormatEXT(int surfaceFormat) const
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
        case SurfaceFormat::Alpha8:
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return RendererFormatVerdict::Supported;
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
            return RendererFormatVerdict::Unsupported;
        default:
            return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict OpenGL4Renderer::ClassifyTexture3DFormatEXT(int surfaceFormat) const
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Alpha8:
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return RendererFormatVerdict::Supported;
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
            return RendererFormatVerdict::Unsupported;
        default:
            return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict OpenGL4Renderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        // The public Color-element transfer path is Color-only for every uncompressed XNA format
        // this renderer stores natively; everything else defers to the framework's own rule.
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format != SurfaceFormat::Color &&
            surfaceFormat >= static_cast<int>(SurfaceFormat::Bgr565) &&
            surfaceFormat <= static_cast<int>(SurfaceFormat::HdrBlendable))
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Defer;
    }

    RendererFormatVerdict OpenGL4Renderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        // Color, the three normalized/packed targets and the float formats are this renderer's own
        // answer. Every non-Color attachment is runtime-probed, so capability reporting cannot
        // outrun the exact resource a render target's creation allocates.
        Detail::RenderTargetColorStorage storage{};
        if (!Detail::MapRenderTargetColorFormat(surfaceFormat, storage))
            return RendererFormatVerdict::Defer;
        if (static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Color)
            return RendererFormatVerdict::Supported;
        if (!storage.isFloat)
        {
            return Detail::ProbeNormalizedRenderTargetSupport(formatSupport_, surfaceFormat)
                ? RendererFormatVerdict::Supported
                : RendererFormatVerdict::Unsupported;
        }
        return Detail::ProbeFloatRenderTargetSupport(formatSupport_, storage.isFullFloat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    bool OpenGL4Renderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        // REMED-GFX-244: SetData hands this renderer raw 4x4 blocks for these three, whether or not
        // the driver has S3TC -- the decode, when needed, happens here.
        return Detail::IsDxtFormat(surfaceFormat);
    }

    bool OpenGL4Renderer::IsCompressedCubeTransferFormatEXT(int surfaceFormat) const
    {
        return IsCompressedTransferFormatEXT(surfaceFormat);
    }

    bool OpenGL4Renderer::LoadsCompressedContentNativelyEXT() const
    {
        return true;
    }

    bool OpenGL4Renderer::SupportsHalfFloatTextureLinearFilteringEXT() const
    {
        // Half-float texture filtering is core in desktop GL 3.0+.
        return true;
    }
}
