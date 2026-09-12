// SPDX-License-Identifier: MS-PL

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    bool bridgeInitialized = false;

    void RlglTraceLog(int level, const char* format, ...);
}

#define TRACELOG(level, ...) RlglTraceLog(level, __VA_ARGS__)
#define RLGL_IMPLEMENTATION
#include "rlgl.h"
#undef RLGL_IMPLEMENTATION
#undef TRACELOG

#include "RlglBridge.hpp"

namespace
{
    void RlglTraceLog(const int level, const char* format, ...)
    {
        char message[2048] = {};
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        const std::string text = std::string("RLGL: ") + message;
        if (level >= RL_LOG_ERROR) CNA::Logger::Error(text, CNA::LogCategory::RENDER);
        else if (level == RL_LOG_WARNING) CNA::Logger::Warn(text, CNA::LogCategory::RENDER);
        else if (level == RL_LOG_DEBUG || level == RL_LOG_TRACE)
            CNA::Logger::Debug(text, CNA::LogCategory::RENDER);
        else CNA::Logger::Info(text, CNA::LogCategory::RENDER);
    }

    void ThrowIfGlError(const char* operation)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) return;

        char value[16] = {};
        std::snprintf(value, sizeof(value), "0x%04X", static_cast<unsigned int>(error));
        throw std::runtime_error(
            std::string("RLGL: OpenGL error after ") + operation + ": " + value);
    }

    void RequireInitialized(const char* operation)
    {
        if (!bridgeInitialized)
        {
            throw std::runtime_error(
                std::string("RLGL: ") + operation + " requires a live rlgl device");
        }
    }

    class TextureBindingRestore final
    {
    public:
        TextureBindingRestore()
        {
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2D_);
        }

        ~TextureBindingRestore()
        {
            glActiveTexture(static_cast<GLenum>(activeTexture_));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2D_));
        }

        TextureBindingRestore(const TextureBindingRestore&) = delete;
        TextureBindingRestore& operator=(const TextureBindingRestore&) = delete;

    private:
        GLint activeTexture_ = GL_TEXTURE0;
        GLint texture2D_ = 0;
    };

    class UnpackAlignmentRestore final
    {
    public:
        UnpackAlignmentRestore()
        {
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment_);
        }

        ~UnpackAlignmentRestore()
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment_);
        }

        UnpackAlignmentRestore(const UnpackAlignmentRestore&) = delete;
        UnpackAlignmentRestore& operator=(const UnpackAlignmentRestore&) = delete;

    private:
        GLint alignment_ = 4;
    };

    [[nodiscard]] GLint SamplerMinFilter(const int filter)
    {
        switch (filter)
        {
        case 0: return GL_LINEAR_MIPMAP_LINEAR;
        case 1: return GL_NEAREST_MIPMAP_NEAREST;
        case 2: return GL_LINEAR_MIPMAP_LINEAR;
        case 3: return GL_LINEAR_MIPMAP_NEAREST;
        case 4: return GL_NEAREST_MIPMAP_LINEAR;
        case 5: return GL_LINEAR_MIPMAP_LINEAR;
        case 6: return GL_LINEAR_MIPMAP_NEAREST;
        case 7: return GL_NEAREST_MIPMAP_LINEAR;
        case 8: return GL_NEAREST_MIPMAP_NEAREST;
        default:
            throw std::invalid_argument("RLGL: invalid TextureFilter ordinal");
        }
    }

    [[nodiscard]] GLint SamplerMagFilter(const int filter)
    {
        switch (filter)
        {
        case 0:
        case 2:
        case 3:
        case 7:
        case 8:
            return GL_LINEAR;
        case 1:
        case 4:
        case 5:
        case 6:
            return GL_NEAREST;
        default:
            throw std::invalid_argument("RLGL: invalid TextureFilter ordinal");
        }
    }

    [[nodiscard]] GLint SamplerWrap(const int addressMode)
    {
        switch (addressMode)
        {
        case 0: return GL_REPEAT;
        case 1: return GL_CLAMP_TO_EDGE;
        case 2: return GL_MIRRORED_REPEAT;
        default:
            throw std::invalid_argument("RLGL: invalid TextureAddressMode ordinal");
        }
    }

    struct TextureFormatInfo
    {
        enum class Swizzle
        {
            Identity,
            OneChannel,
            TwoChannel,
            AlphaOnly
        };

        GLenum internalFormat = 0;
        GLenum transferFormat = 0;
        GLenum transferType = 0;
        int bytesPerTexel = 0;
        int rlglFormat = 0;
        int uploadRotateLeft = 0;
        Swizzle swizzle = Swizzle::Identity;
        bool compressed = false;
        int blockBytes = 0;
    };

    [[nodiscard]] TextureFormatInfo TextureFormat(
        const int surfaceFormat)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4,
                    RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 0};
        case SurfaceFormat::Bgr565:
            return {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2,
                    RL_PIXELFORMAT_UNCOMPRESSED_R5G6B5, 0};
        case SurfaceFormat::Bgra5551:
            return {GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2,
                    RL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1, 1};
        case SurfaceFormat::Bgra4444:
            return {GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2,
                    RL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4, 4};
        case SurfaceFormat::NormalizedByte2:
            return {GL_RG8_SNORM, GL_RG, GL_BYTE, 2, 0, 0,
                    TextureFormatInfo::Swizzle::TwoChannel};
        case SurfaceFormat::NormalizedByte4:
            return {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4, 0, 0};
        case SurfaceFormat::Dxt1:
            return {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 0,
                    RL_PIXELFORMAT_COMPRESSED_DXT1_RGBA, 0,
                    TextureFormatInfo::Swizzle::Identity, true, 8};
        case SurfaceFormat::Dxt3:
            return {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, 0,
                    RL_PIXELFORMAT_COMPRESSED_DXT3_RGBA, 0,
                    TextureFormatInfo::Swizzle::Identity, true, 16};
        case SurfaceFormat::Dxt5:
            return {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 0,
                    RL_PIXELFORMAT_COMPRESSED_DXT5_RGBA, 0,
                    TextureFormatInfo::Swizzle::Identity, true, 16};
        case SurfaceFormat::Rgba1010102:
            return {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4, 0, 0};
        case SurfaceFormat::Rg32:
            return {GL_RG16, GL_RG, GL_UNSIGNED_SHORT, 4, 0, 0,
                    TextureFormatInfo::Swizzle::TwoChannel};
        case SurfaceFormat::Rgba64:
            return {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 8, 0, 0};
        case SurfaceFormat::Alpha8:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 0, 0,
                    TextureFormatInfo::Swizzle::AlphaOnly};
        case SurfaceFormat::Single:
            return {GL_R32F, GL_RED, GL_FLOAT, 4,
                    RL_PIXELFORMAT_UNCOMPRESSED_R32, 0,
                    TextureFormatInfo::Swizzle::OneChannel};
        case SurfaceFormat::Vector2:
            return {GL_RG32F, GL_RG, GL_FLOAT, 8, 0, 0,
                    TextureFormatInfo::Swizzle::TwoChannel};
        case SurfaceFormat::Vector4:
            return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16,
                    RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 0};
        case SurfaceFormat::HalfSingle:
            return {GL_R16F, GL_RED, GL_HALF_FLOAT, 2,
                    RL_PIXELFORMAT_UNCOMPRESSED_R16, 0,
                    TextureFormatInfo::Swizzle::OneChannel};
        case SurfaceFormat::HalfVector2:
            return {GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, 0, 0,
                    TextureFormatInfo::Swizzle::TwoChannel};
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8,
                    RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 0};
        default:
            throw std::runtime_error(
                "RLGL: SurfaceFormat has not passed its Texture2D implementation gate");
        }
    }

    [[nodiscard]] const std::uint8_t* ConvertPackedUpload(
        const TextureFormatInfo& format, const std::uint8_t* pixels,
        const std::size_t texelCount, std::vector<std::uint8_t>& converted)
    {
        if (format.uploadRotateLeft == 0 || pixels == nullptr) return pixels;

        converted.resize(texelCount * 2u);
        for (std::size_t index = 0; index < texelCount; ++index)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, pixels + index * 2u, sizeof(value));
            const int shift = format.uploadRotateLeft;
            value = static_cast<std::uint16_t>((value << shift) | (value >> (16 - shift)));
            std::memcpy(converted.data() + index * 2u, &value, sizeof(value));
        }
        return converted.data();
    }

    void RestorePackedReadback(
        const TextureFormatInfo& format, std::vector<std::uint8_t>& pixels)
    {
        if (format.uploadRotateLeft == 0) return;
        const int shift = format.uploadRotateLeft;
        for (std::size_t offset = 0; offset < pixels.size(); offset += 2u)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, pixels.data() + offset, sizeof(value));
            value = static_cast<std::uint16_t>((value >> shift) | (value << (16 - shift)));
            std::memcpy(pixels.data() + offset, &value, sizeof(value));
        }
    }

    void ApplyTextureSwizzle(const TextureFormatInfo::Swizzle swizzle)
    {
        if (swizzle == TextureFormatInfo::Swizzle::Identity) return;

        GLint red = GL_RED;
        GLint green = GL_GREEN;
        GLint blue = GL_BLUE;
        GLint alpha = GL_ALPHA;
        if (swizzle == TextureFormatInfo::Swizzle::OneChannel)
        {
            green = GL_ONE;
            blue = GL_ONE;
            alpha = GL_ONE;
        }
        else if (swizzle == TextureFormatInfo::Swizzle::TwoChannel)
        {
            blue = GL_ONE;
            alpha = GL_ONE;
        }
        else
        {
            red = GL_ZERO;
            green = GL_ZERO;
            blue = GL_ZERO;
            alpha = GL_RED;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, red);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, green);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, blue);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, alpha);
    }

    [[nodiscard]] std::size_t CompressedLevelBytes(
        const TextureFormatInfo& format, const int width, const int height)
    {
        return static_cast<std::size_t>((width + 3) / 4) *
            static_cast<std::size_t>((height + 3) / 4) * format.blockBytes;
    }

    [[nodiscard]] std::vector<std::uint8_t> DecodeDxt(
        const int surfaceFormat, const std::uint8_t* blocks,
        const std::size_t byteCount, const int width, const int height)
    {
        using CNA::Internal::Graphics::DxtUtil;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Dxt1:
            return DxtUtil::DecompressDxt1(blocks, byteCount, width, height);
        case SurfaceFormat::Dxt3:
            return DxtUtil::DecompressDxt3(blocks, byteCount, width, height);
        case SurfaceFormat::Dxt5:
            return DxtUtil::DecompressDxt5(blocks, byteCount, width, height);
        default:
            throw std::invalid_argument("RLGL: requested DXT decode for a non-DXT format");
        }
    }
}

namespace CNA::Internal::Renderers::Rlgl::Bridge
{
    std::string Initialize(
        const CNA::Platform::GlProcAddressLoader loader, const int width, const int height)
    {
        if (loader == nullptr)
            throw std::runtime_error("RLGL: platform returned a null OpenGL procedure loader");
        if (width <= 0 || height <= 0)
            throw std::runtime_error("RLGL: drawable dimensions must be positive at initialization");

        // rlgl's standalone interface accepts the loader as an opaque pointer and converts it back
        // to its callback type internally. No raylib window or framework initialization occurs.
        rlLoadExtensions(reinterpret_cast<void*>(loader));
        if (glad_glGetString == nullptr || GLAD_GL_VERSION_3_3 == 0)
            throw std::runtime_error("RLGL: rlgl could not load an OpenGL 3.3 dispatch table");

        const GLubyte* driverVersion = glGetString(GL_VERSION);
        if (driverVersion == nullptr)
            throw std::runtime_error("RLGL: current context returned no OpenGL version string");

        rlglInit(width, height);
        try
        {
            if (rlGetVersion() != RL_OPENGL_33)
                throw std::runtime_error("RLGL: standalone rlgl was not compiled for OpenGL 3.3");
            if (rlGetTextureIdDefault() == 0 || rlGetShaderIdDefault() == 0)
                throw std::runtime_error("RLGL: default texture or shader initialization failed");
            ThrowIfGlError("rlglInit");
            bridgeInitialized = true;
        }
        catch (...)
        {
            rlglClose();
            throw;
        }

        return reinterpret_cast<const char*>(driverVersion);
    }

    void Shutdown() noexcept
    {
        bridgeInitialized = false;
        rlglClose();
    }

    void SetFramebufferSize(const int width, const int height)
    {
        rlSetFramebufferWidth(width);
        rlSetFramebufferHeight(height);
    }

    void Clear(const unsigned int planes, const float r, const float g, const float b,
               const float a, const float depth, const int stencil)
    {
        GLbitfield mask = 0;
        GLboolean oldColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean oldDepthMask = GL_TRUE;
        GLint oldFrontStencilMask = -1;
        GLint oldBackStencilMask = -1;

        if ((planes & ColorPlane) != 0)
        {
            glGetBooleanv(GL_COLOR_WRITEMASK, oldColorMask);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(r, g, b, a);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if ((planes & DepthPlane) != 0)
        {
            glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
            glDepthMask(GL_TRUE);
            glClearDepth(depth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if ((planes & StencilPlane) != 0)
        {
            glGetIntegerv(GL_STENCIL_WRITEMASK, &oldFrontStencilMask);
            glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &oldBackStencilMask);
            glStencilMaskSeparate(GL_FRONT, ~0u);
            glStencilMaskSeparate(GL_BACK, ~0u);
            glClearStencil(stencil);
            mask |= GL_STENCIL_BUFFER_BIT;
        }

        glClear(mask);

        if ((planes & ColorPlane) != 0)
            glColorMask(oldColorMask[0], oldColorMask[1], oldColorMask[2], oldColorMask[3]);
        if ((planes & DepthPlane) != 0) glDepthMask(oldDepthMask);
        if ((planes & StencilPlane) != 0)
        {
            glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(oldFrontStencilMask));
            glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(oldBackStencilMask));
        }
    }

    void SetDepthTestEnabled(const bool enabled)
    {
        if (enabled) rlEnableDepthTest();
        else rlDisableDepthTest();
    }

    void SetBlendEnabled(const bool enabled)
    {
        if (enabled) rlEnableColorBlend();
        else rlDisableColorBlend();
    }

    void SetDepthWriteEnabled(const bool enabled)
    {
        if (enabled) rlEnableDepthMask();
        else rlDisableDepthMask();
    }

    void SetViewport(const int x, const int y, const int width, const int height,
                     const float minDepth, const float maxDepth)
    {
        rlViewport(x, y, width, height);
        // rlgl has no public depth-range wrapper; CNA's Viewport exposes both values.
        glDepthRange(static_cast<GLdouble>(minDepth), static_cast<GLdouble>(maxDepth));
    }

    void SetScissor(const int x, const int y, const int width, const int height)
    {
        rlScissor(x, y, width, height);
    }

    void ReadBackbuffer(
        const int x, const int y, const int width, const int height,
        const int framebufferHeight, unsigned char* pixels)
    {
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(
            x, framebufferHeight - y - height, width, height,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        ThrowIfGlError("backbuffer readback");

        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        std::vector<unsigned char> temporary(rowBytes);
        for (int row = 0; row < height / 2; ++row)
        {
            unsigned char* top = pixels + static_cast<std::size_t>(row) * rowBytes;
            unsigned char* bottom =
                pixels + static_cast<std::size_t>(height - row - 1) * rowBytes;
            std::memcpy(temporary.data(), top, rowBytes);
            std::memcpy(top, bottom, rowBytes);
            std::memcpy(bottom, temporary.data(), rowBytes);
        }
    }

    int GetMaxTextureSize()
    {
        RequireInitialized("texture-limit query");
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        ThrowIfGlError("GL_MAX_TEXTURE_SIZE query");
        if (value <= 0)
            throw std::runtime_error("RLGL: driver reported an invalid maximum texture size");
        return value;
    }

    bool SupportsDxtTexture2D(const int surfaceFormat) noexcept
    {
        if (!bridgeInitialized) return false;
        const char* forceFallback = std::getenv("CNA_RLGL_FORCE_DXT_FALLBACK");
        if (forceFallback != nullptr && forceFallback[0] != '\0' && forceFallback[0] != '0')
            return false;
        try
        {
            const TextureFormatInfo format = TextureFormat(surfaceFormat);
            if (!format.compressed) return false;
            unsigned int internalFormat = 0;
            unsigned int transferFormat = 0;
            unsigned int transferType = 0;
            rlGetGlTextureFormats(
                format.rlglFormat, &internalFormat, &transferFormat, &transferType);
            return internalFormat == static_cast<unsigned int>(format.internalFormat);
        }
        catch (...)
        {
            return false;
        }
    }

    unsigned int CreateTexture2D(
        const int surfaceFormat, const int width, const int height,
        const int mipLevels, const std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D creation");
        if (width <= 0 || height <= 0 || mipLevels <= 0 || pixels == nullptr)
            throw std::invalid_argument("RLGL: invalid Texture2D creation request");

        const TextureFormatInfo format = TextureFormat(surfaceFormat);
        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> converted;
        const std::uint8_t* upload = ConvertPackedUpload(
            format, pixels, static_cast<std::size_t>(width) * height, converted);
        const bool nativeCompressed =
            !format.compressed || SupportsDxtTexture2D(surfaceFormat);
        if (format.compressed && !nativeCompressed)
        {
            converted = DecodeDxt(
                surfaceFormat, pixels, CompressedLevelBytes(format, width, height),
                width, height);
            upload = converted.data();
        }
        unsigned int id = 0;
        if (format.rlglFormat != 0 && nativeCompressed)
        {
            id = rlLoadTexture(upload, width, height, format.rlglFormat, 1);
        }
        else if (format.compressed)
        {
            id = rlLoadTexture(
                upload, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        }
        else
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(
                GL_TEXTURE_2D, 0, static_cast<GLint>(format.internalFormat), width, height, 0,
                format.transferFormat, format.transferType, upload);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }
        if (id == 0)
            throw std::runtime_error("RLGL: Texture2D allocation returned a zero texture name");

        try
        {
            glBindTexture(GL_TEXTURE_2D, id);
            int levelWidth = width;
            int levelHeight = height;
            for (int level = 1; level < mipLevels; ++level)
            {
                levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
                levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
                if (format.compressed && nativeCompressed)
                {
                    const std::vector<std::uint8_t> empty(
                        CompressedLevelBytes(format, levelWidth, levelHeight), 0u);
                    glCompressedTexImage2D(
                        GL_TEXTURE_2D, level, format.internalFormat,
                        levelWidth, levelHeight, 0,
                        static_cast<GLsizei>(empty.size()), empty.data());
                }
                else
                {
                    glTexImage2D(
                        GL_TEXTURE_2D, level,
                        static_cast<GLint>(format.compressed ? GL_RGBA8
                                                            : format.internalFormat),
                        levelWidth, levelHeight, 0,
                        format.compressed ? GL_RGBA : format.transferFormat,
                        format.compressed ? GL_UNSIGNED_BYTE : format.transferType, nullptr);
                }
            }
            // rlgl has no general texture-max-level parameter. Clamp even a one-level texture so
            // XNA's mip-carrying sampler defaults never make the object incomplete.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
            ApplyTextureSwizzle(format.swizzle);
            ThrowIfGlError("Texture2D allocation");
        }
        catch (...)
        {
            rlUnloadTexture(id);
            throw;
        }
        return id;
    }

    void DestroyTexture2D(const unsigned int id) noexcept
    {
        if (bridgeInitialized && id != 0) rlUnloadTexture(id);
    }

    void UpdateTexture2D(
        const unsigned int id, const int surfaceFormat, const int level,
        const int width, const int height, const std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D update");
        if (id == 0 || level < 0 || width <= 0 || height <= 0 || pixels == nullptr)
            throw std::invalid_argument("RLGL: invalid Texture2D update request");

        const TextureFormatInfo format = TextureFormat(surfaceFormat);
        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> converted;
        const std::uint8_t* upload = ConvertPackedUpload(
            format, pixels, static_cast<std::size_t>(width) * height, converted);
        const bool nativeCompressed =
            !format.compressed || SupportsDxtTexture2D(surfaceFormat);
        if (format.compressed && !nativeCompressed)
        {
            converted = DecodeDxt(
                surfaceFormat, pixels, CompressedLevelBytes(format, width, height),
                width, height);
            upload = converted.data();
        }
        if (format.compressed && nativeCompressed)
        {
            glBindTexture(GL_TEXTURE_2D, id);
            const std::size_t byteCount = CompressedLevelBytes(format, width, height);
            glCompressedTexSubImage2D(
                GL_TEXTURE_2D, level, 0, 0, width, height,
                format.internalFormat, static_cast<GLsizei>(byteCount), upload);
        }
        else if (format.compressed && level == 0)
        {
            rlUpdateTexture(
                id, 0, 0, width, height,
                RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, upload);
        }
        else if (format.compressed)
        {
            glBindTexture(GL_TEXTURE_2D, id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(
                GL_TEXTURE_2D, level, 0, 0, width, height,
                GL_RGBA, GL_UNSIGNED_BYTE, upload);
        }
        else if (level == 0 && format.rlglFormat != 0)
        {
            // The public wrapper provides the exact level-zero subimage path.
            rlUpdateTexture(
                id, 0, 0, width, height,
                format.rlglFormat, upload);
        }
        else
        {
            // rlUpdateTexture hardcodes mip level zero. Higher declared levels use the dispatch
            // table rlgl loaded, retaining rlgl as owner of the texture name and normal bind path.
            glBindTexture(GL_TEXTURE_2D, id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(
                GL_TEXTURE_2D, level, 0, 0, width, height,
                format.transferFormat, format.transferType, upload);
        }
        ThrowIfGlError("Texture2D update");
    }

    void ReadTexture2D(
        const unsigned int id, const int surfaceFormat, const int level,
        const int levelWidth, const int levelHeight, const int x, const int y,
        const int width, const int height, std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D readback");
        if (id == 0 || level < 0 || levelWidth <= 0 || levelHeight <= 0 ||
            x < 0 || y < 0 || width <= 0 || height <= 0 ||
            x > levelWidth - width || y > levelHeight - height || pixels == nullptr)
        {
            throw std::invalid_argument("RLGL: invalid Texture2D readback request");
        }

        const TextureFormatInfo format = TextureFormat(surfaceFormat);
        const TextureBindingRestore restore;
        glBindTexture(GL_TEXTURE_2D, id);
        if (format.compressed)
        {
            if ((x % 4) != 0 || (y % 4) != 0 ||
                ((width % 4) != 0 && x + width != levelWidth) ||
                ((height % 4) != 0 && y + height != levelHeight))
            {
                throw std::invalid_argument(
                    "RLGL: compressed Texture2D readback rectangle is not block-aligned");
            }
            std::vector<std::uint8_t> levelBytes(
                CompressedLevelBytes(format, levelWidth, levelHeight));
            glGetCompressedTexImage(GL_TEXTURE_2D, level, levelBytes.data());
            ThrowIfGlError("compressed Texture2D readback");

            const int levelBlockColumns = (levelWidth + 3) / 4;
            const int rectangleBlockColumns = (width + 3) / 4;
            const int rectangleBlockRows = (height + 3) / 4;
            const std::size_t sourceRowBytes =
                static_cast<std::size_t>(levelBlockColumns) * format.blockBytes;
            const std::size_t destinationRowBytes =
                static_cast<std::size_t>(rectangleBlockColumns) * format.blockBytes;
            for (int row = 0; row < rectangleBlockRows; ++row)
            {
                const std::uint8_t* source = levelBytes.data()
                    + static_cast<std::size_t>(y / 4 + row) * sourceRowBytes
                    + static_cast<std::size_t>(x / 4) * format.blockBytes;
                std::memcpy(
                    pixels + static_cast<std::size_t>(row) * destinationRowBytes,
                    source, destinationRowBytes);
            }
            return;
        }
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        std::vector<std::uint8_t> levelPixels(
            static_cast<std::size_t>(levelWidth) * levelHeight * format.bytesPerTexel);
        glGetTexImage(
            GL_TEXTURE_2D, level, format.transferFormat, format.transferType,
            levelPixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        ThrowIfGlError("Texture2D readback");
        RestorePackedReadback(format, levelPixels);

        const std::size_t sourceRowBytes =
            static_cast<std::size_t>(levelWidth) * format.bytesPerTexel;
        const std::size_t destinationRowBytes =
            static_cast<std::size_t>(width) * format.bytesPerTexel;
        for (int row = 0; row < height; ++row)
        {
            const std::uint8_t* source = levelPixels.data()
                + static_cast<std::size_t>(y + row) * sourceRowBytes
                + static_cast<std::size_t>(x) * format.bytesPerTexel;
            std::memcpy(
                pixels + static_cast<std::size_t>(row) * destinationRowBytes,
                source, destinationRowBytes);
        }
    }

    void BindTexture2D(const unsigned int id, const int unit)
    {
        RequireInitialized("Texture2D binding");
        if (unit < 0)
            throw std::out_of_range("RLGL: texture unit must be non-negative");
        rlActiveTextureSlot(unit);
        if (id == 0) rlDisableTexture();
        else rlEnableTexture(id);
    }

    unsigned int GetBoundTexture2DForTesting(const int unit)
    {
        RequireInitialized("Texture2D binding query");
        if (unit < 0)
            throw std::out_of_range("RLGL: texture unit must be non-negative");

        GLint previousActiveTexture = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        GLint texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        ThrowIfGlError("Texture2D binding query");
        return static_cast<unsigned int>(texture);
    }

    int GetMaxSamplerSlots()
    {
        RequireInitialized("sampler-slot query");
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
        ThrowIfGlError("GL_MAX_TEXTURE_IMAGE_UNITS query");
        if (value <= 0)
            throw std::runtime_error("RLGL: driver reported no fragment texture units");
        return value;
    }

    float GetMaxSamplerAnisotropy()
    {
        RequireInitialized("sampler-anisotropy query");
        if (!RLGL.ExtSupported.texAnisoFilter) return 1.0f;
        return std::max(1.0f, RLGL.ExtSupported.maxAnisotropyLevel);
    }

    unsigned int CreateSampler()
    {
        RequireInitialized("sampler creation");
        GLuint sampler = 0;
        glGenSamplers(1, &sampler);
        ThrowIfGlError("sampler creation");
        if (sampler == 0)
            throw std::runtime_error("RLGL: OpenGL returned a zero sampler name");
        return sampler;
    }

    void DestroySamplers(const unsigned int* samplers, const std::size_t count) noexcept
    {
        if (!bridgeInitialized || samplers == nullptr || count == 0) return;
        glDeleteSamplers(static_cast<GLsizei>(count), samplers);
    }

    void ApplySampler(
        const unsigned int sampler, const int slot, const int filter,
        const int addressU, const int addressV, const int addressW,
        const int maxAnisotropy, const int maxMipLevel, const float lodBias)
    {
        RequireInitialized("sampler application");
        if (sampler == 0 || slot < 0 || slot >= GetMaxSamplerSlots())
            throw std::out_of_range("RLGL: sampler or texture slot is invalid");

        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, SamplerMinFilter(filter));
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, SamplerMagFilter(filter));
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, SamplerWrap(addressU));
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, SamplerWrap(addressV));
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, SamplerWrap(addressW));
        glSamplerParameterf(
            sampler, GL_TEXTURE_MIN_LOD, static_cast<float>(std::max(maxMipLevel, 0)));
        glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, 1000.0f);
        glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, lodBias);
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);

        if (RLGL.ExtSupported.texAnisoFilter)
        {
            const float requested = filter == 2
                ? static_cast<float>(std::max(maxAnisotropy, 1))
                : 1.0f;
            glSamplerParameterf(
                sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                std::min(requested, GetMaxSamplerAnisotropy()));
        }

        glBindSampler(static_cast<GLuint>(slot), sampler);
        ThrowIfGlError("sampler application");
    }

    SamplerSnapshot GetSamplerSnapshotForTesting(const unsigned int sampler)
    {
        RequireInitialized("sampler state query");
        if (sampler == 0)
            throw std::invalid_argument("RLGL: cannot query sampler zero");

        SamplerSnapshot snapshot;
        glGetSamplerParameteriv(sampler, GL_TEXTURE_MIN_FILTER, &snapshot.minFilter);
        glGetSamplerParameteriv(sampler, GL_TEXTURE_MAG_FILTER, &snapshot.magFilter);
        glGetSamplerParameteriv(sampler, GL_TEXTURE_WRAP_S, &snapshot.wrapS);
        glGetSamplerParameteriv(sampler, GL_TEXTURE_WRAP_T, &snapshot.wrapT);
        glGetSamplerParameteriv(sampler, GL_TEXTURE_WRAP_R, &snapshot.wrapR);
        glGetSamplerParameteriv(sampler, GL_TEXTURE_COMPARE_MODE, &snapshot.compareMode);
        glGetSamplerParameterfv(sampler, GL_TEXTURE_MIN_LOD, &snapshot.minLod);
        glGetSamplerParameterfv(sampler, GL_TEXTURE_MAX_LOD, &snapshot.maxLod);
        glGetSamplerParameterfv(sampler, GL_TEXTURE_LOD_BIAS, &snapshot.lodBias);
        if (RLGL.ExtSupported.texAnisoFilter)
        {
            glGetSamplerParameterfv(
                sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, &snapshot.anisotropy);
        }
        ThrowIfGlError("sampler state query");
        return snapshot;
    }

    unsigned int GetBoundSamplerForTesting(const int slot)
    {
        RequireInitialized("sampler binding query");
        if (slot < 0 || slot >= GetMaxSamplerSlots())
            throw std::out_of_range("RLGL: sampler slot is invalid");
        GLint sampler = 0;
        glGetIntegeri_v(GL_SAMPLER_BINDING, static_cast<GLuint>(slot), &sampler);
        ThrowIfGlError("sampler binding query");
        return static_cast<unsigned int>(sampler);
    }

    void DrawBoundTextureSampleForTesting(
        const float u, const float v, const int width, const int height)
    {
        RequireInitialized("focused sampler draw");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("RLGL: sampler test draw extent must be positive");

        GLint previousActiveTexture = GL_TEXTURE0;
        GLint texture = 0;
        GLint previousViewport[4] = {};
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        if (texture == 0)
            throw std::runtime_error("RLGL: sampler test requires a texture on unit zero");

        const GLboolean cullingWasEnabled = glIsEnabled(GL_CULL_FACE);
        rlDrawRenderBatchActive();
        rlViewport(0, 0, width, height);
        rlDisableBackfaceCulling();

        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        rlOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
        rlMatrixMode(RL_MODELVIEW);
        rlPushMatrix();
        rlLoadIdentity();

        rlSetTexture(static_cast<unsigned int>(texture));
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(u, v); rlVertex2f(0.0f, 0.0f);
        rlTexCoord2f(u, v); rlVertex2f(0.0f, static_cast<float>(height));
        rlTexCoord2f(u, v); rlVertex2f(static_cast<float>(width), static_cast<float>(height));
        rlTexCoord2f(u, v); rlVertex2f(static_cast<float>(width), 0.0f);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetTexture(0);

        rlPopMatrix();
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();
        rlMatrixMode(RL_MODELVIEW);
        if (cullingWasEnabled) rlEnableBackfaceCulling();
        glViewport(
            previousViewport[0], previousViewport[1],
            previousViewport[2], previousViewport[3]);
        ThrowIfGlError("focused sampler draw");
    }

    void BindDefaultFramebuffer()
    {
        rlDisableFramebuffer();
    }
}
