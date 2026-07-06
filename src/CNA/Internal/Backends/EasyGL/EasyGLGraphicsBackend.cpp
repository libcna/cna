#include "CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include <iostream>

#include "CNA/Platform.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <metagl/Emscripten.hpp>
#endif
#include <metagl/Context.hpp>
#include <metagl/ContextEvents.hpp>
#include <metagl/Functions.hpp>

// Verbose 3D rendering trace. Define `CNA_DEBUG_RENDERING` (e.g. via
// -DCNA_DEBUG_RENDERING) to enable. By default these logs are silent so the
// 3D pipeline does not spam the console every frame.
#if defined(CNA_DEBUG_RENDERING)
#define CNA_RENDER_LOG(msg) do { std::cerr << "[CNA EasyGL 3D] " << msg << std::endl; } while (0)
#else
#define CNA_RENDER_LOG(msg) do { } while (0)
#endif
#include <stdexcept>
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include <SDL3/SDL.h>
#include "Microsoft/Xna/Framework/Color.hpp"

#if defined(__EMSCRIPTEN__)
EM_JS(void, CNA_DebugLoseWebGLContext, (), {
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] loseContext: canvas not found'); return; }
    const gl = Module['ctx'] || canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) { console.error('[CNA] loseContext: WebGL context not found'); return; }
    const ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) { console.error('[CNA] WEBGL_lose_context extension not available'); return; }
    console.warn('[CNA] Simulating WebGL context loss');
    ext.loseContext();
});

EM_JS(void, CNA_DebugRestoreWebGLContext, (), {
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] restoreContext: canvas not found'); return; }
    const gl = Module['ctx'] || canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) { console.error('[CNA] restoreContext: WebGL context not found'); return; }
    const ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) { console.error('[CNA] WEBGL_lose_context extension not available'); return; }
    console.warn('[CNA] Simulating WebGL context restore');
    ext.restoreContext();
});
#endif

namespace CNA::Internal::Backends::EasyGL
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    // --- EasyGLTexture3DBackend ---

    static constexpr int kTexLinear       = static_cast<int>(::metagl::TextureMagFilter::Linear);
    static constexpr int kTexClampToEdge  = static_cast<int>(::metagl::TextureWrapMode::ClampToEdge);

    EasyGLTexture3DBackend::EasyGLTexture3DBackend(int w, int h, int depth, bool /*mipMap*/, int /*surfaceFormat*/)
        : width_(w), height_(h), depth_(depth)
    {
        tex_.create();
        tex_.bind(::easygl::TextureTarget::Texture3D);
        tex_.set_image_3d(::easygl::TextureTarget::Texture3D, 0,
                          ::metagl::InternalFormat::Rgba8,
                          w, h, depth,
                          ::metagl::PixelFormat::Rgba,
                          ::metagl::PixelType::UnsignedByte,
                          nullptr);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::metagl::TextureParameter::MinFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::metagl::TextureParameter::MagFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::metagl::TextureParameter::WrapS, kTexClampToEdge);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::metagl::TextureParameter::WrapT, kTexClampToEdge);
    }

    void EasyGLTexture3DBackend::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int /*dataLength*/)
    {
        tex_.bind(::easygl::TextureTarget::Texture3D);
        tex_.set_sub_image_3d(::easygl::TextureTarget::Texture3D, level,
                              x, y, z, w, h, depth,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              data);
    }

    // --- EasyGLTextureCubeBackend ---

    static const ::easygl::TextureTarget kCubeFaceTargets[6] = {
        ::easygl::TextureTarget::TextureCubeMapPositiveX,
        ::easygl::TextureTarget::TextureCubeMapNegativeX,
        ::easygl::TextureTarget::TextureCubeMapPositiveY,
        ::easygl::TextureTarget::TextureCubeMapNegativeY,
        ::easygl::TextureTarget::TextureCubeMapPositiveZ,
        ::easygl::TextureTarget::TextureCubeMapNegativeZ,
    };

    // Mirrors TextureCube.cpp's CalculateMipLevels(size,size) — cube faces are square.
    static int CalculateCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    EasyGLTextureCubeBackend::EasyGLTextureCubeBackend(int size, bool mipMap, int /*surfaceFormat*/)
        : size_(size)
    {
        tex_.create();
        tex_.bind(::easygl::TextureTarget::TextureCubeMap);
        // Pre-allocate GPU storage for every mip level (not just level 0): SetData's box writes
        // use glTexSubImage2D, which requires the target level to already have a defined image —
        // without this loop, SetData(level>0,...) would silently fail (Task 276 finding).
        const int levelCount = mipMap ? CalculateCubeMipLevels(size) : 1;
        for (auto faceTarget : kCubeFaceTargets)
        {
            int levelSize = size;
            for (int level = 0; level < levelCount; ++level)
            {
                tex_.set_image_2d(faceTarget, level,
                                  ::metagl::InternalFormat::Rgba8,
                                  levelSize, levelSize,
                                  ::metagl::PixelFormat::Rgba,
                                  ::metagl::PixelType::UnsignedByte,
                                  nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::metagl::TextureParameter::MinFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::metagl::TextureParameter::MagFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::metagl::TextureParameter::WrapS, kTexClampToEdge);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::metagl::TextureParameter::WrapT, kTexClampToEdge);
    }

    void EasyGLTexture3DBackend::GetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          void* data, int /*dataLength*/) const
    {
        // GLES3 does not have glGetTexImage. Use a temporary FBO per Z-slice
        // with glReadPixels to read back the pixel data.
        const int bytesPerPixel = 4; // RGBA8
        auto* dest = static_cast<uint8_t*>(data);

        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(::easygl::FramebufferTarget::Framebuffer);
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));

        for (int slice = z; slice < z + depth; ++slice)
        {
            fbo.attach_texture_layer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     tex_, level, slice);
            ::metagl::glReadPixels(x, y, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   dest);
            dest += w * h * bytesPerPixel;
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLTextureCubeBackend::BindGL() const
    {
        tex_.bind(::easygl::TextureTarget::TextureCubeMap);
    }

    void EasyGLTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return;
        tex_.bind(::easygl::TextureTarget::TextureCubeMap);
        tex_.set_sub_image_2d(kCubeFaceTargets[face], level, x, y, w, h,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              data);
    }

    void EasyGLTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                            void* data, int /*dataLength*/) const
    {
        if (face < 0 || face >= 6) return;

        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(::easygl::FramebufferTarget::Framebuffer);
        fbo.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                              ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                              kCubeFaceTargets[face],
                              tex_, level);
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));

        ::metagl::glReadPixels(x, y, w, h,
                               ::metagl::PixelFormat::Rgba,
                               ::metagl::PixelType::UnsignedByte,
                               data);

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    // --- EasyGLEffectBackend ---

    bool EasyGLEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        ::easygl::Shader vs(::easygl::ShaderType::Vertex);
        vs.create();
        vs.compile_from_source(vertSrc.c_str());
        if (!vs.is_compiled())
        {
            compileError_ = "VS: " + vs.info_log();
            return false;
        }
        ::easygl::Shader fs(::easygl::ShaderType::Fragment);
        fs.create();
        fs.compile_from_source(fragSrc.c_str());
        if (!fs.is_compiled())
        {
            compileError_ = "FS: " + fs.info_log();
            return false;
        }
        program_.create();
        program_.attach(vs);
        program_.attach(fs);
        program_.link();
        if (!program_.is_linked())
        {
            compileError_ = "Link: " + program_.info_log();
            return false;
        }
        return true;
    }

    void EasyGLEffectBackend::Bind()
    {
        if (program_.is_linked())
            program_.use();
    }

    void EasyGLEffectBackend::Unbind()
    {
        // No easygl::Program::unuse() — the next bind or sprite-batch flush will override.
    }

    bool EasyGLEffectBackend::IsValid() const
    {
        return program_.is_linked();
    }

    std::string EasyGLEffectBackend::GetCompileError() const
    {
        return compileError_;
    }

    void EasyGLEffectBackend::SetUniformFloat(const char* name, float value)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, value);
    }

    void EasyGLEffectBackend::SetUniformInt(const char* name, int value)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, value);
    }

    void EasyGLEffectBackend::SetUniformVec2(const char* name, float x, float y)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y);
    }

    void EasyGLEffectBackend::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y, z);
    }

    void EasyGLEffectBackend::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y, z, w);
    }

    void EasyGLEffectBackend::SetUniformMat4(const char* name, const float* matrix)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_matrix4(loc, matrix);
    }

    // --- EasyGLOcclusionQueryBackend ---

    EasyGLOcclusionQueryBackend::EasyGLOcclusionQueryBackend(::easygl::ResourceRegistry* registry)
        : registry_(registry)
    {
        query_.create();
        if (registry_) registry_->add(this);
    }

    EasyGLOcclusionQueryBackend::~EasyGLOcclusionQueryBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLOcclusionQueryBackend::Begin()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        query_.begin(::easygl::QueryTarget::AnySamplesPassed);
    }

    void EasyGLOcclusionQueryBackend::End()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        query_.end(::easygl::QueryTarget::AnySamplesPassed);
    }

    bool EasyGLOcclusionQueryBackend::IsComplete() const
    {
        if (metagl::IsContextLost() || !query_.is_created()) return false;
        return query_.is_result_available();
    }

    int EasyGLOcclusionQueryBackend::PixelCount() const
    {
        if (!IsComplete()) return 0;
        // GLES3 uses GL_ANY_SAMPLES_PASSED — result is 0 (none) or 1 (any)
        return static_cast<int>(query_.result());
    }

    void EasyGLOcclusionQueryBackend::release_gl_handle_only()
    {
        query_.reset_handle_no_gl();
    }

    void EasyGLOcclusionQueryBackend::recreate_gl_resource()
    {
        query_.create();
    }

    // --- EasyGLTextureBackend ---

    EasyGLTextureBackend::EasyGLTextureBackend(const ImageData& data, ::easygl::ResourceRegistry* registry)
        : registry_(registry)
    {
        width = data.width;
        height = data.height;
        texture.create();
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, data.pixels.data());
        if (registry_) registry_->add(this);
    }

    EasyGLTextureBackend::~EasyGLTextureBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLTextureBackend::release_gl_handle_only()
    {
        texture.reset_handle_no_gl();
    }

    void EasyGLTextureBackend::recreate_gl_resource()
    {
        texture.create();
        if (pixels_ && !pixels_->empty())
        {
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                                 width, height, pixels_->data());
        }
        else
        {
            const std::vector<uint8_t> blank(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                                 width, height, blank.data());
        }
    }

    void EasyGLTextureBackend::BindGL() const
    {
        texture.bind(::easygl::TextureTarget::Texture2D);
    }

    void EasyGLTextureBackend::ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels)
    {
        if (registry_) pixels_ = std::move(pixels);
    }

    void EasyGLTextureBackend::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        // pixels_ (shared with Texture2D::cpuPixels_) is already updated by the caller
        // before this method is invoked — no need to update it here.
        texture.bind(::easygl::TextureTarget::Texture2D);
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, rgba);
    }

    void EasyGLTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        texture.bind(::easygl::TextureTarget::Texture2D);
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, level, levelW, levelH, rgba);
    }

    // --- EasyGLRenderTargetBackend ---

    // Mirrors Texture2D.cpp's/TextureCube.cpp's CalculateMipLevels.
    static int CalculateRenderTargetMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    EasyGLRenderTargetBackend::EasyGLRenderTargetBackend(int w, int h, bool hasDepth,
                                                          ::easygl::ResourceRegistry* registry,
                                                          bool mipMap, int multiSampleCount)
        : width_(w), height_(h), hasDepth_(hasDepth), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount), registry_(registry)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(w, h) : 1;
        CreateResources();
        if (registry_) registry_->add(this);
    }

    EasyGLRenderTargetBackend::~EasyGLRenderTargetBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLRenderTargetBackend::CreateResources()
    {
        colorTex_.create();
        // The 6-parameter set_image_2d overload does not call glBindTexture first;
        // bind the texture explicitly so glTexImage2D targets our handle.
        colorTex_.bind(::easygl::TextureTarget::Texture2D);
        // Pre-allocate GPU storage for every mip level (not just level 0): the mip chain is
        // regenerated from level 0 via generate_mipmap() when the target is unbound (see
        // UnbindAsRenderTarget), mirroring FNA3D's OPENGL_ResolveTarget behavior — without this
        // loop, levels 1+ would have no defined image and glGenerateMipmap's writes would target
        // GL-incomplete storage (Task 336 finding, same root cause as Task 276's TextureCube fix).
        {
            int levelW = width_, levelH = height_;
            for (int level = 0; level < levelCount_; ++level)
            {
                colorTex_.set_image_2d(::easygl::TextureTarget::Texture2D, level,
                                       ::metagl::InternalFormat::Rgba8,
                                       levelW, levelH,
                                       ::metagl::PixelFormat::Rgba,
                                       ::metagl::PixelType::UnsignedByte,
                                       nullptr);
                levelW = std::max(1, levelW / 2);
                levelH = std::max(1, levelH / 2);
            }
        }
        // Default GL min-filter is NEAREST_MIPMAP_LINEAR; since the RT has no mipmaps
        // it would be texture-incomplete when sampled.  Use LINEAR (no mipmaps).
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::metagl::TextureParameter::MinFilter,
                                static_cast<int>(::metagl::TextureMagFilter::Linear));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::metagl::TextureParameter::MagFilter,
                                static_cast<int>(::metagl::TextureMagFilter::Linear));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::metagl::TextureParameter::WrapS,
                                static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::metagl::TextureParameter::WrapT,
                                static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));

        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors, mirroring
        // EasyGLGraphicsBackend::CreateMsaaBuffers / FNA3D's OPENGL_GetMaxMultiSampleCount.
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        fbo_.create();
        // glFramebufferTexture2D/glFramebufferRenderbuffer operate on the currently bound FBO;
        // bind ours first.
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        if (multiSampleCount_ > 0)
        {
            // Render into a multisampled color renderbuffer (matching FNA3D's
            // FNA3D_GenColorRenderbuffer); colorTex_ is only ever the single-sample resolve
            // target, written by UnbindAsRenderTarget()'s blit, never rendered into directly.
            msaaColorRbo_.create();
            msaaColorRbo_.bind();
            msaaColorRbo_.set_storage_multisample(multiSampleCount_,
                                                   ::metagl::InternalFormat::Rgba8,
                                                   width_, height_);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     msaaColorRbo_);

            resolveFbo_.create();
            resolveFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            resolveFbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                          ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                          ::easygl::TextureTarget::Texture2D,
                                          colorTex_, 0);
            fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }
        else
        {
            fbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                   ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                   ::easygl::TextureTarget::Texture2D,
                                   colorTex_, 0);
        }

        if (hasDepth_)
        {
            depthRbo_.create();
            depthRbo_.bind();
            if (multiSampleCount_ > 0)
                depthRbo_.set_storage_multisample(multiSampleCount_,
                                                   ::metagl::InternalFormat::DepthComponent24,
                                                   width_, height_);
            else
                depthRbo_.set_storage(::metagl::InternalFormat::DepthComponent24, width_, height_);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::FramebufferAttachment::Depth,
                                     depthRbo_);
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::BindAsRenderTarget()
    {
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::UnbindAsRenderTarget()
    {
        // Resolve the multisampled color renderbuffer into colorTex_ before mips (if any) are
        // regenerated from it, matching FNA3D's OPENGL_ResolveTarget resolve-then-mipmap order.
        if (multiSampleCount_ > 0)
        {
            fbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
            resolveFbo_.bind(::easygl::FramebufferTarget::DrawFramebuffer);
            ::easygl::Framebuffer::blit(0, 0, width_, height_,
                                        0, 0, width_, height_,
                                        ::metagl::ClearBufferBit::Color,
                                        ::metagl::BlitFilter::Linear);
        }
        // Regenerate the mip chain from level 0's just-rendered (and possibly just-resolved)
        // content, matching FNA3D's OPENGL_ResolveTarget: "if (target->levelCount > 1) { ...
        // glGenerateMipmap... }".
        if (levelCount_ > 1)
        {
            colorTex_.bind(::easygl::TextureTarget::Texture2D);
            colorTex_.generate_mipmap(::easygl::TextureTarget::Texture2D);
        }
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::BindGL() const
    {
        colorTex_.bind(::easygl::TextureTarget::Texture2D);
    }

    unsigned int EasyGLRenderTargetBackend::GetColorGLHandle() const
    {
        return colorTex_.native_handle();
    }

    void EasyGLRenderTargetBackend::release_gl_handle_only()
    {
        fbo_.reset_handle_no_gl();
        resolveFbo_.reset_handle_no_gl();
        colorTex_.reset_handle_no_gl();
        depthRbo_.reset_handle_no_gl();
        msaaColorRbo_.reset_handle_no_gl();
    }

    void EasyGLRenderTargetBackend::recreate_gl_resource()
    {
        CreateResources();
    }

    // --- EasyGLRenderTargetCubeBackend ---

    EasyGLRenderTargetCubeBackend::EasyGLRenderTargetCubeBackend(int size, bool hasDepth,
                                                                    ::easygl::ResourceRegistry* registry,
                                                                    bool mipMap, int multiSampleCount)
        : size_(size), hasDepth_(hasDepth), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount), registry_(registry)
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(size, size) : 1;
        CreateResources();
        if (registry_) registry_->add(this);
    }

    EasyGLRenderTargetCubeBackend::~EasyGLRenderTargetCubeBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLRenderTargetCubeBackend::CreateResources()
    {
        cubeTex_.create();
        cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
        // Allocate storage for all 6 faces, all mip levels (see EasyGLRenderTargetBackend's
        // CreateResources for why — same Task 336 finding, applied to cube render targets).
        static const ::easygl::TextureTarget kFaceTargets[6] = {
            ::easygl::TextureTarget::TextureCubeMapPositiveX,
            ::easygl::TextureTarget::TextureCubeMapNegativeX,
            ::easygl::TextureTarget::TextureCubeMapPositiveY,
            ::easygl::TextureTarget::TextureCubeMapNegativeY,
            ::easygl::TextureTarget::TextureCubeMapPositiveZ,
            ::easygl::TextureTarget::TextureCubeMapNegativeZ,
        };
        for (auto faceTarget : kFaceTargets)
        {
            int levelSize = size_;
            for (int level = 0; level < levelCount_; ++level)
            {
                cubeTex_.set_image_2d(faceTarget, level,
                                       ::metagl::InternalFormat::Rgba8,
                                       levelSize, levelSize,
                                       ::metagl::PixelFormat::Rgba,
                                       ::metagl::PixelType::UnsignedByte,
                                       nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::metagl::TextureParameter::MinFilter,
                               static_cast<int>(::metagl::TextureMagFilter::Linear));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::metagl::TextureParameter::MagFilter,
                               static_cast<int>(::metagl::TextureMagFilter::Linear));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::metagl::TextureParameter::WrapS,
                               static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::metagl::TextureParameter::WrapT,
                               static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));

        // Clamp to GL_MAX_SAMPLES, same as EasyGLRenderTargetBackend.
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        fbo_.create();
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        if (multiSampleCount_ > 0)
        {
            // One shared multisample color renderbuffer, reused across all 6 faces (only one
            // face is ever rendered into at a time) — matches FNA's RenderTargetCube.cs, which
            // also allocates a single glColorBuffer regardless of face. resolveFbo_ is
            // re-attached to whichever face was most recently bound (see BindAsRenderTargetFace)
            // so UnbindAsRenderTarget's blit resolves into the correct face.
            msaaColorRbo_.create();
            msaaColorRbo_.bind();
            msaaColorRbo_.set_storage_multisample(multiSampleCount_,
                                                   ::metagl::InternalFormat::Rgba8,
                                                   size_, size_);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     msaaColorRbo_);
            resolveFbo_.create();
        }

        if (hasDepth_)
        {
            depthRbo_.create();
            depthRbo_.bind();
            if (multiSampleCount_ > 0)
                depthRbo_.set_storage_multisample(multiSampleCount_,
                                                   ::metagl::InternalFormat::DepthComponent24,
                                                   size_, size_);
            else
                depthRbo_.set_storage(::metagl::InternalFormat::DepthComponent24, size_, size_);
            fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::FramebufferAttachment::Depth,
                                      depthRbo_);
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        lastFace_ = face;
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        const auto faceTarget = static_cast<::easygl::TextureTarget>(
            static_cast<unsigned int>(::easygl::TextureTarget::TextureCubeMapPositiveX) + face);
        if (multiSampleCount_ == 0)
        {
            // Non-MSAA: fbo_'s color attachment IS cubeTex_ — re-attach the requested face
            // (0=+X .. 5=-Z) directly, since all faces share this one FBO/texture.
            fbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                    faceTarget,
                                    cubeTex_, 0);
        }
        // MSAA: fbo_'s color attachment is the shared msaaColorRbo_, which is face-agnostic
        // (a renderbuffer, not a cube texture) — nothing to re-attach on bind; the face only
        // matters when UnbindAsRenderTarget resolves into cubeTex_'s specific face image.
    }

    void EasyGLRenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (multiSampleCount_ > 0)
        {
            const auto faceTarget = static_cast<::easygl::TextureTarget>(
                static_cast<unsigned int>(::easygl::TextureTarget::TextureCubeMapPositiveX) + lastFace_);
            resolveFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            resolveFbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                          ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                          faceTarget,
                                          cubeTex_, 0);
            fbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
            resolveFbo_.bind(::easygl::FramebufferTarget::DrawFramebuffer);
            ::easygl::Framebuffer::blit(0, 0, size_, size_,
                                        0, 0, size_, size_,
                                        ::metagl::ClearBufferBit::Color,
                                        ::metagl::BlitFilter::Linear);
        }
        // Regenerate the mip chain for all 6 faces from their just-rendered (and possibly
        // just-resolved) level-0 content.
        if (levelCount_ > 1)
        {
            cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
            cubeTex_.generate_mipmap(::easygl::TextureTarget::TextureCubeMap);
        }
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    unsigned int EasyGLRenderTargetCubeBackend::GetGLHandle() const
    {
        return cubeTex_.native_handle();
    }

    void EasyGLRenderTargetCubeBackend::BindGL() const
    {
        cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
    }

    void EasyGLRenderTargetCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                                 const void* data, int /*dataLength*/)
    {
        if (face < 0 || face >= 6) return;
        cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
        cubeTex_.set_sub_image_2d(kCubeFaceTargets[face], level, x, y, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   data);
    }

    void EasyGLRenderTargetCubeBackend::release_gl_handle_only()
    {
        fbo_.reset_handle_no_gl();
        resolveFbo_.reset_handle_no_gl();
        cubeTex_.reset_handle_no_gl();
        depthRbo_.reset_handle_no_gl();
        msaaColorRbo_.reset_handle_no_gl();
    }

    void EasyGLRenderTargetCubeBackend::recreate_gl_resource()
    {
        CreateResources();
    }

    // --- EasyGLSpriteBatchBackend ---

    EasyGLSpriteBatchBackend::EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                                       EasyGLGraphicsBackend* backend)
        : device_(device)
        , registry_(registry)
        , graphicsBackend_(backend)
    {
        InitializeResources();
        if (registry_) registry_->add(this);
    }

    EasyGLSpriteBatchBackend::~EasyGLSpriteBatchBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLSpriteBatchBackend::release_gl_handle_only()
    {
        program_.reset_handle_no_gl();
        customProgram_.reset_handle_no_gl();
        vao_.reset_handle_no_gl();
        vbo_.reset_handle_no_gl();
        ibo_.reset_handle_no_gl();
    }

    void EasyGLSpriteBatchBackend::recreate_gl_resource()
    {
        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
        transform_ = Matrix::getIdentityProperty();
        compiledFor_ = nullptr;
        InitializeResources();
    }

    void EasyGLSpriteBatchBackend::InitializeResources()
    {
        const char* vertexShaderSource = R"(#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
)";

        const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D texture1;

void main()
{
    FragColor = texture(texture1, TexCoord) * Color;
}
)";

        ::easygl::Shader vertexShader(::easygl::ShaderType::Vertex);
        vertexShader.create();
        vertexShader.compile_from_source(vertexShaderSource);

        if (!vertexShader.is_compiled())
        {
            std::cerr << "Vertex shader compilation failed:\n" << vertexShader.info_log() << std::endl;
        }

        ::easygl::Shader fragmentShader(::easygl::ShaderType::Fragment);
        fragmentShader.create();
        fragmentShader.compile_from_source(fragmentShaderSource);

        if (!fragmentShader.is_compiled())
        {
            std::cerr << "Fragment shader compilation failed:\n" << fragmentShader.info_log() << std::endl;
        }

        program_.create();
        program_.attach(vertexShader);
        program_.attach(fragmentShader);
        program_.link();

        if (!program_.is_linked())
        {
            std::cerr << "Shader program linking failed:\n" << program_.info_log() << std::endl;
        }

        program_.use();
        const int textureLocation = program_.uniform_location("texture1");
        if (textureLocation >= 0)
        {
            program_.set_uniform(textureLocation, 0);
        }

        vbo_.create();
        ibo_.create();
        vao_.create();

        vao_.bind();
        vbo_.bind(::easygl::BufferTarget::Array);

        // Position (0), TexCoord (1), Color (2)
        vao_.enable_attribute(0);
        vao_.set_attribute_pointer(0, 2, ::easygl::DataType::Float, false, 8 * sizeof(float), (void*)0);

        vao_.enable_attribute(1);
        vao_.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false, 8 * sizeof(float),
                                   (void*)(2 * sizeof(float)));

        vao_.enable_attribute(2);
        vao_.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, 8 * sizeof(float),
                                   (void*)(4 * sizeof(float)));

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        vao_.unbind();
    }

    void EasyGLSpriteBatchBackend::Begin()
    {
        begun = true;
        device_.set_blend_enabled(true);
        device_.set_blend_func(::easygl::BlendFactor::SrcAlpha, ::easygl::BlendFactor::OneMinusSrcAlpha);
    }

    void EasyGLSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void EasyGLSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void EasyGLSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void EasyGLSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void EasyGLSpriteBatchBackend::End()
    {
        FlushBatch();
        begun = false;
    }

    void EasyGLSpriteBatchBackend::FlushBatch()
    {
        if (pending_vertices_.empty()) return;

        // Determine which GL program to use: built-in or custom Effect.
        // Access vertex/fragment source via virtual Effect::GetVertexSource()/GetFragmentSource()
        // to avoid a circular dependency on the concrete ShaderEffect type.
        ::easygl::Program* prog = &program_;
        if (customEffect_ && !customEffect_->GetVertexSource().empty())
        {
            if (compiledFor_ != customEffect_)
            {
                const std::string& vertSrc = customEffect_->GetVertexSource();
                const std::string& fragSrc = customEffect_->GetFragmentSource();

                ::easygl::Shader vert(::easygl::ShaderType::Vertex);
                vert.create();
                vert.compile_from_source(vertSrc.c_str());
                if (!vert.is_compiled())
                    std::cerr << "SpriteBatch custom vertex shader failed:\n" << vert.info_log() << "\n";

                ::easygl::Shader frag(::easygl::ShaderType::Fragment);
                frag.create();
                frag.compile_from_source(fragSrc.c_str());
                if (!frag.is_compiled())
                    std::cerr << "SpriteBatch custom fragment shader failed:\n" << frag.info_log() << "\n";

                customProgram_.create();
                customProgram_.attach(vert);
                customProgram_.attach(frag);
                customProgram_.link();
                if (!customProgram_.is_linked())
                    std::cerr << "SpriteBatch custom program link failed:\n" << customProgram_.info_log() << "\n";

                compiledFor_ = customEffect_;
            }
            prog = &customProgram_;
            customEffect_->Apply();
        }
        else if (customEffect_)
        {
            // Effect has no GLSL source; run OnApply() for parameter side-effects
            // and register it as the active effect on the device.
            customEffect_->Apply();
        }

        prog->use();

        int logW = 0, logH = 0;
        if (graphicsBackend_)
        {
            int physW = 0, physH = 0;
            graphicsBackend_->getPhysicalSize(physW, physH);
            if (physW > 0 && physH > 0)
                device_.set_viewport(0, 0, physW, physH);
            graphicsBackend_->getLogicalSize(logW, logH);
        }
        if (logW <= 0 || logH <= 0)
        {
            int vx, vy, vw, vh;
            device_.get_viewport(vx, vy, vw, vh);
            logW = vw;
            logH = vh;
        }

        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logW),
            static_cast<float>(logH), 0.0f,
            -1.0f, 1.0f);
        const Matrix combined = transform_ * orthoM;
        float ortho[16];
        combined.ToColumnMajor(ortho);
        const int projLoc = prog->uniform_location("projection");
        if (projLoc >= 0)
            prog->set_uniform_matrix4(projLoc, ortho);

        current_texture_->BindGL();
        if (graphicsBackend_)
            graphicsBackend_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        vbo_.bind(::easygl::BufferTarget::Array);
        vbo_.set_data(::easygl::BufferTarget::Array,
                      pending_vertices_.data(),
                      pending_vertices_.size() * sizeof(Vertex));

        vao_.bind();

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        ibo_.set_data(::easygl::BufferTarget::ElementArray,
                      pending_indices_.data(),
                      pending_indices_.size() * sizeof(uint16_t));

        device_.draw_elements(
            ::easygl::PrimitiveType::Triangles,
            static_cast<int>(pending_indices_.size()),
            ::easygl::DataType::UnsignedShort,
            nullptr
        );

        vao_.unbind();

        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h),
             Microsoft::Xna::Framework::Color::White);
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        if (!begun) throw std::runtime_error("Draw called before Begin()");

        // Flush pending batch if texture changes
        if (current_texture_ != nullptr && current_texture_ != &texture)
            FlushBatch();
        current_texture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp here — matches FNA, which divides straight through with no clamping
        // (SpriteBatch.cs, e.g. the Draw(..., Rectangle? sourceRectangle, ...) overloads).
        // A sourceRectangle that extends past the texture bounds intentionally produces UVs
        // outside [0,1], letting the bound SamplerState's TextureAddressMode (Wrap/Mirror/Clamp)
        // govern edge sampling — the classic XNA scrolling/tiling-background technique.
        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if ((int)effects & (int)SpriteEffects::FlipHorizontally) std::swap(u1, u2);
        if ((int)effects & (int)SpriteEffects::FlipVertically) std::swap(v1, v2);

        float r = (float)color.getRProperty() / 255.0f;
        float g = (float)color.getGProperty() / 255.0f;
        float b = (float)color.getBProperty() / 255.0f;
        float a = (float)color.getAProperty() / 255.0f;

        float dx = (float)destinationRectangle.X;
        float dy = (float)destinationRectangle.Y;
        float dw = (float)destinationRectangle.Width;
        float dh = (float)destinationRectangle.Height;

        float sw = (float)sourceRectangle.Width;
        float sh = (float)sourceRectangle.Height;

        float ox = origin.X;
        float oy = origin.Y;

        float scaleX = dw / sw;
        float scaleY = dh / sh;

        float p0x = (0.0f - ox) * scaleX,  p0y = (0.0f - oy) * scaleY;
        float p1x = (sw   - ox) * scaleX,  p1y = (0.0f - oy) * scaleY;
        float p2x = (sw   - ox) * scaleX,  p2y = (sh   - oy) * scaleY;
        float p3x = (0.0f - ox) * scaleX,  p3y = (sh   - oy) * scaleY;

        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        const auto base = static_cast<uint16_t>(pending_vertices_.size());

        pending_vertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        pending_vertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        pending_vertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        pending_vertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        pending_indices_.push_back(base + 0);
        pending_indices_.push_back(base + 1);
        pending_indices_.push_back(base + 2);
        pending_indices_.push_back(base + 2);
        pending_indices_.push_back(base + 3);
        pending_indices_.push_back(base + 0);
    }

    // --- EasyGLGraphicsBackend ---

    EasyGLGraphicsBackend::EasyGLGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode mode, bool contextRecoveryEnabled,
                                                  int multiSampleCount, int swapInterval)
        : window(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
        , contextRecoveryEnabled_(contextRecoveryEnabled)
        , sampleCount_(multiSampleCount > 1 ? multiSampleCount : 1)
        , swapInterval_(swapInterval)
    {
        if (!window) throw std::runtime_error("EasyGLGraphicsBackend initialized with null window.");

        // Register this backend so SdlInputBridge can apply the same
        // physical→logical coordinate transform for mouse/touch input.
        IGraphicsBackend::RegisterForWindow(window, this);

        // NOTE: SDL_Window is NOT owned by EasyGL backend.
        // It is owned by GraphicsDevice or higher level platform layer.

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        // Without this, no window ever gets stencil bits (SDL defaults to 0), making
        // DepthStencilState.StencilEnable a permanent no-op regardless of what's requested.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        // NOTE: GL context IS owned by EasyGL backend.
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
        {
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }

        device.initialize(reinterpret_cast<::easygl::GLGetProcAddressFn>(SDL_GL_GetProcAddress));
        std::cout << "EasyGLGraphicsBackend initialized with OpenGL "
            << device.capabilities().context_info().version_string << std::endl;

        SDL_GL_SetSwapInterval(swapInterval_);

        registry_.register_with_meta_gl();

        if (sampleCount_ > 1)
        {
            int physW, physH;
            SDL_GetWindowSize(window, &physW, &physH);
            CreateMsaaBuffers(physW, physH);
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }

#if defined(__EMSCRIPTEN__)
        metagl::InstallEmscriptenContextLossCallbacks();
#endif
    }

    void EasyGLGraphicsBackend::CreateMsaaBuffers(int w, int h)
    {
        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors.
        GLint maxSamples = 0;
        metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
        if (maxSamples > 0 && sampleCount_ > static_cast<int>(maxSamples))
            sampleCount_ = static_cast<int>(maxSamples);

        msaaW_ = w; msaaH_ = h;
        if (!msaaFbo_.is_created()) msaaFbo_.create();
        if (!msaaColorRbo_.is_created()) msaaColorRbo_.create();
        if (!msaaDepthRbo_.is_created()) msaaDepthRbo_.create();

        msaaColorRbo_.bind();
        msaaColorRbo_.set_storage_multisample(sampleCount_,
                                               ::metagl::InternalFormat::Rgba8, w, h);
        msaaDepthRbo_.bind();
        msaaDepthRbo_.set_storage_multisample(sampleCount_,
                                               ::metagl::InternalFormat::DepthComponent24, w, h);

        msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        msaaFbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                      msaaColorRbo_);
        msaaFbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::FramebufferAttachment::Depth,
                                      msaaDepthRbo_);
    }

    void EasyGLGraphicsBackend::BindDefaultFramebuffer()
    {
        if (sampleCount_ > 1)
        {
            // Recreate MSAA FBO if the window was resized.
            int physW, physH;
            SDL_GetWindowSize(window, &physW, &physH);
            if (physW != msaaW_ || physH != msaaH_)
                CreateMsaaBuffers(physW, physH);

            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }
        else
        {
            ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
        }
    }

    void EasyGLGraphicsBackend::ResolveMsaa()
    {
        if (sampleCount_ <= 1) return;
        // Blit colour attachment from MSAA FBO to default framebuffer (FBO 0).
        msaaFbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::DrawFramebuffer);
        ::easygl::Framebuffer::blit(0, 0, msaaW_, msaaH_,
                                     0, 0, msaaW_, msaaH_,
                                     ::metagl::ClearBufferBit::Color,
                                     ::metagl::BlitFilter::Nearest);
    }

    EasyGLGraphicsBackend::~EasyGLGraphicsBackend()
    {
        if (window) IGraphicsBackend::UnregisterForWindow(window);
        if (gl_context) SDL_GL_DestroyContext(gl_context);
        // window is NOT owned by the backend.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    void EasyGLGraphicsBackend::DebugSimulateContextLoss()
    {
#if defined(__EMSCRIPTEN__)
        CNA_DebugLoseWebGLContext();
        // The webglcontextlost canvas event fires asynchronously and triggers
        // metagl::NotifyContextLost() via InstallEmscriptenContextLossCallbacks().
#else
        std::cerr << "[CNA] Simulating desktop GL context loss + immediate recreate" << std::endl;

        // 1. Notify listeners that context is lost. ResourceRegistry calls
        //    release_gl_handle_only() on every tracked resource (zeros handles,
        //    no GL calls made). Context is still valid here for proper cleanup.
        metagl::NotifyContextLost();

        // 3D programs are recreated lazily by their Ensure* helpers.
        // Reset all handles so create() allocates fresh programs.
        prog_colored_.reset_no_gl();
        prog_textured_.reset_no_gl();
        prog_col_textured_.reset_no_gl();
        prog_lit_textured_.reset_no_gl();
        prog_dual_textured_.reset_no_gl();
        prog_env_mapped_.reset_no_gl();
        prog_skinned_.reset_no_gl();
        default_white_texture_.reset_handle_no_gl();
        default_white_texture_ready_ = false;

        // 2. Destroy and recreate the SDL GL context.
        if (gl_context)
        {
            SDL_GL_MakeCurrent(window, nullptr);
            SDL_GL_DestroyContext(gl_context);
            gl_context = nullptr;
        }
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed during debug context loss: ") + SDL_GetError());
        SDL_GL_MakeCurrent(window, gl_context);

        // 3. Reload GL function pointers and increment context generation.
        device.initialize(reinterpret_cast<::easygl::GLGetProcAddressFn>(SDL_GL_GetProcAddress));

        // 4. Notify listeners that context is restored. ResourceRegistry calls
        //    recreate_gl_resource() on every tracked resource (shaders, textures, buffers, VAOs).
        metagl::NotifyContextRestored();

        std::cerr << "[CNA] Desktop GL context recreated and all resources restored" << std::endl;
#endif
    }

    void EasyGLGraphicsBackend::DebugRestoreContext()
    {
#if defined(__EMSCRIPTEN__)
        CNA_DebugRestoreWebGLContext();
        // The webglcontextrestored canvas event fires asynchronously and triggers
        // metagl::NotifyContextRestored() via InstallEmscriptenContextLossCallbacks().
#else
        // On desktop, loss+restore is a single atomic operation.
        DebugSimulateContextLoss();
#endif
    }

    void EasyGLGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (metagl::IsContextLost())
            throw std::runtime_error("ReadBackbuffer: GL context is lost");

        // On the default framebuffer (no render target bound), explicitly select
        // GL_BACK as the read source.  EGL/GLES3 contexts do not guarantee that
        // the read buffer defaults to GL_BACK, so skipping this call can leave
        // the read buffer pointing at GL_NONE and glReadPixels returns zeros.
        // When a render-target FBO is bound, the read buffer is already
        // GL_COLOR_ATTACHMENT0, so no explicit call is needed there.
        if (currentRtHeight_ == 0)
        {
            if (sampleCount_ > 1)
            {
                // Resolve MSAA FBO to FBO 0 so glReadPixels can sample the single-sample copy.
                ResolveMsaa();
                // Bind FBO 0 as the read source and select GL_BACK.
                ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::ReadFramebuffer);
            }
            device.set_read_buffer(::easygl::ReadBuffer::Back);
        }

        // Use the render-target's own height for the Y-flip when an RT is bound;
        // fall back to the window/viewport height for the default framebuffer.
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int vpW;
            GetViewportSize(vpW, fbH);
        }

        // OpenGL origin is bottom-left; flip y so caller gets top-left origin.
        const int glY = fbH - y - h;

        device.read_pixels(x, glY, w, h, ::metagl::PixelFormat::Rgba,
                           ::metagl::PixelType::UnsignedByte, pixels);

        // Flip rows vertically (GL returned bottom-up, XNA expects top-down).
        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int i = 0; i < h / 2; ++i)
        {
            uint8_t* top = pixels + i * rowBytes;
            uint8_t* bot = pixels + (h - 1 - i) * rowBytes;
            std::copy(top, top + rowBytes, tmp.data());
            std::copy(bot, bot + rowBytes, top);
            std::copy(tmp.begin(), tmp.end(), bot);
        }

        // After reading from FBO 0, restore the MSAA FBO as the draw target.
        if (sampleCount_ > 1 && currentRtHeight_ == 0)
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        device.set_viewport(0, 0, width, height);
        device.set_clear_color(r, g, b, a);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth);
    }

    void EasyGLGraphicsBackend::Present()
    {
        if (metagl::IsContextLost()) return;
        if (sampleCount_ > 1)
            ResolveMsaa();
        SDL_GL_SwapWindow(window);
        if (sampleCount_ > 1)
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void EasyGLGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void EasyGLGraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    void EasyGLGraphicsBackend::getLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            SDL_GetWindowSize(window, &width, &height);
            return;
        }
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>((double)physW * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void EasyGLGraphicsBackend::getPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window, &width, &height);
    }

    bool EasyGLGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                          float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool EasyGLGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical: logical = window * (virtualHeight_ / physH), so
        // window = logical * (physH / virtualHeight_). This is a pure uniform scale with NO offset,
        // which is exact for EasyGL's default FixedHeightDynamicWidth presentation: the logical
        // height is fixed and the logical *width* is derived from the window aspect
        // (getLogicalSize), so the logical viewport fills the whole window — there are no letterbox
        // bars and hence no offset to apply (unlike the SDL_Renderer backend's true-letterbox
        // modes, whose offset is handled by SDL_RenderCoordinates{From,To}Window). EasyGL does not
        // implement per-mode offset transforms for its non-default modes; that is a pre-existing
        // graphics-presentation concern, not an input-layer one, and the default mode is exact.
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    void EasyGLGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    std::unique_ptr<ITextureBackend> EasyGLGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<EasyGLTextureBackend>(data, RegistryPtr());
    }

    std::unique_ptr<ISpriteBatchBackend> EasyGLGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<EasyGLSpriteBatchBackend>(device, RegistryPtr(), this);
    }

    std::unique_ptr<IOcclusionQueryBackend> EasyGLGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<EasyGLOcclusionQueryBackend>(RegistryPtr());
    }

    std::unique_ptr<IRenderTargetBackend> EasyGLGraphicsBackend::CreateRenderTarget2D(int w, int h, bool hasDepth, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<EasyGLRenderTargetBackend>(w, h, hasDepth, RegistryPtr(), mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeBackend> EasyGLGraphicsBackend::CreateRenderTargetCube(int size, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<EasyGLRenderTargetCubeBackend>(size, true, RegistryPtr(), mipMap, multiSampleCount);
    }

    std::unique_ptr<ITexture3DBackend> EasyGLGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<EasyGLTexture3DBackend>(w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeBackend> EasyGLGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<EasyGLTextureCubeBackend>(size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IEffectBackend> EasyGLGraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto backend = std::make_unique<EasyGLEffectBackend>();
        backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    void EasyGLGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        mrtFboReady_ = false;
        // Regenerate mips (if requested) for whatever single RT/cube-face was previously
        // active, before switching away from it — see UnbindAsRenderTarget's Task 336 comment.
        if (currentRt2D_ && currentRt2D_ != rt) currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_) currentRtCube_->UnbindAsRenderTarget();
        currentRtCube_ = nullptr;
        currentRt2D_   = rt;
        if (rt)
        {
            currentRtHeight_ = rt->GetHeight();
            rt->BindAsRenderTarget();
        }
        else
        {
            currentRtHeight_ = 0;
            BindDefaultFramebuffer();
        }
    }

    void EasyGLGraphicsBackend::SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
    {
        if (!rt) { SetRenderTarget2D(nullptr); return; }
        mrtFboReady_ = false;
        if (currentRt2D_) currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_ && currentRtCube_ != rt) currentRtCube_->UnbindAsRenderTarget();
        currentRt2D_   = nullptr;
        currentRtCube_ = rt;
        currentRtHeight_ = rt->GetSize();
        rt->BindAsRenderTargetFace(face);
    }

    void EasyGLGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1)
        {
            SetRenderTarget2D(rts[0]);
            return;
        }

        // MRT: unbind whatever single RT/cube-face was previously active (mip regen if needed).
        // MRT + per-target mipmaps is not supported (Task 336) — MRT targets never get tracked
        // as currentRt2D_/currentRtCube_, so switching away from MRT mode cannot regenerate
        // their mips; this is an accepted, documented gap, not a silent correctness issue for
        // the common single-RT case this fix targets.
        if (currentRt2D_)   currentRt2D_->UnbindAsRenderTarget();
        if (currentRtCube_) currentRtCube_->UnbindAsRenderTarget();
        currentRt2D_   = nullptr;
        currentRtCube_ = nullptr;

        // MRT: build a combined FBO with one color attachment per render target.
        if (!mrtFboReady_)
        {
            mrtFbo_.create();
            mrtFboReady_ = true;
        }
        mrtFbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        constexpr int kMaxMRT = 8;
        const int n = count < kMaxMRT ? count : kMaxMRT;
        for (int i = 0; i < n; ++i)
        {
            const auto* eglRT = static_cast<const EasyGLRenderTargetBackend*>(rts[i]);
            const auto colorAttach = static_cast<::metagl::ColorAttachment>(
                static_cast<GLenum>(::metagl::ColorAttachment::Color0) + static_cast<GLenum>(i));
            mrtFbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::to_framebuffer_attachment(colorAttach),
                                      ::easygl::TextureTarget::Texture2D,
                                      eglRT->GetEasyGLColorTexture(), 0);
        }

        ::easygl::DrawBuffer drawBufs[kMaxMRT];
        for (int i = 0; i < n; ++i) {
            const auto colorAttach = static_cast<::metagl::ColorAttachment>(
                static_cast<GLenum>(::metagl::ColorAttachment::Color0) + static_cast<GLenum>(i));
            drawBufs[i] = ::metagl::to_draw_buffer(colorAttach);
        }
        mrtFbo_.set_draw_buffers(std::span<const ::easygl::DrawBuffer>(drawBufs, n));
    }

    namespace
    {
        // XNA Blend enum → easygl BlendFactor
        // Blend: One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
        //        InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        //        DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        //        InverseBlendFactor=11, SourceAlphaSaturation=12
        ::easygl::BlendFactor ToEasyGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
            case  1: return ::easygl::BlendFactor::Zero;
            case  2: return ::easygl::BlendFactor::SrcColor;
            case  3: return ::easygl::BlendFactor::OneMinusSrcColor;
            case  4: return ::easygl::BlendFactor::SrcAlpha;
            case  5: return ::easygl::BlendFactor::OneMinusSrcAlpha;
            case  6: return ::easygl::BlendFactor::DstColor;
            case  7: return ::easygl::BlendFactor::OneMinusDstColor;
            case  8: return ::easygl::BlendFactor::DstAlpha;
            case  9: return ::easygl::BlendFactor::OneMinusDstAlpha;
            case 10: return ::easygl::BlendFactor::ConstantColor;
            case 11: return ::easygl::BlendFactor::OneMinusConstantColor;
            case 12: return ::easygl::BlendFactor::SrcAlphaSaturate;
            default: return ::easygl::BlendFactor::One;  // Blend::One = 0
            }
        }

        // XNA BlendFunction enum → easygl BlendEquation
        // BlendFunction: Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4
        ::easygl::BlendEquation ToEasyGLBlendEquation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
            case 1: return ::easygl::BlendEquation::FuncSubtract;
            case 2: return ::easygl::BlendEquation::FuncReverseSubtract;
            case 3: return ::easygl::BlendEquation::Max;
            case 4: return ::easygl::BlendEquation::Min;
            default: return ::easygl::BlendEquation::FuncAdd;  // BlendFunction::Add = 0
            }
        }

        // XNA CompareFunction enum → easygl CompareFunc
        // CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
        //                  GreaterEqual=5, Greater=6, NotEqual=7
        ::easygl::CompareFunc ToEasyGLCompareFunc(int xnaCompare)
        {
            switch (xnaCompare)
            {
            case 1: return ::easygl::CompareFunc::Never;
            case 2: return ::easygl::CompareFunc::Less;
            case 3: return ::easygl::CompareFunc::Lequal;
            case 4: return ::easygl::CompareFunc::Equal;
            case 5: return ::easygl::CompareFunc::Gequal;
            case 6: return ::easygl::CompareFunc::Greater;
            case 7: return ::easygl::CompareFunc::Notequal;
            default: return ::easygl::CompareFunc::Always;  // CompareFunction::Always = 0
            }
        }

        // XNA StencilOperation ordinals: Keep=0, Zero=1, Replace=2, Increment=3,
        // Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7
        ::easygl::StencilOp ToEasyGLStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
            case 1: return ::easygl::StencilOp::Zero;
            case 2: return ::easygl::StencilOp::Replace;
            case 3: return ::easygl::StencilOp::IncrWrap;
            case 4: return ::easygl::StencilOp::DecrWrap;
            case 5: return ::easygl::StencilOp::Incr;
            case 6: return ::easygl::StencilOp::Decr;
            case 7: return ::easygl::StencilOp::Invert;
            default: return ::easygl::StencilOp::Keep;  // StencilOperation::Keep = 0
            }
        }

        ::easygl::PrimitiveType ToEasyGl(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return ::easygl::PrimitiveType::Triangles;
            case PrimitiveType::TriangleStrip: return ::easygl::PrimitiveType::TriangleStrip;
            case PrimitiveType::LineList:      return ::easygl::PrimitiveType::Lines;
            case PrimitiveType::LineStrip:     return ::easygl::PrimitiveType::LineStrip;
            case PrimitiveType::PointListEXT:  return ::easygl::PrimitiveType::Points;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:       return primitiveCount * 2;
            case PrimitiveType::LineStrip:      return primitiveCount + 1;
            case PrimitiveType::PointListEXT:   return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Graphics state
    // -------------------------------------------------------------------------

    void EasyGLGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc)
    {
        if (metagl::IsContextLost()) return;
        // Blend::One=0, Blend::Zero=1 → Opaque preset: src=One, dst=Zero → effectively no blending
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        device.set_blend_enabled(blendEnabled);
        if (blendEnabled)
        {
            device.set_blend_func_separate(
                ToEasyGLBlendFactor(colorSrcBlend), ToEasyGLBlendFactor(colorDstBlend),
                ToEasyGLBlendFactor(alphaSrcBlend), ToEasyGLBlendFactor(alphaDstBlend));
            device.set_blend_equation_separate(
                ToEasyGLBlendEquation(colorBlendFunc),
                ToEasyGLBlendEquation(alphaBlendFunc));
        }
    }

    void EasyGLGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail, int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask, int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        if (metagl::IsContextLost()) return;

        device.set_depth_test_enabled(depthEnable);
        device.set_depth_mask(depthWriteEnable);
        if (depthEnable)
            device.set_depth_func(ToEasyGLCompareFunc(depthFunc));

        device.set_stencil_test_enabled(stencilEnable);
        if (stencilEnable)
        {
            const auto eglSFail  = ToEasyGLStencilOp(stencilFail);
            const auto eglDFail  = ToEasyGLStencilOp(stencilDepthFail);
            const auto eglPass   = ToEasyGLStencilOp(stencilPass);
            if (twoSidedStencilMode)
            {
                device.set_stencil_func_separate(::easygl::CullFace::Front,
                    ToEasyGLCompareFunc(stencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op_separate(::easygl::CullFace::Front,
                    eglSFail, eglDFail, eglPass);
                device.set_stencil_mask_separate(::easygl::CullFace::Front,
                    static_cast<unsigned int>(stencilWriteMask));

                device.set_stencil_func_separate(::easygl::CullFace::Back,
                    ToEasyGLCompareFunc(ccwStencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op_separate(::easygl::CullFace::Back,
                    ToEasyGLStencilOp(ccwStencilFail),
                    ToEasyGLStencilOp(ccwStencilDepthFail),
                    ToEasyGLStencilOp(ccwStencilPass));
                device.set_stencil_mask_separate(::easygl::CullFace::Back,
                    static_cast<unsigned int>(stencilWriteMask));
            }
            else
            {
                device.set_stencil_func(ToEasyGLCompareFunc(stencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op(eglSFail, eglDFail, eglPass);
                device.set_stencil_mask(static_cast<unsigned int>(stencilWriteMask));
            }
        }
    }

    void EasyGLGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float /*depthBias*/,
                                                      float /*slopeScaleDepthBias*/)
    {
        if (metagl::IsContextLost()) return;
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // OpenGL default front face is CCW; CW faces are back faces.
        if (cullMode == 0)
        {
            device.set_cull_face_enabled(false);
        }
        else
        {
            device.set_cull_face_enabled(true);
            device.set_cull_face(cullMode == 1 ? ::easygl::CullFace::Back
                                                : ::easygl::CullFace::Front);
        }
        device.set_scissor_test_enabled(scissorTestEnable);
        // OpenGL ES has no glPolygonMode; FillMode::WireFrame (1) is emulated at draw
        // time by re-expanding triangles into GL_LINES (see DrawWireframe).
        wireframe_ = (fillMode == 1);
    }

    void EasyGLGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (metagl::IsContextLost()) return;
        if (w <= 0 || h <= 0) return; // invalid rect — leave scissor state unchanged
        // OpenGL scissor origin is bottom-left; convert from top-left XNA coordinates.
        int physW, physH;
        getPhysicalSize(physW, physH);
        device.set_scissor(x, physH - y - h, w, h);
        // Do NOT enable/disable scissor test here — that is controlled exclusively
        // by ApplyRasterizerState via RasterizerState.ScissorTestEnable.
    }

    void EasyGLGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        device.set_blend_color(r, g, b, a);
    }

    void EasyGLGraphicsBackend::ApplySamplerState(int slot, int filter,
                                                   int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (metagl::IsContextLost()) return;
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

        ::easygl::Sampler& s = samplers_[slot];
        if (!s.is_created())
            s.create();

        // TextureFilter → min/mag filter
        // XNA: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        //      PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        //      MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8
        ::easygl::TextureMinFilter minF;
        ::easygl::TextureMagFilter magF;
        switch (filter)
        {
        case 1: // Point — nearest neighbour, no mipmaps
            minF = ::easygl::TextureMinFilter::Nearest;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 2: // Anisotropic
            minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 3: // LinearMipPoint
            minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 4: // PointMipLinear
            minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 5: // MinLinearMagPointMipLinear
            minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 6: // MinLinearMagPointMipPoint
            minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 7: // MinPointMagLinearMipLinear
            minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 8: // MinPointMagLinearMipPoint
            minF = ::easygl::TextureMinFilter::NearestMipmapNearest;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        default: // Linear — bilinear, no mipmaps (CNA does not generate mipmaps by default)
            minF = ::easygl::TextureMinFilter::Linear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        }
        s.set_parameter(::easygl::SamplerParameter::MinFilter, static_cast<int>(minF));
        s.set_parameter(::easygl::SamplerParameter::MagFilter, static_cast<int>(magF));

        // TextureAddressMode → GL wrap: Wrap=0→Repeat, Clamp=1→ClampToEdge, Mirror=2→MirroredRepeat
        auto toWrap = [](int mode) -> int {
            switch (mode) {
            case 1:  return static_cast<int>(::easygl::TextureWrapMode::ClampToEdge);
            case 2:  return static_cast<int>(::easygl::TextureWrapMode::MirroredRepeat);
            default: return static_cast<int>(::easygl::TextureWrapMode::Repeat);
            }
        };
        s.set_parameter(::easygl::SamplerParameter::WrapS, toWrap(addressU));
        s.set_parameter(::easygl::SamplerParameter::WrapT, toWrap(addressV));

        s.bind(static_cast<unsigned int>(slot));
    }

    // -------------------------------------------------------------------------
    // 3D pipeline
    // -------------------------------------------------------------------------

    void EasyGLVertexBufferBackend::InitializeLayout()
    {
        vbo.create();
        vao.create();
        // Attribute layout is configured lazily in ApplyLayout() once stride is known.
    }

    void EasyGLVertexBufferBackend::ApplyLayout(std::size_t stride)
    {
        const int s = static_cast<int>(stride);
        vao.bind();
        vbo.bind(::easygl::BufferTarget::Array);
        switch (stride)
        {
        case 16:
            // VertexPositionColor (packed): float3 position + ubyte4 color
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)12);
            break;
        case 20:
            // VertexPositionTexture (packed): float3 position + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false, s, (void*)12);
            break;
        case 24:
            // VertexPositionColorTexture (packed): float3 position + ubyte4 color + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)16);
            break;
        case 32:
            // VertexPositionNormalTexture (packed): float3 position + float3 normal + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)24);
            break;
        case 52:
            // SkinnedVertex: float3 pos + float3 normal + float2 uv + float4 weights + ubyte4 indices
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 4, ::easygl::DataType::Float, false, s, (void*)32);
            vao.enable_attribute(4);
            vao.set_attribute_i_pointer(4, 4, ::easygl::DataType::UnsignedByte, s, (void*)48);
            break;
        default:
            // Unknown layout: bind position-only as a safe fallback
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            CNA_RENDER_LOG("ApplyLayout: unknown stride=" << stride << ", using position-only fallback");
            break;
        }
        vao.unbind();
    }

    EasyGLVertexBufferBackend::EasyGLVertexBufferBackend(int vertex_capacity, ::easygl::ResourceRegistry* registry)
        : capacity(vertex_capacity)
        , registry_(registry)
    {
        InitializeLayout();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("VertexBuffer created: capacity=" << capacity);
    }

    EasyGLVertexBufferBackend::~EasyGLVertexBufferBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLVertexBufferBackend::release_gl_handle_only()
    {
        vbo.reset_handle_no_gl();
        vao.reset_handle_no_gl();
    }

    void EasyGLVertexBufferBackend::recreate_gl_resource()
    {
        InitializeLayout();
        if (!cpu_data_.empty() && stride_in_bytes_ > 0)
        {
            vbo.bind(::easygl::BufferTarget::Array);
            vbo.set_data(::easygl::BufferTarget::Array, cpu_data_.data(), cpu_data_.size());
            ApplyLayout(stride_in_bytes_);
        }
    }

    void EasyGLVertexBufferBackend::uploadWithOptions(const void* data,
                                                      std::size_t byte_count,
                                                      SetDataOptions options)
    {
        vbo.bind(::easygl::BufferTarget::Array);
        if (options == SetDataOptions::Discard) {
            // Orphan strategy: discard old storage without stalling the GPU pipeline.
            const std::size_t total = static_cast<std::size_t>(capacity) * stride_in_bytes_;
            vbo.set_data(::easygl::BufferTarget::Array, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            vbo.set_sub_data(::easygl::BufferTarget::Array, data, byte_count, 0);
            gpu_allocated_ = true;
        } else if (options == SetDataOptions::NoOverwrite && gpu_allocated_) {
            // NoOverwrite: driver hint that no in-flight data is overwritten.
            vbo.set_sub_data(::easygl::BufferTarget::Array, data, byte_count, 0);
        } else {
            // None (or first-ever upload): standard glBufferData.
            vbo.set_data(::easygl::BufferTarget::Array, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            gpu_allocated_ = true;
        }
    }

    void EasyGLVertexBufferBackend::SetData(const void* data, int count, std::size_t stride_in_bytes)
    {
        vertex_count = count;
        stride_in_bytes_ = stride_in_bytes;
        const std::size_t byte_count = static_cast<std::size_t>(count) * stride_in_bytes;
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        uploadWithOptions(data, byte_count, SetDataOptions::None);
        ApplyLayout(stride_in_bytes_);
        CNA_RENDER_LOG("VertexBuffer SetData: count=" << count << " stride=" << stride_in_bytes
            << " bytes=" << byte_count);
    }

    void EasyGLVertexBufferBackend::SetDataWithOptions(const void* data, int count,
                                                       std::size_t stride_in_bytes,
                                                       SetDataOptions options)
    {
        vertex_count = count;
        stride_in_bytes_ = stride_in_bytes;
        const std::size_t byte_count = static_cast<std::size_t>(count) * stride_in_bytes;
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        uploadWithOptions(data, byte_count, options);
        ApplyLayout(stride_in_bytes_);
        CNA_RENDER_LOG("VertexBuffer SetDataWithOptions: count=" << count
            << " stride=" << stride_in_bytes << " options=" << static_cast<int>(options));
    }

    EasyGLIndexBufferBackend::EasyGLIndexBufferBackend(int index_capacity, bool is32bit,
                                                       ::easygl::ResourceRegistry* registry)
        : thirtyTwoBit(is32bit)
        , capacity(index_capacity)
        , registry_(registry)
    {
        ibo.create();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("IndexBuffer created: capacity=" << capacity << " 32bit=" << is32bit);
    }

    EasyGLIndexBufferBackend::~EasyGLIndexBufferBackend()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLIndexBufferBackend::release_gl_handle_only()
    {
        ibo.reset_handle_no_gl();
    }

    void EasyGLIndexBufferBackend::recreate_gl_resource()
    {
        ibo.create();
        if (!cpu_data_.empty())
        {
            ibo.bind(::easygl::BufferTarget::ElementArray);
            ibo.set_data(::easygl::BufferTarget::ElementArray, cpu_data_.data(), cpu_data_.size());
        }
    }

    void EasyGLIndexBufferBackend::SetData16(const void* data, int count)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint16_t);
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData16: count=" << count);
    }

    void EasyGLIndexBufferBackend::SetData32(const void* data, int count)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint32_t);
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData32: count=" << count);
    }

    void EasyGLIndexBufferBackend::SetData16WithOptions(const void* data, int count,
                                                        SetDataOptions options)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint16_t);
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        if (options == SetDataOptions::Discard) {
            const std::size_t total = static_cast<std::size_t>(capacity) * sizeof(std::uint16_t);
            ibo.set_data(::easygl::BufferTarget::ElementArray, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else if (options == SetDataOptions::NoOverwrite && !cpu_data_.empty()) {
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else {
            ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
        }
        CNA_RENDER_LOG("IndexBuffer SetData16WithOptions: count=" << count
            << " options=" << static_cast<int>(options));
    }

    void EasyGLIndexBufferBackend::SetData32WithOptions(const void* data, int count,
                                                        SetDataOptions options)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint32_t);
        if (registry_)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        if (options == SetDataOptions::Discard) {
            const std::size_t total = static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
            ibo.set_data(::easygl::BufferTarget::ElementArray, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else if (options == SetDataOptions::NoOverwrite && !cpu_data_.empty()) {
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else {
            ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
        }
        CNA_RENDER_LOG("IndexBuffer SetData32WithOptions: count=" << count
            << " options=" << static_cast<int>(options));
    }

    namespace
    {
        void CompileAndLink(::easygl::Program& prog, const char* vsrc, const char* fsrc,
                            const char* label)
        {
            ::easygl::Shader vs(::easygl::ShaderType::Vertex);
            vs.create();
            vs.compile_from_source(vsrc);
            if (!vs.is_compiled())
                std::cerr << "[CNA EasyGL 3D] " << label << " VS failed:\n" << vs.info_log() << "\n";

            ::easygl::Shader fs(::easygl::ShaderType::Fragment);
            fs.create();
            fs.compile_from_source(fsrc);
            if (!fs.is_compiled())
                std::cerr << "[CNA EasyGL 3D] " << label << " FS failed:\n" << fs.info_log() << "\n";

            prog.create();
            prog.attach(vs);
            prog.attach(fs);
            prog.link();
            if (!prog.is_linked())
                std::cerr << "[CNA EasyGL 3D] " << label << " link failed:\n" << prog.info_log() << "\n";
        }
    }

    void EasyGLGraphicsBackend::EnsureColored3DProgram()
    {
        if (prog_colored_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"uniform mat4 uWVP;\n"
"uniform float uFogEnabled;\n"
"uniform float uFogStart;\n"
"uniform float uFogEnd;\n"
"out vec4 vColor;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"    vFogFactor=(uFogEnabled>0.5)?clamp((uFogEnd-aPos.z)/max(uFogEnd-uFogStart,1e-6),0.0,1.0):1.0;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in float vFogFactor;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_colored_.prog, vsrc, fsrc, "colored");
        prog_colored_.loc_wvp         = prog_colored_.prog.uniform_location("uWVP");
        prog_colored_.loc_diffuse     = prog_colored_.prog.uniform_location("uDiffuseColor");
        prog_colored_.loc_alphatest   = prog_colored_.prog.uniform_location("uAlphaTest");
        prog_colored_.loc_fog_enabled = prog_colored_.prog.uniform_location("uFogEnabled");
        prog_colored_.loc_fog_color   = prog_colored_.prog.uniform_location("uFogColor");
        prog_colored_.loc_fog_start   = prog_colored_.prog.uniform_location("uFogStart");
        prog_colored_.loc_fog_end     = prog_colored_.prog.uniform_location("uFogEnd");
        prog_colored_.loc_vertexcolor = prog_colored_.prog.uniform_location("uVertexColorEnabled");
        prog_colored_.ready           = true;
        CNA_RENDER_LOG("colored3D ready loc_wvp=" << prog_colored_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureTextured3DProgram()
    {
        if (prog_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform float uFogEnabled;\n"
"uniform float uFogStart;\n"
"uniform float uFogEnd;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vUV=aUV;\n"
"    vFogFactor=(uFogEnabled>0.5)?clamp((uFogEnd-aPos.z)/max(uFogEnd-uFogStart,1e-6),0.0,1.0):1.0;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=texture(uTexture,vUV)*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_textured_.prog, vsrc, fsrc, "textured");
        prog_textured_.loc_wvp         = prog_textured_.prog.uniform_location("uWVP");
        prog_textured_.loc_diffuse     = prog_textured_.prog.uniform_location("uDiffuseColor");
        prog_textured_.loc_texture     = prog_textured_.prog.uniform_location("uTexture");
        prog_textured_.loc_alphatest   = prog_textured_.prog.uniform_location("uAlphaTest");
        prog_textured_.loc_fog_enabled = prog_textured_.prog.uniform_location("uFogEnabled");
        prog_textured_.loc_fog_color   = prog_textured_.prog.uniform_location("uFogColor");
        prog_textured_.loc_fog_start   = prog_textured_.prog.uniform_location("uFogStart");
        prog_textured_.loc_fog_end     = prog_textured_.prog.uniform_location("uFogEnd");
        prog_textured_.ready           = true;
        CNA_RENDER_LOG("textured3D ready loc_wvp=" << prog_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureColoredTextured3DProgram()
    {
        if (prog_col_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform float uFogEnabled;\n"
"uniform float uFogStart;\n"
"uniform float uFogEnd;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
"    vFogFactor=(uFogEnabled>0.5)?clamp((uFogEnd-aPos.z)/max(uFogEnd-uFogStart,1e-6),0.0,1.0):1.0;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=texture(uTexture,vUV)*vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_col_textured_.prog, vsrc, fsrc, "col+textured");
        prog_col_textured_.loc_wvp         = prog_col_textured_.prog.uniform_location("uWVP");
        prog_col_textured_.loc_texture     = prog_col_textured_.prog.uniform_location("uTexture");
        prog_col_textured_.loc_diffuse     = prog_col_textured_.prog.uniform_location("uDiffuseColor");
        prog_col_textured_.loc_alphatest   = prog_col_textured_.prog.uniform_location("uAlphaTest");
        prog_col_textured_.loc_fog_enabled = prog_col_textured_.prog.uniform_location("uFogEnabled");
        prog_col_textured_.loc_fog_color   = prog_col_textured_.prog.uniform_location("uFogColor");
        prog_col_textured_.loc_fog_start   = prog_col_textured_.prog.uniform_location("uFogStart");
        prog_col_textured_.loc_fog_end     = prog_col_textured_.prog.uniform_location("uFogEnd");
        prog_col_textured_.loc_vertexcolor = prog_col_textured_.prog.uniform_location("uVertexColorEnabled");
        prog_col_textured_.ready           = true;
        CNA_RENDER_LOG("col+textured3D ready loc_wvp=" << prog_col_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureLit3DProgram()
    {
        if (prog_lit_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform float uFogEnabled;\n"
"uniform float uFogStart;\n"
"uniform float uFogEnd;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vNormal=uNormalMatrix*aNormal;\n"
"    vUV=aUV;\n"
"    vFogFactor=(uFogEnabled>0.5)?clamp((uFogEnd-aPos.z)/max(uFogEnd-uFogStart,1e-6),0.0,1.0):1.0;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    float NdotL=max(dot(N,-uLight0Dir),0.0);\n"
"    vec3 litRGB=(uAmbientColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb;\n"
"    FragColor=texture(uTexture,vUV)*vec4(litRGB,uDiffuseColor.a);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_.prog, vsrc, fsrc, "lit+textured");
        prog_lit_textured_.loc_wvp         = prog_lit_textured_.prog.uniform_location("uWVP");
        prog_lit_textured_.loc_normalmat   = prog_lit_textured_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_.loc_diffuse     = prog_lit_textured_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_.loc_ambient     = prog_lit_textured_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_.loc_l0dir       = prog_lit_textured_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_.loc_l0diff      = prog_lit_textured_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_.loc_texture     = prog_lit_textured_.prog.uniform_location("uTexture");
        prog_lit_textured_.loc_alphatest   = prog_lit_textured_.prog.uniform_location("uAlphaTest");
        prog_lit_textured_.loc_fog_enabled = prog_lit_textured_.prog.uniform_location("uFogEnabled");
        prog_lit_textured_.loc_fog_color   = prog_lit_textured_.prog.uniform_location("uFogColor");
        prog_lit_textured_.loc_fog_start   = prog_lit_textured_.prog.uniform_location("uFogStart");
        prog_lit_textured_.loc_fog_end     = prog_lit_textured_.prog.uniform_location("uFogEnd");
        prog_lit_textured_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D ready loc_wvp=" << prog_lit_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureDualTextured3DProgram()
    {
        if (prog_dual_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"out vec2 vUV;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vUV=aUV;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=texture(uTexture,vUV)*texture(uTexture2,vUV)*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"}\n";

        CompileAndLink(prog_dual_textured_.prog, vsrc, fsrc, "dual+textured");
        prog_dual_textured_.loc_wvp       = prog_dual_textured_.prog.uniform_location("uWVP");
        prog_dual_textured_.loc_texture   = prog_dual_textured_.prog.uniform_location("uTexture");
        prog_dual_textured_.loc_texture2  = prog_dual_textured_.prog.uniform_location("uTexture2");
        prog_dual_textured_.loc_diffuse   = prog_dual_textured_.prog.uniform_location("uDiffuseColor");
        prog_dual_textured_.loc_alphatest = prog_dual_textured_.prog.uniform_location("uAlphaTest");
        prog_dual_textured_.ready         = true;
        CNA_RENDER_LOG("dual+textured3D ready loc_wvp=" << prog_dual_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()
    {
        if (prog_env_mapped_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uWorld;\n"
"uniform vec3 uEyePosition;\n"
"out vec3 vWorldNormal;\n"
"out vec3 vEyeDir;\n"
"out vec2 vUV;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vec3 worldPos=(uWorld*vec4(aPos,1.0)).xyz;\n"
"    vWorldNormal=uNormalMatrix*aNormal;\n"
"    vEyeDir=uEyePosition-worldPos;\n"
"    vUV=aUV;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vWorldNormal;\n"
"in vec3 vEyeDir;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTexture;\n"
"uniform samplerCube uEnvMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform float uEnvMapAmount;\n"
"uniform vec3 uEnvMapSpecular;\n"
"uniform vec4 uAlphaTest;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vWorldNormal);\n"
"    vec3 E=normalize(vEyeDir);\n"
"    float NdotL=max(dot(N,-uLight0Dir),0.0);\n"
"    vec3 litRGB=(uEmissiveColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb;\n"
"    vec4 texColor=texture(uTexture,vUV);\n"
"    vec3 reflDir=reflect(-E,N);\n"
"    vec3 envColor=texture(uEnvMap,reflDir).rgb;\n"
"    vec3 rgb=litRGB*texColor.rgb+envColor*uEnvMapAmount+uEnvMapSpecular;\n"
"    FragColor=vec4(rgb,uDiffuseColor.a*texColor.a);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"}\n";

        CompileAndLink(prog_env_mapped_.prog, vsrc, fsrc, "env+mapped");
        auto& p = prog_env_mapped_;
        p.loc_wvp           = p.prog.uniform_location("uWVP");
        p.loc_normalmat     = p.prog.uniform_location("uNormalMatrix");
        p.loc_world         = p.prog.uniform_location("uWorld");
        p.loc_eyepos        = p.prog.uniform_location("uEyePosition");
        p.loc_texture       = p.prog.uniform_location("uTexture");
        p.loc_envmap        = p.prog.uniform_location("uEnvMap");
        p.loc_diffuse       = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive      = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir         = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff        = p.prog.uniform_location("uLight0Diffuse");
        p.loc_envmap_amount = p.prog.uniform_location("uEnvMapAmount");
        p.loc_envmap_spec   = p.prog.uniform_location("uEnvMapSpecular");
        p.loc_alphatest     = p.prog.uniform_location("uAlphaTest");
        p.ready             = true;
        CNA_RENDER_LOG("env+mapped3D ready loc_wvp=" << p.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureSkinnedProgram()
    {
        if (prog_skinned_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aBoneWeights;\n"
"layout(location=4) in uvec4 aBoneIndices;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uBones[72];\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"void main(){\n"
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x\n"
"               +uBones[aBoneIndices.y]*aBoneWeights.y\n"
"               +uBones[aBoneIndices.z]*aBoneWeights.z\n"
"               +uBones[aBoneIndices.w]*aBoneWeights.w;\n"
"    gl_Position=uWVP*skinMat*vec4(aPos,1.0);\n"
"    vNormal=normalize(mat3(skinMat)*aNormal);\n"
"    vUV=aUV;\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec4 uAlphaTest;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    float NdotL=max(dot(N,-uLight0Dir),0.0);\n"
"    vec3 litRGB=(uEmissiveColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb;\n"
"    vec4 texColor=texture(uTexture,vUV);\n"
"    FragColor=vec4(litRGB*texColor.rgb,uDiffuseColor.a*texColor.a);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"}\n";

        CompileAndLink(prog_skinned_.prog, vsrc, fsrc, "skinned");
        auto& p = prog_skinned_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.ready         = true;
        CNA_RENDER_LOG("skinned3D ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    void EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()
    {
        if (default_white_texture_ready_) return;
        static const uint8_t white[4] = {255, 255, 255, 255};
        default_white_texture_.create();
        default_white_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, white);
        default_white_texture_ready_ = true;
    }

    EasyGLGraphicsBackend::Prog3D& EasyGLGraphicsBackend::SelectProgram(std::size_t stride,
                                                                          const GpuDrawParams& params)
    {
        if (params.skinned)
        {
            EnsureSkinnedProgram();
            return prog_skinned_;
        }
        if (params.envMapping)
        {
            EnsureEnvMapped3DProgram();
            return prog_env_mapped_;
        }
        if (params.dualTexture)
        {
            EnsureDualTextured3DProgram();
            return prog_dual_textured_;
        }
        switch (stride)
        {
        case 20: EnsureTextured3DProgram();        return prog_textured_;
        case 24: EnsureColoredTextured3DProgram(); return prog_col_textured_;
        case 32: EnsureLit3DProgram();             return prog_lit_textured_;
        default: EnsureColored3DProgram();         return prog_colored_;
        }
    }

    void EasyGLGraphicsBackend::BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                                               const Matrix& projection, const GpuDrawParams& params)
    {
        // WVP
        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        if (p.loc_wvp >= 0)
            p.prog.set_uniform_matrix4(p.loc_wvp, wvp_col);

        // Normal matrix — upper-left 3x3 of world column-major
        if (p.loc_normalmat >= 0)
        {
            const float* w = params.worldColMajor;
            float nm[9] = { w[0],w[1],w[2], w[4],w[5],w[6], w[8],w[9],w[10] };
            p.prog.set_uniform_matrix3(p.loc_normalmat, nm);
        }

        // Full world matrix (EnvironmentMapEffect VS — position → world space)
        if (p.loc_world >= 0)
            p.prog.set_uniform_matrix4(p.loc_world, params.worldColMajor);

        // Diffuse color
        if (p.loc_diffuse >= 0)
            p.prog.set_uniform(p.loc_diffuse,
                params.diffuseColor[0], params.diffuseColor[1],
                params.diffuseColor[2], params.diffuseColor[3]);

        // VertexColorEnabled gate (colored3D / BasicEffect no-texture path only — Task 364).
        if (p.loc_vertexcolor >= 0)
            p.prog.set_uniform(p.loc_vertexcolor, params.vertexColorEnabled ? 1.0f : 0.0f);

        // Ambient + light0 (lit shader / BasicEffect path only)
        if (p.loc_ambient >= 0)
        {
            if (params.lightingEnabled)
            {
                p.prog.set_uniform(p.loc_ambient,
                    params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]);
                if (p.loc_l0dir >= 0)
                    p.prog.set_uniform(p.loc_l0dir,
                        params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
                if (p.loc_l0diff >= 0)
                    p.prog.set_uniform(p.loc_l0diff,
                        params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]);
            }
            else
            {
                // No lighting: full ambient = diffuse color, light contribution = 0
                p.prog.set_uniform(p.loc_ambient, 1.0f, 1.0f, 1.0f);
                if (p.loc_l0dir  >= 0) p.prog.set_uniform(p.loc_l0dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l0diff >= 0) p.prog.set_uniform(p.loc_l0diff, 0.0f,  0.0f, 0.0f);
            }
        }

        // EnvironmentMapEffect: emissive+ambient (pre-combined) + light0 + eye pos + env map
        if (p.loc_emissive >= 0)
            p.prog.set_uniform(p.loc_emissive,
                params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2]);

        if (p.loc_l0dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0dir,
                params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
        if (p.loc_l0diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0diff,
                params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]);

        if (p.loc_eyepos >= 0)
            p.prog.set_uniform(p.loc_eyepos,
                params.eyePositionWorld[0], params.eyePositionWorld[1], params.eyePositionWorld[2]);

        // Bone palette (SkinnedEffect)
        if (p.loc_bones >= 0 && params.boneCount > 0)
            ::metagl::glUniformMatrix4fv(::metagl::UniformLocation{p.loc_bones}, params.boneCount, 0, params.boneTransforms);

        if (p.loc_envmap_amount >= 0)
            p.prog.set_uniform(p.loc_envmap_amount, params.envMapAmount);

        if (p.loc_envmap_spec >= 0)
            p.prog.set_uniform(p.loc_envmap_spec,
                params.envMapSpecular[0], params.envMapSpecular[1], params.envMapSpecular[2]);

        // Cube map (unit 1 — bind before texture0 to leave unit 0 active)
        if (p.loc_envmap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_envmap, 1);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture1);
            if (params.envMap)
                params.envMap->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        // Second texture (DualTextureEffect — bind before unit 0 to leave unit 0 active)
        if (p.loc_texture2 >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_texture2, 1);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture1);
            if (params.texture1)
                params.texture1->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        // Texture (unit 0)
        if (p.loc_texture >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_texture, 0);
            if (params.texture0)
                params.texture0->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
        }

        // Alpha test (always uploaded; default {0,0,1,1} = Always pass)
        if (p.loc_alphatest >= 0)
            p.prog.set_uniform(p.loc_alphatest,
                params.alphaTest[0], params.alphaTest[1],
                params.alphaTest[2], params.alphaTest[3]);

        // Linear fog (BasicEffect and AlphaTestEffect shaders)
        if (p.loc_fog_enabled >= 0)
            p.prog.set_uniform(p.loc_fog_enabled, params.fogEnabled ? 1.0f : 0.0f);
        if (p.loc_fog_color >= 0)
            p.prog.set_uniform(p.loc_fog_color,
                params.fogColor[0], params.fogColor[1], params.fogColor[2]);
        if (p.loc_fog_start >= 0)
            p.prog.set_uniform(p.loc_fog_start, params.fogStart);
        if (p.loc_fog_end >= 0)
            p.prog.set_uniform(p.loc_fog_end, params.fogEnd);
    }

    void EasyGLGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        if (metagl::IsContextLost()) return;
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        device.set_viewport(0, 0, width, height);
        device.set_clear_color(r, g, b, a);
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth);
    }

    void EasyGLGraphicsBackend::ClearDepth(float depth)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        device.clear(::easygl::ClearFlags::Depth);
    }

    void EasyGLGraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        device.set_depth_test_enabled(enabled);
        if (enabled)
        {
            device.set_depth_func(::easygl::CompareFunc::Lequal);
            device.set_depth_mask(true);
        }
    }

    void EasyGLGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        device.set_blend_enabled(enabled);
        if (enabled)
            device.set_blend_func(::easygl::BlendFactor::SrcAlpha,
                                  ::easygl::BlendFactor::OneMinusSrcAlpha);
    }

    void EasyGLGraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        device.set_depth_mask(enabled);
    }

    std::unique_ptr<IVertexBufferBackend> EasyGLGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<EasyGLVertexBufferBackend>(vertex_capacity, RegistryPtr());
    }

    std::unique_ptr<IIndexBufferBackend> EasyGLGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferBackend>(index_capacity, false, RegistryPtr());
    }

    std::unique_ptr<IIndexBufferBackend> EasyGLGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferBackend>(index_capacity, true, RegistryPtr());
    }

    bool EasyGLGraphicsBackend::DrawWireframe(const EasyGLVertexBufferBackend& vb,
                                              const EasyGLIndexBufferBackend* ib,
                                              PrimitiveType primitive, int primitiveCount,
                                              int startIndex, int baseVertex, int firstVertex)
    {
        // Only triangle geometry needs expanding; line/point primitives are already "wireframe".
        if (primitive != PrimitiveType::TriangleList &&
            primitive != PrimitiveType::TriangleStrip)
            return false;
        if (primitiveCount <= 0) return true;

        // Source vertex index at sequence position `pos` within this draw.
        auto readSrc = [&](int pos) -> std::uint32_t {
            if (!ib) return static_cast<std::uint32_t>(firstVertex + pos);
            const auto& bytes = ib->GetCpuBytes();
            if (ib->IsThirtyTwoBit()) {
                std::uint32_t v;
                std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 4, 4);
                return v;
            }
            std::uint16_t v;
            std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 2, 2);
            return static_cast<std::uint32_t>(v);
        };

        wireframeScratch_.clear();
        auto edge = [&](std::uint32_t a, std::uint32_t b) {
            wireframeScratch_.push_back(a);
            wireframeScratch_.push_back(b);
        };
        if (primitive == PrimitiveType::TriangleList) {
            for (int t = 0; t < primitiveCount; ++t) {
                const std::uint32_t a = readSrc(3 * t);
                const std::uint32_t b = readSrc(3 * t + 1);
                const std::uint32_t c = readSrc(3 * t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        } else { // TriangleStrip: primitiveCount triangles over primitiveCount+2 vertices
            for (int t = 0; t < primitiveCount; ++t) {
                const std::uint32_t a = readSrc(t);
                const std::uint32_t b = readSrc(t + 1);
                const std::uint32_t c = readSrc(t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        }

        if (!wireframeIboCreated_) { wireframeIbo_.create(); wireframeIboCreated_ = true; }
        vb.vao.bind();
        wireframeIbo_.bind(::easygl::BufferTarget::ElementArray);
        wireframeIbo_.set_data(::easygl::BufferTarget::ElementArray,
                               wireframeScratch_.data(),
                               wireframeScratch_.size() * sizeof(std::uint32_t),
                               ::easygl::BufferUsage::DynamicDraw);
        const int lineIndexCount = static_cast<int>(wireframeScratch_.size());
        if (baseVertex == 0) {
            device.draw_elements(::easygl::PrimitiveType::Lines, lineIndexCount,
                                 ::easygl::DataType::UnsignedInt, nullptr);
        } else {
            ::metagl::glDrawElementsBaseVertex(::easygl::PrimitiveType::Lines, lineIndexCount,
                                               ::easygl::DataType::UnsignedInt, nullptr, baseVertex);
        }
        vb.vao.unbind();
        return true;
    }

    void EasyGLGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                      const Matrix& world,
                                                      const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive,
                                                      int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const EasyGLVertexBufferBackend&>(vb_in);

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        prog_colored_.prog.use();
        if (prog_colored_.loc_wvp >= 0)
            prog_colored_.prog.set_uniform_matrix4(prog_colored_.loc_wvp, wvp_col);
        // This path carries no BasicEffect diffuse; output the raw vertex colors
        // (uDiffuseColor would otherwise default to 0 and render everything black).
        if (prog_colored_.loc_diffuse >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        // Same reasoning for uVertexColorEnabled: it would otherwise default to 0 (uninitialized
        // GLSL uniform) and force vColor out of the multiply, turning every pixel constant white.
        if (prog_colored_.loc_vertexcolor >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_vertexcolor, 1.0f);

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " verts=" << vertex_count);

        if (wireframe_ && DrawWireframe(vb, nullptr, primitive, primitiveCount, 0, 0, 0))
            return;

        vb.vao.bind();
        device.draw_arrays(ToEasyGl(primitive), 0, vertex_count);
        vb.vao.unbind();
    }

    void EasyGLGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                             const IIndexBufferBackend& ib_in,
                                                             const Matrix& world,
                                                             const Matrix& view,
                                                             const Matrix& projection,
                                                             PrimitiveType primitive,
                                                             int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const EasyGLVertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const EasyGLIndexBufferBackend&>(ib_in);

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        prog_colored_.prog.use();
        if (prog_colored_.loc_wvp >= 0)
            prog_colored_.prog.set_uniform_matrix4(prog_colored_.loc_wvp, wvp_col);
        // This path carries no BasicEffect diffuse; output the raw vertex colors
        // (uDiffuseColor would otherwise default to 0 and render everything black).
        if (prog_colored_.loc_diffuse >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        // Same reasoning for uVertexColorEnabled: it would otherwise default to 0 (uninitialized
        // GLSL uniform) and force vColor out of the multiply, turning every pixel constant white.
        if (prog_colored_.loc_vertexcolor >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_vertexcolor, 1.0f);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " indices=" << index_count);

        if (wireframe_ && DrawWireframe(vb, &ib, primitive, primitiveCount, 0, 0, 0))
            return;

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                              : ::easygl::DataType::UnsignedShort;
        device.draw_elements(ToEasyGl(primitive), index_count, idxType, nullptr);
        vb.vao.unbind();
    }

    void EasyGLGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                 const Matrix& world,
                                                 const Matrix& view,
                                                 const Matrix& projection,
                                                 PrimitiveType primitive,
                                                 int primitiveCount,
                                                 const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
        const auto& vb  = static_cast<const EasyGLVertexBufferBackend&>(vb_in);
        Prog3D& p = SelectProgram(vb.GetStride(), params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawPrimitivesEx: stride=" << vb.GetStride()
            << " prim=" << static_cast<int>(primitive) << " verts=" << vertex_count);

        if (wireframe_ && DrawWireframe(vb, nullptr, primitive, primitiveCount,
                                        0, 0, params.vertexStart))
            return;

        vb.vao.bind();
        device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
        vb.vao.unbind();
    }

    void EasyGLGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                        const IIndexBufferBackend& ib_in,
                                                        const Matrix& world,
                                                        const Matrix& view,
                                                        const Matrix& projection,
                                                        PrimitiveType primitive,
                                                        int primitiveCount,
                                                        const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
        const auto& vb  = static_cast<const EasyGLVertexBufferBackend&>(vb_in);
        const auto& ib  = static_cast<const EasyGLIndexBufferBackend&>(ib_in);
        Prog3D& p = SelectProgram(vb.GetStride(), params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedPrimitivesEx: stride=" << vb.GetStride()
            << " prim=" << static_cast<int>(primitive) << " indices=" << index_count);

        if (wireframe_ && DrawWireframe(vb, &ib, primitive, primitiveCount,
                                        params.startIndex, params.baseVertex, 0))
            return;

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType2 = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                               : ::easygl::DataType::UnsignedShort;
        const int indexSize = ib.thirtyTwoBit ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSize));
        if (params.baseVertex == 0) {
            device.draw_elements(ToEasyGl(primitive), index_count, idxType2, indexOffset);
        } else {
            ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxType2,
                                               indexOffset, params.baseVertex);
        }
        vb.vao.unbind();
    }

    void EasyGLGraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                          const IIndexBufferBackend& ib_in,
                                                          const Matrix& world,
                                                          const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          int primitiveCount,
                                                          int instanceCount,
                                                          const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
        const auto& vb  = static_cast<const EasyGLVertexBufferBackend&>(vb_in);
        const auto& ib  = static_cast<const EasyGLIndexBufferBackend&>(ib_in);
        Prog3D& p = SelectProgram(vb.GetStride(), params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                             : ::easygl::DataType::UnsignedShort;

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        device.draw_elements_instanced(ToEasyGl(primitive), index_count, idxType,
                                       nullptr, instanceCount);
        vb.vao.unbind();
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_EASYGL
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<EasyGL::EasyGLGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight,
            args.presentationMode, args.contextRecoveryEnabled,
            args.multiSampleCount, args.swapInterval);
    }
#endif
}
