#include "CNA/Internal/Renderers/OpenGL1/OpenGL1RenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL1
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

    // plan_opengl1.md item 21 (EasyGL parity): loaded alongside the required FBO entry points
    // above, but NOT required for `loaded_` to become true -- RenderTarget2D itself must keep
    // working even on the (expected-unreachable-in-practice) driver that has ARB_framebuffer_object
    // but genuinely lacks glGenerateMipmap; mipMap-requesting render targets on such a driver just
    // never report HasMips()==true, the same honest-failure shape OpenGL1TextureRenderer's own
    // RegenerateMips() falls back to when neither mechanism is present (minus the CPU box-filter
    // tier, which needs a CPU-side pixel buffer this GPU-only surface never has).
    PFNGLGENERATEMIPMAPPROC glGenerateMipmap_ = nullptr;

    // plan_opengl1.md item 25: unlike glGenerateMipmap (a separate, older extension family that
    // can exist independently), glRenderbufferStorageMultisample/glBlitFramebuffer are part of
    // the SAME ARB_framebuffer_object/core-3.0 entry-point family the required FBO functions
    // above already are -- loaded here alongside them (still not required for `loaded_`, so a
    // driver that somehow lacks just these two doesn't lose basic RenderTarget2D support, only
    // MSAA specifically).
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample_ = nullptr;
    PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer_ = nullptr;

    // DepthFormat: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3 (see this project's own
    // IGraphicsRenderer::CreateRenderTarget2D doc comment for the raw-ordinal convention).
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
    auto load = [](const char* name) { return LoadPlatformGlProcAddress(name); };
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
    glGenerateMipmap_ = reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(load("glGenerateMipmap"));
    glRenderbufferStorageMultisample_ = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC>(load("glRenderbufferStorageMultisample"));
    glBlitFramebuffer_ = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(load("glBlitFramebuffer"));

    loaded_ = glGenFramebuffers_ && glBindFramebuffer_ && glDeleteFramebuffers_
        && glFramebufferTexture2D_ && glCheckFramebufferStatus_ && glGenRenderbuffers_
        && glBindRenderbuffer_ && glDeleteRenderbuffers_ && glRenderbufferStorage_
        && glFramebufferRenderbuffer_;
    return loaded_;
}

OpenGL1RenderTargetRenderer::OpenGL1RenderTargetRenderer(int width, int height, int depthFormat, bool mipMap, int multiSampleCount, OpenGL1ResourceRegistry* registry)
    : requestedMultiSampleCount_(multiSampleCount), width_(width), height_(height), depthFormat_(depthFormat), mipMap_(mipMap), registry_(registry)
{
    Build();
    BuildMsaa();
    if (registry_) registry_->Add(this);
}

// plan_opengl1.md phase 8: shared by the constructor and RecreateGLResource() -- a render
// target's content is GPU-produced (nothing to restore from), so both paths build an identical
// empty FBO/color-texture/depth-renderbuffer from width_/height_/depthFormat_ alone.
void OpenGL1RenderTargetRenderer::Build()
{
    if (!loaded_)
        throw std::runtime_error("OpenGL1RenderTargetRenderer: framebuffer object functions not loaded");

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
        throw std::runtime_error("OpenGL1RenderTargetRenderer: incomplete framebuffer (GL status 0x"
            + std::to_string(status) + ")");
    }
}

// plan_opengl1.md item 25: builds the separate multisample draw FBO -- best-effort, NOT fatal on
// failure (see the header's own doc comment). Deliberately does not throw: an incomplete/
// unsupported MSAA framebuffer just means multiSampleCount_ stays 0 and every draw/resolve call
// below correctly falls back to the plain single-sample fbo_ path, matching FNA3D's own
// "real, device-clamped value, possibly 0" MultiSampleCount semantics.
void OpenGL1RenderTargetRenderer::BuildMsaa()
{
    multiSampleCount_ = 0;
    if (requestedMultiSampleCount_ <= 1 || !glRenderbufferStorageMultisample_ || !glBlitFramebuffer_)
        return;

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    int samples = requestedMultiSampleCount_;
    if (maxSamples > 0 && samples > maxSamples) samples = maxSamples;
    if (samples <= 1) return;

    glGenFramebuffers_(1, &msaaFbo_);
    glBindFramebuffer_(GL_FRAMEBUFFER, msaaFbo_);

    glGenRenderbuffers_(1, &msaaColorRbo_);
    glBindRenderbuffer_(GL_RENDERBUFFER, msaaColorRbo_);
    glRenderbufferStorageMultisample_(GL_RENDERBUFFER, samples, GL_RGBA8, width_, height_);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRbo_);

    bool hasStencil = false;
    const GLenum depthInternalFormat = DepthRenderbufferInternalFormat(depthFormat_, hasStencil);
    if (depthInternalFormat != 0)
    {
        glGenRenderbuffers_(1, &msaaDepthRbo_);
        glBindRenderbuffer_(GL_RENDERBUFFER, msaaDepthRbo_);
        glRenderbufferStorageMultisample_(GL_RENDERBUFFER, samples, depthInternalFormat, width_, height_);
        glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRbo_);
        if (hasStencil)
            glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRbo_);
    }

    const GLenum status = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        if (msaaDepthRbo_) { glDeleteRenderbuffers_(1, &msaaDepthRbo_); msaaDepthRbo_ = 0; }
        if (msaaColorRbo_) { glDeleteRenderbuffers_(1, &msaaColorRbo_); msaaColorRbo_ = 0; }
        if (msaaFbo_) { glDeleteFramebuffers_(1, &msaaFbo_); msaaFbo_ = 0; }
        std::cerr << "CNA: OpenGL1RenderTargetRenderer MSAA framebuffer incomplete (GL status 0x"
                  << std::hex << status << std::dec << ") -- falling back to single-sample" << std::endl;
        return;
    }

    multiSampleCount_ = samples;
}

OpenGL1RenderTargetRenderer::~OpenGL1RenderTargetRenderer()
{
    if (registry_) registry_->Remove(this);
    if (msaaDepthRbo_) glDeleteRenderbuffers_(1, &msaaDepthRbo_);
    if (msaaColorRbo_) glDeleteRenderbuffers_(1, &msaaColorRbo_);
    if (msaaFbo_) glDeleteFramebuffers_(1, &msaaFbo_);
    if (depthRbo_) glDeleteRenderbuffers_(1, &depthRbo_);
    if (fbo_) glDeleteFramebuffers_(1, &fbo_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
}

void OpenGL1RenderTargetRenderer::ReleaseGLHandleOnly()
{
    fbo_ = 0;
    colorTex_ = 0;
    depthRbo_ = 0;
    msaaFbo_ = 0;
    msaaColorRbo_ = 0;
    msaaDepthRbo_ = 0;
    multiSampleCount_ = 0;
    hasMips_ = false;
}

void OpenGL1RenderTargetRenderer::RecreateGLResource()
{
    try { Build(); }
    catch (const std::exception& e)
    {
        std::cerr << "CNA: OpenGL1RenderTargetRenderer failed to recreate after context loss: "
                  << e.what() << std::endl;
    }
    BuildMsaa();
}

void OpenGL1RenderTargetRenderer::BindGL(int /*unit*/) const { glBindTexture(GL_TEXTURE_2D, colorTex_); }

// plan_opengl1.md item 25: targets the multisample draw FBO when active, so all rendering goes
// to the multisample buffers -- UnbindAsRenderTarget() below resolves it into fbo_/colorTex_.
void OpenGL1RenderTargetRenderer::BindAsRenderTarget() { glBindFramebuffer_(GL_FRAMEBUFFER, multiSampleCount_ > 1 ? msaaFbo_ : fbo_); }

// plan_opengl1.md item 21 (EasyGL parity): following FNA3D's own OPENGL_ResolveTarget mechanism
// -- the whole mip chain is unconditionally regenerated from level 0 every time the target stops
// being active, matching how EasyGL's own Task 336 fix (docs/rendertarget-support.md) works.
// Must run before glBindFramebuffer_(GL_FRAMEBUFFER, 0): glGenerateMipmap reads the CURRENTLY
// bound texture object, not the framebuffer, so binding order between the two calls doesn't
// actually matter for correctness here, but is kept FBO-bound-first to mirror BindAsRenderTarget's
// own ordering and avoid a redundant glBindTexture while some other texture might be active.
//
// plan_opengl1.md item 25: when the multisample path is active, resolves msaaFbo_ into
// fbo_/colorTex_ via glBlitFramebuffer FIRST -- mips must be generated from the just-resolved
// single-sample image, not stale prior content. A multisample->single-sample blit is the
// standard, only-legal way to resolve MSAA content (src/dst dimensions must match exactly, which
// they always do here); GL_NEAREST is required by spec for a multisample source regardless of
// what filter is requested, since per-pixel sample averaging -- not spatial filtering -- is what
// actually resolves the content.
void OpenGL1RenderTargetRenderer::UnbindAsRenderTarget()
{
    if (multiSampleCount_ > 1)
    {
        glBindFramebuffer_(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, fbo_);
        glBlitFramebuffer_(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer_(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, 0);
    }

    if (mipMap_ && glGenerateMipmap_)
    {
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        glGenerateMipmap_(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        hasMips_ = true;
    }
    else if (mipMap_)
    {
        hasMips_ = false;
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
}

bool OpenGL1RenderTargetRenderer::GetData(int /*level*/, int x, int y, int w, int h, void* data, int /*dataLength*/) const
{
    if (!data || w <= 0 || h <= 0) return false;
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const int glY = height_ - y - h;
    glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);

    // Flip rows vertically (GL returned bottom-up, every other CNA CPU-side pixel buffer --
    // Texture2D::SetData/GetData, ReadBackbuffer -- is top-down), matching
    // OpenGL1Renderer::ReadBackbuffer's own identical convention.
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
    return true;
}

// plan_opengl1.md item 24 (EasyGL parity): shared by the constructor and RecreateGLResource(),
// same "GPU-produced, nothing to restore" reasoning as OpenGL1RenderTargetRenderer::Build(). Every
// face is pre-allocated with an empty (nullptr) level-0 image up front -- glFramebufferTexture2D
// (in BindAsRenderTargetFace()) requires the target level to already have a defined image, the
// same reasoning OpenGL1TextureCubeRenderer::Build() already documents for its own per-level
// pre-allocation.
void OpenGL1RenderTargetCubeRenderer::Build()
{
    if (!loaded_)
        throw std::runtime_error("OpenGL1RenderTargetCubeRenderer: framebuffer object functions not loaded");

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    for (int face = 0; face < 6; ++face)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, size_, size_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers_(1, &fbo_);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo_);
    // Attach face 0 just to check completeness here -- BindAsRenderTargetFace() re-attaches
    // whichever face is actually requested on every bind (one FBO, six re-attachments, not six
    // separate FBOs).
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, id_, 0);

    bool hasStencil = false;
    const GLenum depthInternalFormat = DepthRenderbufferInternalFormat(depthFormat_, hasStencil);
    if (depthInternalFormat != 0)
    {
        // One depth/stencil renderbuffer shared across all 6 faces -- only one face is ever the
        // active draw target at a time, so a single buffer sized to match is sufficient (the same
        // convention any real env-map-capture renderer uses), avoiding 6x the depth memory.
        glGenRenderbuffers_(1, &depthRbo_);
        glBindRenderbuffer_(GL_RENDERBUFFER, depthRbo_);
        glRenderbufferStorage_(GL_RENDERBUFFER, depthInternalFormat, size_, size_);
        glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
        if (hasStencil)
            glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
    }

    const GLenum status = glCheckFramebufferStatus_(GL_FRAMEBUFFER);
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        if (depthRbo_) { glDeleteRenderbuffers_(1, &depthRbo_); depthRbo_ = 0; }
        if (fbo_) { glDeleteFramebuffers_(1, &fbo_); fbo_ = 0; }
        if (id_) { glDeleteTextures(1, &id_); id_ = 0; }
        throw std::runtime_error("OpenGL1RenderTargetCubeRenderer: incomplete framebuffer (GL status 0x"
            + std::to_string(status) + ")");
    }
}

OpenGL1RenderTargetCubeRenderer::OpenGL1RenderTargetCubeRenderer(int size, int depthFormat, bool mipMap, OpenGL1ResourceRegistry* registry)
    : size_(size), depthFormat_(depthFormat), mipMap_(mipMap), registry_(registry)
{
    Build();
    if (registry_) registry_->Add(this);
}

OpenGL1RenderTargetCubeRenderer::~OpenGL1RenderTargetCubeRenderer()
{
    if (registry_) registry_->Remove(this);
    if (depthRbo_) glDeleteRenderbuffers_(1, &depthRbo_);
    if (fbo_) glDeleteFramebuffers_(1, &fbo_);
    if (id_) glDeleteTextures(1, &id_);
}

void OpenGL1RenderTargetCubeRenderer::ReleaseGLHandleOnly()
{
    fbo_ = 0;
    id_ = 0;
    depthRbo_ = 0;
    hasMips_ = false;
}

void OpenGL1RenderTargetCubeRenderer::RecreateGLResource()
{
    try { Build(); }
    catch (const std::exception& e)
    {
        std::cerr << "CNA: OpenGL1RenderTargetCubeRenderer failed to recreate after context loss: "
                  << e.what() << std::endl;
    }
}

void OpenGL1RenderTargetCubeRenderer::BindGL(int /*unit*/) const { glBindTexture(GL_TEXTURE_CUBE_MAP, id_); }

void OpenGL1RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
{
    if (face < 0 || face >= 6) return;
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, id_, 0);
}

// Same FNA3D OPENGL_ResolveTarget reasoning as OpenGL1RenderTargetRenderer::UnbindAsRenderTarget()
// -- glGenerateMipmap(GL_TEXTURE_CUBE_MAP) regenerates all 6 faces' mip chains in a single call,
// matching OpenGL1TextureCubeRenderer::RegenerateMips()'s own identical observation.
void OpenGL1RenderTargetCubeRenderer::UnbindAsRenderTarget()
{
    if (mipMap_ && glGenerateMipmap_)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
        glGenerateMipmap_(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        hasMips_ = true;
    }
    else if (mipMap_)
    {
        hasMips_ = false;
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
}

// Reads back via glGetTexImage directly on the cube texture object, not glReadPixels against the
// bound FBO -- a cube face's rendered content is already retrievable straight from the texture
// object regardless of which FBO/attachment point last wrote it (the same mechanism
// OpenGL1TextureCubeRenderer::GetData() already uses for CPU-uploaded content). Still needs the
// SAME row-flip OpenGL1RenderTargetRenderer::GetData() applies, though: GPU-rasterized content is
// bottom-up, unlike a CPU-uploaded face's top-down convention.
bool OpenGL1RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const
{
    if (face < 0 || face >= 6 || level < 0 || !data || w <= 0 || h <= 0) return false;
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    int levelSize = size_;
    for (int i = 0; i < level; ++i) levelSize = std::max(1, levelSize / 2);
    std::vector<uint8_t> full(static_cast<size_t>(levelSize) * levelSize * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    auto* dest = static_cast<uint8_t*>(data);
    const int rowBytes = w * 4;
    for (int row = 0; row < h; ++row)
    {
        const int srcRow = levelSize - (y + row) - 1;
        std::copy(full.data() + (static_cast<size_t>(srcRow) * levelSize + x) * 4,
                   full.data() + (static_cast<size_t>(srcRow) * levelSize + x) * 4 + rowBytes,
                   dest + static_cast<size_t>(row) * rowBytes);
    }
    return true;
}
}
