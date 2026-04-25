#include "CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <vector>
#include <cmath>
#include <SDL3/SDL.h>
#include "Microsoft/Xna/Framework/Color.hpp"

namespace CNA::Internal::Backends::EasyGL {

    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Backends;

    // --- EasyGLTextureBackend ---
    
    EasyGLTextureBackend::EasyGLTextureBackend(const ImageData& data) {
        width = data.width;
        height = data.height;

        texture.create();
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, data.pixels.data());
    }

    // --- EasyGLSpriteBatchBackend ---

    EasyGLSpriteBatchBackend::EasyGLSpriteBatchBackend(::easygl::Device& device)
        : device_(device)
    {
        InitializeResources();
    }

    void EasyGLSpriteBatchBackend::InitializeResources() {
        const char* vertexShaderSource = R"(
            #version 330 core
            layout (location = 0) in vec2 aPos;
            layout (location = 1) in vec2 aTexCoord;
            layout (location = 2) in vec4 aColor;
            
            out vec2 TexCoord;
            out vec4 Color;
            
            uniform mat4 projection;
            
            void main() {
                gl_Position = projection * vec4(aPos, 0.0, 1.0);
                TexCoord = aTexCoord;
                Color = aColor;
            }
        )";

        const char* fragmentShaderSource = R"(
            #version 330 core
            in vec2 TexCoord;
            in vec4 Color;
            out vec4 FragColor;
            
            uniform sampler2D texture1;
            
            void main() {
                FragColor = texture(texture1, TexCoord) * Color;
            }
        )";

        ::easygl::Shader vertexShader(::easygl::ShaderStage::Vertex);
        vertexShader.create();
        vertexShader.compile_from_source(vertexShaderSource);

        if (!vertexShader.is_compiled()) {
            std::cerr << "Vertex shader compilation failed:\n" << vertexShader.info_log() << std::endl;
        }

        ::easygl::Shader fragmentShader(::easygl::ShaderStage::Fragment);
        fragmentShader.create();
        fragmentShader.compile_from_source(fragmentShaderSource);

        if (!fragmentShader.is_compiled()) {
            std::cerr << "Fragment shader compilation failed:\n" << fragmentShader.info_log() << std::endl;
        }

        program_.create();
        program_.attach(vertexShader);
        program_.attach(fragmentShader);
        program_.link();

        if (!program_.is_linked()) {
            std::cerr << "Shader program linking failed:\n" << program_.info_log() << std::endl;
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
        vao_.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false, 8 * sizeof(float), (void*)(2 * sizeof(float)));
        
        vao_.enable_attribute(2);
        vao_.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, 8 * sizeof(float), (void*)(4 * sizeof(float)));

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        vao_.unbind();
    }

    void EasyGLSpriteBatchBackend::Begin() {
        begun = true;
        device_.set_blend_enabled(true);
        device_.set_blend_func(::easygl::BlendFactor::SrcAlpha, ::easygl::BlendFactor::OneMinusSrcAlpha);
    }

    void EasyGLSpriteBatchBackend::End() {
        begun = false;
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y) {
        auto& glTex = static_cast<const EasyGLTextureBackend&>(texture);
        Draw(texture, Rectangle((int)x, (int)y, glTex.width, glTex.height), Rectangle(0, 0, glTex.width, glTex.height), Microsoft::Xna::Framework::White);
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color) {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    struct Vertex {
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
                                       float layerDepth) {
        if (!begun) throw std::runtime_error("Draw called before Begin()");

        auto& glTex = static_cast<const EasyGLTextureBackend&>(texture);
        
        float u1 = (float)sourceRectangle.X / (float)glTex.width;
        float v1 = (float)sourceRectangle.Y / (float)glTex.height;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width) / (float)glTex.width;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / (float)glTex.height;

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

        auto rotateAndTranslate = [&](float x, float y, float& rx, float& ry) {
            rx = dx + x * cosR - y * sinR;
            ry = dy + x * sinR + y * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        Vertex vertices[4] = {
            { v0x, v0y, u1, v1, r, g, b, a },
            { v1x, v1y, u2, v1, r, g, b, a },
            { v2x, v2y, u2, v2, r, g, b, a },
            { v3x, v3y, u1, v2, r, g, b, a }
        };

        uint16_t indices[6] = { 0, 1, 2, 2, 3, 0 };

        program_.use();
        
        int vx, vy, vw, vh;
        device_.get_viewport(vx, vy, vw, vh);
        
        float ortho[16] = {
            2.0f/vw, 0, 0, 0,
            0, -2.0f/vh, 0, 0,
            0, 0, -1, 0,
            -1, 1, 0, 1
        };

        int projLoc = program_.uniform_location("projection");
        program_.set_uniform_matrix4(projLoc, ortho);

        glTex.texture.bind(::easygl::TextureTarget::Texture2D);
        
        vbo_.set_data(vertices, sizeof(vertices));
        ibo_.set_data(indices, sizeof(indices));

        vao_.bind();
        device_.draw_elements(::easygl::PrimitiveType::Triangles, 6, ::easygl::DataType::UnsignedShort, nullptr);
        vao_.unbind();
    }

    // --- EasyGLGraphicsBackend ---

    EasyGLGraphicsBackend::EasyGLGraphicsBackend(SDL_Window* window) : window(window) {
        if (!window) throw std::runtime_error("EasyGLGraphicsBackend initialized with null window.");

        // NOTE: SDL_Window is NOT owned by EasyGL backend.
        // It is owned by GraphicsDevice or higher level platform layer.

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        // NOTE: GL context IS owned by EasyGL backend.
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context) {
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }

        device.initialize(reinterpret_cast<::easygl::GLGetProcAddressFn>(SDL_GL_GetProcAddress));
        std::cout << "EasyGLGraphicsBackend initialized with OpenGL " 
                  << device.capabilities().context_info().version_string << std::endl;
    }

    EasyGLGraphicsBackend::~EasyGLGraphicsBackend() {
        if (gl_context) SDL_GL_DestroyContext(gl_context);
        // window is NOT owned by the backend.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    void EasyGLGraphicsBackend::Clear(float r, float g, float b, float a) {
        device.set_clear_color(r, g, b, a);
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth);
    }

    void EasyGLGraphicsBackend::Present() {
        SDL_GL_SwapWindow(window);
    }

    void EasyGLGraphicsBackend::GetViewportSize(int& width, int& height) {
        SDL_GetWindowSize(window, &width, &height);
    }

    std::unique_ptr<ITextureBackend> EasyGLGraphicsBackend::CreateTexture(const ImageData& data) {
        return std::make_unique<EasyGLTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> EasyGLGraphicsBackend::CreateSpriteBatch() {
        return std::make_unique<EasyGLSpriteBatchBackend>(device);
    }

}

namespace CNA::Internal::Backends {
#ifdef CNA_BACKEND_EASYGL
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args) {
        return std::make_unique<EasyGL::EasyGLGraphicsBackend>(args.window);
    }
#endif
}
