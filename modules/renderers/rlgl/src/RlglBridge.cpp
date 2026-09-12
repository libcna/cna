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

    void BindDefaultFramebuffer()
    {
        rlDisableFramebuffer();
    }
}
