#include "CNA/Internal/Backends/OpenGL2/OpenGL2GraphicsBackend.hpp"

#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Backends::OpenGL2
{
    namespace
    {
        GLuint CompileShader(GLenum type, const char* src)
        {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);

            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (!compiled)
            {
                GLint logLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
                std::string log(std::max(1, logLength), '\0');
                glGetShaderInfoLog(shader, logLength, nullptr, log.data());
                glDeleteShader(shader);
                throw std::runtime_error("OPENGL2 shader compile failed: " + log);
            }
            return shader;
        }

        GLuint LinkProgram(const char* vertexSrc, const char* fragmentSrc)
        {
            GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
            GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);
            GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glBindAttribLocation(program, 0, "aPosition");
            glBindAttribLocation(program, 1, "aColor");
            glBindAttribLocation(program, 2, "aTexCoord");
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint linked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked)
            {
                GLint logLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
                std::string log(std::max(1, logLength), '\0');
                glGetProgramInfoLog(program, logLength, nullptr, log.data());
                glDeleteProgram(program);
                throw std::runtime_error("OPENGL2 program link failed: " + log);
            }
            return program;
        }

        GLenum ToGLPrimitiveMode(PrimitiveType type)
        {
            switch (type)
            {
                case PrimitiveType::TriangleList:  return GL_TRIANGLES;
                case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
                case PrimitiveType::LineList:      return GL_LINES;
                case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
                case PrimitiveType::PointListEXT:  return GL_POINTS;
            }
            throw std::runtime_error("OPENGL2: unrecognized primitive type");
        }

        int VertexCountForPrimitives(PrimitiveType type, int primitiveCount)
        {
            switch (type)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
            }
            throw std::runtime_error("OPENGL2: unrecognized primitive type");
        }

        // XNA Blend enum ordinals: One=0, Zero=1, SourceColor=2, InverseSourceColor=3,
        // SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        // DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10, InverseBlendFactor=11,
        // SourceAlphaSaturation=12. Mirrors EasyGLGraphicsBackend's ToEasyGLBlendFactor mapping.
        GLenum ToGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
                case 1:  return GL_ZERO;
                case 2:  return GL_SRC_COLOR;
                case 3:  return GL_ONE_MINUS_SRC_COLOR;
                case 4:  return GL_SRC_ALPHA;
                case 5:  return GL_ONE_MINUS_SRC_ALPHA;
                case 6:  return GL_DST_COLOR;
                case 7:  return GL_ONE_MINUS_DST_COLOR;
                case 8:  return GL_DST_ALPHA;
                case 9:  return GL_ONE_MINUS_DST_ALPHA;
                case 10: return GL_CONSTANT_COLOR;
                case 11: return GL_ONE_MINUS_CONSTANT_COLOR;
                case 12: return GL_SRC_ALPHA_SATURATE;
                default: return GL_ONE; // Blend::One = 0
            }
        }

        // XNA BlendFunction enum ordinals: Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4.
        GLenum ToGLBlendEquation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
                case 1:  return GL_FUNC_SUBTRACT;
                case 2:  return GL_FUNC_REVERSE_SUBTRACT;
                case 3:  return GL_MAX;
                case 4:  return GL_MIN;
                default: return GL_FUNC_ADD; // BlendFunction::Add = 0
            }
        }

        // XNA CompareFunction enum ordinals: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
        // GreaterEqual=5, Greater=6, NotEqual=7. Mirrors EasyGLGraphicsBackend's
        // ToEasyGLCompareFunc mapping.
        GLenum ToGLCompareFunc(int xnaCompare)
        {
            switch (xnaCompare)
            {
                case 1: return GL_NEVER;
                case 2: return GL_LESS;
                case 3: return GL_LEQUAL;
                case 4: return GL_EQUAL;
                case 5: return GL_GEQUAL;
                case 6: return GL_GREATER;
                case 7: return GL_NOTEQUAL;
                default: return GL_ALWAYS; // CompareFunction::Always = 0
            }
        }

        // XNA StencilOperation enum ordinals: Keep=0, Zero=1, Replace=2, Increment=3,
        // Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7. Increment/Decrement
        // wrap on overflow (GL_*_WRAP); IncrementSaturation/DecrementSaturation clamp instead
        // (plain GL_INCR/GL_DECR) -- mirrors EasyGLGraphicsBackend's ToEasyGLStencilOp mapping.
        GLenum ToGLStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
                case 1: return GL_ZERO;
                case 2: return GL_REPLACE;
                case 3: return GL_INCR_WRAP;
                case 4: return GL_DECR_WRAP;
                case 5: return GL_INCR;
                case 6: return GL_DECR;
                case 7: return GL_INVERT;
                default: return GL_KEEP; // StencilOperation::Keep = 0
            }
        }

        // XNA TextureFilter enum ordinals: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        // PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        // MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8. This backend has no mipmaps
        // (single-level textures only) and no separate min/mag GL sampler control worth adding
        // for that reason -- mirrors SdlGraphicsBackend::SetSamplerFilter's own reasoning: since
        // SpriteBatch draws are near-universally magnification-dominant, the MAGNIFICATION
        // ("Mag"/first-listed) component is what visibly matters, so it alone selects GL_LINEAR
        // vs GL_NEAREST (applied to both TEXTURE_MIN_FILTER and TEXTURE_MAG_FILTER, since without
        // mipmaps there is no separate minification LOD behavior to preserve).
        GLint ToGLFilter(int xnaFilter)
        {
            switch (xnaFilter)
            {
                case 0: case 2: case 3: case 7: case 8: return GL_LINEAR;
                default: return GL_NEAREST; // Point=1, PointMipLinear=4, MinLinearMagPointMipLinear=5,
                                            // MinLinearMagPointMipPoint=6
            }
        }

        // XNA TextureAddressMode enum ordinals: Wrap=0, Clamp=1, Mirror=2.
        GLint ToGLWrapMode(int xnaAddressMode)
        {
            switch (xnaAddressMode)
            {
                case 1: return GL_CLAMP_TO_EDGE;
                case 2: return GL_MIRRORED_REPEAT;
                default: return GL_REPEAT; // TextureAddressMode::Wrap = 0
            }
        }

        void MultiplyRowMajor(const Matrix& a, const Matrix& b, float out[16])
        {
            const float A[16] = {a.M11, a.M12, a.M13, a.M14, a.M21, a.M22, a.M23, a.M24,
                                  a.M31, a.M32, a.M33, a.M34, a.M41, a.M42, a.M43, a.M44};
            const float B[16] = {b.M11, b.M12, b.M13, b.M14, b.M21, b.M22, b.M23, b.M24,
                                  b.M31, b.M32, b.M33, b.M34, b.M41, b.M42, b.M43, b.M44};
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        sum += A[r * 4 + k] * B[k * 4 + c];
                    out[r * 4 + c] = sum;
                }
        }

        // Combines World*View*Projection (XNA row-major) and writes it out column-major,
        // ready for glUniformMatrix4fv(..., GL_FALSE, ...).
        void ComputeColumnMajorWVP(const Matrix& world, const Matrix& view, const Matrix& projection, float out[16])
        {
            float wv[16];
            MultiplyRowMajor(world, view, wv);
            const Matrix worldView(wv[0], wv[1], wv[2], wv[3], wv[4], wv[5], wv[6], wv[7],
                                   wv[8], wv[9], wv[10], wv[11], wv[12], wv[13], wv[14], wv[15]);
            float wvp[16];
            MultiplyRowMajor(worldView, projection, wvp);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    out[c * 4 + r] = wvp[r * 4 + c];
        }

        class Tex final : public ITextureBackend
        {
        public:
            GLuint id{};
            int w{};
            int h{};

            explicit Tex(const ImageData& data) : w(data.width), h(data.height)
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.pixels.data());
            }

            ~Tex() override { if (id) glDeleteTextures(1, &id); }

            int GetWidth() const override { return w; }
            int GetHeight() const override { return h; }
            SDL_Texture* GetNativeTexture() const override { return nullptr; }
            void BindGL() const override { glBindTexture(GL_TEXTURE_2D, id); }

            void UpdatePixels(const uint8_t* pixels, int stride) override
            {
                glBindTexture(GL_TEXTURE_2D, id);
                if (stride == w * 4)
                {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                }
                else
                {
                    std::vector<uint8_t> tight(static_cast<std::size_t>(w) * h * 4);
                    for (int y = 0; y < h; ++y)
                        std::memcpy(tight.data() + y * w * 4, pixels + y * stride, w * 4);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tight.data());
                }
            }
        };

        class VB final : public IVertexBufferBackend
        {
        public:
            GLuint id{};
            int count{};
            std::size_t stride{};

            VB() { glGenBuffers(1, &id); }
            ~VB() override { if (id) glDeleteBuffers(1, &id); }

            void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
            {
                count = vertex_count;
                stride = stride_in_bytes;
                glBindBuffer(GL_ARRAY_BUFFER, id);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_count * stride_in_bytes), data, GL_DYNAMIC_DRAW);
            }

            int GetVertexCount() const override { return count; }
        };

        class IB final : public IIndexBufferBackend
        {
        public:
            GLuint id{};
            int count{};
            bool thirtyTwoBit{};

            explicit IB(bool isThirtyTwoBit) : thirtyTwoBit(isThirtyTwoBit) { glGenBuffers(1, &id); }
            ~IB() override { if (id) glDeleteBuffers(1, &id); }

            void SetData16(const void* data, int index_count) override
            {
                count = index_count;
                thirtyTwoBit = false;
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * 2, data, GL_DYNAMIC_DRAW);
            }

            // Desktop OpenGL (unlike GLES2 without an extension) has always accepted
            // GL_UNSIGNED_INT indices in glDrawElements, so 32-bit index buffers just work here.
            void SetData32(const void* data, int index_count) override
            {
                count = index_count;
                thirtyTwoBit = true;
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * 4, data, GL_DYNAMIC_DRAW);
            }

            int GetIndexCount() const override { return count; }
            bool IsThirtyTwoBit() const override { return thirtyTwoBit; }
        };

        // Owns its own tiny shader program + streaming VBO (mirrors EasyGLSpriteBatchBackend's
        // per-instance GL resources) rather than a function-local static -- a static handle would
        // go stale across DebugSimulateContextLoss()/multiple GraphicsDevice instances in one
        // process.
        class Sprite final : public ISpriteBatchBackend
        {
        public:
            explicit Sprite(OpenGL2GraphicsBackend* backend) : backend_(backend) {}

            ~Sprite() override
            {
                if (vbo_) glDeleteBuffers(1, &vbo_);
                if (program_) glDeleteProgram(program_);
            }

            void Begin() override { begun_ = true; }
            void End() override { begun_ = false; }
            void SetTransformMatrix(const Matrix& m) override { transform_ = m; }
            void SetSamplerFilter(int textureFilter) override { filter_ = textureFilter; }
            void SetSamplerAddressMode(int addressU, int addressV) override
            {
                addressU_ = addressU;
                addressV_ = addressV;
            }

            void Draw(const ITextureBackend& texture, float x, float y) override
            {
                const Rectangle destination(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
                const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
                Draw(texture, destination, source, Color::White);
            }

            void Draw(const ITextureBackend& texture, const Rectangle& destination,
                     const Rectangle& source, const Color& color) override
            {
                Draw(texture, destination, source, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
            }

            void Draw(const ITextureBackend& texture, const Rectangle& destination, const Rectangle& source,
                     const Color& color, float rotation, const Vector2& origin,
                     SpriteEffects effects, float layerDepth) override
            {
                if (!begun_) return;
                const auto* tex = dynamic_cast<const Tex*>(&texture);
                if (!tex) return;

                EnsureResources();

                int viewportWidth = 0, viewportHeight = 0;
                backend_->GetViewportSize(viewportWidth, viewportHeight);

                const float r = color.getRProperty() / 255.0f;
                const float g = color.getGProperty() / 255.0f;
                const float b = color.getBProperty() / 255.0f;
                const float a = color.getAProperty() / 255.0f;

                float u0 = static_cast<float>(source.X) / texture.GetWidth();
                float v0 = static_cast<float>(source.Y) / texture.GetHeight();
                float u1 = static_cast<float>(source.X + source.Width) / texture.GetWidth();
                float v1 = static_cast<float>(source.Y + source.Height) / texture.GetHeight();
                if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) std::swap(u0, u1);
                if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) std::swap(v0, v1);

                const float localCorners[4][2] = {
                    {-origin.X, -origin.Y},
                    {destination.Width - origin.X, -origin.Y},
                    {destination.Width - origin.X, destination.Height - origin.Y},
                    {-origin.X, destination.Height - origin.Y},
                };
                const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                const float cosR = std::cos(rotation);
                const float sinR = std::sin(rotation);

                struct SpriteVertex { float x, y, z; float r, g, b, a; float u, v; };
                SpriteVertex corners[4];
                for (int i = 0; i < 4; ++i)
                {
                    const float px = localCorners[i][0] * cosR - localCorners[i][1] * sinR + destination.X + origin.X;
                    const float py = localCorners[i][0] * sinR + localCorners[i][1] * cosR + destination.Y + origin.Y;
                    // SpriteBatch.Begin(transformMatrix, ...)'s camera/scroll transform is applied
                    // in screen space, BEFORE the screen->clip ortho mapping below -- row-vector
                    // convention (v * transform_), matching EasyGLSpriteBatchBackend's own
                    // `transform_ * orthoM` combined-matrix order (only the XY affine part matters;
                    // SpriteBatch's transform is always 2D).
                    const float tx = px * transform_.M11 + py * transform_.M21 + transform_.M41;
                    const float ty = px * transform_.M12 + py * transform_.M22 + transform_.M42;
                    corners[i] = {(tx / viewportWidth) * 2.0f - 1.0f, 1.0f - (ty / viewportHeight) * 2.0f, layerDepth,
                                  r, g, b, a, uv[i][0], uv[i][1]};
                }
                const SpriteVertex quad[6] = {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]};

                glUseProgram(program_);
                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, x)));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, r)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, u)));

                glActiveTexture(GL_TEXTURE0);
                tex->BindGL();
                glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
                // GL 2.1 has no separate sampler objects (those are GL 3.3+) -- SamplerState is
                // applied directly onto the currently-bound texture object's own parameters,
                // exactly like Tex's own constructor defaults. Mutating it per-draw is the
                // standard legacy-GL approach and matches this backend's single-texture-unit,
                // no-sampler-cache design.
                const GLint glFilter = ToGLFilter(filter_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(addressU_));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(addressV_));

                // Blending itself is NOT touched here -- GraphicsDevice::setBlendStateProperty()
                // (driven by SpriteBatch::Begin()'s BlendState argument, BlendState::AlphaBlend by
                // default) already reached OpenGL2GraphicsBackend::ApplyBlendState() before this
                // draw, exactly like EasyGLSpriteBatchBackend::Draw(). Hardcoding a blend func here
                // would both ignore custom BlendStates and fight that call.
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }

        private:
            void EnsureResources()
            {
                if (program_) return;
                static const char* vertexSrc =
                    "attribute vec3 aPosition;attribute vec4 aColor;attribute vec2 aTexCoord;"
                    "varying vec4 vColor;varying vec2 vTex;"
                    "void main(){gl_Position=vec4(aPosition,1.0);vColor=aColor;vTex=aTexCoord;}";
                static const char* fragmentSrc =
                    "varying vec4 vColor;varying vec2 vTex;uniform sampler2D uTex;"
                    "void main(){gl_FragColor=texture2D(uTex,vTex)*vColor;}";
                program_ = LinkProgram(vertexSrc, fragmentSrc);
                glGenBuffers(1, &vbo_);
            }

            OpenGL2GraphicsBackend* backend_;
            bool begun_ = false;
            GLuint program_ = 0;
            GLuint vbo_ = 0;
            Matrix transform_ = Matrix::getIdentityProperty();
            int filter_ = 0;   // TextureFilter::Linear
            int addressU_ = 1; // TextureAddressMode::Clamp (matches SamplerState::LinearClamp,
            int addressV_ = 1; // the default SpriteBatch.Begin() falls back to)
        };
    }

    OpenGL2GraphicsBackend::OpenGL2GraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                     CnaPresentationMode presentationMode, int swapInterval)
        : window_(window), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight), presentationMode_(presentationMode)
    {
        if (!window_)
            throw std::runtime_error("OPENGL2 backend: null SDL window");

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        context_ = SDL_GL_CreateContext(window_);
        if (!context_)
            throw std::runtime_error(std::string("OPENGL2 SDL_GL_CreateContext failed: ") + SDL_GetError());

        SDL_GL_MakeCurrent(window_, context_);
        SetSwapInterval(swapInterval);
        ensurePrograms();

        // Sane default until the first real GraphicsDevice.BlendState reaches ApplyBlendState()
        // (matches BlendState::AlphaBlend: One / InverseSourceAlpha, premultiplied convention).
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    }

    OpenGL2GraphicsBackend::~OpenGL2GraphicsBackend()
    {
        if (colorProgram_) glDeleteProgram(colorProgram_);
        if (texturedProgram_) glDeleteProgram(texturedProgram_);
        if (context_) SDL_GL_DestroyContext(context_);
    }

    void OpenGL2GraphicsBackend::ensurePrograms()
    {
        const char* colorVertexSrc =
            "attribute vec3 aPosition;attribute vec4 aColor;uniform mat4 uWVP;varying vec4 vColor;"
            "void main(){gl_Position=uWVP*vec4(aPosition,1.0);vColor=aColor;}";
        const char* colorFragmentSrc =
            "varying vec4 vColor;uniform vec4 uDiffuse;"
            "void main(){gl_FragColor=vColor*uDiffuse;}";
        const char* texturedVertexSrc =
            "attribute vec3 aPosition;attribute vec4 aColor;attribute vec2 aTexCoord;uniform mat4 uWVP;"
            "varying vec4 vColor;varying vec2 vTex;"
            "void main(){gl_Position=uWVP*vec4(aPosition,1.0);vColor=aColor;vTex=aTexCoord;}";
        const char* texturedFragmentSrc =
            "varying vec4 vColor;varying vec2 vTex;uniform vec4 uDiffuse;uniform sampler2D uTex;"
            "void main(){gl_FragColor=texture2D(uTex,vTex)*vColor*uDiffuse;}";
        colorProgram_ = LinkProgram(colorVertexSrc, colorFragmentSrc);
        texturedProgram_ = LinkProgram(texturedVertexSrc, texturedFragmentSrc);
    }

    void OpenGL2GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::Present() { SDL_GL_SwapWindow(window_); }

    void OpenGL2GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SDL_GetWindowSize(window_, &width, &height);
        if (virtualWidth_ > 0 && virtualHeight_ > 0)
        {
            width = virtualWidth_;
            height = virtualHeight_;
        }
    }

    void OpenGL2GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGL2GraphicsBackend::SetPresentationMode(int mode) { presentationMode_ = static_cast<CnaPresentationMode>(mode); }
    void OpenGL2GraphicsBackend::SetSwapInterval(int interval) { SDL_GL_SetSwapInterval(interval); }

    std::unique_ptr<ITextureBackend> OpenGL2GraphicsBackend::CreateTexture(const ImageData& data) { return std::make_unique<Tex>(data); }
    std::unique_ptr<ISpriteBatchBackend> OpenGL2GraphicsBackend::CreateSpriteBatch() { return std::make_unique<Sprite>(this); }

    void OpenGL2GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);

        glReadBuffer(GL_BACK);
        // OpenGL origin is bottom-left; flip y so the caller gets top-left origin (mirrors
        // EasyGLGraphicsBackend::ReadBackbuffer's identical convention).
        const int glY = windowHeight - y - h;
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const int rowBytes = w * 4;
        std::vector<uint8_t> rowBuffer(rowBytes);
        for (int i = 0; i < h / 2; ++i)
        {
            uint8_t* top = pixels + i * rowBytes;
            uint8_t* bottom = pixels + (h - 1 - i) * rowBytes;
            std::copy(top, top + rowBytes, rowBuffer.data());
            std::copy(bottom, bottom + rowBytes, top);
            std::copy(rowBuffer.begin(), rowBuffer.end(), bottom);
        }
    }

    void OpenGL2GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::ClearDepth(float depth)
    {
        glClearDepth(depth);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepth(depth);
        glClearStencil(stencil);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::SetDepthTestEnabled(bool enabled) { enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); }
    void OpenGL2GraphicsBackend::SetBlendEnabled(bool enabled) { enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND); }
    void OpenGL2GraphicsBackend::SetDepthWriteEnabled(bool enabled) { glDepthMask(enabled ? GL_TRUE : GL_FALSE); }

    std::unique_ptr<IVertexBufferBackend> OpenGL2GraphicsBackend::CreateVertexBuffer(int) { return std::make_unique<VB>(); }
    std::unique_ptr<IIndexBufferBackend> OpenGL2GraphicsBackend::CreateIndexBuffer16(int) { return std::make_unique<IB>(false); }
    std::unique_ptr<IIndexBufferBackend> OpenGL2GraphicsBackend::CreateIndexBuffer32(int) { return std::make_unique<IB>(true); }

    void OpenGL2GraphicsBackend::drawInternal(const IVertexBufferBackend& vbi, const IIndexBufferBackend* ibi,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params)
    {
        const auto* vb = dynamic_cast<const VB*>(&vbi);
        if (!vb)
            throw std::runtime_error("OPENGL2: incompatible vertex buffer");
        const auto* ib = dynamic_cast<const IB*>(ibi);

        const bool textured = params && params->texture0 && vb->stride >= 20;
        const GLuint program = textured ? texturedProgram_ : colorProgram_;
        glUseProgram(program);

        float wvp[16];
        ComputeColumnMajorWVP(world, view, projection, wvp);
        glUniformMatrix4fv(glGetUniformLocation(program, "uWVP"), 1, GL_FALSE, wvp);

        float diffuse[4] = {1, 1, 1, 1};
        if (params) std::memcpy(diffuse, params->diffuseColor, sizeof(diffuse));
        glUniform4fv(glGetUniformLocation(program, "uDiffuse"), 1, diffuse);

        glBindBuffer(GL_ARRAY_BUFFER, vb->id);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vb->stride), nullptr);

        // Stride-based vertex-layout dispatch (VertexPositionColor=16, VertexPositionTexture=20,
        // VertexPositionColorTexture=24, VertexPositionNormalTexture>=32 with the normal ignored --
        // no lighting shader yet, see plan_opengl2.md's follow-up list).
        if (vb->stride == 16)
        {
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(vb->stride), reinterpret_cast<void*>(12));
            glDisableVertexAttribArray(2);
        }
        else if (vb->stride == 20)
        {
            glDisableVertexAttribArray(1);
            glVertexAttrib4f(1, 1, 1, 1, 1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vb->stride), reinterpret_cast<void*>(12));
        }
        else if (vb->stride == 24)
        {
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(vb->stride), reinterpret_cast<void*>(12));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vb->stride), reinterpret_cast<void*>(16));
        }
        else if (vb->stride >= 32)
        {
            glDisableVertexAttribArray(1);
            glVertexAttrib4f(1, 1, 1, 1, 1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vb->stride), reinterpret_cast<void*>(24));
        }
        else
        {
            throw std::runtime_error("OPENGL2: unsupported vertex stride");
        }

        if (textured)
        {
            glActiveTexture(GL_TEXTURE0);
            params->texture0->BindGL();
            glUniform1i(glGetUniformLocation(program, "uTex"), 0);
        }

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (ib)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
            glDrawElements(ToGLPrimitiveMode(primitive), vertexCount,
                           ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
        }
        else
        {
            glDrawArrays(ToGLPrimitiveMode(primitive), 0, vertexCount);
        }
    }

    void OpenGL2GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                                                        const Matrix& projection, PrimitiveType primitive, int primitiveCount)
    {
        drawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void OpenGL2GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        drawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void OpenGL2GraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                                   const GpuDrawParams& params)
    {
        drawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
    }

    void OpenGL2GraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        drawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
    }

    void OpenGL2GraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                                                  int colorBlendFunc, int alphaBlendFunc)
    {
        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero => effectively no
        // blending (matches EasyGLGraphicsBackend::ApplyBlendState's identical detection).
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 && alphaSrcBlend == 0 && alphaDstBlend == 1);
        blendEnabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        if (!blendEnabled) return;

        glBlendFuncSeparate(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend),
                            ToGLBlendFactor(alphaSrcBlend), ToGLBlendFactor(alphaDstBlend));
        glBlendEquationSeparate(ToGLBlendEquation(colorBlendFunc), ToGLBlendEquation(alphaBlendFunc));
    }

    void OpenGL2GraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                         bool stencilEnable, int stencilFunc,
                                                         int stencilPass, int stencilFail, int stencilDepthFail,
                                                         int stencilMask, int stencilWriteMask, int referenceStencil,
                                                         bool twoSidedStencilMode,
                                                         int ccwStencilFunc, int ccwStencilPass,
                                                         int ccwStencilFail, int ccwStencilDepthFail)
    {
        SetDepthTestEnabled(depthEnable);
        SetDepthWriteEnabled(depthWriteEnable);
        if (depthEnable)
            glDepthFunc(ToGLCompareFunc(depthFunc));

        stencilEnable ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
        if (!stencilEnable) return;

        const GLenum glSFail = ToGLStencilOp(stencilFail);
        const GLenum glDFail = ToGLStencilOp(stencilDepthFail);
        const GLenum glPass  = ToGLStencilOp(stencilPass);

        // Mirrors EasyGLGraphicsBackend::ApplyDepthStencilState's own GL_FRONT/GL_BACK
        // assignment: the primary Stencil*/ReferenceStencil fields go to GL_FRONT, the
        // CcwStencil* fields (XNA's "the other side" of two-sided stencil) go to GL_BACK.
        if (twoSidedStencilMode)
        {
            glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(stencilFunc),
                                  referenceStencil, static_cast<GLuint>(stencilMask));
            glStencilOpSeparate(GL_FRONT, glSFail, glDFail, glPass);
            glStencilMaskSeparate(GL_FRONT, static_cast<GLuint>(stencilWriteMask));

            glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(ccwStencilFunc),
                                  referenceStencil, static_cast<GLuint>(stencilMask));
            glStencilOpSeparate(GL_BACK, ToGLStencilOp(ccwStencilFail),
                                ToGLStencilOp(ccwStencilDepthFail), ToGLStencilOp(ccwStencilPass));
            glStencilMaskSeparate(GL_BACK, static_cast<GLuint>(stencilWriteMask));
        }
        else
        {
            glStencilFunc(ToGLCompareFunc(stencilFunc), referenceStencil, static_cast<GLuint>(stencilMask));
            glStencilOp(glSFail, glDFail, glPass);
            glStencilMask(static_cast<GLuint>(stencilWriteMask));
        }
    }

    void OpenGL2GraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                       float depthBias, float slopeScaleDepthBias)
    {
        glPolygonMode(GL_FRONT_AND_BACK, fillMode == 0 ? GL_FILL : GL_LINE);

        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(slopeScaleDepthBias, depthBias);
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        if (cullMode == 0)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT);
        }

        scissorTestEnable ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    }

    void OpenGL2GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
        glScissor(x, windowHeight - y - h, w, h);
    }

    void OpenGL2GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return;
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
        glViewport(x, windowHeight - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
    }

    bool OpenGL2GraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0 || windowWidth <= 0 || windowHeight <= 0) return false;
        logX = windowX * virtualWidth_ / windowWidth;
        logY = windowY * virtualHeight_ / windowHeight;
        return true;
    }

    bool OpenGL2GraphicsBackend::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        int windowWidth = 0, windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0) return false;
        windowX = logX * windowWidth / virtualWidth_;
        windowY = logY * windowHeight / virtualHeight_;
        return true;
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<OpenGL2::OpenGL2GraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
