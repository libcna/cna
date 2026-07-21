// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/OpenGL4/OpenGL4GraphicsBackend.hpp"
#include "System/InvalidOperationException.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace CNA::Internal::Backends::OpenGL4::GL4;

namespace CNA::Internal::Backends::OpenGL4
{
    namespace
    {
        GLenum ToGLPrimitive(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return GL_TRIANGLES;
            case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveType::LineList:      return GL_LINES;
            case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
            case PrimitiveType::PointListEXT:  return GL_POINTS;
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
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        // ---- Built-in GLSL 410 core shaders -----------------------------------------------

        const char* kSpriteVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main()
{
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)GLSL";

        const char* kSpriteFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
out vec4 fragColor;
void main()
{
    fragColor = texture(uTexture, vUV) * vColor;
}
)GLSL";

        const char* kColored3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uWorldViewProj;
out vec4 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kColored3DFragSrc = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 fragColor;
void main()
{
    fragColor = vColor;
}
)GLSL";

        // plan_opengl4.md GL4-13: textured3d (VertexPositionTexture, stride 20). Algorithmic
        // reference: VulkanGraphicsBackend's textured3d.vert/frag.glsl (no fog, no Y-flip --
        // OpenGL's own NDC convention needs none, unlike Vulkan's flipped clip space).
        const char* kTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uWorldViewProj;
out vec2 vUV;
void main()
{
    vUV = aUV;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uDiffuseColor;
uniform bool uTextureEnabled;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    fragColor = tex * uDiffuseColor;
}
)GLSL";

        // colored_textured3d (VertexPositionColorTexture, stride 24).
        const char* kColoredTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform vec4 uDiffuseColor;
uniform bool uVertexColorEnabled;
out vec2 vUV;
out vec4 vTint;
void main()
{
    vUV = aUV;
    vTint = uVertexColorEnabled ? (aColor * uDiffuseColor) : uDiffuseColor;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kColoredTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec4 vTint;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
out vec4 fragColor;
void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    fragColor = tex * vTint;
}
)GLSL";

        // lit_textured3d (VertexPositionNormalTexture, stride 32) -- BasicEffect's default
        // 3-directional-light rig. Ported from VulkanGraphicsBackend's lit_textured3d.vert/
        // frag.glsl: FNA's Lighting.fxh ComputeLights() (ambient + per-light Lambertian diffuse +
        // Blinn-Phong specular, EmissiveColor added post-multiply, specular added post-texture
        // scaled by alpha). No fog (same deliberate deferral as the other 3D stride variants).
        // World's inverse-transpose upper-left 3x3 is used for the normal matrix (not MVP's),
        // matching EnvironmentMapEffect's own already-correct pattern -- an MVP-based transform
        // would bake View/Projection into the normal.
        const char* kLitTextured3DVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uWorldViewProj;
uniform mat4 uWorld;
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
void main()
{
    vUV = aUV;
    mat3 normalMatrix = transpose(inverse(mat3(uWorld)));
    vNormal = normalize(normalMatrix * aNormal);
    vWorldPos = (uWorld * vec4(aPos, 1.0)).xyz;
    gl_Position = uWorldViewProj * vec4(aPos, 1.0);
}
)GLSL";

        const char* kLitTextured3DFragSrc = R"GLSL(
#version 410 core
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
uniform sampler2D uTexture;
uniform bool uTextureEnabled;
uniform bool uLightingEnabled;
uniform vec4 uDiffuseColor;
uniform vec3 uAmbientColor;
uniform vec3 uLight0Dir;
uniform vec3 uLight0Diffuse;
uniform vec3 uLight0Specular;
uniform vec3 uLight1Dir;
uniform vec3 uLight1Diffuse;
uniform vec3 uLight1Specular;
uniform vec3 uLight2Dir;
uniform vec3 uLight2Diffuse;
uniform vec3 uLight2Specular;
uniform vec3 uEmissiveColor;
uniform vec3 uEyePosition;
uniform vec3 uSpecularColor;
uniform float uSpecularPower;
out vec4 fragColor;

// Guards against normalize(0,0,0) on a disabled/unconfigured DirectionalLight -- a real bug
// found while porting WebGPU's own lit3d shader (plan_webgpu.md): normalize() on a true zero
// vector is undefined and can poison the whole light sum with NaN.
vec3 safeNormalize(vec3 v)
{
    float len = length(v);
    return len > 1e-6 ? (v / len) : vec3(0.0, -1.0, 0.0);
}

void main()
{
    vec4 tex = uTextureEnabled ? texture(uTexture, vUV) : vec4(1.0);
    vec4 color;
    if (uLightingEnabled)
    {
        vec3 N = normalize(vNormal);
        vec3 E = normalize(uEyePosition - vWorldPos);
        vec3 nL0 = safeNormalize(uLight0Dir);
        vec3 nL1 = safeNormalize(uLight1Dir);
        vec3 nL2 = safeNormalize(uLight2Dir);
        // Direction fields point FROM the light, so negate for the dot with N.
        float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
        float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
        float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
        vec3 lightSum = uAmbientColor + NdotL0 * uLight0Diffuse + NdotL1 * uLight1Diffuse + NdotL2 * uLight2Diffuse;
        vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, uSpecularPower);
        vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, uSpecularPower);
        vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, uSpecularPower);
        vec3 specularRGB = (spec0 * uLight0Specular + spec1 * uLight1Specular + spec2 * uLight2Specular) * uSpecularColor;
        // EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it
        // (matches FNA's Lighting.fxh: result.Diffuse = sum*DiffuseColor + EmissiveColor).
        vec3 lit = lightSum * uDiffuseColor.rgb + uEmissiveColor;
        color = vec4(lit, uDiffuseColor.a) * tex;
        // Specular is added after the texture*diffuse multiply, scaled by the resulting alpha
        // (FNA's AddSpecular macro), never by the texture directly.
        color.rgb += specularRGB * color.a;
    }
    else
    {
        color = uDiffuseColor * tex;
    }
    fragColor = color;
}
)GLSL";

        // XNA TextureFilter ordinal -> (GL min filter, GL mag filter). No mip chains are
        // generated by CreateTexture() in this phase, so every "Mip*" variant collapses to its
        // plain non-mip counterpart (matches EasyGLGraphicsBackend::ApplySamplerState's own
        // "CNA does not generate mipmaps by default" comment/behavior).
        void FilterToGL(int filter, GLint& minFilter, GLint& magFilter)
        {
            switch (filter)
            {
            case 1: // Point
                minFilter = GL_NEAREST; magFilter = GL_NEAREST; break;
            case 2: // Anisotropic
            case 3: case 5: case 7: // *MipLinear variants -> Linear
                minFilter = GL_LINEAR; magFilter = GL_LINEAR; break;
            case 4: case 6: case 8: // *MipPoint variants -> mixed collapses to Point/Linear split
                minFilter = GL_LINEAR; magFilter = GL_NEAREST; break;
            default: // Linear
                minFilter = GL_LINEAR; magFilter = GL_LINEAR; break;
            }
        }

        GLint AddressModeToGL(int mode)
        {
            switch (mode)
            {
            case 0: return GL_REPEAT;          // Wrap
            case 2: return GL_MIRRORED_REPEAT;  // Mirror
            default: return GL_CLAMP_TO_EDGE;   // Clamp
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4RawProgram
    // ------------------------------------------------------------------------------------

    OpenGL4RawProgram::~OpenGL4RawProgram() { Destroy(); }

    OpenGL4RawProgram::OpenGL4RawProgram(OpenGL4RawProgram&& other) noexcept
        : program_(other.program_), error_(std::move(other.error_))
    {
        other.program_ = 0;
    }

    OpenGL4RawProgram& OpenGL4RawProgram::operator=(OpenGL4RawProgram&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            program_ = other.program_;
            error_ = std::move(other.error_);
            other.program_ = 0;
        }
        return *this;
    }

    void OpenGL4RawProgram::Destroy()
    {
        if (program_ != 0)
        {
            gl4_glDeleteProgram(program_);
            program_ = 0;
        }
    }

    bool OpenGL4RawProgram::Compile(const std::string& vertSrc, const std::string& fragSrc)
    {
        Destroy();
        error_.clear();

        const GLuint vs = gl4_glCreateShader(GL_VERTEX_SHADER);
        const char* vsSrc = vertSrc.c_str();
        gl4_glShaderSource(vs, 1, &vsSrc, nullptr);
        gl4_glCompileShader(vs);
        GLint vsOk = 0;
        gl4_glGetShaderiv(vs, GL_COMPILE_STATUS, &vsOk);
        if (!vsOk)
        {
            char log[1024] = {};
            gl4_glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
            error_ = std::string("vertex shader: ") + log;
            gl4_glDeleteShader(vs);
            return false;
        }

        const GLuint fs = gl4_glCreateShader(GL_FRAGMENT_SHADER);
        const char* fsSrc = fragSrc.c_str();
        gl4_glShaderSource(fs, 1, &fsSrc, nullptr);
        gl4_glCompileShader(fs);
        GLint fsOk = 0;
        gl4_glGetShaderiv(fs, GL_COMPILE_STATUS, &fsOk);
        if (!fsOk)
        {
            char log[1024] = {};
            gl4_glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
            error_ = std::string("fragment shader: ") + log;
            gl4_glDeleteShader(vs);
            gl4_glDeleteShader(fs);
            return false;
        }

        const GLuint prog = gl4_glCreateProgram();
        gl4_glAttachShader(prog, vs);
        gl4_glAttachShader(prog, fs);
        gl4_glLinkProgram(prog);

        GLint linkOk = 0;
        gl4_glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);

        // Shaders may be deleted once linked -- the program keeps its own compiled copy.
        gl4_glDeleteShader(vs);
        gl4_glDeleteShader(fs);

        if (!linkOk)
        {
            char log[1024] = {};
            gl4_glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            error_ = std::string("link: ") + log;
            gl4_glDeleteProgram(prog);
            return false;
        }

        program_ = prog;
        return true;
    }

    void OpenGL4RawProgram::Use() const
    {
        gl4_glUseProgram(program_);
    }

    int OpenGL4RawProgram::UniformLocation(const char* name) const
    {
        return gl4_glGetUniformLocation(program_, name);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4TextureBackend
    // ------------------------------------------------------------------------------------

    OpenGL4TextureBackend::OpenGL4TextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height)
    {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     data.pixels.empty() ? nullptr : data.pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    OpenGL4TextureBackend::~OpenGL4TextureBackend()
    {
        if (texture_ != 0)
            glDeleteTextures(1, &texture_);
    }

    void OpenGL4TextureBackend::BindGL() const
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
    }

    void OpenGL4TextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const int expectedStride = width_ * 4;
        if (stride == expectedStride || stride <= 0)
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        }
        else
        {
            // Row-by-row upload when the caller's row pitch doesn't match a tightly packed
            // width*4 buffer.
            for (int y = 0; y < height_; ++y)
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, width_, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                                 rgba + static_cast<std::size_t>(y) * stride);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4VertexBufferBackend
    // ------------------------------------------------------------------------------------

    OpenGL4VertexBufferBackend::OpenGL4VertexBufferBackend(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
    }

    OpenGL4VertexBufferBackend::~OpenGL4VertexBufferBackend()
    {
        if (vbo_ != 0) gl4_glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4VertexBufferBackend::ApplyLayout(std::size_t stride)
    {
        const auto s = static_cast<GLsizei>(stride);
        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        switch (stride)
        {
        case 16:
            // VertexPositionColor (packed): float3 position + ubyte4 color
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void*)12);
            break;
        case 20:
            // VertexPositionTexture (packed): float3 position + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (void*)12);
            break;
        case 24:
            // VertexPositionColorTexture (packed): float3 position + ubyte4 color + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)16);
            break;
        case 32:
            // VertexPositionNormalTexture (packed): float3 position + float3 normal + float2 texcoord
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            gl4_glEnableVertexAttribArray(1);
            gl4_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, (void*)12);
            gl4_glEnableVertexAttribArray(2);
            gl4_glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)24);
            break;
        default:
            // Unknown layout (not yet ported to this backend, plan_opengl4.md remaining work):
            // bind position-only as a safe fallback, matching EasyGL's own precedent.
            gl4_glEnableVertexAttribArray(0);
            gl4_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
            break;
        }

        gl4_glBindVertexArray(0);
    }

    void OpenGL4VertexBufferBackend::SetData(const void* data, int vertex_count, std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void OpenGL4VertexBufferBackend::SetDataWithOptions(const void* data, int vertex_count,
                                                        std::size_t stride_in_bytes, SetDataOptions /*options*/)
    {
        vertexCount_ = vertex_count;
        strideInBytes_ = stride_in_bytes;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(vertex_count) * stride_in_bytes);

        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glBufferData(GL_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
        ApplyLayout(stride_in_bytes);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4IndexBufferBackend
    // ------------------------------------------------------------------------------------

    OpenGL4IndexBufferBackend::OpenGL4IndexBufferBackend(int indexCapacity)
        : capacity_(indexCapacity)
    {
        gl4_glGenBuffers(1, &ibo_);
    }

    OpenGL4IndexBufferBackend::~OpenGL4IndexBufferBackend()
    {
        if (ibo_ != 0) gl4_glDeleteBuffers(1, &ibo_);
    }

    void OpenGL4IndexBufferBackend::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferBackend::SetData16WithOptions(const void* data, int index_count, SetDataOptions /*options*/)
    {
        indexCount_ = index_count;
        const auto byteCount = static_cast<GLsizeiptr4>(static_cast<std::size_t>(index_count) * sizeof(uint16_t));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4SpriteBatchBackend
    // ------------------------------------------------------------------------------------

    OpenGL4SpriteBatchBackend::OpenGL4SpriteBatchBackend(OpenGL4GraphicsBackend& owner)
        : owner_(&owner)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
        gl4_glGenBuffers(1, &ibo_);

        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glEnableVertexAttribArray(0);
        gl4_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, x));
        gl4_glEnableVertexAttribArray(1);
        gl4_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, u));
        gl4_glEnableVertexAttribArray(2);
        gl4_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (void*)offsetof(SpriteVertex, r));
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBindVertexArray(0);
    }

    OpenGL4SpriteBatchBackend::~OpenGL4SpriteBatchBackend()
    {
        gl4_glDeleteBuffers(1, &vbo_);
        gl4_glDeleteBuffers(1, &ibo_);
        gl4_glDeleteVertexArrays(1, &vao_);
    }

    void OpenGL4SpriteBatchBackend::Begin()
    {
        // SpriteBatch::Begin() (the public XNA-facing class) calls SetTransformMatrix()/
        // SetSamplerFilter()/SetSamplerAddressMode() BEFORE this Begin() runs (see
        // SpriteBatch.cpp) -- resetting those fields here would silently discard whatever the
        // caller just requested. Matches EasyGLSpriteBatchBackend::Begin()'s own precedent,
        // which only flips the begun_ flag.
        begun_ = true;
    }

    void OpenGL4SpriteBatchBackend::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h), Color::White);
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void OpenGL4SpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        if (!begun_) throw std::runtime_error("Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA's own straight-through divide (SpriteBatch.cs); a
        // sourceRectangle extending past the texture bounds intentionally lets the sampler's
        // TextureAddressMode govern edge sampling.
        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width) / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if ((int)effects & (int)SpriteEffects::FlipHorizontally) std::swap(u1, u2);
        if ((int)effects & (int)SpriteEffects::FlipVertically) std::swap(v1, v2);

        const float r = (float)color.getRProperty() / 255.0f;
        const float g = (float)color.getGProperty() / 255.0f;
        const float b = (float)color.getBProperty() / 255.0f;
        const float a = (float)color.getAProperty() / 255.0f;

        const float dx = (float)destinationRectangle.X;
        const float dy = (float)destinationRectangle.Y;
        const float dw = (float)destinationRectangle.Width;
        const float dh = (float)destinationRectangle.Height;

        const float sw = (float)sourceRectangle.Width;
        const float sh = (float)sourceRectangle.Height;

        const float ox = origin.X;
        const float oy = origin.Y;

        const float scaleX = sw != 0.0f ? dw / sw : 0.0f;
        const float scaleY = sh != 0.0f ? dh / sh : 0.0f;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

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

        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        pendingVertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        pendingVertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        pendingVertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }

    void OpenGL4SpriteBatchBackend::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

        int physW = 0, physH = 0;
        owner_->GetPhysicalSize(physW, physH);
        int logW = 0, logH = 0;
        owner_->GetLogicalSize(logW, logH);
        if (physW > 0 && physH > 0)
            glViewport(0, 0, physW, physH);
        if (logW <= 0 || logH <= 0) { logW = physW; logH = physH; }

        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logW), static_cast<float>(logH), 0.0f, -1.0f, 1.0f);
        const Matrix combined = transform_ * orthoM;
        float ortho[16];
        combined.ToColumnMajor(ortho);

        OpenGL4RawProgram& prog = owner_->GetOrCreateSpriteProgram();
        prog.Use();
        const int projLoc = prog.UniformLocation("uProjection");
        if (projLoc >= 0) gl4_glUniformMatrix4fv(projLoc, 1, GL_FALSE, ortho);
        const int texLoc = prog.UniformLocation("uTexture");
        if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        gl4_glActiveTexture(GL_TEXTURE0);
        currentTexture_->BindGL();
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl4_glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingVertices_.size() * sizeof(SpriteVertex)),
                         pendingVertices_.data(), GL_STREAM_DRAW);
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr4>(pendingIndices_.size() * sizeof(uint16_t)),
                         pendingIndices_.data(), GL_STREAM_DRAW);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(pendingIndices_.size()), GL_UNSIGNED_SHORT, nullptr);

        gl4_glBindVertexArray(0);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4GraphicsBackend
    // ------------------------------------------------------------------------------------

    OpenGL4GraphicsBackend::OpenGL4GraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                   CnaPresentationMode mode, int swapInterval)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
        , swapInterval_(swapInterval)
    {
        if (!window_) throw std::runtime_error("OpenGL4GraphicsBackend initialized with null window.");

        IGraphicsBackend::RegisterForWindow(window_, this);

        // Real desktop OpenGL 4.1 core profile -- unlike EasyGLGraphicsBackend, which requests
        // SDL_GL_CONTEXT_PROFILE_ES (OpenGL ES 3.0 / WebGL2). 4.1 is the highest core version
        // macOS's own GL driver ever exposes, so it is the widest-portable "real OpenGL 4" floor;
        // Linux/Windows drivers report whatever higher core version they actually support once
        // the context is current (see the version string logged below).
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_GLContext ctx = SDL_GL_CreateContext(window_);
        if (!ctx)
            throw std::runtime_error(std::string("OpenGL4: SDL_GL_CreateContext failed: ") + SDL_GetError());
        glContext_ = ctx;

        if (!SDL_GL_MakeCurrent(window_, ctx))
            throw std::runtime_error(std::string("OpenGL4: SDL_GL_MakeCurrent failed: ") + SDL_GetError());

        if (!GL4::LoadGL4Functions(reinterpret_cast<GL4::GetProcAddressFn>(SDL_GL_GetProcAddress)))
            throw std::runtime_error("OpenGL4: failed to resolve required GL 4.x core entry points");

        const auto* versionStr = glGetString(GL_VERSION);
        std::cout << "OpenGL4GraphicsBackend initialized with OpenGL "
                  << (versionStr ? reinterpret_cast<const char*>(versionStr) : "(unknown)") << std::endl;

        SDL_GL_SetSwapInterval(swapInterval_);

        gl4_glGenSamplers(kMaxSamplerSlots, samplers_);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    }

    OpenGL4GraphicsBackend::~OpenGL4GraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        gl4_glDeleteSamplers(kMaxSamplerSlots, samplers_);
        if (glContext_)
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
    }

    void OpenGL4GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::Present()
    {
        SDL_GL_SwapWindow(window_);
    }

    void OpenGL4GraphicsBackend::GetPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window_, &width, &height);
    }

    void OpenGL4GraphicsBackend::GetLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSize(width, height);
            return;
        }
        int physW = 0, physH = 0;
        GetPhysicalSize(physW, physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>((double)physW * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void OpenGL4GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void OpenGL4GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGL4GraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void OpenGL4GraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    std::unique_ptr<ITextureBackend> OpenGL4GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<OpenGL4TextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> OpenGL4GraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<OpenGL4SpriteBatchBackend>(*this);
    }

    void OpenGL4GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        int fbH = 0, fbW = 0;
        GetPhysicalSize(fbW, fbH);

        // OpenGL's origin is bottom-left; flip Y so the caller gets top-left-origin game
        // coordinates, matching EasyGLGraphicsBackend::ReadBackbuffer's own convention.
        const int glY = fbH - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int row = 0; row < h / 2; ++row)
        {
            uint8_t* a = pixels + static_cast<std::size_t>(row) * rowBytes;
            uint8_t* b = pixels + static_cast<std::size_t>(h - 1 - row) * rowBytes;
            std::memcpy(tmp.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, tmp.data(), rowBytes);
        }
    }

    void OpenGL4GraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearDepth(float depth)
    {
        glClearDepth(depth);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepth(depth);
        glClearStencil(stencil);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL4GraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClearStencil(stencil);
        const GLboolean wasWritable = depthWriteEnabled_ ? GL_TRUE : GL_FALSE;
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthMask(wasWritable);
    }

    void OpenGL4GraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }

    void OpenGL4GraphicsBackend::SetBlendEnabled(bool enabled)
    {
        if (enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

    void OpenGL4GraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    std::unique_ptr<IVertexBufferBackend> OpenGL4GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<OpenGL4VertexBufferBackend>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> OpenGL4GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<OpenGL4IndexBufferBackend>(index_capacity);
    }

    void OpenGL4GraphicsBackend::EnsureColored3DProgram()
    {
        if (colored3DProgram_.IsValid()) return;
        if (!colored3DProgram_.Compile(kColored3DVertSrc, kColored3DFragSrc))
            throw std::runtime_error("OpenGL4: colored3d program failed to compile: " + colored3DProgram_.GetError());
        colored3DWvpLoc_ = colored3DProgram_.UniformLocation("uWorldViewProj");
    }

    void OpenGL4GraphicsBackend::EnsureTextured3DProgram()
    {
        if (textured3DProgram_.IsValid()) return;
        if (!textured3DProgram_.Compile(kTextured3DVertSrc, kTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: textured3d program failed to compile: " + textured3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureColoredTextured3DProgram()
    {
        if (coloredTextured3DProgram_.IsValid()) return;
        if (!coloredTextured3DProgram_.Compile(kColoredTextured3DVertSrc, kColoredTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: colored_textured3d program failed to compile: " + coloredTextured3DProgram_.GetError());
    }

    void OpenGL4GraphicsBackend::EnsureLitTextured3DProgram()
    {
        if (litTextured3DProgram_.IsValid()) return;
        if (!litTextured3DProgram_.Compile(kLitTextured3DVertSrc, kLitTextured3DFragSrc))
            throw std::runtime_error("OpenGL4: lit_textured3d program failed to compile: " + litTextured3DProgram_.GetError());
    }

    bool OpenGL4GraphicsBackend::BindProgramForStride(std::size_t strideInBytes, const Matrix& world, const Matrix& view,
                                                       const Matrix& projection, const GpuDrawParams& params)
    {
        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        const bool hasTexture0 = params.texture0 != nullptr;
        if (hasTexture0)
        {
            gl4_glActiveTexture(GL_TEXTURE0);
            params.texture0->BindGL();
            ApplySamplerState(0, 0, 1, 1, 1); // Linear/Clamp -- matches this phase's SpriteBatch default.
        }

        switch (strideInBytes)
        {
        case 20: // VertexPositionTexture
        {
            EnsureTextured3DProgram();
            textured3DProgram_.Use();
            const int wvpLoc = textured3DProgram_.UniformLocation("uWorldViewProj");
            if (wvpLoc >= 0) gl4_glUniformMatrix4fv(wvpLoc, 1, GL_FALSE, wvpCol);
            const int diffuseLoc = textured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            const int texEnabledLoc = textured3DProgram_.UniformLocation("uTextureEnabled");
            if (texEnabledLoc >= 0) gl4_glUniform1i(texEnabledLoc, (params.textureEnabled && hasTexture0) ? 1 : 0);
            const int texLoc = textured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            return true;
        }
        case 24: // VertexPositionColorTexture
        {
            EnsureColoredTextured3DProgram();
            coloredTextured3DProgram_.Use();
            const int wvpLoc = coloredTextured3DProgram_.UniformLocation("uWorldViewProj");
            if (wvpLoc >= 0) gl4_glUniformMatrix4fv(wvpLoc, 1, GL_FALSE, wvpCol);
            const int diffuseLoc = coloredTextured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            const int vcLoc = coloredTextured3DProgram_.UniformLocation("uVertexColorEnabled");
            if (vcLoc >= 0) gl4_glUniform1i(vcLoc, params.vertexColorEnabled ? 1 : 0);
            const int texEnabledLoc = coloredTextured3DProgram_.UniformLocation("uTextureEnabled");
            if (texEnabledLoc >= 0) gl4_glUniform1i(texEnabledLoc, (params.textureEnabled && hasTexture0) ? 1 : 0);
            const int texLoc = coloredTextured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            return true;
        }
        case 32: // VertexPositionNormalTexture
        {
            EnsureLitTextured3DProgram();
            litTextured3DProgram_.Use();
            float worldCol[16];
            world.ToColumnMajor(worldCol);
            const auto setM4 = [&](const char* name, const float* m) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, m);
            };
            const auto setV3 = [&](const char* name, const float* v) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform3f(loc, v[0], v[1], v[2]);
            };
            const auto setB = [&](const char* name, bool v) {
                const int loc = litTextured3DProgram_.UniformLocation(name);
                if (loc >= 0) gl4_glUniform1i(loc, v ? 1 : 0);
            };
            setM4("uWorldViewProj", wvpCol);
            setM4("uWorld", worldCol);
            const int diffuseLoc = litTextured3DProgram_.UniformLocation("uDiffuseColor");
            if (diffuseLoc >= 0) gl4_glUniform4f(diffuseLoc, params.diffuseColor[0], params.diffuseColor[1],
                                                 params.diffuseColor[2], params.diffuseColor[3]);
            setB("uTextureEnabled", params.textureEnabled && hasTexture0);
            setB("uLightingEnabled", params.lightingEnabled);
            setV3("uAmbientColor", params.ambientColor);
            setV3("uLight0Dir", params.light0Dir);
            setV3("uLight0Diffuse", params.light0Diffuse);
            setV3("uLight0Specular", params.light0Specular);
            setV3("uLight1Dir", params.light1Dir);
            setV3("uLight1Diffuse", params.light1Diffuse);
            setV3("uLight1Specular", params.light1Specular);
            setV3("uLight2Dir", params.light2Dir);
            setV3("uLight2Diffuse", params.light2Diffuse);
            setV3("uLight2Specular", params.light2Specular);
            setV3("uEmissiveColor", params.emissiveColor);
            setV3("uEyePosition", params.eyePositionWorld);
            setV3("uSpecularColor", params.specularColor);
            const int specPowerLoc = litTextured3DProgram_.UniformLocation("uSpecularPower");
            if (specPowerLoc >= 0) gl4_glUniform1f(specPowerLoc, params.specularPower);
            const int texLoc = litTextured3DProgram_.UniformLocation("uTexture");
            if (texLoc >= 0) gl4_glUniform1i(texLoc, 0);
            return true;
        }
        default:
            return false;
        }
    }

    void OpenGL4GraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb_in,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            DrawColoredPrimitives(vb_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        glDrawArrays(ToGLPrimitive(primitive), params.vertexStart, vertexCount);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferBackend&>(ib_in);
        if (!BindProgramForStride(vb.GetStrideInBytes(), world, view, projection, params))
        {
            DrawIndexedColoredPrimitives(vb_in, ib_in, world, view, projection, primitive, primitiveCount);
            return;
        }

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const auto byteOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * sizeof(uint16_t));
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        glDrawElements(ToGLPrimitive(primitive), indexCount, GL_UNSIGNED_SHORT, byteOffset);
        gl4_glBindVertexArray(0);
    }

    OpenGL4RawProgram& OpenGL4GraphicsBackend::GetOrCreateSpriteProgram()
    {
        if (!spriteProgram_.IsValid())
        {
            if (!spriteProgram_.Compile(kSpriteVertSrc, kSpriteFragSrc))
                throw std::runtime_error("OpenGL4: sprite program failed to compile: " + spriteProgram_.GetError());
        }
        return spriteProgram_;
    }

    void OpenGL4GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb_in,
                                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        colored3DProgram_.Use();
        if (colored3DWvpLoc_ >= 0)
            gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        glDrawArrays(ToGLPrimitive(primitive), 0, vertexCount);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb_in, const IIndexBufferBackend& ib_in,
                                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                                              PrimitiveType primitive, int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const OpenGL4VertexBufferBackend&>(vb_in);
        const auto& ib = static_cast<const OpenGL4IndexBufferBackend&>(ib_in);

        const Matrix wvp = world * view * projection;
        float wvpCol[16];
        wvp.ToColumnMajor(wvpCol);

        colored3DProgram_.Use();
        if (colored3DWvpLoc_ >= 0)
            gl4_glUniformMatrix4fv(colored3DWvpLoc_, 1, GL_FALSE, wvpCol);

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        gl4_glBindVertexArray(vb.VaoHandle());
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.IboHandle());
        glDrawElements(ToGLPrimitive(primitive), indexCount, GL_UNSIGNED_SHORT, nullptr);
        gl4_glBindVertexArray(0);
    }

    void OpenGL4GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return; // invalid rect -- leave viewport state unchanged

        // GraphicsDevice::UpdateViewportFromWindow() calls this after every window resize (and
        // once at device creation) -- without a real override here the GL viewport is left at
        // whatever the driver's own initial default was, which 3D draws silently depend on
        // (unlike SpriteBatch's own FlushBatch, which sets glViewport() itself every flush).
        // OpenGL's viewport origin is bottom-left; convert from top-left XNA coordinates,
        // matching EasyGLGraphicsBackend::SetViewport's own fbH-based flip.
        int physW = 0, fbH = 0;
        GetPhysicalSize(physW, fbH);
        glViewport(x, fbH - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
    }

    void OpenGL4GraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

        const unsigned int sampler = samplers_[slot];
        GLint minFilter = GL_LINEAR, magFilter = GL_LINEAR;
        FilterToGL(filter, minFilter, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, AddressModeToGL(addressU));
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, AddressModeToGL(addressV));
        if (filter == 2) // Anisotropic
            gl4_glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, static_cast<float>(maxAnisotropy));

        gl4_glBindSampler(static_cast<GLuint>(slot), sampler);
    }
}

namespace CNA::Internal::Backends
{
#ifdef CNA_BACKEND_OPENGL4
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<OpenGL4::OpenGL4GraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
#endif
}
