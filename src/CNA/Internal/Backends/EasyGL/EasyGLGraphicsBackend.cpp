#include "CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include <iostream>
#include <span>

#include "CNA/Platform.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <metagl/Emscripten.hpp>
#endif
#include <metagl/Capabilities.hpp>
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

    // Mirrors Texture3D.cpp's CalculateMipLevels(w,h) — depth does not participate in the level
    // count, matching FNA's Texture3D constructor, but each level's own GPU storage still halves
    // in all 3 dimensions (standard volume-mip behavior).
    static int CalculateTexture3DMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    EasyGLTexture3DBackend::EasyGLTexture3DBackend(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
        : width_(w), height_(h), depth_(depth)
    {
        tex_.create();
        tex_.bind(::easygl::TextureTarget::Texture3D);
        // Pre-allocate GPU storage for every mip level (not just level 0): SetData's box writes
        // use glTexSubImage3D, which requires the target level to already have a defined image —
        // without this loop, SetData(level>0,...) would silently fail (same bug shape as Task
        // 276's TextureCube finding).
        const int levelCount = mipMap ? CalculateTexture3DMipLevels(w, h) : 1;
        int levelW = w, levelH = h, levelD = depth;
        for (int level = 0; level < levelCount; ++level)
        {
            tex_.set_image_3d(::easygl::TextureTarget::Texture3D, level,
                              ::metagl::InternalFormat::Rgba8,
                              levelW, levelH, levelD,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              nullptr);
            levelW = std::max(1, levelW / 2);
            levelH = std::max(1, levelH / 2);
            levelD = std::max(1, levelD / 2);
        }
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

    void EasyGLTexture3DBackend::BindGL() const
    {
        tex_.bind(::easygl::TextureTarget::Texture3D);
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

    void EasyGLEffectBackend::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count)), 1);
    }

    void EasyGLEffectBackend::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count) * 2), 2);
    }

    void EasyGLEffectBackend::BindTexture(int unit, ITextureBackend* texture)
    {
        if (!texture) return;
        const auto textureUnit = static_cast<::metagl::TextureUnit>(
            static_cast<GLenum>(::metagl::TextureUnit::Texture0) + unit);
        ::metagl::glActiveTexture(textureUnit);
        texture->BindGL();
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
    }

    // Task 1081: same shape as BindTexture(), but for a samplerCube -- ITextureCubeBackend is
    // its own interface (not a subtype of ITextureBackend), so this can't just overload/reuse
    // BindTexture() at the call site.
    void EasyGLEffectBackend::BindTextureCube(int unit, ITextureCubeBackend* texture)
    {
        if (!texture) return;
        const auto textureUnit = static_cast<::metagl::TextureUnit>(
            static_cast<GLenum>(::metagl::TextureUnit::Texture0) + unit);
        ::metagl::glActiveTexture(textureUnit);
        texture->BindGL();
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
    }

    // plan_graphics.md Task 863: same shape as BindTextureCube(), but for a sampler3D --
    // ITexture3DBackend is its own interface (not a subtype of ITextureBackend), so this can't
    // just overload/reuse BindTexture() at the call site.
    void EasyGLEffectBackend::BindTexture3D(int unit, ITexture3DBackend* texture)
    {
        if (!texture) return;
        const auto textureUnit = static_cast<::metagl::TextureUnit>(
            static_cast<GLenum>(::metagl::TextureUnit::Texture0) + unit);
        ::metagl::glActiveTexture(textureUnit);
        texture->BindGL();
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
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
        : registry_(registry), mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        width = data.width;
        height = data.height;
        texture.create();
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, data.pixels.data());
        // Task 924: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- otherwise a mipmap-
        // requiring TextureFilter (e.g. Anisotropic) treats this as an incomplete mipmap chain
        // (GL's own default max level is 1000) and renders solid black, even for an ordinary
        // single-level (mipLevels_==1) texture that never uploads any level beyond 0.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::metagl::TextureParameter::MaxLevel,
                               mipLevels_ - 1);
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
        // Task 924: the fresh GL texture object defaults GL_TEXTURE_MAX_LEVEL back to 1000 --
        // reapply the same clamp the constructor set, matching this texture's real level count.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::metagl::TextureParameter::MaxLevel,
                               mipLevels_ - 1);
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

    // Task 877: maps a Microsoft::Xna::Framework::Graphics::DepthFormat ordinal to the GL
    // renderbuffer internal format and framebuffer attachment point a render target's
    // depth/stencil buffer should use. Returns false for DepthFormat::None, meaning no
    // depth/stencil attachment should be created at all.
    static bool MapDepthFormat(int depthFormat, ::metagl::InternalFormat& outFormat,
                                ::metagl::FramebufferAttachment& outAttachment)
    {
        using ::Microsoft::Xna::Framework::Graphics::DepthFormat;
        switch (static_cast<DepthFormat>(depthFormat))
        {
        case DepthFormat::Depth16:
            outFormat = ::metagl::InternalFormat::DepthComponent16;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24:
            outFormat = ::metagl::InternalFormat::DepthComponent24;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24Stencil8:
            outFormat = ::metagl::InternalFormat::Depth24Stencil8;
            outAttachment = ::metagl::FramebufferAttachment::DepthStencil;
            return true;
        case DepthFormat::None:
        default:
            return false;
        }
    }

    EasyGLRenderTargetBackend::EasyGLRenderTargetBackend(int w, int h, int depthFormat,
                                                          ::easygl::ResourceRegistry* registry,
                                                          bool mipMap, int multiSampleCount)
        : width_(w), height_(h), depthFormat_(depthFormat), mipMap_(mipMap),
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

        {
            ::metagl::InternalFormat depthGlFormat;
            ::metagl::FramebufferAttachment depthAttachment;
            if (MapDepthFormat(depthFormat_, depthGlFormat, depthAttachment))
            {
                depthRbo_.create();
                depthRbo_.bind();
                if (multiSampleCount_ > 0)
                    depthRbo_.set_storage_multisample(multiSampleCount_, depthGlFormat, width_, height_);
                else
                    depthRbo_.set_storage(depthGlFormat, width_, height_);
                fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                         depthAttachment, depthRbo_);
            }
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

    EasyGLRenderTargetCubeBackend::EasyGLRenderTargetCubeBackend(int size, int depthFormat,
                                                                    ::easygl::ResourceRegistry* registry,
                                                                    bool mipMap, int multiSampleCount)
        : size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
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

        ::metagl::InternalFormat depthGlFormat;
        ::metagl::FramebufferAttachment depthAttachment;
        if (MapDepthFormat(depthFormat_, depthGlFormat, depthAttachment))
        {
            depthRbo_.create();
            depthRbo_.bind();
            if (multiSampleCount_ > 0)
                depthRbo_.set_storage_multisample(multiSampleCount_, depthGlFormat, size_, size_);
            else
                depthRbo_.set_storage(depthGlFormat, size_, size_);
            fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      depthAttachment,
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
        // Task 956 fix: this previously hardcoded set_blend_enabled(true) +
        // SrcAlpha/OneMinusSrcAlpha unconditionally, clobbering whatever
        // EasyGLGraphicsBackend::ApplyBlendState had just set via
        // GraphicsDevice::setBlendStateProperty(blendState) -- called by SpriteBatch::Begin()
        // immediately before backend_->Begin() runs (see SpriteBatch.cpp). This both silently
        // ignored any non-AlphaBlend BlendState passed to SpriteBatch::Begin() (e.g. Opaque or
        // NonPremultiplied always rendered as if AlphaBlend-with-straight-alpha-factors had been
        // requested) and left the real GL blend state permanently stuck at that hardcoded value
        // after End() -- any 3D draw issued afterward without the game explicitly reassigning
        // BlendState inherited SpriteBatch's leftover raw GL state instead of whatever
        // GraphicsDevice.BlendState still claimed was active. Matches the same bug shape SDL_Renderer
        // already fixed (Task 695) -- see docs/sdl-renderer-2d-completeness.md.
        begun = true;
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
        // Task 1077 fix: bind the SAME compiled program the Effect itself owns
        // (Effect::GetEffectBackendPtr(), overridden by ShaderEffect) instead of recompiling a
        // second, independent copy from GLSL source text -- the old recompiled-copy approach
        // meant any ShaderEffect::SetUniformXxx() call (which writes to the effect's OWN
        // program) had no way to ever reach the program actually bound for the real draw.
        ::easygl::Program* prog = &program_;
        if (customEffect_)
        {
            auto* backend = dynamic_cast<EasyGLEffectBackend*>(customEffect_->GetEffectBackendPtr());
            if (backend && backend->IsValid())
                prog = &backend->GetProgram();
            customEffect_->Apply();
        }

        prog->use();

        int logW = 0, logH = 0;
        int rtW = 0, rtH = 0;
        const bool haveRt = graphicsBackend_ && graphicsBackend_->GetCurrentRenderTarget2DSize(rtW, rtH)
                            && rtW > 0 && rtH > 0;

        // REMED-GFX-072: honor a custom GraphicsDevice.Viewport. XNA/FNA build the SpriteBatch ortho
        // from Viewport.Width/Height (CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height,
        // 0)), so a custom sub-Viewport makes sprite coordinates VIEWPORT-LOCAL. The GL viewport that
        // GraphicsDevice.Viewport set via SetViewport() survives Clear() (Task 880), so read it here:
        // if it is a genuine sub-region of the full target, size the projection to it AND leave it in
        // place as the rasterizer viewport (its X/Y position the [-1,1] result at Viewport.X/Y).
        // Previously FlushBatch unconditionally reset the GL viewport to the full target and built the
        // ortho from the full target/logical size, so a custom Viewport was ignored for sprites. The
        // default full-target viewport keeps the exact prior behavior (reset + full-target/logical ortho).
        int curVx = 0, curVy = 0, curVw = 0, curVh = 0;
        device_.get_viewport(curVx, curVy, curVw, curVh);
        int fullW = 0, fullH = 0;
        if (haveRt) { fullW = rtW; fullH = rtH; }
        else if (graphicsBackend_) graphicsBackend_->getPhysicalSize(fullW, fullH);
        const bool customVp = curVw > 0 && curVh > 0 && fullW > 0 && fullH > 0
                              && (curVx != 0 || curVy != 0 || curVw != fullW || curVh != fullH);

        // Task 1078: a custom-effect draw into a bound RenderTarget2D must size its viewport
        // and orthographic projection to that RT, not the window -- getPhysicalSize()/
        // getLogicalSize() are always window-sized, which only happened to work in every prior
        // test because those tests' RTs all coincidentally matched the window size.
        if (customVp)
        {
            // Keep the custom GL viewport (do NOT reset to the full target); project by Viewport.W/H.
            logW = curVw;
            logH = curVh;
        }
        else if (haveRt)
        {
            device_.set_viewport(0, 0, rtW, rtH);
            logW = rtW;
            logH = rtH;
        }
        else if (graphicsBackend_)
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

        // Task 456: one-time startup capability dump. Task 918 wired up real
        // GL_EXT_texture_filter_anisotropic support in ApplySamplerState(); report the real,
        // runtime-detected status here instead of a hardcoded claim.
        {
            GLint maxSamplesCap = 0;
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamplesCap);
            const bool hasAniso = metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            GLfloat maxAnisoCap = 1.0f;
            if (hasAniso)
                metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
            std::cout << "CNA: EasyGL capabilities -- MSAA up to " << maxSamplesCap
                      << "x; MRT up to 4 targets (FNA MAX_RENDERTARGET_BINDINGS); "
                         "anisotropic filtering: "
                      << (hasAniso ? ("supported (Task 918, up to " + std::to_string(static_cast<int>(maxAnisoCap)) + "x)")
                                   : std::string("NOT supported (falls back to trilinear)"))
                      << "; SurfaceFormat: Color only (Task 176)" << std::endl;
        }

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

        // Register last, after every fallible step above has succeeded (matches
        // WebGPU/Canvas/SdlGpu) -- a constructor that throws never runs its destructor, so
        // registering earlier would leave a dangling entry in IGraphicsBackend's static window
        // registry, later dereferenced unconditionally by SdlInputBridge.cpp/Mouse.cpp.
        IGraphicsBackend::RegisterForWindow(window, this);
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

    bool EasyGLGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            {
                GLint maxSamplesCap = 0;
                metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamplesCap);
                return maxSamplesCap > 1;
            }
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            case CNA::GraphicsCapability::WireFrame:
                // GLES3 (EasyGL's underlying API) has no wireframe fill mode at all -- matches
                // the XNA 4.0 Graphics API coverage table's own "EasyGL N/A (GLES3)" entry.
                return false;
            default:
                return true;
        }
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
        prog_lit_textured_vertexlit_.reset_no_gl();
        prog_dual_textured_.reset_no_gl();
        prog_dual_textured_colored_.reset_no_gl();
        prog_env_mapped_.reset_no_gl();
        prog_skinned_.reset_no_gl();
        prog_skinned_vertexlit_.reset_no_gl();
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
        // Task 880: glClear() is viewport-independent (only the scissor rect, if enabled, can
        // narrow it) -- no longer hardcodes the viewport to the full window here, so a
        // previously-set custom Viewport (Task 880) survives across Clear() instead of being
        // silently reset to full size as a side effect.
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

    bool EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (!currentRt2D_) return false;
        width  = currentRt2D_->GetWidth();
        height = currentRt2D_->GetHeight();
        return true;
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

    std::unique_ptr<IRenderTargetBackend> EasyGLGraphicsBackend::CreateRenderTarget2D(int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<EasyGLRenderTargetBackend>(w, h, depthFormat, RegistryPtr(), mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeBackend> EasyGLGraphicsBackend::CreateRenderTargetCube(int size, int depthFormat, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<EasyGLRenderTargetCubeBackend>(size, depthFormat, RegistryPtr(), mipMap, multiSampleCount);
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
                                                      float depthBias,
                                                      float slopeScaleDepthBias)
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
        // Task 767: DepthBias/SlopeScaleDepthBias map directly onto real GL polygon offset
        // (matches this project's own already-established Vulkan convention, see
        // VulkanGraphicsBackend::ApplyRasterizerState's comment: "matching FNA's
        // glPolygonOffset(slopeScaleDepthBias, depthBias)"). Always enabled -- factor=0/units=0
        // is a genuine no-op in GL, so there is no need to conditionally disable it.
        device.set_polygon_offset_fill_enabled(true);
        device.set_polygon_offset(slopeScaleDepthBias, depthBias);
    }

    void EasyGLGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (metagl::IsContextLost()) return;
        if (w <= 0 || h <= 0) return; // invalid rect — leave scissor state unchanged
        // OpenGL scissor origin is bottom-left; convert from top-left XNA coordinates.
        // Use the render target's own height for the Y-flip when an RT is bound (mirrors
        // ReadBackbuffer's identical fbH pattern); fall back to the window's physical
        // height for the default framebuffer. Task 880: previously always used the
        // window's physical height even while an RT was bound, which is only latent
        // (scissor test is opt-in via RasterizerState.ScissorTestEnable) but wrong.
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int physW;
            getPhysicalSize(physW, fbH);
        }
        device.set_scissor(x, fbH - y - h, w, h);
        // Do NOT enable/disable scissor test here — that is controlled exclusively
        // by ApplyRasterizerState via RasterizerState.ScissorTestEnable.
    }

    void EasyGLGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        device.set_blend_color(r, g, b, a);
    }

    void EasyGLGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (metagl::IsContextLost()) return;
        if (w <= 0 || h <= 0) return; // invalid rect — leave viewport state unchanged
        // OpenGL viewport origin is bottom-left; convert from top-left XNA coordinates.
        // Use the render target's own height for the Y-flip when an RT is bound (mirrors
        // ReadBackbuffer's/SetScissorRect's identical fbH pattern); fall back to the
        // window's physical height for the default framebuffer. Using the window's height
        // unconditionally while an RT is bound produces a viewport y-offset that falls
        // entirely outside the RT's actual pixel range whenever the RT is smaller than the
        // window, discarding every fragment.
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int physW;
            getPhysicalSize(physW, fbH);
        }
        device.set_viewport(x, fbH - y - h, w, h);
        device.set_depth_range(minDepth, maxDepth);
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

        // Task 918: real anisotropic filtering via GL_EXT_texture_filter_anisotropic, gated on
        // the extension genuinely being available; falls back to the plain trilinear filter set
        // above (unchanged) when it isn't, exactly like before this fix.
        if (filter == 2 && metagl::HasExtension("GL_EXT_texture_filter_anisotropic"))
        {
            GLfloat maxAnisoCap = 1.0f;
            metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
            float requested = static_cast<float>(maxAnisotropy);
            float clamped = (maxAnisoCap > 0.0f && requested > maxAnisoCap) ? maxAnisoCap : requested;
            if (clamped < 1.0f) clamped = 1.0f;
            s.set_parameter(::easygl::SamplerParameter::MaxAnisotropy, clamped);
        }

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

    namespace
    {
        struct VertexAttribFormat
        {
            int componentCount;
            ::easygl::DataType type;
            bool normalized;
            bool isInteger;
        };

        // Task 1080: maps XNA's VertexElementFormat to the GL attribute shape needed to bind it
        // -- component count, GL scalar type, whether values are normalized to [0,1]/[-1,1], and
        // whether the attribute must be read as a true integer (glVertexAttribIPointer) rather
        // than converted to float (glVertexAttribPointer). Byte4 is the one format needing the
        // integer path -- XNA's own format for BLENDINDICES-style semantics (read as int4 in
        // HLSL), matching the existing skinned-vertex BlendIndices precedent below (offset 48,
        // case 52) that this table generalizes to arbitrary declarations.
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:          return { 1, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector2:         return { 2, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector3:         return { 3, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector4:         return { 4, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Color:           return { 4, ::easygl::DataType::UnsignedByte, true,  false };
            case VertexElementFormat::Byte4:           return { 4, ::easygl::DataType::UnsignedByte, false, true  };
            case VertexElementFormat::Short2:          return { 2, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::Short4:          return { 4, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::NormalizedShort2:return { 2, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::NormalizedShort4:return { 4, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::HalfVector2:     return { 2, ::easygl::DataType::HalfFloat,    false, false };
            case VertexElementFormat::HalfVector4:     return { 4, ::easygl::DataType::HalfFloat,    false, false };
            }
            return { 3, ::easygl::DataType::Float, false, false };
        }
    }

    void EasyGLVertexBufferBackend::SetVertexDeclaration(const std::vector<VertexElement>& elements)
    {
        declarationElements_ = elements;
    }

    void EasyGLVertexBufferBackend::ApplyLayout(std::size_t stride)
    {
        const int s = static_cast<int>(stride);
        vao.bind();
        vbo.bind(::easygl::BufferTarget::Array);

        if (!declarationElements_.empty())
        {
            // Task 1080: generic layout binding driven by the caller's own VertexDeclaration.
            // Attribute location = the element's own index within the declaration's element
            // list, matching this project's established "layout(location=N) == Nth field of the
            // ported HLSL input struct" convention used by every hand-ported .fx vertex shader
            // this session -- not a fixed byte-stride dispatch, so this covers genuinely custom
            // layouts (e.g. NormalMapping.fx's Position+Normal+Tangent+TexCoord) that don't match
            // any of the built-in strides the switch below recognizes.
            for (std::size_t i = 0; i < declarationElements_.size(); ++i)
            {
                const VertexElement& element = declarationElements_[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const auto location = static_cast<unsigned int>(i);
                const void* offset = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(element.getOffsetProperty()));
                vao.enable_attribute(location);
                if (desc.isInteger)
                    vao.set_attribute_i_pointer(location, desc.componentCount, desc.type, s, offset);
                else
                    vao.set_attribute_pointer(location, desc.componentCount, desc.type,
                                              desc.normalized, s, offset);
            }
            vao.unbind();
            return;
        }

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
        case 48:
            // plan_cnj.md CNB-57 (Phase 13A): VertexPositionNormalTangentTexture (packed):
            // float3 position + float3 normal + float4 tangent (xyz + bitangent handedness in w)
            // + float2 texcoord -- the layout PbrEffect's normal mapping needs to build a
            // per-pixel TBN basis.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            break;
        case 52:
            // Task 11.10: this layout is independently duplicated (magic stride 52) in
            // BgfxGraphicsBackend.cpp's MakeBgfxLayout and VulkanGraphicsBackend.cpp's
            // GetOrCreatePipelineSkinned3D - none derive it from the canonical
            // VertexPositionNormalTextureSkinned::getVertexDeclarationStatic() (5 VertexElements:
            // offset 0 Vector3 Position, 12 Vector3 Normal, 24 Vector2 TextureCoordinate,
            // 32 Vector4 BlendWeight, 48 Byte4 BlendIndices). If that struct's field order or
            // any individual offset ever changes (its total size is already guarded by
            // VertexBuffer.cpp's own static_assert(sizeof(GpuVertex) == 52)), all 3 copies below
            // must be updated together - investigated deriving this from a shared VertexElement
            // list instead, but every backend's API boundary currently only receives a raw
            // stride, not a VertexDeclaration; doing so would mean widening IGraphicsBackend's
            // interface across all 4 backends for every existing magic-stride case (16/32/52),
            // not just this one - deferred as a larger cross-backend refactor, not attempted here.
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
        case 56:
            // CNB-67 (Phase 13C): the stride-52 SkinnedVertex layout above with a per-vertex
            // Color (normalized ubyte4) appended at the end (offset 52), rather than inserted
            // mid-layout -- keeps attribute locations 0-4 byte-identical to the stride-52 case
            // above, so EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()'s single shader
            // pair serves both strides; location 5 (aColor) is simply left unbound for stride-52
            // draws, and GpuDrawParams::vertexColorEnabled (gated by SkinnedEffect::
            // VertexColorEnabled, see FillGpuDrawParams) already governs whether the shader reads
            // it at all.
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
            vao.enable_attribute(5);
            vao.set_attribute_pointer(5, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)52);
            break;
        case 68:
            // PBR + skinning combo (VertexPositionNormalTangentTextureSkinned): the stride-48
            // Position+Normal+Tangent+TextureCoordinate layout with the stride-52/56 skinning
            // suffix (BlendWeight, BlendIndices) appended, matching those precedents' own
            // "append rather than insert" convention -- locations 0-3 stay byte-identical to the
            // stride-48 PbrEffect layout, locations 4-5 mirror the stride-52 skinning suffix's
            // own attribute shape.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            vao.enable_attribute(4);
            vao.set_attribute_pointer(4, 4, ::easygl::DataType::Float, false, s, (void*)48);
            vao.enable_attribute(5);
            vao.set_attribute_i_pointer(5, 4, ::easygl::DataType::UnsignedByte, s, (void*)64);
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
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
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
        prog_colored_.loc_fog_vector = prog_colored_.prog.uniform_location("uFogVector");
        prog_colored_.loc_fog_color   = prog_colored_.prog.uniform_location("uFogColor");
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
"uniform vec4 uFogVector;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
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
        prog_textured_.loc_fog_vector = prog_textured_.prog.uniform_location("uFogVector");
        prog_textured_.loc_fog_color   = prog_textured_.prog.uniform_location("uFogColor");
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
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
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
        prog_col_textured_.loc_fog_vector = prog_col_textured_.prog.uniform_location("uFogVector");
        prog_col_textured_.loc_fog_color   = prog_col_textured_.prog.uniform_location("uFogColor");
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
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vNormal=uNormalMatrix*aNormal;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"    vWorldPos=(uWorld*vec4(aPos,1.0)).xyz;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 E=normalize(uEyePosition-vWorldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
"    vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"    FragColor=texture(uTexture,vUV)*vec4(litRGB,uDiffuseColor.a);\n"
"    FragColor.rgb+=specularRGB*FragColor.a;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_.prog, vsrc, fsrc, "lit+textured");
        prog_lit_textured_.loc_wvp         = prog_lit_textured_.prog.uniform_location("uWVP");
        prog_lit_textured_.loc_world       = prog_lit_textured_.prog.uniform_location("uWorld");
        prog_lit_textured_.loc_normalmat   = prog_lit_textured_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_.loc_diffuse     = prog_lit_textured_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_.loc_ambient     = prog_lit_textured_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_.loc_l0dir       = prog_lit_textured_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_.loc_l0diff      = prog_lit_textured_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_.loc_l1dir       = prog_lit_textured_.prog.uniform_location("uLight1Dir");
        prog_lit_textured_.loc_l1diff      = prog_lit_textured_.prog.uniform_location("uLight1Diffuse");
        prog_lit_textured_.loc_l2dir       = prog_lit_textured_.prog.uniform_location("uLight2Dir");
        prog_lit_textured_.loc_l2diff      = prog_lit_textured_.prog.uniform_location("uLight2Diffuse");
        prog_lit_textured_.loc_l0spec      = prog_lit_textured_.prog.uniform_location("uLight0Specular");
        prog_lit_textured_.loc_l1spec      = prog_lit_textured_.prog.uniform_location("uLight1Specular");
        prog_lit_textured_.loc_l2spec      = prog_lit_textured_.prog.uniform_location("uLight2Specular");
        prog_lit_textured_.loc_specularcolor = prog_lit_textured_.prog.uniform_location("uSpecularColor");
        prog_lit_textured_.loc_specularpower = prog_lit_textured_.prog.uniform_location("uSpecularPower");
        prog_lit_textured_.loc_eyepos      = prog_lit_textured_.prog.uniform_location("uEyePosition");
        prog_lit_textured_.loc_emissive    = prog_lit_textured_.prog.uniform_location("uEmissiveColor");
        prog_lit_textured_.loc_texture     = prog_lit_textured_.prog.uniform_location("uTexture");
        prog_lit_textured_.loc_alphatest   = prog_lit_textured_.prog.uniform_location("uAlphaTest");
        prog_lit_textured_.loc_fog_vector = prog_lit_textured_.prog.uniform_location("uFogVector");
        prog_lit_textured_.loc_fog_color   = prog_lit_textured_.prog.uniform_location("uFogColor");
        prog_lit_textured_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D ready loc_wvp=" << prog_lit_textured_.loc_wvp);
    }

    // Task 1102 (plan_graphics.md Phase 80 / plan_dx9.md Divergence 1): real XNA's
    // BasicEffect/SkinnedEffect default to PreferPerPixelLighting=false, which selects a
    // per-vertex-lit shader family (VSBasicVertexLighting*) -- lighting is computed ONCE per
    // vertex and Gouraud-interpolated across the triangle, not re-evaluated per fragment.
    // EnsureLit3DProgram() above is the PreferPerPixelLighting=true family; this is its
    // per-vertex-lit sibling, selected by SelectProgram() when the flag is false (XNA's own
    // default). Identical Blinn-Phong math to EnsureLit3DProgram() (FNA's own Lighting.fxh
    // ComputeLights(), same formula, same inputs) -- only the STAGE it runs in changes: the lit
    // RGB (ambient+diffuse sum) and specular RGB are computed once per vertex and passed as
    // `out` varyings, and the fragment shader's own math (texture sample, alpha test, fog) is
    // structurally identical to EnsureLit3DProgram()'s, just reading the interpolated varyings
    // instead of recomputing them. Declares the SAME uniform names as EnsureLit3DProgram() (just
    // in the vertex stage for the lighting-specific ones) so BindDrawParams() needs no changes at
    // all -- it already looks up each uniform generically by Prog3D's own loc_* fields, regardless
    // of which shader stage actually declared that uniform in the linked program.
    void EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()
    {
        if (prog_lit_textured_vertexlit_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
// uDiffuseColor is read by BOTH stages here (vertex needs .rgb for vLitRGB, fragment needs .a) --
// GLSL ES 3.00 requires a uniform shared across stages to have the SAME precision qualification,
// and this shader's own vertex/fragment stages have different DEFAULT float precisions (highp
// here vs. mediump in the fragment stage below), so it must be qualified explicitly and
// identically in both declarations or linking fails ("mismatching precision qualifiers") --
// found via a real link failure, not assumed.
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec3 uEmissiveColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vLitRGB;\n"
"out vec3 vSpecularRGB;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"    vec3 worldPos=(uWorld*vec4(aPos,1.0)).xyz;\n"
"    vec3 N=normalize(uNormalMatrix*aNormal);\n"
"    vec3 E=normalize(uEyePosition-worldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
"    vLitRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vLitRGB;\n"
"in vec3 vSpecularRGB;\n"
"uniform sampler2D uTexture;\n"
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=texture(uTexture,vUV)*vec4(vLitRGB,uDiffuseColor.a);\n"
"    FragColor.rgb+=vSpecularRGB*FragColor.a;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_vertexlit_.prog, vsrc, fsrc, "lit+textured (vertex-lit)");
        prog_lit_textured_vertexlit_.loc_wvp         = prog_lit_textured_vertexlit_.prog.uniform_location("uWVP");
        prog_lit_textured_vertexlit_.loc_world       = prog_lit_textured_vertexlit_.prog.uniform_location("uWorld");
        prog_lit_textured_vertexlit_.loc_normalmat   = prog_lit_textured_vertexlit_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_vertexlit_.loc_diffuse     = prog_lit_textured_vertexlit_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_vertexlit_.loc_ambient     = prog_lit_textured_vertexlit_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_vertexlit_.loc_l0dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_vertexlit_.loc_l0diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_vertexlit_.loc_l1dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Dir");
        prog_lit_textured_vertexlit_.loc_l1diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Diffuse");
        prog_lit_textured_vertexlit_.loc_l2dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Dir");
        prog_lit_textured_vertexlit_.loc_l2diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Diffuse");
        prog_lit_textured_vertexlit_.loc_l0spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Specular");
        prog_lit_textured_vertexlit_.loc_l1spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Specular");
        prog_lit_textured_vertexlit_.loc_l2spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Specular");
        prog_lit_textured_vertexlit_.loc_specularcolor = prog_lit_textured_vertexlit_.prog.uniform_location("uSpecularColor");
        prog_lit_textured_vertexlit_.loc_specularpower = prog_lit_textured_vertexlit_.prog.uniform_location("uSpecularPower");
        prog_lit_textured_vertexlit_.loc_eyepos      = prog_lit_textured_vertexlit_.prog.uniform_location("uEyePosition");
        prog_lit_textured_vertexlit_.loc_emissive    = prog_lit_textured_vertexlit_.prog.uniform_location("uEmissiveColor");
        prog_lit_textured_vertexlit_.loc_texture     = prog_lit_textured_vertexlit_.prog.uniform_location("uTexture");
        prog_lit_textured_vertexlit_.loc_alphatest   = prog_lit_textured_vertexlit_.prog.uniform_location("uAlphaTest");
        prog_lit_textured_vertexlit_.loc_fog_vector = prog_lit_textured_vertexlit_.prog.uniform_location("uFogVector");
        prog_lit_textured_vertexlit_.loc_fog_color   = prog_lit_textured_vertexlit_.prog.uniform_location("uFogColor");
        prog_lit_textured_vertexlit_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D (vertex-lit) ready loc_wvp=" << prog_lit_textured_vertexlit_.loc_wvp);
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
"uniform vec4 uFogVector;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 base=texture(uTexture,vUV);\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,vUV)*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_dual_textured_.prog, vsrc, fsrc, "dual+textured");
        prog_dual_textured_.loc_wvp         = prog_dual_textured_.prog.uniform_location("uWVP");
        prog_dual_textured_.loc_texture     = prog_dual_textured_.prog.uniform_location("uTexture");
        prog_dual_textured_.loc_texture2    = prog_dual_textured_.prog.uniform_location("uTexture2");
        prog_dual_textured_.loc_diffuse     = prog_dual_textured_.prog.uniform_location("uDiffuseColor");
        prog_dual_textured_.loc_alphatest   = prog_dual_textured_.prog.uniform_location("uAlphaTest");
        prog_dual_textured_.loc_fog_vector = prog_dual_textured_.prog.uniform_location("uFogVector");
        prog_dual_textured_.loc_fog_color   = prog_dual_textured_.prog.uniform_location("uFogColor");
        prog_dual_textured_.ready           = true;
        CNA_RENDER_LOG("dual+textured3D ready loc_wvp=" << prog_dual_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureDualTexturedColored3DProgram()
    {
        if (prog_dual_textured_colored_.ready) return;

        // Task 889: stride-24 (VertexPositionColorTexture) variant of the dual-texture shader
        // above -- reads the vertex color and gates it by VertexColorEnabled, mirroring
        // EnsureColoredTextured3DProgram()'s already-correct BasicEffect formula.
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    vec4 base=texture(uTexture,vUV);\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,vUV)*vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_dual_textured_colored_.prog, vsrc, fsrc, "dual+textured+colored");
        prog_dual_textured_colored_.loc_wvp         = prog_dual_textured_colored_.prog.uniform_location("uWVP");
        prog_dual_textured_colored_.loc_texture     = prog_dual_textured_colored_.prog.uniform_location("uTexture");
        prog_dual_textured_colored_.loc_texture2    = prog_dual_textured_colored_.prog.uniform_location("uTexture2");
        prog_dual_textured_colored_.loc_diffuse     = prog_dual_textured_colored_.prog.uniform_location("uDiffuseColor");
        prog_dual_textured_colored_.loc_alphatest   = prog_dual_textured_colored_.prog.uniform_location("uAlphaTest");
        prog_dual_textured_colored_.loc_fog_vector = prog_dual_textured_colored_.prog.uniform_location("uFogVector");
        prog_dual_textured_colored_.loc_fog_color   = prog_dual_textured_colored_.prog.uniform_location("uFogColor");
        prog_dual_textured_colored_.loc_vertexcolor = prog_dual_textured_colored_.prog.uniform_location("uVertexColorEnabled");
        prog_dual_textured_colored_.ready           = true;
        CNA_RENDER_LOG("dual+textured+colored3D ready loc_wvp=" << prog_dual_textured_colored_.loc_wvp);
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
"uniform vec4 uFogVector;\n"
"uniform float uEnvMapAmount;\n"
"uniform float uFresnelEnabled;\n"
"uniform float uFresnelFactor;\n"
"out vec3 vWorldNormal;\n"
"out vec3 vEyeDir;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out float vFresnel;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vec3 worldPos=(uWorld*vec4(aPos,1.0)).xyz;\n"
"    vec3 worldNormal=normalize(uNormalMatrix*aNormal);\n"
"    vec3 eyeVector=normalize(uEyePosition-worldPos);\n"
"    vWorldNormal=worldNormal;\n"
"    vEyeDir=eyeVector;\n"
"    vUV=aUV;\n"
// Real XNA (EnvironmentMapEffect.fx's ComputeFresnelFactor) evaluates this per-VERTEX, in the
// vertex shader, from each vertex's own un-interpolated normal/eye vector, then Gouraud-
// interpolates the resulting scalar -- NOT a per-fragment recompute from an interpolated normal
// (Task 1112: the two are not equivalent once vertices carry different normals).
"    float viewAngle=dot(eyeVector,worldNormal);\n"
"    vFresnel=(uFresnelEnabled>0.5)\n"
"        ? pow(max(1.0-abs(viewAngle),0.0),uFresnelFactor)*uEnvMapAmount\n"
"        : uEnvMapAmount;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vWorldNormal;\n"
"in vec3 vEyeDir;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in float vFresnel;\n"
"uniform sampler2D uTexture;\n"
"uniform samplerCube uEnvMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEnvMapSpecular;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vWorldNormal);\n"
"    vec3 E=normalize(vEyeDir);\n"
"    float NdotL0=max(dot(N,-uLight0Dir),0.0);\n"
"    float NdotL1=max(dot(N,-uLight1Dir),0.0);\n"
"    float NdotL2=max(dot(N,-uLight2Dir),0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// Same FNA-fidelity fix as EnsureSkinnedProgram below - EnvironmentMapEffect.fx routes its own
// lighting through the identical Lighting.fxh ComputeLights() in FNA, so it composes emissive
// exactly the same way (added after the diffuse multiply, not multiplied by it).
"    vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec4 texColor=texture(uTexture,vUV);\n"
"    vec3 reflDir=reflect(-E,N);\n"
"    vec4 envSample=texture(uEnvMap,reflDir);\n"
"    vec3 baseColor=litRGB*texColor.rgb;\n"
"    float combinedAlpha=uDiffuseColor.a*texColor.a;\n"
"    float blendFactor=vFresnel;\n"
"    vec3 rgb=mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)+uEnvMapSpecular*envSample.a*combinedAlpha;\n"
"    FragColor=vec4(rgb,combinedAlpha);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
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
        p.loc_l1dir         = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff        = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir         = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff        = p.prog.uniform_location("uLight2Diffuse");
        p.loc_envmap_amount   = p.prog.uniform_location("uEnvMapAmount");
        p.loc_envmap_spec     = p.prog.uniform_location("uEnvMapSpecular");
        p.loc_fresnel_enabled = p.prog.uniform_location("uFresnelEnabled");
        p.loc_fresnel_factor  = p.prog.uniform_location("uFresnelFactor");
        p.loc_alphatest       = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector     = p.prog.uniform_location("uFogVector");
        p.loc_fog_color       = p.prog.uniform_location("uFogColor");
        p.ready               = true;
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
"layout(location=5) in vec4 aColor;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"out vec4 vColor;\n"
"void main(){\n"
// Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
// weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    gl_Position=uWVP*skinnedPos;\n"
// A vertex blended near-evenly between two bones whose current relative rotation is
// close to 180 degrees (reachable in practice: wide weight-blend joint regions x a
// large-angle animation pose, e.g. Wave) can make the linearly-blended skinMat's
// rotational part nearly cancel out for this particular normal, so its transformed
// length collapses toward zero. normalize() of a near-zero vector is numerically
// unstable (can yield NaN), which then poisons the entire downstream lighting sum --
// observed as solid-black blotches independent of ambient/diffuse light color, not a
// plausible-but-wrong shading direction. Falls back to the untransformed bind-pose
// normal for just that vertex rather than propagating NaN; XNA/FNA's own Skin() was
// never validated against this degenerate case, so this is a numerical-safety guard,
// not a deviation from its intended per-vertex transform.
"    vec3 skinnedNormal=mat3(skinMat)*aNormal;\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
// REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
// (uNormalMatrix = transpose(inverse(World3x3)), CPU-precomputed in BindDrawParams() exactly as
// every non-skinned lit program here already receives it). FNA's SkinnedEffect.fx Skin() applies
// the bone 3x3, then Lighting.fxh applies mul(normal, WorldInverseTranspose); this shader dropped
// the outer world factor entirely (audit Variant A), so any rotated or non-uniformly-scaled
// skinned model was lit as if World were identity. The fragment stage re-normalizes vNormal.
"    vNormal=uNormalMatrix*boneNormal;\n"
"    vUV=aUV;\n"
"    vWorldPos=(uWorld*skinnedPos).xyz;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
// Skinned: dot the POST-skin position (FNA Skin() mutates vin.Position before ComputeFogFactor).
"    vFogFactor=1.0-clamp(dot(vec4(skinnedPos.xyz,1.0),uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"in vec4 vColor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 E=normalize(uEyePosition-vWorldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// audit_net.md remediation (2026-07-18, fourth round): EmissiveColor is ADDED after the
// diffuse multiply, never multiplied by it - matches FNA's own Lighting.fxh ComputeLights()
// verbatim (`mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor`), and matches what
// EnsureLit3DProgram/EnsureLit3DVertexLitProgram in this same file already did correctly.
// This shader had `(uEmissiveColor+lightSum)*uDiffuseColor.rgb`, which multiplied the emissive
// term by DiffuseColor a second time. Since FillGpuDrawParams pre-folds ambient into emissive
// (`emissive + ambient*diffuse`, itself correct and FNA-faithful), the old form computed
// ambient*diffuse^2 - a quadratic suppression that crushed DARK materials specifically: the
// avatar's shoes (diffuse 0.14) got an ambient floor of 0.5*0.14*0.14 = 0.0098 -> 2.5/255
// (confirmed by direct pixel sampling: the darkest foot pixels read exactly (3,3,3)) instead
// of the correct 0.5*0.14 = 0.07 -> 18/255. This is the real reason raising ambient kept
// giving diminishing returns on the dark regions.
"    vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"    vec4 texColor=texture(uTexture,vUV);\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vec4(litRGB*texColor.rgb,uDiffuseColor.a*texColor.a*vc.a);\n"
"    FragColor.rgb+=specularRGB*FragColor.a;\n"
// Vertex color modulates the whole combined diffuse+specular output, not just diffuse -- applied
// after the specular add so VertexColorEnabled=true with a black vertex color genuinely zeroes
// the pixel (a specular highlight added afterward would otherwise leak through unmodulated).
"    FragColor.rgb*=vc.rgb;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_skinned_.prog, vsrc, fsrc, "skinned");
        auto& p = prog_skinned_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_l0spec    = p.prog.uniform_location("uLight0Specular");
        p.loc_l1spec    = p.prog.uniform_location("uLight1Specular");
        p.loc_l2spec    = p.prog.uniform_location("uLight2Specular");
        p.loc_specularcolor = p.prog.uniform_location("uSpecularColor");
        p.loc_specularpower = p.prog.uniform_location("uSpecularPower");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.ready         = true;
        CNA_RENDER_LOG("skinned3D ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    // Task 1102b (plan_graphics.md Phase 80 / plan_dx9.md Divergence 1): SkinnedEffect's own
    // per-vertex-lit sibling, mirroring Task 1102's EnsureLit3DVertexLitProgram() for BasicEffect
    // exactly -- same technique (move FNA's Lighting.fxh ComputeLights() Blinn-Phong math from the
    // fragment stage into the vertex stage, Gouraud-interpolate the result via varyings, keep the
    // fragment stage's non-lighting math -- texture sample, alpha test, fog -- structurally
    // identical), applied to EnsureSkinnedProgram() above (the PreferPerPixelLighting=true family)
    // instead of EnsureLit3DProgram(). The skinning itself is unchanged -- only WHERE lighting is
    // evaluated moves, exactly like Task 1102's own BasicEffect case. Selected by SelectProgram()
    // when params.skinned && params.lightingEnabled && !params.preferPerPixelLighting (XNA's own
    // default). No separate uAmbientColor uniform here, matching EnsureSkinnedProgram()'s own
    // shape: SkinnedEffect::FillGpuDrawParams() already pre-folds ambient into emissiveColor
    // (verified at that call site), so BindDrawParams()'s existing "p.loc_ambient < 0" gating
    // (which already distinguishes the ambient-having BasicEffect programs from the
    // ambient-less EnvironmentMapEffect/SkinnedEffect ones) correctly routes this new program's
    // light/specular uniform uploads too, with zero BindDrawParams() changes needed -- identical to
    // Task 1102's own finding for BasicEffect.
    void EasyGLGraphicsBackend::EnsureSkinnedVertexLitProgram()
    {
        if (prog_skinned_vertexlit_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aBoneWeights;\n"
"layout(location=4) in uvec4 aBoneIndices;\n"
"layout(location=5) in vec4 aColor;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
// uDiffuseColor is read by BOTH stages here (vertex needs .rgb for vLitRGB, fragment needs .a) --
// same GLSL ES "matching precision qualifier across stages" requirement Task 1102 already found
// and fixed for BasicEffect's own vertex-lit shader -- qualified explicitly and identically in
// both declarations here too, not assumed safe by analogy.
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vLitRGB;\n"
"out vec3 vSpecularRGB;\n"
"out vec4 vColor;\n"
"void main(){\n"
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    gl_Position=uWVP*skinnedPos;\n"
"    vUV=aUV;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(vec4(skinnedPos.xyz,1.0),uFogVector),0.0,1.0);\n"
"    vec3 worldPos=(uWorld*skinnedPos).xyz;\n"
// Same degenerate-blend-normal guard as EnsureSkinnedProgram() above (see its own
// comment for the root cause) -- this vertex-lit sibling does the identical skinning
// and normal transform, just with lighting evaluated per-vertex instead of per-pixel.
"    vec3 skinnedNormal=mat3(skinMat)*aNormal;\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
// REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix (uNormalMatrix =
// transpose(inverse(World3x3)), CPU-precomputed in BindDrawParams()). This vertex-lit sibling had
// the identical missing-world-factor defect (audit Variant A) as EnsureSkinnedProgram; unlike that
// per-pixel program (whose fragment stage re-normalizes vNormal), lighting here is evaluated in
// this stage, so the world-transformed normal must be re-normalized before the dot products.
"    vec3 N=normalize(uNormalMatrix*boneNormal);\n"
"    vec3 E=normalize(uEyePosition-worldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// Same FNA-fidelity fix as EnsureSkinnedProgram above - see its own comment for the full
// reasoning; this vertex-lit sibling had the identical emissive-multiplied-twice bug.
"    vLitRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vLitRGB;\n"
"in vec3 vSpecularRGB;\n"
"in vec4 vColor;\n"
"uniform sampler2D uTexture;\n"
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 texColor=texture(uTexture,vUV);\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vec4(vLitRGB*texColor.rgb,uDiffuseColor.a*texColor.a*vc.a);\n"
"    FragColor.rgb+=vSpecularRGB*FragColor.a;\n"
// See EnsureSkinnedProgram()'s identical comment: vertex color modulates the whole combined
// diffuse+specular output, applied after the specular add.
"    FragColor.rgb*=vc.rgb;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_skinned_vertexlit_.prog, vsrc, fsrc, "skinned (vertex-lit)");
        auto& p = prog_skinned_vertexlit_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_l0spec    = p.prog.uniform_location("uLight0Specular");
        p.loc_l1spec    = p.prog.uniform_location("uLight1Specular");
        p.loc_l2spec    = p.prog.uniform_location("uLight2Specular");
        p.loc_specularcolor = p.prog.uniform_location("uSpecularColor");
        p.loc_specularpower = p.prog.uniform_location("uSpecularPower");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.ready         = true;
        CNA_RENDER_LOG("skinned3D (vertex-lit) ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    // plan_cnj.md CNB-58 (Phase 13A): real glTF metallic-roughness BRDF (glTF 2.0 spec Appendix
    // B) -- GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX visibility, Schlick
    // Fresnel -- driven by the same 3-DirectionalLight + AmbientLightColor convention every other
    // CNA stock effect already uses (so existing scene-lighting setup code transfers directly),
    // rather than image-based lighting (a much larger, separate feature: irradiance/prefiltered
    // environment maps + a BRDF LUT). Normal mapping via a per-pixel TBN basis built from the
    // vertex tangent (re-orthogonalized against the interpolated normal) and glTF's own
    // bitangent-handedness-sign convention (Bitangent = cross(Normal,Tangent.xyz)*Tangent.w).
    // Only the EasyGL backend implements this program (CNB-58 explicitly scopes other backends to
    // separate follow-ups, CNB-61) -- PbrEffect::FillGpuDrawParams() still fills GpuDrawParams
    // completely, so a non-EasyGL backend simply ignores the new pbr*/texture fields already,
    // matching this codebase's established "unimplemented field is safely ignored" convention.
    void EasyGLGraphicsBackend::EnsurePbrProgram()
    {
        if (prog_pbr_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec3 vTangent;\n"
"out float vBitangentSign;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vNormal=uNormalMatrix*aNormal;\n"
// Tangent transforms as a plain direction under mat3(uWorld) (not the inverse-transpose
// uNormalMatrix use for the normal) -- correct for uniform-scale World transforms, a documented
// simplification for non-uniform scale shared with most real-time engines lacking a full
// per-tangent inverse-transpose.
"    vTangent=mat3(uWorld)*aTangent.xyz;\n"
"    vBitangentSign=aTangent.w;\n"
"    vUV=aUV;\n"
"    vWorldPos=(uWorld*vec4(aPos,1.0)).xyz;\n"
"    vFogFactor=1.0-clamp(dot(vec4(aPos,1.0),uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting k=(roughness+1)^2/8), and
// Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF (Appendix B.3.3/B.3.4/B.3.2).
"vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, float roughness, float metallic){\n"
"    vec3 H=normalize(V+L);\n"
"    float NdotL=max(dot(N,L),0.0);\n"
"    float NdotV=max(dot(N,V),1e-4);\n"
"    float NdotH=max(dot(N,H),0.0);\n"
"    float VdotH=max(dot(V,H),0.0);\n"
"    float a2=pow(roughness,4.0);\n"
"    float dTerm=(NdotH*NdotH*(a2-1.0)+1.0);\n"
"    float D=a2/(3.14159265*dTerm*dTerm+1e-7);\n"
"    float k=(roughness+1.0); k=k*k/8.0;\n"
"    float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));\n"
"    vec3 F=F0+(vec3(1.0)-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);\n"
"    vec3 specular=(D*G*F)/max(4.0*NdotV*NdotL,1e-4);\n"
"    vec3 diffuseColor=albedo*(1.0-metallic);\n"
"    vec3 kd=vec3(1.0)-F;\n"
"    return (kd*diffuseColor/3.14159265+specular)*lightColor*NdotL;\n"
"}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,vUV);\n"
"    vec3 albedo=baseColorTex.rgb*uDiffuseColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,vUV).rgb*2.0-1.0;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,vUV);\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    vec3 F0=mix(vec3(0.04),albedo,metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,roughness,metallic);\n"
"    float occlusion=texture(uOcclusionMap,vUV).r;\n"
"    vec3 ambient=uAmbientColor*albedo*occlusion;\n"
"    vec3 emissive=uEmissiveColor*texture(uEmissiveMap,vUV).rgb;\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_pbr_.prog, vsrc, fsrc, "pbr");
        auto& p = prog_pbr_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_ambient   = p.prog.uniform_location("uAmbientColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_pbr_normalmap     = p.prog.uniform_location("uNormalMap");
        p.loc_pbr_mr            = p.prog.uniform_location("uMetallicRoughnessMap");
        p.loc_pbr_emissivemap   = p.prog.uniform_location("uEmissiveMap");
        p.loc_pbr_occlusionmap  = p.prog.uniform_location("uOcclusionMap");
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.ready         = true;
        CNA_RENDER_LOG("pbr3D ready loc_wvp=" << p.loc_wvp);
    }

    // PBR + skinning combo: EnsureSkinnedProgram()'s bone-palette vertex transform (applied to
    // Position, Normal, and now also Tangent, since a skinned normal map needs a skinned TBN
    // basis too) feeding EnsurePbrProgram()'s own fragment-stage BRDF unchanged -- the two
    // programs' logic is additive, not a new algorithm (SkinnedPbrEffect, PBR+skinning combo).
    void EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()
    {
        if (prog_pbr_skinned_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
"layout(location=4) in vec4 aBoneWeights;\n"
"layout(location=5) in uvec4 aBoneIndices;\n"
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec3 vTangent;\n"
"out float vBitangentSign;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    gl_Position=uWVP*skinnedPos;\n"
"    mat3 skinNormalMat=mat3(skinMat);\n"
// REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix (uNormalMatrix),
// not raw mat3(uWorld). Raw World is only correct for rotation and uniform scale and diverges from
// FNA's mul(normal, WorldInverseTranspose) under non-uniform scale; it also contradicted this
// file's own unskinned EnsurePbrProgram, which already uses uNormalMatrix. The tangent stays on
// raw World: tangents transform as directions, not as normals (glTF convention, unchanged).
"    vNormal=normalize(uNormalMatrix*(skinNormalMat*aNormal));\n"
"    vTangent=mat3(uWorld)*(skinNormalMat*aTangent.xyz);\n"
"    vBitangentSign=aTangent.w;\n"
"    vUV=aUV;\n"
"    vWorldPos=(uWorld*skinnedPos).xyz;\n"
"    vFogFactor=1.0-clamp(dot(vec4(skinnedPos.xyz,1.0),uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, float roughness, float metallic){\n"
"    vec3 H=normalize(V+L);\n"
"    float NdotL=max(dot(N,L),0.0);\n"
"    float NdotV=max(dot(N,V),1e-4);\n"
"    float NdotH=max(dot(N,H),0.0);\n"
"    float VdotH=max(dot(V,H),0.0);\n"
"    float a2=pow(roughness,4.0);\n"
"    float dTerm=(NdotH*NdotH*(a2-1.0)+1.0);\n"
"    float D=a2/(3.14159265*dTerm*dTerm+1e-7);\n"
"    float k=(roughness+1.0); k=k*k/8.0;\n"
"    float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));\n"
"    vec3 F=F0+(vec3(1.0)-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);\n"
"    vec3 specular=(D*G*F)/max(4.0*NdotV*NdotL,1e-4);\n"
"    vec3 diffuseColor=albedo*(1.0-metallic);\n"
"    vec3 kd=vec3(1.0)-F;\n"
"    return (kd*diffuseColor/3.14159265+specular)*lightColor*NdotL;\n"
"}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,vUV);\n"
"    vec3 albedo=baseColorTex.rgb*uDiffuseColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,vUV).rgb*2.0-1.0;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,vUV);\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    vec3 F0=mix(vec3(0.04),albedo,metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,roughness,metallic);\n"
"    float occlusion=texture(uOcclusionMap,vUV).r;\n"
"    vec3 ambient=uAmbientColor*albedo*occlusion;\n"
"    vec3 emissive=uEmissiveColor*texture(uEmissiveMap,vUV).rgb;\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_pbr_skinned_.prog, vsrc, fsrc, "pbr_skinned");
        auto& p = prog_pbr_skinned_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_ambient   = p.prog.uniform_location("uAmbientColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_pbr_normalmap     = p.prog.uniform_location("uNormalMap");
        p.loc_pbr_mr            = p.prog.uniform_location("uMetallicRoughnessMap");
        p.loc_pbr_emissivemap   = p.prog.uniform_location("uEmissiveMap");
        p.loc_pbr_occlusionmap  = p.prog.uniform_location("uOcclusionMap");
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.ready         = true;
        CNA_RENDER_LOG("pbr_skinned3D ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    void EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()
    {
        if (default_white_texture_ready_) return;
        static const uint8_t white[4] = {255, 255, 255, 255};
        default_white_texture_.create();
        default_white_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, white);
        default_white_texture_ready_ = true;
    }

    // plan_cnj.md CNB-58 (Phase 13A): fallback for PbrEffect::NormalMap when unbound -- a "flat"
    // tangent-space normal (0,0,1) encoded as RGB (128,128,255), so the sampled/decoded (rgb*2-1)
    // normal is exactly the geometric normal (no perturbation). The other 3 PBR map fallbacks
    // (metallic-roughness, emissive, occlusion) all reuse the existing default_white_texture_
    // instead -- their respective factor/no-op semantics already make (1,1,1,1) the correct
    // "map absent" value (factor*1.0=factor; emissive tint*1.0=tint; occlusion 1.0=unoccluded).
    void EasyGLGraphicsBackend::EnsureDefaultFlatNormalTexture()
    {
        if (default_flat_normal_texture_ready_) return;
        static const uint8_t flatNormal[4] = {128, 128, 255, 255};
        default_flat_normal_texture_.create();
        default_flat_normal_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, flatNormal);
        default_flat_normal_texture_ready_ = true;
    }

    EasyGLGraphicsBackend::Prog3D& EasyGLGraphicsBackend::SelectProgram(std::size_t stride,
                                                                          const GpuDrawParams& params)
    {
        if (params.pbr && params.skinned)
        {
            EnsurePbrSkinnedProgram();
            return prog_pbr_skinned_;
        }
        if (params.pbr)
        {
            EnsurePbrProgram();
            return prog_pbr_;
        }
        if (params.skinned)
        {
            // Task 1102b (plan_dx9.md Divergence 1): real XNA's SkinnedEffect defaults
            // PreferPerPixelLighting=false too, same as BasicEffect (Task 1102). Only
            // meaningfully distinct while lighting is actually on, same reasoning as Task 1102's
            // own stride-32 gate.
            if (params.lightingEnabled && !params.preferPerPixelLighting)
            {
                EnsureSkinnedVertexLitProgram();
                return prog_skinned_vertexlit_;
            }
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
            // Task 889: stride 24 (VertexPositionColorTexture) gets its own vertex-color-aware
            // program; stride 20 (VertexPositionTexture) keeps the original color-less shader.
            if (stride == 24)
            {
                EnsureDualTexturedColored3DProgram();
                return prog_dual_textured_colored_;
            }
            EnsureDualTextured3DProgram();
            return prog_dual_textured_;
        }
        switch (stride)
        {
        case 20: EnsureTextured3DProgram();        return prog_textured_;
        case 24: EnsureColoredTextured3DProgram(); return prog_col_textured_;
        case 32:
            // Task 1102 (plan_dx9.md Divergence 1): real XNA's BasicEffect defaults
            // PreferPerPixelLighting=false (per-vertex/Gouraud-shaded lighting), the opposite of
            // what this backend rendered unconditionally before this task. Only meaningfully
            // distinct while lighting is actually on -- with lighting disabled, both programs
            // degenerate to the identical trivial ambient=(1,1,1) case (see BindDrawParams()'s
            // own else-branch), so the existing pixel-lit program stays selected there to avoid
            // an unnecessary program switch.
            if (params.lightingEnabled && !params.preferPerPixelLighting)
            {
                EnsureLit3DVertexLitProgram();
                return prog_lit_textured_vertexlit_;
            }
            EnsureLit3DProgram();
            return prog_lit_textured_;
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

        // Normal matrix — transpose(inverse(world3x3)), via the cofactor/det shortcut, so
        // non-uniform-scale World transforms don't skew the transformed normal (Task 398 fix;
        // the raw upper-left 3x3 used before was only correct for rotation/uniform-scale/
        // translation World matrices).
        if (p.loc_normalmat >= 0)
        {
            const float* w = params.worldColMajor;
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            float nm[9] = {
                (e * i - f * h) * invDet, -(b * i - c * h) * invDet, (b * f - c * e) * invDet,
                -(d * i - f * g) * invDet, (a * i - c * g) * invDet, -(a * f - c * d) * invDet,
                (d * h - e * g) * invDet, -(a * h - b * g) * invDet, (a * e - b * d) * invDet,
            };
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
                if (p.loc_l1dir >= 0)
                    p.prog.set_uniform(p.loc_l1dir,
                        params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
                if (p.loc_l1diff >= 0)
                    p.prog.set_uniform(p.loc_l1diff,
                        params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]);
                if (p.loc_l2dir >= 0)
                    p.prog.set_uniform(p.loc_l2dir,
                        params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
                if (p.loc_l2diff >= 0)
                    p.prog.set_uniform(p.loc_l2diff,
                        params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]);
                // BasicEffect specular (Task 886) -- lit shader only.
                if (p.loc_l0spec >= 0)
                    p.prog.set_uniform(p.loc_l0spec,
                        params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]);
                if (p.loc_l1spec >= 0)
                    p.prog.set_uniform(p.loc_l1spec,
                        params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]);
                if (p.loc_l2spec >= 0)
                    p.prog.set_uniform(p.loc_l2spec,
                        params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]);
                if (p.loc_specularcolor >= 0)
                    p.prog.set_uniform(p.loc_specularcolor,
                        params.specularColor[0], params.specularColor[1], params.specularColor[2]);
                if (p.loc_specularpower >= 0)
                    p.prog.set_uniform(p.loc_specularpower, params.specularPower);
            }
            else
            {
                // No lighting: full ambient = diffuse color, light contribution = 0
                p.prog.set_uniform(p.loc_ambient, 1.0f, 1.0f, 1.0f);
                if (p.loc_l0dir  >= 0) p.prog.set_uniform(p.loc_l0dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l0diff >= 0) p.prog.set_uniform(p.loc_l0diff, 0.0f,  0.0f, 0.0f);
                if (p.loc_l1dir  >= 0) p.prog.set_uniform(p.loc_l1dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l1diff >= 0) p.prog.set_uniform(p.loc_l1diff, 0.0f,  0.0f, 0.0f);
                if (p.loc_l2dir  >= 0) p.prog.set_uniform(p.loc_l2dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l2diff >= 0) p.prog.set_uniform(p.loc_l2diff, 0.0f,  0.0f, 0.0f);
                // No lighting: zero every light's specular color regardless of its own
                // Enabled/SpecularColor forwarding, matching the diffuse-zeroing above.
                if (p.loc_l0spec >= 0) p.prog.set_uniform(p.loc_l0spec, 0.0f, 0.0f, 0.0f);
                if (p.loc_l1spec >= 0) p.prog.set_uniform(p.loc_l1spec, 0.0f, 0.0f, 0.0f);
                if (p.loc_l2spec >= 0) p.prog.set_uniform(p.loc_l2spec, 0.0f, 0.0f, 0.0f);
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
        // Task 890: EnvironmentMapEffect's own DirectionalLight1/2 forwarding (same generic
        // Prog3D fields the lit-textured/BasicEffect path above uses, gated the same way via
        // loc_ambient's absence to identify this is the env-map program).
        if (p.loc_l1dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1dir,
                params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
        if (p.loc_l1diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1diff,
                params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]);
        if (p.loc_l2dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2dir,
                params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
        if (p.loc_l2diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2diff,
                params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]);
        // Task 894: SkinnedEffect's own specular forwarding (same generic Prog3D fields the
        // lit-textured/BasicEffect path above uses; harmless no-op for env-map, which never
        // declares these uniforms so loc_l0spec etc. stay -1 there).
        if (p.loc_l0spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0spec,
                params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]);
        if (p.loc_l1spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1spec,
                params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]);
        if (p.loc_l2spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2spec,
                params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]);
        if (p.loc_specularcolor >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_specularcolor,
                params.specularColor[0], params.specularColor[1], params.specularColor[2]);
        if (p.loc_specularpower >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_specularpower, params.specularPower);

        if (p.loc_eyepos >= 0)
            p.prog.set_uniform(p.loc_eyepos,
                params.eyePositionWorld[0], params.eyePositionWorld[1], params.eyePositionWorld[2]);

        // Bone palette (SkinnedEffect)
        if (p.loc_bones >= 0 && params.boneCount > 0)
            ::metagl::glUniformMatrix4fv(::metagl::UniformLocation{p.loc_bones}, params.boneCount, 0, params.boneTransforms);
        // Task 895: WeightsPerVertex (1, 2, or 4) -- only the first N weight/index pairs contribute.
        if (p.loc_weightsPerVertex >= 0)
            p.prog.set_uniform(p.loc_weightsPerVertex, params.weightsPerVertex);

        if (p.loc_envmap_amount >= 0)
            p.prog.set_uniform(p.loc_envmap_amount, params.envMapAmount);

        if (p.loc_envmap_spec >= 0)
            p.prog.set_uniform(p.loc_envmap_spec,
                params.envMapSpecular[0], params.envMapSpecular[1], params.envMapSpecular[2]);

        if (p.loc_fresnel_enabled >= 0)
            p.prog.set_uniform(p.loc_fresnel_enabled, params.fresnelEnabled ? 1.0f : 0.0f);

        if (p.loc_fresnel_factor >= 0)
            p.prog.set_uniform(p.loc_fresnel_factor, params.fresnelFactor);

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

        // plan_cnj.md CNB-58 (Phase 13A): PbrEffect's 4 additional maps (units 1-4, bound before
        // unit 0 to leave it active last, matching the envMap/texture2 precedent above). Each
        // falls back to a texture whose sampled value is the correct "map absent" constant for
        // its own semantic (see EnsureDefaultFlatNormalTexture()'s own doc comment).
        if (p.loc_pbr_normalmap >= 0)
        {
            EnsureDefaultFlatNormalTexture();
            p.prog.set_uniform(p.loc_pbr_normalmap, 1);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture1);
            if (params.pbrNormalMap)
                params.pbrNormalMap->BindGL();
            else
                default_flat_normal_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_mr >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_mr, 2);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture2);
            if (params.pbrMetallicRoughnessMap)
                params.pbrMetallicRoughnessMap->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_emissivemap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_emissivemap, 3);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture3);
            if (params.pbrEmissiveMap)
                params.pbrEmissiveMap->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_occlusionmap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_occlusionmap, 4);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture4);
            if (params.pbrOcclusionMap)
                params.pbrOcclusionMap->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_metallic >= 0)
            p.prog.set_uniform(p.loc_pbr_metallic, params.pbrMetallicFactor);
        if (p.loc_pbr_roughness >= 0)
            p.prog.set_uniform(p.loc_pbr_roughness, params.pbrRoughnessFactor);

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

        // REMED-GFX-010: upload the FNA view-space fog vector (GpuDrawParams.fogVector). It bakes
        // World*View's third column + fogStart/fogEnd, is zero when fog is disabled, and (0,0,0,1) for
        // the degenerate fogStart==fogEnd case, so no separate start/end/enabled uniforms are needed —
        // the shader computes vFogFactor = 1 - saturate(dot(pos, uFogVector)).
        if (p.loc_fog_vector >= 0)
            p.prog.set_uniform(p.loc_fog_vector,
                params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]);
        if (p.loc_fog_color >= 0)
            p.prog.set_uniform(p.loc_fog_color,
                params.fogColor[0], params.fogColor[1], params.fogColor[2]);
    }

    void EasyGLGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        if (metagl::IsContextLost()) return;
        // Task 880: see Clear()'s identical comment -- glClear() is viewport-independent.
        device.set_clear_color(r, g, b, a);
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth);
    }

    // Task 871: glClear(GL_STENCIL_BUFFER_BIT) is itself masked by the currently-active
    // glStencilMask -- forcing it to all-1s here (mirroring ClearDepth's identical
    // set_depth_mask(true) override) guarantees the requested clear value actually reaches every
    // stencil bit regardless of whatever DepthStencilState::StencilWriteMask a previous draw left
    // active; ApplyDepthStencilState() reissues the real write mask before the next draw anyway.
    void EasyGLGraphicsBackend::ClearStencil(int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_stencil(stencil);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Stencil);
    }

    void EasyGLGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_clear_stencil(stencil);
        device.set_depth_mask(true);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Depth | ::easygl::ClearFlags::Stencil);
    }

    void EasyGLGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_color(r, g, b, a);
        device.set_clear_stencil(stencil);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Stencil);
    }

    void EasyGLGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_color(r, g, b, a);
        device.set_clear_depth(depth);
        device.set_clear_stencil(stencil);
        device.set_depth_mask(true);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth | ::easygl::ClearFlags::Stencil);
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

    namespace
    {
        // Task 1079: binds a ShaderEffect's own compiled program (bypassing the built-in
        // stride-dispatched shaders) and its World/View/Projection uniforms, matching the exact
        // uniform names every original XNA sample's own .fx source already declares.
        void BindCustomEffectMatrices(IEffectBackend& backend,
                                      const Matrix& world, const Matrix& view, const Matrix& projection)
        {
            backend.Bind();
            float worldCM[16], viewCM[16], projCM[16];
            world.ToColumnMajor(worldCM);
            view.ToColumnMajor(viewCM);
            projection.ToColumnMajor(projCM);
            backend.SetUniformMat4("World", worldCM);
            backend.SetUniformMat4("View", viewCM);
            backend.SetUniformMat4("Projection", projCM);
        }
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

        if (params.customEffectBackend)
        {
            BindCustomEffectMatrices(*params.customEffectBackend, world, view, projection);
            const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
            vb.vao.bind();
            device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
            vb.vao.unbind();
            return;
        }

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

        if (params.customEffectBackend)
        {
            BindCustomEffectMatrices(*params.customEffectBackend, world, view, projection);
            const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
            vb.vao.bind();
            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
            const auto idxTypeCustom = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                                        : ::easygl::DataType::UnsignedShort;
            const int indexSizeCustom = ib.thirtyTwoBit ? 4 : 2;
            const void* indexOffsetCustom = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSizeCustom));
            if (params.baseVertex == 0) {
                device.draw_elements(ToEasyGl(primitive), index_count, idxTypeCustom, indexOffsetCustom);
            } else {
                ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxTypeCustom,
                                                   indexOffsetCustom, params.baseVertex);
            }
            vb.vao.unbind();
            return;
        }

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

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                             : ::easygl::DataType::UnsignedShort;

        if (params.customEffectBackend)
        {
            // Task 1082: hardware instancing with a custom ShaderEffect. The per-vertex mesh
            // buffer's own attributes are already bound (via ApplyLayout, at SetData time) into
            // vb's own VAO; bind the *second*, per-instance buffer's own attributes into that
            // same VAO here, continuing at locations right after the mesh buffer's own, each
            // with a divisor of 1 (advance once per instance -- InstancedModel.fx's own
            // `instanceTransform : BLENDWEIGHT` case, XNA's most common instancing pattern; a
            // non-1 `VertexBufferBinding.InstanceFrequency` is a documented, deliberate scope
            // reduction, not threaded through -- see this task's own plan_graphics.md write-up).
            BindCustomEffectMatrices(*params.customEffectBackend, world, view, projection);

            // vb_in/vb are const (matching the interface's own signature), but attribute-config
            // calls below mutate GPU-side VAO state only, not C++ object state -- the same
            // category easy-gl's own bind()/unbind() are already marked const for; this const_cast
            // is a local workaround for that inconsistency, not a real constness violation.
            auto& vao = const_cast<::easygl::VertexArray&>(vb.vao);

            vao.bind();

            if (params.instanceVb)
            {
                const auto& instVb = static_cast<const EasyGLVertexBufferBackend&>(*params.instanceVb);
                const auto& meshDecl = vb.GetDeclarationElements();
                const auto& instDecl = instVb.GetDeclarationElements();
                const auto baseLocation = static_cast<unsigned int>(meshDecl.size());
                const int instStride = static_cast<int>(instVb.GetStride());

                instVb.vbo.bind(::easygl::BufferTarget::Array);
                for (std::size_t i = 0; i < instDecl.size(); ++i)
                {
                    const VertexElement& element = instDecl[i];
                    const VertexAttribFormat desc =
                        DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                    const auto location = baseLocation + static_cast<unsigned int>(i);
                    const void* offset = reinterpret_cast<void*>(
                        static_cast<std::uintptr_t>(element.getOffsetProperty()));
                    vao.enable_attribute(location);
                    if (desc.isInteger)
                        vao.set_attribute_i_pointer(location, desc.componentCount, desc.type,
                                                    instStride, offset);
                    else
                        vao.set_attribute_pointer(location, desc.componentCount, desc.type,
                                                  desc.normalized, instStride, offset);
                    vao.set_attribute_divisor(location, 1);
                }
            }

            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
            device.draw_elements_instanced(ToEasyGl(primitive), index_count, idxType,
                                           nullptr, instanceCount);

            if (params.instanceVb)
            {
                const auto& instVb = static_cast<const EasyGLVertexBufferBackend&>(*params.instanceVb);
                const auto& meshDecl = vb.GetDeclarationElements();
                const auto& instDecl = instVb.GetDeclarationElements();
                const auto baseLocation = static_cast<unsigned int>(meshDecl.size());
                for (std::size_t i = 0; i < instDecl.size(); ++i)
                    vao.disable_attribute(baseLocation + static_cast<unsigned int>(i));
            }

            vao.unbind();
            return;
        }

        Prog3D& p = SelectProgram(vb.GetStride(), params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

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
