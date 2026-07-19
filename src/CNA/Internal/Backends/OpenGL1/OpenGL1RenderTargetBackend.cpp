#include "CNA/Internal/Backends/OpenGL1/OpenGL1RenderTargetBackend.hpp"
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::OpenGL1
{
namespace
{
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ = nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers_ = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ = nullptr;
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_ = nullptr;
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_ = nullptr;
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers_ = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_ = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ = nullptr;
    bool loaded_ = false;

    // DepthFormat: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3 (see this project's own
    // IGraphicsBackend::CreateRenderTarget2D doc comment for the raw-ordinal convention).
    GLenum DepthRenderbufferInternalFormat(int depthFormat, bool& hasStencil)
    {
        hasStencil = false;
        switch (depthFormat)
        {
            case 1: return GL_DEPTH_COMPONENT16;
            case 2: return GL_DEPTH_COMPONENT24;
            case 3: hasStencil = true; return GL_DEPTH24_STENCIL8;
            default: return 0;
        }
    }
}

bool TryLoadOpenGL1FramebufferObjectFunctions()
{
    auto load = [](const char* name) { return SDL_GL_GetProcAddress(name); };
    glGenFramebuffers_ = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(load("glGenFramebuffers"));
    glBindFramebuffer_ = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(load("glBindFramebuffer"));
    glDeleteFramebuffers_ = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(load("glDeleteFramebuffers"));
    glFramebufferTexture2D_ = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(load("glFramebufferTexture2D"));
    glCheckFramebufferStatus_ = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(load("glCheckFramebufferStatus"));
    glGenRenderbuffers_ = reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(load("glGenRenderbuffers"));
    glBindRenderbuffer_ = reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(load("glBindRenderbuffer"));
    glDeleteRenderbuffers_ = reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(load("glDeleteRenderbuffers"));
    glRenderbufferStorage_ = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(load("glRenderbufferStorage"));
    glFramebufferRenderbuffer_ = reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(load("glFramebufferRenderbuffer"));

    loaded_ = glGenFramebuffers_ && glBindFramebuffer_ && glDeleteFramebuffers_
        && glFramebufferTexture2D_ && glCheckFramebufferStatus_ && glGenRenderbuffers_
        && glBindRenderbuffer_ && glDeleteRenderbuffers_ && glRenderbufferStorage_
        && glFramebufferRenderbuffer_;
    return loaded_;
}

OpenGL1RenderTargetBackend::OpenGL1RenderTargetBackend(int width, int height, int depthFormat, OpenGL1ResourceRegistry* registry)
    : width_(width), height_(height), depthFormat_(depthFormat), registry_(registry)
{
    Build();
    if (registry_) registry_->Add(this);
}

// plan_opengl1.md phase 8: shared by the constructor and RecreateGLResource() -- a render
// target's content is GPU-produced (nothing to restore from), so both paths build an identical
// empty FBO/color-texture/depth-renderbuffer from width_/height_/depthFormat_ alone.
void OpenGL1RenderTargetBackend::Build()
{
    if (!loaded_)
        throw std::runtime_error("OpenGL1RenderTargetBackend: framebuffer object functions not loaded");

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers_(1, &fbo_);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    bool hasStencil = false;
    const GLenum depthInternalFormat = DepthRenderbufferInternalFormat(depthFormat_, hasStencil);
    if (depthInternalFormat != 0)
    {
        glGenRenderbuffers_(1, &depthRbo_);
        glBindRenderbuffer_(GL_RENDERBUFFER, depthRbo_);
        glRenderbufferStorage_(GL_RENDERBUFFER, depthInternalFormat, width_, height_);
        glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
        // Depth24Stencil8 is one packed renderbuffer attached to both points, rather than relying
        // on the combined GL_DEPTH_STENCIL_ATTACHMENT token -- identical result, but only needs
        // the two attachment points every ARB_framebuffer_object implementation has always had.
        if (hasStencil)
            glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
    }

    const GLenum status = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        if (depthRbo_) { glDeleteRenderbuffers_(1, &depthRbo_); depthRbo_ = 0; }
        if (fbo_) { glDeleteFramebuffers_(1, &fbo_); fbo_ = 0; }
        if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
        throw std::runtime_error("OpenGL1RenderTargetBackend: incomplete framebuffer (GL status 0x"
            + std::to_string(status) + ")");
    }
}

OpenGL1RenderTargetBackend::~OpenGL1RenderTargetBackend()
{
    if (registry_) registry_->Remove(this);
    if (depthRbo_) glDeleteRenderbuffers_(1, &depthRbo_);
    if (fbo_) glDeleteFramebuffers_(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
}

void OpenGL1RenderTargetBackend::ReleaseGLHandleOnly()
{
    fbo_ = 0;
    colorTex_ = 0;
    depthRbo_ = 0;
}

void OpenGL1RenderTargetBackend::RecreateGLResource()
{
    try { Build(); }
    catch (const std::exception& e)
    {
        std::cerr << "CNA: OpenGL1RenderTargetBackend failed to recreate after context loss: "
                  << e.what() << std::endl;
    }
}

void OpenGL1RenderTargetBackend::BindGL() const { glBindTexture(GL_TEXTURE_2D, colorTex_); }

void OpenGL1RenderTargetBackend::BindAsRenderTarget() { glBindFramebuffer_(GL_FRAMEBUFFER, fbo_); }

void OpenGL1RenderTargetBackend::UnbindAsRenderTarget() { glBindFramebuffer_(GL_FRAMEBUFFER, 0); }

void OpenGL1RenderTargetBackend::GetData(int /*level*/, int x, int y, int w, int h, void* data, int /*dataLength*/) const
{
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const int glY = height_ - y - h;
    glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);

    // Flip rows vertically (GL returned bottom-up, every other CNA CPU-side pixel buffer --
    // Texture2D::SetData/GetData, ReadBackbuffer -- is top-down), matching
    // OpenGL1GraphicsBackend::ReadBackbuffer's own identical convention.
    auto* bytes = static_cast<uint8_t*>(data);
    const int rowBytes = w * 4;
    std::vector<uint8_t> tmp(rowBytes);
    for (int i = 0; i < h / 2; ++i)
    {
        uint8_t* top = bytes + static_cast<size_t>(i) * rowBytes;
        uint8_t* bot = bytes + static_cast<size_t>(h - 1 - i) * rowBytes;
        std::copy(top, top + rowBytes, tmp.data());
        std::copy(bot, bot + rowBytes, top);
        std::copy(tmp.begin(), tmp.end(), bot);
    }
}
}
