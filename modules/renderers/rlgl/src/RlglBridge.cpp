// SPDX-License-Identifier: MS-PL

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <array>
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
    unsigned int bufferUploadVertexArray = 0;

    void RlglTraceLog(int level, const char* format, ...);
}

#define TRACELOG(level, ...) RlglTraceLog(level, __VA_ARGS__)
#define RLGL_IMPLEMENTATION
#include "rlgl.h"
#undef RLGL_IMPLEMENTATION
#undef TRACELOG

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

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

    class ElementBufferScope final
    {
    public:
        ElementBufferScope()
        {
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray_);
            if (bufferUploadVertexArray == 0)
            {
                bufferUploadVertexArray = rlLoadVertexArray();
                if (bufferUploadVertexArray == 0)
                    throw std::runtime_error("RLGL: failed to create the index upload VAO");
            }
            if (!rlEnableVertexArray(bufferUploadVertexArray))
                throw std::runtime_error("RLGL: failed to bind the index upload VAO");
        }

        ~ElementBufferScope()
        {
            if (previousVertexArray_ == 0) rlDisableVertexArray();
            else (void)rlEnableVertexArray(static_cast<unsigned int>(previousVertexArray_));
        }

        ElementBufferScope(const ElementBufferScope&) = delete;
        ElementBufferScope& operator=(const ElementBufferScope&) = delete;

    private:
        GLint previousVertexArray_ = 0;
    };

    class TextureBindingRestore final
    {
    public:
        TextureBindingRestore()
        {
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture2D_);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &textureCube_);
        }

        ~TextureBindingRestore()
        {
            glActiveTexture(static_cast<GLenum>(activeTexture_));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture2D_));
            glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(textureCube_));
        }

        TextureBindingRestore(const TextureBindingRestore&) = delete;
        TextureBindingRestore& operator=(const TextureBindingRestore&) = delete;

    private:
        GLint activeTexture_ = GL_TEXTURE0;
        GLint texture2D_ = 0;
        GLint textureCube_ = 0;
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

    class FramebufferBindingRestore final
    {
    public:
        FramebufferBindingRestore()
        {
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        }

        ~FramebufferBindingRestore()
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
        }

        FramebufferBindingRestore(const FramebufferBindingRestore&) = delete;
        FramebufferBindingRestore& operator=(const FramebufferBindingRestore&) = delete;

        [[nodiscard]] unsigned int DrawFramebuffer() const noexcept
        {
            return static_cast<unsigned int>(drawFramebuffer_);
        }

        void ForgetDeletedFramebuffer(const unsigned int framebuffer) noexcept
        {
            if (drawFramebuffer_ == static_cast<GLint>(framebuffer)) drawFramebuffer_ = 0;
            if (readFramebuffer_ == static_cast<GLint>(framebuffer)) readFramebuffer_ = 0;
        }

    private:
        GLint drawFramebuffer_ = 0;
        GLint readFramebuffer_ = 0;
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

    [[nodiscard]] GLenum GlBlendFactor(const int blend)
    {
        switch (blend)
        {
        case 0: return GL_ONE;
        case 1: return GL_ZERO;
        case 2: return GL_SRC_COLOR;
        case 3: return GL_ONE_MINUS_SRC_COLOR;
        case 4: return GL_SRC_ALPHA;
        case 5: return GL_ONE_MINUS_SRC_ALPHA;
        case 6: return GL_DST_COLOR;
        case 7: return GL_ONE_MINUS_DST_COLOR;
        case 8: return GL_DST_ALPHA;
        case 9: return GL_ONE_MINUS_DST_ALPHA;
        case 10: return GL_CONSTANT_COLOR;
        case 11: return GL_ONE_MINUS_CONSTANT_COLOR;
        case 12: return GL_SRC_ALPHA_SATURATE;
        default:
            throw std::invalid_argument("RLGL: invalid Blend ordinal");
        }
    }

    [[nodiscard]] GLenum GlBlendEquation(const int function)
    {
        switch (function)
        {
        case 0: return GL_FUNC_ADD;
        case 1: return GL_FUNC_SUBTRACT;
        case 2: return GL_FUNC_REVERSE_SUBTRACT;
        case 3: return GL_MAX;
        case 4: return GL_MIN;
        default:
            throw std::invalid_argument("RLGL: invalid BlendFunction ordinal");
        }
    }

    [[nodiscard]] GLenum GlCompareFunction(const int function)
    {
        switch (function)
        {
        case 0: return GL_ALWAYS;
        case 1: return GL_NEVER;
        case 2: return GL_LESS;
        case 3: return GL_LEQUAL;
        case 4: return GL_EQUAL;
        case 5: return GL_GEQUAL;
        case 6: return GL_GREATER;
        case 7: return GL_NOTEQUAL;
        default:
            throw std::invalid_argument("RLGL: invalid CompareFunction ordinal");
        }
    }

    [[nodiscard]] GLenum GlStencilOperation(const int operation)
    {
        switch (operation)
        {
        case 0: return GL_KEEP;
        case 1: return GL_ZERO;
        case 2: return GL_REPLACE;
        case 3: return GL_INCR_WRAP;
        case 4: return GL_DECR_WRAP;
        case 5: return GL_INCR;
        case 6: return GL_DECR;
        case 7: return GL_INVERT;
        default:
            throw std::invalid_argument("RLGL: invalid StencilOperation ordinal");
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

    [[nodiscard]] bool IsClassicRenderTargetFormat(const int surfaceFormat) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
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
            return true;
        default:
            return false;
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
    namespace
    {
        PrimitiveDrawSnapshot lastPrimitiveDraw;
    }

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
        if (bufferUploadVertexArray != 0)
        {
            rlUnloadVertexArray(bufferUploadVertexArray);
            bufferUploadVertexArray = 0;
        }
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
        std::array<std::array<GLboolean, 4>, 4> oldColorMasks{};
        GLboolean oldDepthMask = GL_TRUE;
        GLint oldFrontStencilMask = -1;
        GLint oldBackStencilMask = -1;

        if ((planes & ColorPlane) != 0)
        {
            for (unsigned int slot = 0; slot < oldColorMasks.size(); ++slot)
            {
                glGetBooleani_v(GL_COLOR_WRITEMASK, slot, oldColorMasks[slot].data());
                glColorMaski(slot, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            }
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
        {
            for (unsigned int slot = 0; slot < oldColorMasks.size(); ++slot)
            {
                const auto& colorMask = oldColorMasks[slot];
                glColorMaski(
                    slot, colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
            }
        }
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

    void ApplyBlendState(
        const int colorSourceBlend, const int alphaSourceBlend,
        const int colorDestinationBlend, const int alphaDestinationBlend,
        const int colorBlendFunction, const int alphaBlendFunction,
        const int* colorWriteMasks, const unsigned int sampleMask)
    {
        RequireInitialized("blend-state application");
        if (colorWriteMasks == nullptr)
            throw std::invalid_argument("RLGL: color write masks must not be null");

        const bool blendEnabled = !(colorSourceBlend == 0 && colorDestinationBlend == 1 &&
                                    alphaSourceBlend == 0 && alphaDestinationBlend == 1);
        if (blendEnabled)
        {
            // The public rlgl custom-separate route updates rlgl's cache before applying the
            // exact XNA RGB/alpha factors and equations.
            rlSetBlendFactorsSeparate(
                static_cast<int>(GlBlendFactor(colorSourceBlend)),
                static_cast<int>(GlBlendFactor(colorDestinationBlend)),
                static_cast<int>(GlBlendFactor(alphaSourceBlend)),
                static_cast<int>(GlBlendFactor(alphaDestinationBlend)),
                static_cast<int>(GlBlendEquation(colorBlendFunction)),
                static_cast<int>(GlBlendEquation(alphaBlendFunction)));
            rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
            rlEnableColorBlend();
        }
        else
        {
            rlDisableColorBlend();
        }

        // rlColorMask exposes only attachment zero. GL 3.3 core provides the four independent
        // masks carried by CNA's BlendWriteState, so the bridge handles that measured gap.
        for (unsigned int index = 0; index < 4; ++index)
        {
            const int mask = colorWriteMasks[index];
            glColorMaski(
                index, (mask & 1) != 0, (mask & 2) != 0,
                (mask & 4) != 0, (mask & 8) != 0);
        }

        glSampleMaski(0, sampleMask);
        if (sampleMask == 0xFFFFFFFFu) glDisable(GL_SAMPLE_MASK);
        else glEnable(GL_SAMPLE_MASK);
        ThrowIfGlError("blend-state application");
    }

    void SetBlendFactor(
        const float r, const float g, const float b, const float a)
    {
        RequireInitialized("blend-factor application");
        glBlendColor(r, g, b, a);
        ThrowIfGlError("blend-factor application");
    }

    void ApplyDepthStencilState(
        const bool depthEnable, const bool depthWriteEnable, const int depthFunction,
        const bool stencilEnable, const int stencilFunction,
        const int stencilPass, const int stencilFail, const int stencilDepthFail,
        const int stencilReadMask, const int stencilWriteMask, const int referenceStencil,
        const bool twoSidedStencilMode, const int counterClockwiseStencilFunction,
        const int counterClockwiseStencilPass, const int counterClockwiseStencilFail,
        const int counterClockwiseStencilDepthFail)
    {
        RequireInitialized("depth/stencil-state application");
        if (depthEnable)
        {
            rlEnableDepthTest();
            glDepthFunc(GlCompareFunction(depthFunction));
        }
        else
        {
            rlDisableDepthTest();
        }
        if (depthWriteEnable) rlEnableDepthMask();
        else rlDisableDepthMask();

        if (!stencilEnable)
        {
            glDisable(GL_STENCIL_TEST);
            ThrowIfGlError("depth/stencil-state application");
            return;
        }

        glEnable(GL_STENCIL_TEST);
        const GLenum frontFail = GlStencilOperation(stencilFail);
        const GLenum frontDepthFail = GlStencilOperation(stencilDepthFail);
        const GLenum frontPass = GlStencilOperation(stencilPass);
        if (twoSidedStencilMode)
        {
            glStencilFuncSeparate(
                GL_FRONT, GlCompareFunction(stencilFunction),
                referenceStencil, static_cast<GLuint>(stencilReadMask));
            glStencilOpSeparate(GL_FRONT, frontFail, frontDepthFail, frontPass);
            glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(stencilWriteMask));
            glStencilFuncSeparate(
                GL_BACK, GlCompareFunction(counterClockwiseStencilFunction),
                referenceStencil, static_cast<GLuint>(stencilReadMask));
            glStencilOpSeparate(
                GL_BACK, GlStencilOperation(counterClockwiseStencilFail),
                GlStencilOperation(counterClockwiseStencilDepthFail),
                GlStencilOperation(counterClockwiseStencilPass));
            glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(stencilWriteMask));
        }
        else
        {
            glStencilFunc(
                GlCompareFunction(stencilFunction), referenceStencil,
                static_cast<GLuint>(stencilReadMask));
            glStencilOp(frontFail, frontDepthFail, frontPass);
            glStencilMask(static_cast<GLuint>(stencilWriteMask));
        }
        ThrowIfGlError("depth/stencil-state application");
    }

    void SetStencilReference(
        const bool stencilEnable, const bool twoSidedStencilMode,
        const int stencilFunction, const int counterClockwiseStencilFunction,
        const int stencilReadMask, const int referenceStencil)
    {
        RequireInitialized("reference-stencil application");
        if (!stencilEnable) return;
        if (twoSidedStencilMode)
        {
            glStencilFuncSeparate(
                GL_FRONT, GlCompareFunction(stencilFunction),
                referenceStencil, static_cast<GLuint>(stencilReadMask));
            glStencilFuncSeparate(
                GL_BACK, GlCompareFunction(counterClockwiseStencilFunction),
                referenceStencil, static_cast<GLuint>(stencilReadMask));
        }
        else
        {
            glStencilFunc(
                GlCompareFunction(stencilFunction), referenceStencil,
                static_cast<GLuint>(stencilReadMask));
        }
        ThrowIfGlError("reference-stencil application");
    }

    void ApplyRasterizerState(
        const int cullMode, const int fillMode, const bool scissorTestEnable,
        const float depthBiasUnits, const float slopeScaleDepthBias)
    {
        RequireInitialized("rasterizer-state application");
        if (cullMode == 0)
        {
            rlDisableBackfaceCulling();
        }
        else if (cullMode == 1 || cullMode == 2)
        {
            rlEnableBackfaceCulling();
            rlSetCullFace(cullMode == 1 ? RL_CULL_FACE_BACK : RL_CULL_FACE_FRONT);
        }
        else
        {
            throw std::invalid_argument("RLGL: invalid CullMode ordinal");
        }

        if (fillMode == 0) rlDisableWireMode();
        else if (fillMode == 1) rlEnableWireMode();
        else throw std::invalid_argument("RLGL: invalid FillMode ordinal");

        if (scissorTestEnable) rlEnableScissorTest();
        else rlDisableScissorTest();

        // rlgl has no polygon-offset wrapper. Always-on zero offset is a true no-op and matches
        // EasyGL's deterministic state policy.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(slopeScaleDepthBias, depthBiasUnits);
        ThrowIfGlError("rasterizer-state application");
    }

    PipelineSnapshot GetPipelineSnapshotForTesting()
    {
        RequireInitialized("pipeline-state query");
        PipelineSnapshot snapshot;
        snapshot.blendEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
        glGetIntegerv(GL_BLEND_SRC_RGB, &snapshot.colorSourceBlend);
        glGetIntegerv(GL_BLEND_DST_RGB, &snapshot.colorDestinationBlend);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &snapshot.alphaSourceBlend);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &snapshot.alphaDestinationBlend);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &snapshot.colorBlendFunction);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &snapshot.alphaBlendFunction);
        for (unsigned int index = 0; index < snapshot.colorWriteMasks.size(); ++index)
        {
            GLboolean mask[4] = {};
            glGetBooleani_v(GL_COLOR_WRITEMASK, index, mask);
            snapshot.colorWriteMasks[index] =
                (mask[0] ? 1 : 0) | (mask[1] ? 2 : 0) |
                (mask[2] ? 4 : 0) | (mask[3] ? 8 : 0);
        }
        snapshot.sampleMaskEnabled = glIsEnabled(GL_SAMPLE_MASK) == GL_TRUE;
        GLint sampleMask = 0;
        glGetIntegeri_v(GL_SAMPLE_MASK_VALUE, 0, &sampleMask);
        snapshot.sampleMask = static_cast<unsigned int>(sampleMask);
        glGetFloatv(GL_BLEND_COLOR, snapshot.blendFactor.data());

        snapshot.depthTestEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        GLboolean depthWrite = GL_FALSE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        snapshot.depthWriteEnabled = depthWrite == GL_TRUE;
        glGetIntegerv(GL_DEPTH_FUNC, &snapshot.depthFunction);

        snapshot.stencilTestEnabled = glIsEnabled(GL_STENCIL_TEST) == GL_TRUE;
        glGetIntegerv(GL_STENCIL_FUNC, &snapshot.frontStencilFunction);
        glGetIntegerv(GL_STENCIL_REF, &snapshot.frontStencilReference);
        GLint integerValue = 0;
        glGetIntegerv(GL_STENCIL_VALUE_MASK, &integerValue);
        snapshot.frontStencilReadMask = static_cast<unsigned int>(integerValue);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &integerValue);
        snapshot.frontStencilWriteMask = static_cast<unsigned int>(integerValue);
        glGetIntegerv(GL_STENCIL_FAIL, &snapshot.frontStencilFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &snapshot.frontStencilDepthFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &snapshot.frontStencilPass);
        glGetIntegerv(GL_STENCIL_BACK_FUNC, &snapshot.backStencilFunction);
        glGetIntegerv(GL_STENCIL_BACK_REF, &snapshot.backStencilReference);
        glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &integerValue);
        snapshot.backStencilReadMask = static_cast<unsigned int>(integerValue);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &integerValue);
        snapshot.backStencilWriteMask = static_cast<unsigned int>(integerValue);
        glGetIntegerv(GL_STENCIL_BACK_FAIL, &snapshot.backStencilFail);
        glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &snapshot.backStencilDepthFail);
        glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &snapshot.backStencilPass);

        snapshot.cullEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        glGetIntegerv(GL_CULL_FACE_MODE, &snapshot.cullFace);
        snapshot.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
        glGetIntegerv(GL_SCISSOR_BOX, snapshot.scissorBox.data());
        GLint polygonModes[2] = {};
        glGetIntegerv(GL_POLYGON_MODE, polygonModes);
        snapshot.polygonMode = polygonModes[0];
        snapshot.polygonOffsetFillEnabled =
            glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE;
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &snapshot.polygonOffsetFactor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &snapshot.polygonOffsetUnits);
        ThrowIfGlError("pipeline-state query");
        return snapshot;
    }

    SpritePipeline CreateSpritePipeline(
        const int vertexCapacity, const int indexCapacity)
    {
        RequireInitialized("SpriteBatch pipeline creation");
        if (vertexCapacity <= 0 || indexCapacity <= 0)
            throw std::invalid_argument("RLGL: SpriteBatch capacities must be positive");

        static constexpr const char* vertexShader = R"(#version 330 core
layout(location = 0) in vec2 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec4 vertexColor;

uniform mat4 projection;

out vec2 fragmentTexCoord;
out vec4 fragmentColor;

void main()
{
    gl_Position = projection * vec4(vertexPosition, 0.0, 1.0);
    fragmentTexCoord = vertexTexCoord;
    fragmentColor = vertexColor;
}
)";
        static constexpr const char* fragmentShader = R"(#version 330 core
in vec2 fragmentTexCoord;
in vec4 fragmentColor;

uniform sampler2D texture0;

out vec4 finalColor;

void main()
{
    finalColor = texture(texture0, fragmentTexCoord) * fragmentColor;
}
)";

        SpritePipeline pipeline;
        pipeline.vertexCapacity = vertexCapacity;
        pipeline.indexCapacity = indexCapacity;
        try
        {
            pipeline.program = rlLoadShaderProgram(vertexShader, fragmentShader);
            if (pipeline.program == 0 || pipeline.program == rlGetShaderIdDefault())
                throw std::runtime_error("RLGL: SpriteBatch shader creation failed");

            pipeline.projectionLocation =
                rlGetLocationUniform(pipeline.program, "projection");
            pipeline.textureLocation =
                rlGetLocationUniform(pipeline.program, "texture0");
            if (pipeline.projectionLocation < 0 || pipeline.textureLocation < 0)
                throw std::runtime_error("RLGL: SpriteBatch shader uniforms are incomplete");

            pipeline.vertexArray = rlLoadVertexArray();
            if (pipeline.vertexArray == 0 || !rlEnableVertexArray(pipeline.vertexArray))
                throw std::runtime_error("RLGL: SpriteBatch requires a vertex array object");

            constexpr int vertexStride = 8 * static_cast<int>(sizeof(float));
            pipeline.vertexBuffer = rlLoadVertexBuffer(
                nullptr, vertexCapacity * vertexStride, true);
            pipeline.indexBuffer = rlLoadVertexBufferElement(
                nullptr, indexCapacity * static_cast<int>(sizeof(std::uint16_t)), true);
            if (pipeline.vertexBuffer == 0 || pipeline.indexBuffer == 0)
                throw std::runtime_error("RLGL: SpriteBatch buffer creation failed");

            rlEnableVertexBuffer(pipeline.vertexBuffer);
            rlEnableVertexAttribute(0);
            rlSetVertexAttribute(0, 2, RL_FLOAT, false, vertexStride, 0);
            rlEnableVertexAttribute(1);
            rlSetVertexAttribute(
                1, 2, RL_FLOAT, false, vertexStride,
                2 * static_cast<int>(sizeof(float)));
            rlEnableVertexAttribute(2);
            rlSetVertexAttribute(
                2, 4, RL_FLOAT, false, vertexStride,
                4 * static_cast<int>(sizeof(float)));
            rlEnableVertexBufferElement(pipeline.indexBuffer);
            rlDisableVertexArray();
            rlDisableVertexBuffer();
            rlDisableVertexBufferElement();
            ThrowIfGlError("SpriteBatch pipeline creation");
            return pipeline;
        }
        catch (...)
        {
            rlDisableVertexArray();
            DestroySpritePipeline(pipeline);
            throw;
        }
    }

    void DestroySpritePipeline(SpritePipeline& pipeline) noexcept
    {
        if (bridgeInitialized)
        {
            if (pipeline.indexBuffer != 0) rlUnloadVertexBuffer(pipeline.indexBuffer);
            if (pipeline.vertexBuffer != 0) rlUnloadVertexBuffer(pipeline.vertexBuffer);
            if (pipeline.vertexArray != 0) rlUnloadVertexArray(pipeline.vertexArray);
            if (pipeline.program != 0 && pipeline.program != rlGetShaderIdDefault())
                rlUnloadShaderProgram(pipeline.program);
        }
        pipeline = {};
    }

    void FlushImmediateBatch()
    {
        RequireInitialized("immediate-batch flush");
        rlDrawRenderBatchActive();
        ThrowIfGlError("immediate-batch flush");
    }

    void DrawSpriteGeometry(
        const SpritePipeline& pipeline,
        const float* vertices, const int vertexCount,
        const std::uint16_t* indices, const int indexCount,
        const float* projectionColumnMajor)
    {
        RequireInitialized("SpriteBatch draw");
        if (pipeline.program == 0 || pipeline.vertexArray == 0 ||
            pipeline.vertexBuffer == 0 || pipeline.indexBuffer == 0)
        {
            throw std::runtime_error("RLGL: SpriteBatch pipeline is not complete");
        }
        if (vertices == nullptr || indices == nullptr || projectionColumnMajor == nullptr ||
            vertexCount <= 0 || vertexCount > pipeline.vertexCapacity ||
            indexCount <= 0 || indexCount > pipeline.indexCapacity)
        {
            throw std::out_of_range("RLGL: SpriteBatch upload exceeds its pipeline capacity");
        }

        rlEnableShader(pipeline.program);

        ::Matrix projection{};
        projection.m0 = projectionColumnMajor[0];
        projection.m1 = projectionColumnMajor[1];
        projection.m2 = projectionColumnMajor[2];
        projection.m3 = projectionColumnMajor[3];
        projection.m4 = projectionColumnMajor[4];
        projection.m5 = projectionColumnMajor[5];
        projection.m6 = projectionColumnMajor[6];
        projection.m7 = projectionColumnMajor[7];
        projection.m8 = projectionColumnMajor[8];
        projection.m9 = projectionColumnMajor[9];
        projection.m10 = projectionColumnMajor[10];
        projection.m11 = projectionColumnMajor[11];
        projection.m12 = projectionColumnMajor[12];
        projection.m13 = projectionColumnMajor[13];
        projection.m14 = projectionColumnMajor[14];
        projection.m15 = projectionColumnMajor[15];
        rlSetUniformMatrix(pipeline.projectionLocation, projection);
        constexpr int textureUnit = 0;
        rlSetUniform(
            pipeline.textureLocation, &textureUnit, RL_SHADER_UNIFORM_INT, 1);

        if (!rlEnableVertexArray(pipeline.vertexArray))
        {
            rlDisableShader();
            throw std::runtime_error("RLGL: SpriteBatch vertex array became unavailable");
        }
        rlUpdateVertexBuffer(
            pipeline.vertexBuffer, vertices,
            vertexCount * 8 * static_cast<int>(sizeof(float)), 0);
        rlUpdateVertexBufferElements(
            pipeline.indexBuffer, indices,
            indexCount * static_cast<int>(sizeof(std::uint16_t)), 0);
        rlDrawVertexArrayElements(0, indexCount, nullptr);
        rlDisableVertexArray();
        rlDisableVertexBuffer();
        rlDisableShader();
        ThrowIfGlError("SpriteBatch draw");
    }

    unsigned int CreateVertexBuffer(const int byteCapacity)
    {
        RequireInitialized("vertex-buffer creation");
        if (byteCapacity < 0)
            throw std::invalid_argument("RLGL: vertex-buffer capacity must be non-negative");

        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        const unsigned int id = rlLoadVertexBuffer(nullptr, byteCapacity, true);
        rlEnableVertexBuffer(static_cast<unsigned int>(previous));
        ThrowIfGlError("vertex-buffer creation");
        if (id == 0)
            throw std::runtime_error("RLGL: vertex-buffer creation returned no buffer name");
        return id;
    }

    unsigned int CreateIndexBuffer(const int byteCapacity)
    {
        RequireInitialized("index-buffer creation");
        if (byteCapacity < 0)
            throw std::invalid_argument("RLGL: index-buffer capacity must be non-negative");

        const ElementBufferScope vertexArray;
        GLint previous = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous);
        const unsigned int id = rlLoadVertexBufferElement(nullptr, byteCapacity, true);
        rlEnableVertexBufferElement(static_cast<unsigned int>(previous));
        ThrowIfGlError("index-buffer creation");
        if (id == 0)
            throw std::runtime_error("RLGL: index-buffer creation returned no buffer name");
        return id;
    }

    void DestroyBuffer(const unsigned int id) noexcept
    {
        if (bridgeInitialized && id != 0) rlUnloadVertexBuffer(id);
    }

    void UpdateVertexBuffer(
        const unsigned int id, const void* const data, const int byteCount)
    {
        RequireInitialized("vertex-buffer update");
        if (id == 0 || data == nullptr || byteCount <= 0)
            throw std::invalid_argument("RLGL: invalid vertex-buffer update");

        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        rlUpdateVertexBuffer(id, data, byteCount, 0);
        rlEnableVertexBuffer(static_cast<unsigned int>(previous));
        ThrowIfGlError("vertex-buffer update");
    }

    void UpdateIndexBuffer(
        const unsigned int id, const void* const data, const int byteCount)
    {
        RequireInitialized("index-buffer update");
        if (id == 0 || data == nullptr || byteCount <= 0)
            throw std::invalid_argument("RLGL: invalid index-buffer update");

        const ElementBufferScope vertexArray;
        GLint previous = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous);
        rlUpdateVertexBufferElements(id, data, byteCount, 0);
        rlEnableVertexBufferElement(static_cast<unsigned int>(previous));
        ThrowIfGlError("index-buffer update");
    }

    void OrphanBuffer(
        const unsigned int id, const bool indexBuffer, const int byteCapacity)
    {
        RequireInitialized("buffer orphan");
        if (id == 0 || byteCapacity < 0)
            throw std::invalid_argument("RLGL: invalid buffer orphan request");

        if (indexBuffer)
        {
            const ElementBufferScope vertexArray;
            GLint previous = 0;
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous);
            rlEnableVertexBufferElement(id);
            // rlgl 6.0 exposes subrange updates but no storage-orphaning wrapper. Discard needs
            // fresh driver storage while retaining the renderer resource's stable buffer name.
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCapacity, nullptr, GL_DYNAMIC_DRAW);
            rlEnableVertexBufferElement(static_cast<unsigned int>(previous));
        }
        else
        {
            GLint previous = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
            rlEnableVertexBuffer(id);
            glBufferData(GL_ARRAY_BUFFER, byteCapacity, nullptr, GL_DYNAMIC_DRAW);
            rlEnableVertexBuffer(static_cast<unsigned int>(previous));
        }
        ThrowIfGlError("buffer orphan");
    }

    BufferSnapshot GetBufferSnapshotForTesting(
        const unsigned int id, const bool indexBuffer)
    {
        RequireInitialized("buffer snapshot");
        if (id == 0)
            throw std::invalid_argument("RLGL: cannot snapshot buffer zero");

        BufferSnapshot snapshot;
        const auto capture = [&](const GLenum target)
        {
            glGetBufferParameteriv(target, GL_BUFFER_SIZE, &snapshot.byteSize);
            glGetBufferParameteriv(target, GL_BUFFER_USAGE, &snapshot.usage);
            if (snapshot.byteSize > 0)
            {
                snapshot.bytes.resize(static_cast<std::size_t>(snapshot.byteSize));
                glGetBufferSubData(target, 0, snapshot.byteSize, snapshot.bytes.data());
            }
        };

        if (indexBuffer)
        {
            const ElementBufferScope vertexArray;
            GLint previous = 0;
            glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previous);
            rlEnableVertexBufferElement(id);
            capture(GL_ELEMENT_ARRAY_BUFFER);
            rlEnableVertexBufferElement(static_cast<unsigned int>(previous));
        }
        else
        {
            GLint previous = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
            rlEnableVertexBuffer(id);
            capture(GL_ARRAY_BUFFER);
            rlEnableVertexBuffer(static_cast<unsigned int>(previous));
        }
        ThrowIfGlError("buffer snapshot");
        return snapshot;
    }

    PrimitivePipeline CreatePrimitivePipeline()
    {
        RequireInitialized("primitive pipeline creation");
        static constexpr const char* vertexShader = R"(#version 330 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec4 vertexColor;
layout(location = 2) in vec2 vertexTexCoord;
layout(location = 3) in vec3 vertexNormal;
layout(location = 4) in vec2 vertexTexCoord1;
layout(location = 5) in vec4 vertexBoneWeights;
layout(location = 6) in vec4 vertexBoneIndices;

uniform mat4 worldViewProjection;
uniform mat4 world;
uniform vec3 normalMatrix0;
uniform vec3 normalMatrix1;
uniform vec3 normalMatrix2;
uniform vec4 diffuseColor;
uniform float vertexColorEnabled;
uniform vec4 fogVector;
uniform float lightingEnabled;
uniform vec3 ambientColor;
uniform vec3 emissiveColor;
uniform vec3 eyePosition;
uniform vec3 light0Direction;
uniform vec3 light1Direction;
uniform vec3 light2Direction;
uniform vec3 light0Diffuse;
uniform vec3 light1Diffuse;
uniform vec3 light2Diffuse;
uniform vec3 light0Specular;
uniform vec3 light1Specular;
uniform vec3 light2Specular;
uniform vec3 specularColor;
uniform float specularPower;
uniform float skinned;
uniform vec4 boneRows[216];
uniform int weightsPerVertex;

out vec4 fragmentVertexColor;
out vec2 fragmentTexCoord;
out vec2 fragmentTexCoord1;
out float fragmentFogFactor;
out vec3 fragmentWorldPosition;
out vec3 fragmentNormal;
out vec3 fragmentVertexLitRgb;
out vec3 fragmentVertexSpecularRgb;
out float fragmentVertexAlpha;

void addBoneRows(int boneIndex, float weight,
                 inout vec4 row0, inout vec4 row1, inout vec4 row2)
{
    int base = boneIndex * 3;
    row0 += boneRows[base] * weight;
    row1 += boneRows[base + 1] * weight;
    row2 += boneRows[base + 2] * weight;
}

vec3 transformSkinNormal(mat3 matrix, vec3 normal)
{
    vec3 column0 = matrix[0];
    vec3 column1 = matrix[1];
    vec3 column2 = matrix[2];
    vec3 cofactor0 = cross(column1, column2);
    vec3 cofactor1 = cross(column2, column0);
    vec3 cofactor2 = cross(column0, column1);
    float determinant = dot(column0, cofactor0);
    vec3 transformed = mat3(cofactor0, cofactor1, cofactor2) * normal;
    return (abs(determinant) > 1e-6)
        ? transformed * sign(determinant) : matrix * normal;
}

void computeLights(vec3 worldPosition, vec3 normal, out vec3 litRgb, out vec3 specularRgb)
{
    vec3 eye = normalize(eyePosition - worldPosition);
    float dotL0 = dot(normal, -light0Direction);
    float dotL1 = dot(normal, -light1Direction);
    float dotL2 = dot(normal, -light2Direction);
    float zeroL0 = step(0.0, dotL0);
    float zeroL1 = step(0.0, dotL1);
    float zeroL2 = step(0.0, dotL2);
    vec3 lightSum = ambientColor +
        light0Diffuse * max(dotL0, 0.0) +
        light1Diffuse * max(dotL1, 0.0) +
        light2Diffuse * max(dotL2, 0.0);
    litRgb = lightSum * diffuseColor.rgb + emissiveColor;
    vec3 half0 = normalize(eye - light0Direction);
    vec3 half1 = normalize(eye - light1Direction);
    vec3 half2 = normalize(eye - light2Direction);
    float spec0 = pow(max(dot(half0, normal), 0.0) * zeroL0, specularPower);
    float spec1 = pow(max(dot(half1, normal), 0.0) * zeroL1, specularPower);
    float spec2 = pow(max(dot(half2, normal), 0.0) * zeroL2, specularPower);
    specularRgb = (spec0 * light0Specular + spec1 * light1Specular +
        spec2 * light2Specular) * specularColor;
}

void main()
{
    vec3 effectPosition = vertexPosition;
    vec3 effectNormal = vertexNormal;
    if (skinned > 0.5)
    {
        vec4 skinRow0 = vec4(0.0);
        vec4 skinRow1 = vec4(0.0);
        vec4 skinRow2 = vec4(0.0);
        addBoneRows(int(vertexBoneIndices.x), vertexBoneWeights.x,
                    skinRow0, skinRow1, skinRow2);
        if (weightsPerVertex >= 2)
        {
            addBoneRows(int(vertexBoneIndices.y), vertexBoneWeights.y,
                        skinRow0, skinRow1, skinRow2);
        }
        if (weightsPerVertex >= 4)
        {
            addBoneRows(int(vertexBoneIndices.z), vertexBoneWeights.z,
                        skinRow0, skinRow1, skinRow2);
            addBoneRows(int(vertexBoneIndices.w), vertexBoneWeights.w,
                        skinRow0, skinRow1, skinRow2);
        }
        vec4 position = vec4(vertexPosition, 1.0);
        effectPosition = vec3(
            dot(skinRow0, position),
            dot(skinRow1, position),
            dot(skinRow2, position));
        mat3 skinMatrix = mat3(
            vec3(skinRow0.x, skinRow1.x, skinRow2.x),
            vec3(skinRow0.y, skinRow1.y, skinRow2.y),
            vec3(skinRow0.z, skinRow1.z, skinRow2.z));
        vec3 skinNormal = transformSkinNormal(skinMatrix, vertexNormal);
        float skinNormalLength = length(skinNormal);
        effectNormal = skinNormalLength > 1e-6
            ? skinNormal / skinNormalLength : vertexNormal;
    }
    gl_Position = worldViewProjection * vec4(effectPosition, 1.0);
    gl_PointSize = 1.0;
    fragmentVertexColor = (vertexColorEnabled > 0.5) ? vertexColor : vec4(1.0);
    fragmentTexCoord = vertexTexCoord;
    fragmentTexCoord1 = vertexTexCoord1;
    fragmentFogFactor = 1.0 - clamp(
        dot(vec4(effectPosition, 1.0), fogVector), 0.0, 1.0);
    fragmentWorldPosition = (world * vec4(effectPosition, 1.0)).xyz;
    mat3 normalMatrix = mat3(normalMatrix0, normalMatrix1, normalMatrix2);
    fragmentNormal = normalMatrix * effectNormal;
    fragmentVertexLitRgb = vec3(0.0);
    fragmentVertexSpecularRgb = vec3(0.0);
    fragmentVertexAlpha = diffuseColor.a * fragmentVertexColor.a;
    if (lightingEnabled > 0.5)
    {
        vec3 litRgb;
        vec3 specularRgb;
        computeLights(fragmentWorldPosition, normalize(fragmentNormal), litRgb, specularRgb);
        // D3D9's oD0/oD1 colour outputs saturate before interpolation. XNA depends on that
        // ordering when multiple light/material terms exceed one at only some vertices.
        fragmentVertexLitRgb = clamp(litRgb * fragmentVertexColor.rgb, 0.0, 1.0);
        fragmentVertexSpecularRgb = clamp(specularRgb, 0.0, 1.0);
    }
}
)";
        static constexpr const char* fragmentShader = R"(#version 330 core
in vec4 fragmentVertexColor;
in vec2 fragmentTexCoord;
in vec2 fragmentTexCoord1;
in float fragmentFogFactor;
in vec3 fragmentWorldPosition;
in vec3 fragmentNormal;
in vec3 fragmentVertexLitRgb;
in vec3 fragmentVertexSpecularRgb;
in float fragmentVertexAlpha;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec2 textureFlipV;
uniform float textureEnabled;
uniform float dualTexture;
uniform vec4 alphaTest;
uniform vec3 fogColor;
uniform vec4 diffuseColor;
uniform float lightingEnabled;
uniform float preferPerPixelLighting;
uniform vec3 ambientColor;
uniform vec3 emissiveColor;
uniform vec3 eyePosition;
uniform vec3 light0Direction;
uniform vec3 light1Direction;
uniform vec3 light2Direction;
uniform vec3 light0Diffuse;
uniform vec3 light1Diffuse;
uniform vec3 light2Diffuse;
uniform vec3 light0Specular;
uniform vec3 light1Specular;
uniform vec3 light2Specular;
uniform vec3 specularColor;
uniform float specularPower;

out vec4 finalColor;

void computeLights(vec3 worldPosition, vec3 normal, out vec3 litRgb, out vec3 specularRgb)
{
    vec3 eye = normalize(eyePosition - worldPosition);
    float dotL0 = dot(normal, -light0Direction);
    float dotL1 = dot(normal, -light1Direction);
    float dotL2 = dot(normal, -light2Direction);
    float zeroL0 = step(0.0, dotL0);
    float zeroL1 = step(0.0, dotL1);
    float zeroL2 = step(0.0, dotL2);
    vec3 lightSum = ambientColor +
        light0Diffuse * max(dotL0, 0.0) +
        light1Diffuse * max(dotL1, 0.0) +
        light2Diffuse * max(dotL2, 0.0);
    litRgb = lightSum * diffuseColor.rgb + emissiveColor;
    vec3 half0 = normalize(eye - light0Direction);
    vec3 half1 = normalize(eye - light1Direction);
    vec3 half2 = normalize(eye - light2Direction);
    float spec0 = pow(max(dot(half0, normal), 0.0) * zeroL0, specularPower);
    float spec1 = pow(max(dot(half1, normal), 0.0) * zeroL1, specularPower);
    float spec2 = pow(max(dot(half2, normal), 0.0) * zeroL2, specularPower);
    specularRgb = (spec0 * light0Specular + spec1 * light1Specular +
        spec2 * light2Specular) * specularColor;
}

void main()
{
    vec2 textureCoordinate0 = vec2(
        fragmentTexCoord.x,
        mix(fragmentTexCoord.y, 1.0 - fragmentTexCoord.y, textureFlipV.x));
    vec2 textureCoordinate1 = vec2(
        fragmentTexCoord1.x,
        mix(fragmentTexCoord1.y, 1.0 - fragmentTexCoord1.y, textureFlipV.y));
    vec4 sampled = (textureEnabled > 0.5)
        ? texture(texture0, textureCoordinate0) : vec4(1.0);
    if (dualTexture > 0.5)
    {
        sampled.rgb *= 2.0;
        sampled *= texture(texture1, textureCoordinate1);
    }
    if (lightingEnabled > 0.5)
    {
        vec3 litRgb = fragmentVertexLitRgb;
        vec3 specularRgb = fragmentVertexSpecularRgb;
        if (preferPerPixelLighting > 0.5)
        {
            computeLights(fragmentWorldPosition, normalize(fragmentNormal), litRgb, specularRgb);
            litRgb *= fragmentVertexColor.rgb;
        }
        finalColor = sampled * vec4(litRgb, fragmentVertexAlpha);
        finalColor.rgb += specularRgb * finalColor.a;
    }
    else
    {
        finalColor = sampled * diffuseColor * fragmentVertexColor;
    }
    float comparison = (alphaTest.y > 0.0)
        ? ((abs(finalColor.a - alphaTest.x) < alphaTest.y)
            ? alphaTest.z : alphaTest.w)
        : ((finalColor.a < alphaTest.x) ? alphaTest.z : alphaTest.w);
    if (comparison < 0.0) discard;
    finalColor.rgb = mix(fogColor, finalColor.rgb, fragmentFogFactor);
}
)";

        PrimitivePipeline pipeline;
        try
        {
            GLint maxVertexUniformComponents = 0;
            glGetIntegerv(
                GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxVertexUniformComponents);
            if (maxVertexUniformComponents < 1024)
            {
                throw std::runtime_error(
                    "RLGL: GL context does not provide the OpenGL 3.3 minimum of 1024 "
                    "vertex uniform components required by the 72-bone stock pipeline");
            }
            pipeline.program = rlLoadShaderProgram(vertexShader, fragmentShader);
            if (pipeline.program == 0 || pipeline.program == rlGetShaderIdDefault())
                throw std::runtime_error("RLGL: primitive shader creation failed");
            pipeline.worldViewProjectionLocation =
                rlGetLocationUniform(pipeline.program, "worldViewProjection");
            pipeline.worldLocation = rlGetLocationUniform(pipeline.program, "world");
            pipeline.normalMatrixLocations = {
                rlGetLocationUniform(pipeline.program, "normalMatrix0"),
                rlGetLocationUniform(pipeline.program, "normalMatrix1"),
                rlGetLocationUniform(pipeline.program, "normalMatrix2")};
            pipeline.diffuseColorLocation =
                rlGetLocationUniform(pipeline.program, "diffuseColor");
            pipeline.vertexColorEnabledLocation =
                rlGetLocationUniform(pipeline.program, "vertexColorEnabled");
            pipeline.textureLocation =
                rlGetLocationUniform(pipeline.program, "texture0");
            pipeline.texture1Location =
                rlGetLocationUniform(pipeline.program, "texture1");
            pipeline.textureFlipVLocation =
                rlGetLocationUniform(pipeline.program, "textureFlipV");
            pipeline.textureEnabledLocation =
                rlGetLocationUniform(pipeline.program, "textureEnabled");
            pipeline.dualTextureLocation =
                rlGetLocationUniform(pipeline.program, "dualTexture");
            pipeline.alphaTestLocation =
                rlGetLocationUniform(pipeline.program, "alphaTest");
            pipeline.fogVectorLocation =
                rlGetLocationUniform(pipeline.program, "fogVector");
            pipeline.fogColorLocation =
                rlGetLocationUniform(pipeline.program, "fogColor");
            pipeline.lightingEnabledLocation =
                rlGetLocationUniform(pipeline.program, "lightingEnabled");
            pipeline.preferPerPixelLightingLocation =
                rlGetLocationUniform(pipeline.program, "preferPerPixelLighting");
            pipeline.skinnedLocation =
                rlGetLocationUniform(pipeline.program, "skinned");
            pipeline.boneRowsLocation =
                rlGetLocationUniform(pipeline.program, "boneRows[0]");
            pipeline.weightsPerVertexLocation =
                rlGetLocationUniform(pipeline.program, "weightsPerVertex");
            pipeline.ambientColorLocation =
                rlGetLocationUniform(pipeline.program, "ambientColor");
            pipeline.emissiveColorLocation =
                rlGetLocationUniform(pipeline.program, "emissiveColor");
            pipeline.eyePositionLocation =
                rlGetLocationUniform(pipeline.program, "eyePosition");
            pipeline.lightDirectionLocations = {
                rlGetLocationUniform(pipeline.program, "light0Direction"),
                rlGetLocationUniform(pipeline.program, "light1Direction"),
                rlGetLocationUniform(pipeline.program, "light2Direction")};
            pipeline.lightDiffuseLocations = {
                rlGetLocationUniform(pipeline.program, "light0Diffuse"),
                rlGetLocationUniform(pipeline.program, "light1Diffuse"),
                rlGetLocationUniform(pipeline.program, "light2Diffuse")};
            pipeline.lightSpecularLocations = {
                rlGetLocationUniform(pipeline.program, "light0Specular"),
                rlGetLocationUniform(pipeline.program, "light1Specular"),
                rlGetLocationUniform(pipeline.program, "light2Specular")};
            pipeline.specularColorLocation =
                rlGetLocationUniform(pipeline.program, "specularColor");
            pipeline.specularPowerLocation =
                rlGetLocationUniform(pipeline.program, "specularPower");
            if (pipeline.worldViewProjectionLocation < 0 ||
                pipeline.worldLocation < 0 || pipeline.normalMatrixLocations[0] < 0 ||
                pipeline.normalMatrixLocations[1] < 0 ||
                pipeline.normalMatrixLocations[2] < 0 || pipeline.diffuseColorLocation < 0 ||
                pipeline.vertexColorEnabledLocation < 0 ||
                pipeline.textureLocation < 0 || pipeline.texture1Location < 0 ||
                pipeline.textureFlipVLocation < 0 ||
                pipeline.textureEnabledLocation < 0 || pipeline.dualTextureLocation < 0 ||
                pipeline.alphaTestLocation < 0 || pipeline.fogVectorLocation < 0 ||
                pipeline.fogColorLocation < 0 || pipeline.lightingEnabledLocation < 0 ||
                pipeline.preferPerPixelLightingLocation < 0 ||
                pipeline.skinnedLocation < 0 || pipeline.boneRowsLocation < 0 ||
                pipeline.weightsPerVertexLocation < 0 ||
                pipeline.ambientColorLocation < 0 || pipeline.emissiveColorLocation < 0 ||
                pipeline.eyePositionLocation < 0 ||
                pipeline.lightDirectionLocations[0] < 0 ||
                pipeline.lightDirectionLocations[1] < 0 ||
                pipeline.lightDirectionLocations[2] < 0 ||
                pipeline.lightDiffuseLocations[0] < 0 ||
                pipeline.lightDiffuseLocations[1] < 0 ||
                pipeline.lightDiffuseLocations[2] < 0 ||
                pipeline.lightSpecularLocations[0] < 0 ||
                pipeline.lightSpecularLocations[1] < 0 ||
                pipeline.lightSpecularLocations[2] < 0 ||
                pipeline.specularColorLocation < 0 || pipeline.specularPowerLocation < 0)
            {
                throw std::runtime_error("RLGL: primitive shader uniforms are incomplete");
            }
            pipeline.vertexArray = rlLoadVertexArray();
            if (pipeline.vertexArray == 0)
                throw std::runtime_error("RLGL: primitive VAO creation failed");
            ThrowIfGlError("primitive pipeline creation");
            return pipeline;
        }
        catch (...)
        {
            DestroyPrimitivePipeline(pipeline);
            throw;
        }
    }

    void DestroyPrimitivePipeline(PrimitivePipeline& pipeline) noexcept
    {
        if (bridgeInitialized)
        {
            if (pipeline.vertexArray != 0)
                rlUnloadVertexArray(pipeline.vertexArray);
            if (pipeline.program != 0 && pipeline.program != rlGetShaderIdDefault())
                rlUnloadShaderProgram(pipeline.program);
        }
        pipeline = {};
    }

    void DrawPrimitiveGeometry(
        const PrimitivePipeline& pipeline,
        const unsigned int vertexBuffer, const unsigned int indexBuffer,
        const VertexAttributeBinding* const attributes, const int attributeCount,
        const float* const worldViewProjectionColumnMajor,
        const unsigned int texture, const unsigned int texture1,
        const GpuDrawParams& params,
        const int primitiveType, const int elementCount,
        const int firstVertex, const int startIndex, const int baseVertex,
        const bool thirtyTwoBitIndices)
    {
        RequireInitialized("primitive draw");
        if (pipeline.program == 0 || pipeline.vertexArray == 0 ||
            vertexBuffer == 0 || attributes == nullptr || attributeCount <= 0 ||
            worldViewProjectionColumnMajor == nullptr || elementCount <= 0 ||
            firstVertex < 0 || startIndex < 0 || baseVertex < 0)
        {
            throw std::invalid_argument("RLGL: invalid primitive draw request");
        }

        GLenum mode = GL_TRIANGLES;
        switch (primitiveType)
        {
        case 0: mode = GL_TRIANGLES; break;
        case 1: mode = GL_TRIANGLE_STRIP; break;
        case 2: mode = GL_LINES; break;
        case 3: mode = GL_LINE_STRIP; break;
        case 4: mode = GL_POINTS; break;
        default:
            throw std::invalid_argument("RLGL: invalid PrimitiveType ordinal");
        }

        FlushImmediateBatch();
        rlEnableShader(pipeline.program);
        ::Matrix matrix{};
        matrix.m0 = worldViewProjectionColumnMajor[0];
        matrix.m1 = worldViewProjectionColumnMajor[1];
        matrix.m2 = worldViewProjectionColumnMajor[2];
        matrix.m3 = worldViewProjectionColumnMajor[3];
        matrix.m4 = worldViewProjectionColumnMajor[4];
        matrix.m5 = worldViewProjectionColumnMajor[5];
        matrix.m6 = worldViewProjectionColumnMajor[6];
        matrix.m7 = worldViewProjectionColumnMajor[7];
        matrix.m8 = worldViewProjectionColumnMajor[8];
        matrix.m9 = worldViewProjectionColumnMajor[9];
        matrix.m10 = worldViewProjectionColumnMajor[10];
        matrix.m11 = worldViewProjectionColumnMajor[11];
        matrix.m12 = worldViewProjectionColumnMajor[12];
        matrix.m13 = worldViewProjectionColumnMajor[13];
        matrix.m14 = worldViewProjectionColumnMajor[14];
        matrix.m15 = worldViewProjectionColumnMajor[15];
        rlSetUniformMatrix(pipeline.worldViewProjectionLocation, matrix);

        ::Matrix worldMatrix{};
        worldMatrix.m0 = params.worldColMajor[0];
        worldMatrix.m1 = params.worldColMajor[1];
        worldMatrix.m2 = params.worldColMajor[2];
        worldMatrix.m3 = params.worldColMajor[3];
        worldMatrix.m4 = params.worldColMajor[4];
        worldMatrix.m5 = params.worldColMajor[5];
        worldMatrix.m6 = params.worldColMajor[6];
        worldMatrix.m7 = params.worldColMajor[7];
        worldMatrix.m8 = params.worldColMajor[8];
        worldMatrix.m9 = params.worldColMajor[9];
        worldMatrix.m10 = params.worldColMajor[10];
        worldMatrix.m11 = params.worldColMajor[11];
        worldMatrix.m12 = params.worldColMajor[12];
        worldMatrix.m13 = params.worldColMajor[13];
        worldMatrix.m14 = params.worldColMajor[14];
        worldMatrix.m15 = params.worldColMajor[15];
        rlSetUniformMatrix(pipeline.worldLocation, worldMatrix);

        const float* const w = params.worldColMajor;
        const float a=w[0], d=w[1], g=w[2];
        const float b=w[4], e=w[5], h=w[6];
        const float c=w[8], f=w[9], i=w[10];
        const float determinant = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
        const float inverseDeterminant = determinant != 0.0f ? 1.0f/determinant : 0.0f;
        const float normalMatrix[3][3] = {
            {(e*i-f*h)*inverseDeterminant, -(b*i-c*h)*inverseDeterminant,
             (b*f-c*e)*inverseDeterminant},
            {-(d*i-f*g)*inverseDeterminant, (a*i-c*g)*inverseDeterminant,
             -(a*f-c*d)*inverseDeterminant},
            {(d*h-e*g)*inverseDeterminant, -(a*h-b*g)*inverseDeterminant,
             (a*e-b*d)*inverseDeterminant}};
        for (int column = 0; column < 3; ++column)
        {
            rlSetUniform(
                pipeline.normalMatrixLocations[static_cast<std::size_t>(column)],
                normalMatrix[column], RL_SHADER_UNIFORM_VEC3, 1);
        }
        rlSetUniform(
            pipeline.diffuseColorLocation, params.diffuseColor, RL_SHADER_UNIFORM_VEC4, 1);
        const float vertexColorFlag = params.vertexColorEnabled ? 1.0f : 0.0f;
        rlSetUniform(
            pipeline.vertexColorEnabledLocation, &vertexColorFlag,
            RL_SHADER_UNIFORM_FLOAT, 1);
        constexpr int textureUnit = 0;
        rlSetUniform(pipeline.textureLocation, &textureUnit, RL_SHADER_UNIFORM_INT, 1);
        constexpr int textureUnit1 = 1;
        rlSetUniform(pipeline.texture1Location, &textureUnit1, RL_SHADER_UNIFORM_INT, 1);
        const float textureFlipV[2] = {
            params.textureEnabled && params.texture0 != nullptr &&
                    SampledRowsAreBottomUp(*params.texture0)
                ? 1.0f : 0.0f,
            params.dualTexture && params.texture1 != nullptr &&
                    SampledRowsAreBottomUp(*params.texture1)
                ? 1.0f : 0.0f};
        rlSetUniform(
            pipeline.textureFlipVLocation, textureFlipV, RL_SHADER_UNIFORM_VEC2, 1);
        const float textureFlag = params.textureEnabled ? 1.0f : 0.0f;
        rlSetUniform(
            pipeline.textureEnabledLocation, &textureFlag,
            RL_SHADER_UNIFORM_FLOAT, 1);
        const float dualTextureFlag = params.dualTexture ? 1.0f : 0.0f;
        rlSetUniform(
            pipeline.dualTextureLocation, &dualTextureFlag,
            RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(
            pipeline.alphaTestLocation, params.alphaTest, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniform(
            pipeline.fogVectorLocation, params.fogVector, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniform(
            pipeline.fogColorLocation, params.fogColor, RL_SHADER_UNIFORM_VEC3, 1);
        const float lightingFlag = params.lightingEnabled ? 1.0f : 0.0f;
        const float perPixelFlag = params.preferPerPixelLighting ? 1.0f : 0.0f;
        rlSetUniform(
            pipeline.lightingEnabledLocation, &lightingFlag, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(
            pipeline.preferPerPixelLightingLocation, &perPixelFlag,
            RL_SHADER_UNIFORM_FLOAT, 1);
        const float skinnedFlag = params.skinned ? 1.0f : 0.0f;
        rlSetUniform(
            pipeline.skinnedLocation, &skinnedFlag, RL_SHADER_UNIFORM_FLOAT, 1);
        if (params.skinned)
        {
            if (params.boneCount <= 0 || params.boneCount > 72 ||
                (params.weightsPerVertex != 1 && params.weightsPerVertex != 2 &&
                 params.weightsPerVertex != 4))
            {
                rlDisableShader();
                throw std::invalid_argument("RLGL: invalid SkinnedEffect bone parameters");
            }
            std::array<float, 72 * 12> boneRows{};
            for (int bone = 0; bone < params.boneCount; ++bone)
            {
                const float* const matrixValues = params.boneTransforms + bone * 16;
                float* const rows = boneRows.data() + bone * 12;
                rows[0] = matrixValues[0];
                rows[1] = matrixValues[4];
                rows[2] = matrixValues[8];
                rows[3] = matrixValues[12];
                rows[4] = matrixValues[1];
                rows[5] = matrixValues[5];
                rows[6] = matrixValues[9];
                rows[7] = matrixValues[13];
                rows[8] = matrixValues[2];
                rows[9] = matrixValues[6];
                rows[10] = matrixValues[10];
                rows[11] = matrixValues[14];
            }
            rlSetUniform(
                pipeline.boneRowsLocation, boneRows.data(),
                RL_SHADER_UNIFORM_VEC4, params.boneCount * 3);
            rlSetUniform(
                pipeline.weightsPerVertexLocation, &params.weightsPerVertex,
                RL_SHADER_UNIFORM_INT, 1);
        }
        rlSetUniform(
            pipeline.ambientColorLocation, params.ambientColor, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(
            pipeline.emissiveColorLocation, params.emissiveColor, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(
            pipeline.eyePositionLocation, params.eyePositionWorld,
            RL_SHADER_UNIFORM_VEC3, 1);
        const float* const lightDirections[3] = {
            params.light0Dir, params.light1Dir, params.light2Dir};
        const float* const lightDiffuse[3] = {
            params.light0Diffuse, params.light1Diffuse, params.light2Diffuse};
        const float* const lightSpecular[3] = {
            params.light0Specular, params.light1Specular, params.light2Specular};
        for (int light = 0; light < 3; ++light)
        {
            const std::size_t index = static_cast<std::size_t>(light);
            rlSetUniform(
                pipeline.lightDirectionLocations[index], lightDirections[light],
                RL_SHADER_UNIFORM_VEC3, 1);
            rlSetUniform(
                pipeline.lightDiffuseLocations[index], lightDiffuse[light],
                RL_SHADER_UNIFORM_VEC3, 1);
            rlSetUniform(
                pipeline.lightSpecularLocations[index], lightSpecular[light],
                RL_SHADER_UNIFORM_VEC3, 1);
        }
        rlSetUniform(
            pipeline.specularColorLocation, params.specularColor,
            RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(
            pipeline.specularPowerLocation, &params.specularPower,
            RL_SHADER_UNIFORM_FLOAT, 1);
        if (params.dualTexture)
        {
            rlActiveTextureSlot(textureUnit1);
            rlEnableTexture(texture1 != 0 ? texture1 : rlGetTextureIdDefault());
        }
        if (params.textureEnabled)
        {
            rlActiveTextureSlot(textureUnit);
            rlEnableTexture(texture != 0 ? texture : rlGetTextureIdDefault());
        }

        if (!rlEnableVertexArray(pipeline.vertexArray))
        {
            if (params.textureEnabled)
            {
                rlActiveTextureSlot(textureUnit);
                rlDisableTexture();
            }
            if (params.dualTexture)
            {
                rlActiveTextureSlot(textureUnit1);
                rlDisableTexture();
                rlActiveTextureSlot(textureUnit);
            }
            rlDisableShader();
            throw std::runtime_error("RLGL: primitive VAO became unavailable");
        }
        for (unsigned int location = 0; location < 16u; ++location)
        {
            rlDisableVertexAttribute(location);
            rlSetVertexAttributeDivisor(location, 0);
        }
        rlEnableVertexBuffer(vertexBuffer);
        for (int index = 0; index < attributeCount; ++index)
        {
            const VertexAttributeBinding& attribute = attributes[index];
            if (attribute.location >= 16u || attribute.componentCount <= 0 ||
                attribute.stride <= 0 || attribute.offset < 0)
            {
                rlDisableVertexArray();
                rlDisableVertexBuffer();
                if (params.textureEnabled)
                {
                    rlActiveTextureSlot(textureUnit);
                    rlDisableTexture();
                }
                if (params.dualTexture)
                {
                    rlActiveTextureSlot(textureUnit1);
                    rlDisableTexture();
                    rlActiveTextureSlot(textureUnit);
                }
                rlDisableShader();
                throw std::invalid_argument("RLGL: invalid vertex attribute binding");
            }
            rlEnableVertexAttribute(attribute.location);
            rlSetVertexAttribute(
                attribute.location, attribute.componentCount, attribute.scalarType,
                attribute.normalized, attribute.stride, attribute.offset);
        }

        PrimitiveDrawSnapshot snapshot;
        snapshot.primitiveMode = static_cast<int>(mode);
        snapshot.elementCount = elementCount;
        snapshot.firstVertex = firstVertex;
        snapshot.startIndex = startIndex;
        snapshot.baseVertex = baseVertex;
        snapshot.indexed = indexBuffer != 0;
        snapshot.texture = texture;
        snapshot.texture1 = texture1;
        snapshot.textureEnabled = params.textureEnabled;
        snapshot.dualTexture = params.dualTexture;
        snapshot.skinned = params.skinned;
        if (indexBuffer == 0)
        {
            if (mode == GL_TRIANGLES)
            {
                rlDrawVertexArray(firstVertex, elementCount);
                snapshot.usedRlglDrawWrapper = true;
            }
            else
            {
                // rlgl 6.0 hardcodes GL_TRIANGLES in its public draw wrapper.
                glDrawArrays(mode, firstVertex, elementCount);
            }
        }
        else
        {
            rlEnableVertexBufferElement(indexBuffer);
            const GLenum indexType = thirtyTwoBitIndices
                ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            snapshot.indexType = static_cast<int>(indexType);
            if (mode == GL_TRIANGLES && !thirtyTwoBitIndices &&
                startIndex == 0 && baseVertex == 0)
            {
                rlDrawVertexArrayElements(0, elementCount, nullptr);
                snapshot.usedRlglDrawWrapper = true;
            }
            else
            {
                const std::uintptr_t byteOffset =
                    static_cast<std::uintptr_t>(startIndex) *
                    (thirtyTwoBitIndices ? sizeof(std::uint32_t) : sizeof(std::uint16_t));
                const void* const indices = reinterpret_cast<const void*>(byteOffset);
                // rlgl's public indexed wrapper is triangle-only, unsigned-short-only, and has
                // no base-vertex parameter. The loaded GL 3.3 dispatch supplies the exact gap.
                if (baseVertex == 0)
                    glDrawElements(mode, elementCount, indexType, indices);
                else
                    glDrawElementsBaseVertex(mode, elementCount, indexType, indices, baseVertex);
            }
        }

        rlDisableVertexArray();
        rlDisableVertexBuffer();
        if (params.textureEnabled)
        {
            rlActiveTextureSlot(textureUnit);
            rlDisableTexture();
        }
        if (params.dualTexture)
        {
            rlActiveTextureSlot(textureUnit1);
            rlDisableTexture();
            rlActiveTextureSlot(textureUnit);
        }
        rlDisableShader();
        ThrowIfGlError("primitive draw");
        lastPrimitiveDraw = snapshot;
    }

    PrimitiveDrawSnapshot GetPrimitiveDrawSnapshotForTesting()
    {
        return lastPrimitiveDraw;
    }

    int GetMaxVertexUniformComponentsForTesting()
    {
        RequireInitialized("vertex-uniform limit query");
        GLint result = 0;
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &result);
        ThrowIfGlError("vertex-uniform limit query");
        return result;
    }

    VertexAttributeSnapshot GetVertexAttributeSnapshotForTesting(
        const unsigned int vertexBuffer, const VertexAttributeBinding& attribute)
    {
        RequireInitialized("vertex-attribute snapshot");
        if (vertexBuffer == 0 || attribute.location >= 16u ||
            attribute.componentCount <= 0 || attribute.stride <= 0 || attribute.offset < 0)
        {
            throw std::invalid_argument("RLGL: invalid vertex-attribute snapshot request");
        }

        GLint previousVertexArray = 0;
        GLint previousVertexBuffer = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousVertexBuffer);
        const unsigned int vertexArray = rlLoadVertexArray();
        if (vertexArray == 0 || !rlEnableVertexArray(vertexArray))
        {
            if (vertexArray != 0) rlUnloadVertexArray(vertexArray);
            throw std::runtime_error("RLGL: attribute snapshot could not create a VAO");
        }

        VertexAttributeSnapshot snapshot;
        rlEnableVertexBuffer(vertexBuffer);
        rlEnableVertexAttribute(attribute.location);
        rlSetVertexAttribute(
            attribute.location, attribute.componentCount, attribute.scalarType,
            attribute.normalized, attribute.stride, attribute.offset);
        GLint value = 0;
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &value);
        snapshot.enabled = value == GL_TRUE;
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_SIZE, &snapshot.componentCount);
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_TYPE, &snapshot.scalarType);
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &value);
        snapshot.normalized = value == GL_TRUE;
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &snapshot.stride);
        glGetVertexAttribiv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &value);
        snapshot.buffer = static_cast<unsigned int>(value);
        void* pointer = nullptr;
        glGetVertexAttribPointerv(attribute.location, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
        snapshot.offset = static_cast<int>(reinterpret_cast<std::uintptr_t>(pointer));

        rlUnloadVertexArray(vertexArray);
        if (previousVertexArray != 0)
            (void)rlEnableVertexArray(static_cast<unsigned int>(previousVertexArray));
        rlEnableVertexBuffer(static_cast<unsigned int>(previousVertexBuffer));
        ThrowIfGlError("vertex-attribute snapshot");
        return snapshot;
    }

    std::uint8_t ReadStencilForTesting(
        const int x, const int y, const int framebufferHeight)
    {
        RequireInitialized("stencil readback");
        std::uint8_t value = 0;
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(
            x, framebufferHeight - y - 1, 1, 1,
            GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &value);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        ThrowIfGlError("stencil readback");
        return value;
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
        if (width <= 0 || height <= 0 || mipLevels <= 0)
            throw std::invalid_argument("RLGL: invalid Texture2D creation request");

        const TextureFormatInfo format = TextureFormat(surfaceFormat);
        if (format.compressed && pixels == nullptr)
            throw std::invalid_argument("RLGL: compressed Texture2D creation requires blocks");
        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> converted;
        const std::uint8_t* upload = pixels;
        if (pixels != nullptr)
        {
            upload = ConvertPackedUpload(
                format, pixels, static_cast<std::size_t>(width) * height, converted);
        }
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

    unsigned int CreateTextureCubeColor(const int size, const int mipLevels)
    {
        RequireInitialized("TextureCube creation");
        if (size <= 0 || mipLevels <= 0)
            throw std::invalid_argument("RLGL: invalid TextureCube creation request");

        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const unsigned int id = rlLoadTextureCubemap(
            nullptr, size, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, mipLevels);
        if (id == 0)
            throw std::runtime_error("RLGL: TextureCube allocation returned a zero texture name");

        try
        {
            glBindTexture(GL_TEXTURE_CUBE_MAP, id);
            // rlLoadTextureCubemap owns every face/level allocation, but it does not clamp the
            // declared range. XNA textures with one level must remain complete under mip samplers.
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            ThrowIfGlError("TextureCube allocation");
        }
        catch (...)
        {
            rlUnloadTexture(id);
            throw;
        }
        return id;
    }

    void DestroyTextureCube(const unsigned int id) noexcept
    {
        if (bridgeInitialized && id != 0) rlUnloadTexture(id);
    }

    void UpdateTextureCubeColor(
        const unsigned int id, const int face, const int level,
        const int x, const int y, const int width, const int height,
        const std::uint8_t* pixels)
    {
        RequireInitialized("TextureCube update");
        if (id == 0 || face < 0 || face >= 6 || level < 0 ||
            x < 0 || y < 0 || width <= 0 || height <= 0 || pixels == nullptr)
        {
            throw std::invalid_argument("RLGL: invalid TextureCube update request");
        }

        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);
        glTexSubImage2D(
            static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
            level, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        ThrowIfGlError("TextureCube update");
    }

    void ReadTextureCubeColor(
        const unsigned int id, const int face, const int level, const int levelSize,
        const int x, const int y, const int width, const int height,
        std::uint8_t* pixels)
    {
        RequireInitialized("TextureCube readback");
        if (id == 0 || face < 0 || face >= 6 || level < 0 || levelSize <= 0 ||
            x < 0 || y < 0 || width <= 0 || height <= 0 ||
            x > levelSize - width || y > levelSize - height || pixels == nullptr)
        {
            throw std::invalid_argument("RLGL: invalid TextureCube readback request");
        }

        const TextureBindingRestore bindingRestore;
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> levelPixels(
            static_cast<std::size_t>(levelSize) * levelSize * 4u);
        glGetTexImage(
            static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
            level, GL_RGBA, GL_UNSIGNED_BYTE, levelPixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        ThrowIfGlError("TextureCube readback");

        const std::size_t sourceRowBytes = static_cast<std::size_t>(levelSize) * 4u;
        const std::size_t destinationRowBytes = static_cast<std::size_t>(width) * 4u;
        for (int row = 0; row < height; ++row)
        {
            const std::uint8_t* source = levelPixels.data()
                + static_cast<std::size_t>(y + row) * sourceRowBytes
                + static_cast<std::size_t>(x) * 4u;
            std::memcpy(
                pixels + static_cast<std::size_t>(row) * destinationRowBytes,
                source, destinationRowBytes);
        }
    }

    void BindTextureCube(const unsigned int id, const int unit)
    {
        RequireInitialized("TextureCube binding");
        if (unit < 0)
            throw std::out_of_range("RLGL: texture unit must be non-negative");
        rlActiveTextureSlot(unit);
        if (id == 0) rlDisableTextureCubemap();
        else rlEnableTextureCubemap(id);
        ThrowIfGlError("TextureCube binding");
    }

    unsigned int GetBoundTextureCubeForTesting(const int unit)
    {
        RequireInitialized("TextureCube binding query");
        if (unit < 0)
            throw std::out_of_range("RLGL: texture unit must be non-negative");

        GLint previousActiveTexture = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        GLint texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &texture);
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        ThrowIfGlError("TextureCube binding query");
        return static_cast<unsigned int>(texture);
    }

    TextureCubeSnapshot GetTextureCubeSnapshotForTesting(
        const unsigned int id, const int levelCount)
    {
        RequireInitialized("TextureCube snapshot");
        if (id == 0 || levelCount <= 0)
            throw std::invalid_argument("RLGL: invalid TextureCube snapshot request");

        const TextureBindingRestore bindingRestore;
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);
        TextureCubeSnapshot snapshot;
        snapshot.texture = id;
        for (int face = 0; face < 6; ++face)
        {
            const GLenum target = static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
            glGetTexLevelParameteriv(
                target, 0, GL_TEXTURE_WIDTH, &snapshot.levelZeroWidths[face]);
            glGetTexLevelParameteriv(
                target, levelCount - 1, GL_TEXTURE_WIDTH,
                &snapshot.finalLevelWidths[face]);
        }
        glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, &snapshot.baseLevel);
        glGetTexParameteriv(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, &snapshot.maxLevel);
        glGetTexLevelParameteriv(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_INTERNAL_FORMAT,
            &snapshot.internalFormat);
        ThrowIfGlError("TextureCube snapshot");
        return snapshot;
    }

    RenderTargetStorage CreateRenderTarget2D(
        const int width, const int height, const int levelCount, const int depthFormat,
        const int multiSampleCount, const int surfaceFormat)
    {
        RequireInitialized("RenderTarget2D creation");
        if (width <= 0 || height <= 0 || levelCount <= 0 ||
            depthFormat < 0 || depthFormat > 3 || multiSampleCount < 0 ||
            !IsClassicRenderTargetFormat(surfaceFormat))
        {
            throw std::invalid_argument("RLGL: invalid RenderTarget2D creation request");
        }
        const TextureFormatInfo colorFormat = TextureFormat(surfaceFormat);
        const std::uint64_t texelCount =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (texelCount > static_cast<std::uint64_t>(
                SIZE_MAX / static_cast<std::size_t>(colorFormat.bytesPerTexel)))
            throw std::overflow_error("RLGL: RenderTarget2D allocation size overflow");

        const FramebufferBindingRestore framebufferRestore;
        RenderTargetStorage storage;
        if (multiSampleCount > 0)
        {
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            storage.multiSampleCount = std::min(multiSampleCount, static_cast<int>(maxSamples));
            if (storage.multiSampleCount < 2) storage.multiSampleCount = 0;
        }
        storage.colorTexture = CreateTexture2D(
            surfaceFormat, width, height, levelCount, nullptr);
        try
        {
            storage.framebuffer = rlLoadFramebuffer();
            if (storage.framebuffer == 0)
                throw std::runtime_error("RLGL: framebuffer allocation returned a zero name");
            if (storage.multiSampleCount > 0)
            {
                glGenRenderbuffers(1, &storage.multisampleColorRenderbuffer);
                glBindRenderbuffer(
                    GL_RENDERBUFFER, storage.multisampleColorRenderbuffer);
                glRenderbufferStorageMultisample(
                    GL_RENDERBUFFER, storage.multiSampleCount,
                    colorFormat.internalFormat, width, height);
                GLint actualSamples = 0;
                glGetRenderbufferParameteriv(
                    GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &actualSamples);
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
                if (storage.multisampleColorRenderbuffer == 0 || actualSamples < 2)
                {
                    throw std::runtime_error(
                        "RLGL: multisample color renderbuffer allocation failed");
                }
                storage.multiSampleCount = actualSamples;
                rlFramebufferAttach(
                    storage.framebuffer, storage.multisampleColorRenderbuffer,
                    RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_RENDERBUFFER, 0);

                storage.resolveFramebuffer = rlLoadFramebuffer();
                if (storage.resolveFramebuffer == 0)
                    throw std::runtime_error(
                        "RLGL: resolve framebuffer allocation returned a zero name");
                rlFramebufferAttach(
                    storage.resolveFramebuffer, storage.colorTexture,
                    RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
                if (!rlFramebufferComplete(storage.resolveFramebuffer))
                    throw std::runtime_error("RLGL: resolve framebuffer is incomplete");
            }
            else
            {
                rlFramebufferAttach(
                    storage.framebuffer, storage.colorTexture,
                    RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
            }

            if (depthFormat != 0)
            {
                GLenum internalFormat = GL_DEPTH_COMPONENT16;
                if (depthFormat == 2) internalFormat = GL_DEPTH_COMPONENT24;
                else if (depthFormat == 3) internalFormat = GL_DEPTH24_STENCIL8;

                // rlLoadTextureDepth deliberately chooses an implementation-defined precision
                // and has no packed-stencil parameter. XNA exposes the applied DepthFormat, so
                // this one missing rlgl resource wrapper uses its already-loaded GL dispatch.
                glGenRenderbuffers(1, &storage.depthStencilRenderbuffer);
                glBindRenderbuffer(GL_RENDERBUFFER, storage.depthStencilRenderbuffer);
                if (storage.multiSampleCount > 0)
                {
                    glRenderbufferStorageMultisample(
                        GL_RENDERBUFFER, storage.multiSampleCount,
                        internalFormat, width, height);
                }
                else
                {
                    glRenderbufferStorage(
                        GL_RENDERBUFFER, internalFormat, width, height);
                }
                if (storage.multiSampleCount > 0)
                {
                    GLint actualDepthSamples = 0;
                    glGetRenderbufferParameteriv(
                        GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &actualDepthSamples);
                    if (actualDepthSamples != storage.multiSampleCount)
                    {
                        glBindRenderbuffer(GL_RENDERBUFFER, 0);
                        glDeleteRenderbuffers(1, &storage.depthStencilRenderbuffer);
                        storage.depthStencilRenderbuffer = 0;
                        throw std::runtime_error(
                            "RLGL: color and depth multisample counts do not match");
                    }
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
                if (storage.depthStencilRenderbuffer == 0)
                    throw std::runtime_error(
                        "RLGL: depth/stencil renderbuffer allocation returned a zero name");
                rlFramebufferAttach(
                    storage.framebuffer, storage.depthStencilRenderbuffer,
                    RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
                if (depthFormat == 3)
                {
                    rlFramebufferAttach(
                        storage.framebuffer, storage.depthStencilRenderbuffer,
                        RL_ATTACHMENT_STENCIL, RL_ATTACHMENT_RENDERBUFFER, 0);
                }
            }

            if (!rlFramebufferComplete(storage.framebuffer))
            {
                throw std::runtime_error(
                    "RLGL: RenderTarget2D framebuffer is incomplete for " +
                    std::to_string(width) + "x" + std::to_string(height) +
                    ", DepthFormat ordinal " + std::to_string(depthFormat));
            }
            ThrowIfGlError("RenderTarget2D allocation");
        }
        catch (...)
        {
            if (storage.resolveFramebuffer != 0)
            {
                rlUnloadFramebuffer(storage.resolveFramebuffer);
                storage.resolveFramebuffer = 0;
            }
            if (storage.framebuffer != 0)
            {
                rlUnloadFramebuffer(storage.framebuffer);
                storage.framebuffer = 0;
                storage.depthStencilRenderbuffer = 0;
            }
            else if (storage.depthStencilRenderbuffer != 0)
            {
                glDeleteRenderbuffers(1, &storage.depthStencilRenderbuffer);
                storage.depthStencilRenderbuffer = 0;
            }
            if (storage.multisampleColorRenderbuffer != 0)
            {
                glDeleteRenderbuffers(1, &storage.multisampleColorRenderbuffer);
                storage.multisampleColorRenderbuffer = 0;
            }
            if (storage.colorTexture != 0)
            {
                rlUnloadTexture(storage.colorTexture);
                storage.colorTexture = 0;
            }
            throw;
        }
        return storage;
    }

    int GetMaxRenderTargets()
    {
        RequireInitialized("MRT limit query");
        GLint drawBuffers = 0;
        GLint colorAttachments = 0;
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &colorAttachments);
        ThrowIfGlError("MRT limit query");
        return std::clamp(
            std::min(static_cast<int>(drawBuffers), static_cast<int>(colorAttachments)),
            1, 4);
    }

    unsigned int CreateMrtFramebuffer(
        const MrtAttachment* const attachments, const int count)
    {
        RequireInitialized("MRT framebuffer creation");
        if (attachments == nullptr || count < 2 || count > 4)
            throw std::invalid_argument("RLGL: MRT requires two through four attachments");

        const int samples = attachments[0].multiSampleCount;
        for (int slot = 0; slot < count; ++slot)
        {
            const bool hasTexture = attachments[slot].colorTexture != 0;
            const bool hasRenderbuffer =
                attachments[slot].multisampleColorRenderbuffer != 0;
            if (!hasTexture || (samples > 0) != hasRenderbuffer ||
                attachments[slot].multiSampleCount != samples)
            {
                throw std::invalid_argument(
                    "RLGL: MRT attachments have invalid or mismatched native storage");
            }
        }

        const FramebufferBindingRestore framebufferRestore;
        unsigned int framebuffer = rlLoadFramebuffer();
        if (framebuffer == 0)
            throw std::runtime_error("RLGL: MRT framebuffer allocation returned a zero name");
        try
        {
            for (int slot = 0; slot < count; ++slot)
            {
                const unsigned int colorObject = samples > 0
                    ? attachments[slot].multisampleColorRenderbuffer
                    : attachments[slot].colorTexture;
                rlFramebufferAttach(
                    framebuffer, colorObject, RL_ATTACHMENT_COLOR_CHANNEL0 + slot,
                    samples > 0 ? RL_ATTACHMENT_RENDERBUFFER : RL_ATTACHMENT_TEXTURE2D, 0);
            }

            if (attachments[0].depthStencilRenderbuffer != 0)
            {
                rlFramebufferAttach(
                    framebuffer, attachments[0].depthStencilRenderbuffer,
                    RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
                if (attachments[0].depthFormat == 3)
                {
                    rlFramebufferAttach(
                        framebuffer, attachments[0].depthStencilRenderbuffer,
                        RL_ATTACHMENT_STENCIL, RL_ATTACHMENT_RENDERBUFFER, 0);
                }
            }

            rlEnableFramebuffer(framebuffer);
            rlActiveDrawBuffers(count);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            if (!rlFramebufferComplete(framebuffer))
                throw std::runtime_error("RLGL: MRT framebuffer is incomplete");
            ThrowIfGlError("MRT framebuffer creation");
        }
        catch (...)
        {
            DestroyMrtFramebuffer(framebuffer);
            throw;
        }
        return framebuffer;
    }

    void DestroyMrtFramebuffer(unsigned int& framebuffer) noexcept
    {
        if (framebuffer == 0) return;
        if (!bridgeInitialized)
        {
            framebuffer = 0;
            return;
        }

        FramebufferBindingRestore restore;
        restore.ForgetDeletedFramebuffer(framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        rlUnloadFramebuffer(framebuffer);
        framebuffer = 0;
    }

    MrtFramebufferSnapshot GetMrtFramebufferSnapshotForTesting(
        const unsigned int framebuffer)
    {
        RequireInitialized("MRT framebuffer query");
        if (framebuffer == 0)
            throw std::invalid_argument("RLGL: cannot query a zero MRT framebuffer");

        const FramebufferBindingRestore framebufferRestore;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        MrtFramebufferSnapshot snapshot;
        snapshot.framebuffer = framebuffer;
        for (unsigned int slot = 0; slot < snapshot.colorObjects.size(); ++slot)
        {
            GLint type = GL_NONE;
            glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + slot,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            if (type != GL_NONE)
            {
                GLint object = 0;
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + slot,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
                snapshot.colorObjects[slot] = static_cast<unsigned int>(object);
            }
            glGetIntegerv(
                GL_DRAW_BUFFER0 + slot, &snapshot.drawBuffers[slot]);
        }
        const auto queryAttachment = [](const GLenum attachment)
        {
            GLint type = GL_NONE;
            glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, attachment,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            if (type == GL_NONE) return 0u;
            GLint object = 0;
            glGetFramebufferAttachmentParameteriv(
                GL_FRAMEBUFFER, attachment,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object);
            return static_cast<unsigned int>(object);
        };
        snapshot.depthObject = queryAttachment(GL_DEPTH_ATTACHMENT);
        snapshot.stencilObject = queryAttachment(GL_STENCIL_ATTACHMENT);
        glGetIntegerv(GL_READ_BUFFER, &snapshot.readBuffer);
        snapshot.complete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        ThrowIfGlError("MRT framebuffer query");
        return snapshot;
    }

    MrtFramebufferSnapshot GetBoundMrtFramebufferSnapshotForTesting()
    {
        RequireInitialized("bound MRT framebuffer query");
        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        ThrowIfGlError("bound MRT framebuffer query");
        if (framebuffer == 0)
            throw std::runtime_error("RLGL: no non-default draw framebuffer is bound");
        return GetMrtFramebufferSnapshotForTesting(
            static_cast<unsigned int>(framebuffer));
    }

    bool ProbeRenderTargetFormat(const int surfaceFormat)
    {
        RequireInitialized("RenderTarget2D format probe");
        if (!IsClassicRenderTargetFormat(surfaceFormat)) return false;

        RenderTargetStorage storage;
        try
        {
            storage = CreateRenderTarget2D(1, 1, 1, 0, 0, surfaceFormat);
        }
        catch (...)
        {
            DestroyRenderTarget2D(storage);
            return false;
        }
        DestroyRenderTarget2D(storage);
        return true;
    }

    int RenderTargetBytesPerTexel(const int surfaceFormat)
    {
        if (!IsClassicRenderTargetFormat(surfaceFormat))
            throw std::invalid_argument("RLGL: SurfaceFormat is not render-target compatible");
        return TextureFormat(surfaceFormat).bytesPerTexel;
    }

    void DestroyRenderTarget2D(RenderTargetStorage& storage) noexcept
    {
        if (!bridgeInitialized)
        {
            storage = {};
            return;
        }
        FramebufferBindingRestore restore;
        restore.ForgetDeletedFramebuffer(storage.framebuffer);
        restore.ForgetDeletedFramebuffer(storage.resolveFramebuffer);
        if (storage.resolveFramebuffer != 0)
            rlUnloadFramebuffer(storage.resolveFramebuffer);
        if (storage.framebuffer != 0) rlUnloadFramebuffer(storage.framebuffer);
        if (storage.multisampleColorRenderbuffer != 0)
            glDeleteRenderbuffers(1, &storage.multisampleColorRenderbuffer);
        if (storage.colorTexture != 0) rlUnloadTexture(storage.colorTexture);
        storage = {};
    }

    void BindFramebuffer(const unsigned int framebuffer)
    {
        RequireInitialized("framebuffer binding");
        if (framebuffer == 0)
            throw std::invalid_argument("RLGL: cannot bind a zero render-target framebuffer");
        rlEnableFramebuffer(framebuffer);
        ThrowIfGlError("framebuffer binding");
    }

    void ResolveRenderTarget2D(
        const RenderTargetStorage& storage, const int width, const int height)
    {
        RequireInitialized("RenderTarget2D multisample resolve");
        if (storage.multiSampleCount <= 0) return;
        if (storage.framebuffer == 0 || storage.resolveFramebuffer == 0 ||
            width <= 0 || height <= 0)
        {
            throw std::invalid_argument("RLGL: invalid RenderTarget2D resolve request");
        }

        const FramebufferBindingRestore framebufferRestore;
        rlBindFramebuffer(RL_READ_FRAMEBUFFER, storage.framebuffer);
        rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, storage.resolveFramebuffer);
        rlBlitFramebuffer(
            0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT);
        ThrowIfGlError("RenderTarget2D multisample resolve");
    }

    void GenerateRenderTargetMipmaps(
        const unsigned int texture, const int width, const int height,
        const int expectedLevelCount)
    {
        RequireInitialized("RenderTarget2D mip generation");
        if (texture == 0 || width <= 0 || height <= 0 || expectedLevelCount <= 1)
            throw std::invalid_argument("RLGL: invalid RenderTarget2D mip generation request");
        const TextureBindingRestore textureRestore;
        int generatedLevels = 1;
        rlGenTextureMipmaps(
            texture, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
            &generatedLevels);
        ThrowIfGlError("RenderTarget2D mip generation");
        if (generatedLevels != expectedLevelCount)
        {
            throw std::runtime_error(
                "RLGL: generated RenderTarget2D mip count did not match its allocation");
        }
    }

    void ReadRenderTarget2D(
        const unsigned int framebuffer, const unsigned int texture, const int level,
        const int levelWidth, const int levelHeight, const int x, const int y,
        const int width, const int height, std::uint8_t* const pixels,
        const int surfaceFormat)
    {
        RequireInitialized("RenderTarget2D readback");
        if (framebuffer == 0 || texture == 0 || level < 0 ||
            levelWidth <= 0 || levelHeight <= 0 || x < 0 || y < 0 ||
            width <= 0 || height <= 0 || x > levelWidth - width ||
            y > levelHeight - height || pixels == nullptr ||
            !IsClassicRenderTargetFormat(surfaceFormat))
        {
            throw std::invalid_argument("RLGL: invalid RenderTarget2D readback request");
        }

        const TextureFormatInfo colorFormat = TextureFormat(surfaceFormat);
        const FramebufferBindingRestore framebufferRestore;
        unsigned int readFramebuffer = framebuffer;
        if (level > 0)
        {
            readFramebuffer = rlLoadFramebuffer();
            if (readFramebuffer == 0)
                throw std::runtime_error("RLGL: mip readback framebuffer allocation failed");
            rlFramebufferAttach(
                readFramebuffer, texture,
                RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, level);
            if (!rlFramebufferComplete(readFramebuffer))
            {
                rlUnloadFramebuffer(readFramebuffer);
                throw std::runtime_error("RLGL: mip readback framebuffer is incomplete");
            }
        }

        rlBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(
            x, levelHeight - y - height, width, height,
            colorFormat.transferFormat, colorFormat.transferType, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        const GLenum readError = glGetError();

        if (level > 0) rlUnloadFramebuffer(readFramebuffer);

        const std::size_t rowBytes = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(colorFormat.bytesPerTexel);
        std::vector<std::uint8_t> temporary(rowBytes);
        for (int row = 0; row < height / 2; ++row)
        {
            std::uint8_t* const top = pixels + static_cast<std::size_t>(row) * rowBytes;
            std::uint8_t* const bottom =
                pixels + static_cast<std::size_t>(height - row - 1) * rowBytes;
            std::memcpy(temporary.data(), top, rowBytes);
            std::memcpy(top, bottom, rowBytes);
            std::memcpy(bottom, temporary.data(), rowBytes);
        }
        if (readError != GL_NO_ERROR)
        {
            char value[16] = {};
            std::snprintf(value, sizeof(value), "0x%04X", static_cast<unsigned int>(readError));
            throw std::runtime_error(
                std::string("RLGL: OpenGL error after RenderTarget2D readback: ") + value);
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
        const GLboolean cullingWasEnabled = glIsEnabled(GL_CULL_FACE);
        rlDisableBackfaceCulling();
        DrawBoundTextureRectangleForTesting(
            u, v, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
            width, height);
        if (cullingWasEnabled) rlEnableBackfaceCulling();
    }

    void DrawBoundTextureRectangleForTesting(
        const float u, const float v, const float x, const float y,
        const float width, const float height,
        const int framebufferWidth, const int framebufferHeight)
    {
        RequireInitialized("focused pipeline draw");
        if (width <= 0.0f || height <= 0.0f ||
            framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            throw std::invalid_argument("RLGL: pipeline test draw extent must be positive");
        }

        GLint previousActiveTexture = GL_TEXTURE0;
        GLint texture = 0;
        GLint previousViewport[4] = {};
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        if (texture == 0)
            throw std::runtime_error("RLGL: pipeline test requires a texture on unit zero");

        rlDrawRenderBatchActive();
        rlViewport(0, 0, framebufferWidth, framebufferHeight);

        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        rlOrtho(
            0.0, static_cast<double>(framebufferWidth),
            static_cast<double>(framebufferHeight), 0.0, -1.0, 1.0);
        rlMatrixMode(RL_MODELVIEW);
        rlPushMatrix();
        rlLoadIdentity();

        rlSetTexture(static_cast<unsigned int>(texture));
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(u, v); rlVertex2f(x, y);
        rlTexCoord2f(u, v); rlVertex2f(x, y + height);
        rlTexCoord2f(u, v); rlVertex2f(x + width, y + height);
        rlTexCoord2f(u, v); rlVertex2f(x + width, y);
        rlEnd();
        rlDrawRenderBatchActive();
        rlSetTexture(0);

        rlPopMatrix();
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();
        rlMatrixMode(RL_MODELVIEW);
        glViewport(
            previousViewport[0], previousViewport[1],
            previousViewport[2], previousViewport[3]);
        ThrowIfGlError("focused pipeline draw");
    }

    void BindDefaultFramebuffer()
    {
        rlDisableFramebuffer();
    }
}
