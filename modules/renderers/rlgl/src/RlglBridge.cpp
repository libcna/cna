// SPDX-License-Identifier: MS-PL

#include "CNA/Logger.hpp"

#include <cstdarg>
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

    unsigned int CreateTexture2DRgba8(
        const int width, const int height, const int mipLevels, const std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D creation");
        if (width <= 0 || height <= 0 || mipLevels <= 0 || pixels == nullptr)
            throw std::invalid_argument("RLGL: invalid RGBA8 Texture2D creation request");

        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        const unsigned int id = rlLoadTexture(
            pixels, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        if (id == 0)
            throw std::runtime_error("RLGL: rlLoadTexture failed for an RGBA8 Texture2D");

        try
        {
            glBindTexture(GL_TEXTURE_2D, id);
            int levelWidth = width;
            int levelHeight = height;
            for (int level = 1; level < mipLevels; ++level)
            {
                levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
                levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
                glTexImage2D(
                    GL_TEXTURE_2D, level, GL_RGBA8, levelWidth, levelHeight, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            // rlgl has no general texture-max-level parameter. Clamp even a one-level texture so
            // XNA's mip-carrying sampler defaults never make the object incomplete.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
            ThrowIfGlError("RGBA8 Texture2D allocation");
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

    void UpdateTexture2DRgba8(
        const unsigned int id, const int level, const int width, const int height,
        const std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D update");
        if (id == 0 || level < 0 || width <= 0 || height <= 0 || pixels == nullptr)
            throw std::invalid_argument("RLGL: invalid RGBA8 Texture2D update request");

        const TextureBindingRestore bindingRestore;
        const UnpackAlignmentRestore unpackRestore;
        if (level == 0)
        {
            // The public wrapper provides the exact level-zero subimage path.
            rlUpdateTexture(
                id, 0, 0, width, height,
                RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, pixels);
        }
        else
        {
            // rlUpdateTexture hardcodes mip level zero. Higher declared levels use the dispatch
            // table rlgl loaded, retaining rlgl as owner of the texture name and normal bind path.
            glBindTexture(GL_TEXTURE_2D, id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(
                GL_TEXTURE_2D, level, 0, 0, width, height,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        ThrowIfGlError("RGBA8 Texture2D update");
    }

    void ReadTexture2DRgba8(
        const unsigned int id, const int level, const int levelWidth, const int levelHeight,
        const int x, const int y, const int width, const int height, std::uint8_t* pixels)
    {
        RequireInitialized("Texture2D readback");
        if (id == 0 || level < 0 || levelWidth <= 0 || levelHeight <= 0 ||
            x < 0 || y < 0 || width <= 0 || height <= 0 ||
            x > levelWidth - width || y > levelHeight - height || pixels == nullptr)
        {
            throw std::invalid_argument("RLGL: invalid RGBA8 Texture2D readback request");
        }

        const TextureBindingRestore restore;
        glBindTexture(GL_TEXTURE_2D, id);
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        std::vector<std::uint8_t> levelPixels(
            static_cast<std::size_t>(levelWidth) * levelHeight * 4u);
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, levelPixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        ThrowIfGlError("RGBA8 Texture2D readback");

        const std::size_t sourceRowBytes = static_cast<std::size_t>(levelWidth) * 4u;
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

    void BindDefaultFramebuffer()
    {
        rlDisableFramebuffer();
    }
}
