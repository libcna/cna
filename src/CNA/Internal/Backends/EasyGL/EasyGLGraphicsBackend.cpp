#include "CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp"
#include <iostream>

#include "CNA/Platform.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <metagl/Emscripten.hpp>
#endif
#include <metagl/Context.hpp>
#include <metagl/ContextEvents.hpp>

// Verbose 3D rendering trace. Define `CNA_DEBUG_RENDERING` (e.g. via
// -DCNA_DEBUG_RENDERING) to enable. By default these logs are silent so the
// 3D pipeline does not spam the console every frame.
#if defined(CNA_DEBUG_RENDERING)
#define CNA_RENDER_LOG(msg) do { std::cerr << "[CNA EasyGL 3D] " << msg << std::endl; } while (0)
#else
#define CNA_RENDER_LOG(msg) do { } while (0)
#endif
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <vector>
#include <cmath>
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
        : image_data_(data)
        , registry_(registry)
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
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                             image_data_.width, image_data_.height,
                             image_data_.pixels.data());
    }

    void EasyGLTextureBackend::BindGL() const
    {
        texture.bind(::easygl::TextureTarget::Texture2D);
    }

    // --- EasyGLRenderTargetBackend ---

    EasyGLRenderTargetBackend::EasyGLRenderTargetBackend(int w, int h, bool hasDepth,
                                                          ::easygl::ResourceRegistry* registry)
        : width_(w), height_(h), hasDepth_(hasDepth), registry_(registry)
    {
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
        colorTex_.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                               ::metagl::InternalFormat::Rgba8,
                               width_, height_,
                               ::metagl::PixelFormat::Rgba,
                               ::metagl::PixelType::UnsignedByte,
                               nullptr);

        fbo_.create();
        fbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                               ::metagl::FramebufferAttachment::Color0,
                               ::easygl::TextureTarget::Texture2D,
                               colorTex_.native_handle(), 0);

        if (hasDepth_)
        {
            depthRbo_.create();
            depthRbo_.bind();
            depthRbo_.set_storage(::metagl::InternalFormat::DepthComponent24, width_, height_);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::FramebufferAttachment::Depth,
                                     depthRbo_.native_handle());
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::BindAsRenderTarget()
    {
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::UnbindAsRenderTarget()
    {
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetBackend::BindGL() const
    {
        colorTex_.bind(::easygl::TextureTarget::Texture2D);
    }

    void EasyGLRenderTargetBackend::release_gl_handle_only()
    {
        fbo_.reset_handle_no_gl();
        colorTex_.reset_handle_no_gl();
        depthRbo_.reset_handle_no_gl();
    }

    void EasyGLRenderTargetBackend::recreate_gl_resource()
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

    void EasyGLSpriteBatchBackend::End()
    {
        FlushBatch();
        begun = false;
    }

    void EasyGLSpriteBatchBackend::FlushBatch()
    {
        if (pending_vertices_.empty()) return;

        program_.use();

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
        float ortho[16] = {
            2.0f / logW, 0, 0, 0,
            0, -2.0f / logH, 0, 0,
            0, 0, -1, 0,
            -1, 1, 0, 1
        };
        program_.set_uniform_matrix4(program_.uniform_location("projection"), ortho);

        current_texture_->BindGL();

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

        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        u1 = std::clamp(u1, 0.0f, 1.0f);
        v1 = std::clamp(v1, 0.0f, 1.0f);
        u2 = std::clamp(u2, 0.0f, 1.0f);
        v2 = std::clamp(v2, 0.0f, 1.0f);

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
                                                  CnaPresentationMode mode)
        : window(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
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

        // NOTE: GL context IS owned by EasyGL backend.
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
        {
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }

        device.initialize(reinterpret_cast<::easygl::GLGetProcAddressFn>(SDL_GL_GetProcAddress));
        std::cout << "EasyGLGraphicsBackend initialized with OpenGL "
            << device.capabilities().context_info().version_string << std::endl;

        registry_.register_with_meta_gl();

#if defined(__EMSCRIPTEN__)
        metagl::InstallEmscriptenContextLossCallbacks();
#endif
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
        SDL_GL_SwapWindow(window);
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

    void EasyGLGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    std::unique_ptr<ITextureBackend> EasyGLGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<EasyGLTextureBackend>(data, &registry_);
    }

    std::unique_ptr<ISpriteBatchBackend> EasyGLGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<EasyGLSpriteBatchBackend>(device, &registry_, this);
    }

    std::unique_ptr<IOcclusionQueryBackend> EasyGLGraphicsBackend::CreateOcclusionQuery()
    {
        return std::make_unique<EasyGLOcclusionQueryBackend>(&registry_);
    }

    std::unique_ptr<IRenderTargetBackend> EasyGLGraphicsBackend::CreateRenderTarget2D(int w, int h, bool hasDepth)
    {
        return std::make_unique<EasyGLRenderTargetBackend>(w, h, hasDepth, &registry_);
    }

    void EasyGLGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt)
            rt->BindAsRenderTarget();
        else
            ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
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

        ::easygl::PrimitiveType ToEasyGl(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList: return ::easygl::PrimitiveType::Triangles;
            case PrimitiveType::TriangleStrip: return ::easygl::PrimitiveType::TriangleStrip;
            case PrimitiveType::LineList: return ::easygl::PrimitiveType::Lines;
            case PrimitiveType::LineStrip: return ::easygl::PrimitiveType::LineStrip;
            }
            return ::easygl::PrimitiveType::Triangles;
        }

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList: return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList: return primitiveCount * 2;
            case PrimitiveType::LineStrip: return primitiveCount + 1;
            }
            return 0;
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
                                                        int depthFunc)
    {
        if (metagl::IsContextLost()) return;
        device.set_depth_test_enabled(depthEnable);
        device.set_depth_mask(depthWriteEnable);
        if (depthEnable)
            device.set_depth_func(ToEasyGLCompareFunc(depthFunc));
    }

    void EasyGLGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable)
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
        // FillMode::WireFrame not supported in OpenGL ES — silently ignored
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

    void EasyGLVertexBufferBackend::SetData(const void* data, int count, std::size_t stride_in_bytes)
    {
        vertex_count = count;
        stride_in_bytes_ = stride_in_bytes;
        const std::size_t byte_count = static_cast<std::size_t>(count) * stride_in_bytes;
        const auto* bytes = static_cast<const uint8_t*>(data);
        cpu_data_.assign(bytes, bytes + byte_count);
        vbo.bind(::easygl::BufferTarget::Array);
        vbo.set_data(::easygl::BufferTarget::Array, data, byte_count);
        ApplyLayout(stride_in_bytes_);
        CNA_RENDER_LOG("VertexBuffer SetData: count=" << count << " stride=" << stride_in_bytes
            << " bytes=" << byte_count);
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
        const auto* bytes = static_cast<const uint8_t*>(data);
        cpu_data_.assign(bytes, bytes + byte_count);
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData16: count=" << count);
    }

    void EasyGLIndexBufferBackend::SetData32(const void* data, int count)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint32_t);
        const auto* bytes = static_cast<const uint8_t*>(data);
        cpu_data_.assign(bytes, bytes + byte_count);
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData32: count=" << count);
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
"out vec4 vColor;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"out vec4 FragColor;\n"
"void main(){ FragColor=vColor; }\n";

        CompileAndLink(prog_colored_.prog, vsrc, fsrc, "colored");
        prog_colored_.loc_wvp = prog_colored_.prog.uniform_location("uWVP");
        prog_colored_.ready   = true;
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
"uniform vec4 uDiffuseColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=texture(uTexture,vUV)*uDiffuseColor;\n"
"}\n";

        CompileAndLink(prog_textured_.prog, vsrc, fsrc, "textured");
        prog_textured_.loc_wvp     = prog_textured_.prog.uniform_location("uWVP");
        prog_textured_.loc_diffuse = prog_textured_.prog.uniform_location("uDiffuseColor");
        prog_textured_.loc_texture = prog_textured_.prog.uniform_location("uTexture");
        prog_textured_.ready       = true;
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
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTexture;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=texture(uTexture,vUV)*vColor;\n"
"}\n";

        CompileAndLink(prog_col_textured_.prog, vsrc, fsrc, "col+textured");
        prog_col_textured_.loc_wvp     = prog_col_textured_.prog.uniform_location("uWVP");
        prog_col_textured_.loc_texture = prog_col_textured_.prog.uniform_location("uTexture");
        prog_col_textured_.ready       = true;
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
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"void main(){\n"
"    gl_Position=uWVP*vec4(aPos,1.0);\n"
"    vNormal=uNormalMatrix*aNormal;\n"
"    vUV=aUV;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    float NdotL=max(dot(N,-uLight0Dir),0.0);\n"
"    vec3 litRGB=(uAmbientColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb;\n"
"    FragColor=texture(uTexture,vUV)*vec4(litRGB,uDiffuseColor.a);\n"
"}\n";

        CompileAndLink(prog_lit_textured_.prog, vsrc, fsrc, "lit+textured");
        prog_lit_textured_.loc_wvp       = prog_lit_textured_.prog.uniform_location("uWVP");
        prog_lit_textured_.loc_normalmat = prog_lit_textured_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_.loc_diffuse   = prog_lit_textured_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_.loc_ambient   = prog_lit_textured_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_.loc_l0dir     = prog_lit_textured_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_.loc_l0diff    = prog_lit_textured_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_.loc_texture   = prog_lit_textured_.prog.uniform_location("uTexture");
        prog_lit_textured_.ready         = true;
        CNA_RENDER_LOG("lit+textured3D ready loc_wvp=" << prog_lit_textured_.loc_wvp);
    }

    void EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()
    {
        if (default_white_texture_ready_) return;
        static const uint8_t white[4] = {255, 255, 255, 255};
        default_white_texture_.create();
        default_white_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, white);
        default_white_texture_ready_ = true;
    }

    EasyGLGraphicsBackend::Prog3D& EasyGLGraphicsBackend::SelectProgram(std::size_t stride)
    {
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

        // Diffuse color
        if (p.loc_diffuse >= 0)
            p.prog.set_uniform(p.loc_diffuse,
                params.diffuseColor[0], params.diffuseColor[1],
                params.diffuseColor[2], params.diffuseColor[3]);

        // Ambient + light0 (lit shader only)
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

        // Texture
        if (p.loc_texture >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_texture, 0);
            if (params.texture0)
                params.texture0->BindGL();
            else
                default_white_texture_.bind(::easygl::TextureTarget::Texture2D);
        }
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
        return std::make_unique<EasyGLVertexBufferBackend>(vertex_capacity, &registry_);
    }

    std::unique_ptr<IIndexBufferBackend> EasyGLGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferBackend>(index_capacity, false, &registry_);
    }

    std::unique_ptr<IIndexBufferBackend> EasyGLGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferBackend>(index_capacity, true, &registry_);
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

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " verts=" << vertex_count);

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

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " indices=" << index_count);

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
        Prog3D& p = SelectProgram(vb.GetStride());
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawPrimitivesEx: stride=" << vb.GetStride()
            << " prim=" << static_cast<int>(primitive) << " verts=" << vertex_count);

        vb.vao.bind();
        device.draw_arrays(ToEasyGl(primitive), 0, vertex_count);
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
        Prog3D& p = SelectProgram(vb.GetStride());
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedPrimitivesEx: stride=" << vb.GetStride()
            << " prim=" << static_cast<int>(primitive) << " indices=" << index_count);

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType2 = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                               : ::easygl::DataType::UnsignedShort;
        device.draw_elements(ToEasyGl(primitive), index_count, idxType2, nullptr);
        vb.vao.unbind();
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_EASYGL
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<EasyGL::EasyGLGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
#endif
}
