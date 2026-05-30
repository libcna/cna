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

    // --- EasyGLSpriteBatchBackend ---

    EasyGLSpriteBatchBackend::EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry)
        : device_(device)
        , registry_(registry)
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
        begun = false;
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        auto& glTex = static_cast<const EasyGLTextureBackend&>(texture);
        Draw(texture, Rectangle((int)x, (int)y, glTex.width, glTex.height), Rectangle(0, 0, glTex.width, glTex.height),
             Microsoft::Xna::Framework::Color::White);
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    struct Vertex
    {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

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

        auto& glTex = static_cast<const EasyGLTextureBackend&>(texture);

        float u1 = (float)sourceRectangle.X / (float)glTex.width;
        float v1 = (float)sourceRectangle.Y / (float)glTex.height;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width) / (float)glTex.width;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / (float)glTex.height;

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

        // Scaling factor relative to source rectangle
        float scaleX = dw / sw;
        float scaleY = dh / sh;

        // Local coordinates after scaling, before rotation and translation
        float p0x = (0.0f - ox) * scaleX;
        float p0y = (0.0f - oy) * scaleY;
        float p1x = (sw - ox) * scaleX;
        float p1y = (0.0f - oy) * scaleY;
        float p2x = (sw - ox) * scaleX;
        float p2y = (sh - oy) * scaleY;
        float p3x = (0.0f - ox) * scaleX;
        float p3y = (sh - oy) * scaleY;

        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float x, float y, float& rx, float& ry)
        {
            rx = dx + x * cosR - y * sinR;
            ry = dy + x * sinR + y * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        Vertex vertices[4] = {
            {v0x, v0y, u1, v1, r, g, b, a},
            {v1x, v1y, u2, v1, r, g, b, a},
            {v2x, v2y, u2, v2, r, g, b, a},
            {v3x, v3y, u1, v2, r, g, b, a}
        };

        uint16_t indices[6] = {0, 1, 2, 2, 3, 0};

        program_.use();

        int vx, vy, vw, vh;
        device_.get_viewport(vx, vy, vw, vh);

        float ortho[16] = {
            2.0f / vw, 0, 0, 0,
            0, -2.0f / vh, 0, 0,
            0, 0, -1, 0,
            -1, 1, 0, 1
        };

        int projLoc = program_.uniform_location("projection");
        program_.set_uniform_matrix4(projLoc, ortho);

        glTex.texture.bind(::easygl::TextureTarget::Texture2D);

        vbo_.bind(::easygl::BufferTarget::Array);
        vbo_.set_data(
            ::easygl::BufferTarget::Array,
            vertices,
            sizeof(vertices)
        );

        vao_.bind();

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        ibo_.set_data(
            ::easygl::BufferTarget::ElementArray,
            indices,
            sizeof(indices)
        );

        device_.draw_elements(
            ::easygl::PrimitiveType::Triangles,
            6,
            ::easygl::DataType::UnsignedShort,
            nullptr
        );

        vao_.unbind();
    }

    // --- EasyGLGraphicsBackend ---

    EasyGLGraphicsBackend::EasyGLGraphicsBackend(SDL_Window* window) : window(window)
    {
        if (!window) throw std::runtime_error("EasyGLGraphicsBackend initialized with null window.");

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

        // program3d_ is not tracked by ResourceRegistry (it is recreated lazily
        // by EnsureColored3DProgram). Reset its handle now so that create() in
        // EnsureColored3DProgram() allocates a fresh program instead of reusing
        // the stale handle from the destroyed context.
        program3d_.reset_handle_no_gl();
        program3d_ready_ = false;

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

    void EasyGLGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SDL_GetWindowSize(window, &width, &height);
    }

    std::unique_ptr<ITextureBackend> EasyGLGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<EasyGLTextureBackend>(data, &registry_);
    }

    std::unique_ptr<ISpriteBatchBackend> EasyGLGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<EasyGLSpriteBatchBackend>(device, &registry_);
    }

    // -------------------------------------------------------------------------
    // 3D pipeline
    // -------------------------------------------------------------------------

    void EasyGLVertexBufferBackend::InitializeLayout()
    {
        vbo.create();
        vao.create();
        // Layout for VertexPositionColor: vec3 position (offset 0) + 4xUByte color (offset 12).
        // Total stride is 16 bytes (sizeof(VertexPositionColor)).
        vao.bind();
        vbo.bind(::easygl::BufferTarget::Array);
        vao.enable_attribute(0);
        vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, 16, (void*)0);
        vao.enable_attribute(1);
        vao.set_attribute_pointer(1, 4, ::easygl::DataType::UnsignedByte, true, 16, (void*)12);
        vao.unbind();
    }

    EasyGLVertexBufferBackend::EasyGLVertexBufferBackend(int vertex_capacity, ::easygl::ResourceRegistry* registry)
        : capacity(vertex_capacity)
        , registry_(registry)
    {
        InitializeLayout();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("VertexBuffer created: capacity=" << capacity << " stride=16");
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
    }

    void EasyGLVertexBufferBackend::SetData(const void* data, int count, std::size_t stride_in_bytes)
    {
        vertex_count = count;
        vbo.bind(::easygl::BufferTarget::Array);
        vbo.set_data(::easygl::BufferTarget::Array, data, static_cast<std::size_t>(count) * stride_in_bytes);
        CNA_RENDER_LOG("VertexBuffer SetData: count=" << count << " stride=" << stride_in_bytes
            << " bytes=" << (static_cast<std::size_t>(count) * stride_in_bytes));
    }

    EasyGLIndexBufferBackend::EasyGLIndexBufferBackend(int index_capacity, ::easygl::ResourceRegistry* registry)
        : capacity(index_capacity)
        , registry_(registry)
    {
        ibo.create();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("IndexBuffer created: capacity=" << capacity);
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
    }

    void EasyGLIndexBufferBackend::SetData16(const void* data, int count)
    {
        index_count = count;
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data,
                     static_cast<std::size_t>(count) * sizeof(std::uint16_t));
        CNA_RENDER_LOG("IndexBuffer SetData16: count=" << count);
    }

    void EasyGLGraphicsBackend::EnsureColored3DProgram()
    {
        if (program3d_ready_) return;

        const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec4 aColor;\n"
"uniform mat4 uWorldViewProjection;\n"
"out vec4 vColor;\n"
"void main() {\n"
"    gl_Position = uWorldViewProjection * vec4(aPos, 1.0);\n"
"    vColor = aColor;\n"
"}\n";
        const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"out vec4 FragColor;\n"
"void main() {\n"
"    FragColor = vColor;\n"
"}\n";

        ::easygl::Shader vs(::easygl::ShaderType::Vertex);
        vs.create();
        vs.compile_from_source(vsrc);
        if (!vs.is_compiled())
        {
            std::cerr << "[CNA EasyGL 3D] vertex shader compile failed:\n" << vs.info_log() << std::endl;
        }
        ::easygl::Shader fs(::easygl::ShaderType::Fragment);
        fs.create();
        fs.compile_from_source(fsrc);
        if (!fs.is_compiled())
        {
            std::cerr << "[CNA EasyGL 3D] fragment shader compile failed:\n" << fs.info_log() << std::endl;
        }

        program3d_.create();
        program3d_.attach(vs);
        program3d_.attach(fs);
        program3d_.link();
        if (!program3d_.is_linked())
        {
            std::cerr << "[CNA EasyGL 3D] program link failed:\n" << program3d_.info_log() << std::endl;
        }

        loc_world_view_projection_ = program3d_.uniform_location("uWorldViewProjection");
        program3d_ready_ = true;
        CNA_RENDER_LOG("3D program ready: linked=" << program3d_.is_linked()
            << " loc(uWorldViewProjection)=" << loc_world_view_projection_);
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
        return std::make_unique<EasyGLIndexBufferBackend>(index_capacity, &registry_);
    }

    namespace
    {
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

    void EasyGLGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                      const Matrix& world,
                                                      const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive,
                                                      int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const EasyGLVertexBufferBackend&>(vb_in);

        // XNA convention is row-vector: v_row * (W * V * P). Combined with the
        // transposing pack in `Matrix::ToColumnMajor`, this produces the
        // correct column-major matrix expected by the GLSL shader.
        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        program3d_.use();
        if (loc_world_view_projection_ >= 0)
        {
            program3d_.set_uniform_matrix4(loc_world_view_projection_, wvp_col);
        }

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawColoredPrimitives: primitive=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " vertices=" << vertex_count);

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

        // XNA convention is row-vector: v_row * (W * V * P).
        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        program3d_.use();
        if (loc_world_view_projection_ >= 0)
        {
            program3d_.set_uniform_matrix4(loc_world_view_projection_, wvp_col);
        }

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedColoredPrimitives: primitive=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " indices=" << index_count);

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        device.draw_elements(
            ToEasyGl(primitive),
            index_count,
            ::easygl::DataType::UnsignedShort,
            nullptr
        );
        vb.vao.unbind();
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_EASYGL
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<EasyGL::EasyGLGraphicsBackend>(args.window);
    }
#endif
}
