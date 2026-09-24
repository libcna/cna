// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenGL4/OpenGL4Renderer.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace CNA::Internal::Renderers::OpenGL4::GL4;

namespace CNA::Internal::Renderers::OpenGL4
{
    namespace
    {
        // plans/plan_opengl4_modern_graphics.md GL4-0002: OpenGL4's public floor is desktop 4.1 core
        // (the highest core version macOS exposes). Newer facilities are discovered at runtime and
        // used only behind a live check.
        constexpr int kRequiredMajor = 4;
        constexpr int kRequiredMinor = 1;

        CNA::Platform::GlContextDescription RequestedContext()
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = kRequiredMajor;
            description.minorVersion = kRequiredMinor;
            description.profile = CNA::Platform::GlProfile::Core;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            return description;
        }

        // XNA Blend enum -> GL blend factor. Blend: One=0, Zero=1, SourceColor=2,
        // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
        // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        // InverseBlendFactor=11, SourceAlphaSaturation=12.
        GLenum ToGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
            case  1: return GL_ZERO;
            case  2: return GL_SRC_COLOR;
            case  3: return GL_ONE_MINUS_SRC_COLOR;
            case  4: return GL_SRC_ALPHA;
            case  5: return GL_ONE_MINUS_SRC_ALPHA;
            case  6: return GL_DST_COLOR;
            case  7: return GL_ONE_MINUS_DST_COLOR;
            case  8: return GL_DST_ALPHA;
            case  9: return GL_ONE_MINUS_DST_ALPHA;
            case 10: return GL_CONSTANT_COLOR;
            case 11: return GL_ONE_MINUS_CONSTANT_COLOR;
            case 12: return GL_SRC_ALPHA_SATURATE;
            default: return GL_ONE;
            }
        }

        // XNA BlendFunction: Add=0, Subtract=1, ReverseSubtract=2, Min=3, Max=4.
        GLenum ToGLBlendEquation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
            case 1: return GL_FUNC_SUBTRACT;
            case 2: return GL_FUNC_REVERSE_SUBTRACT;
            case 3: return GL_MIN;
            case 4: return GL_MAX;
            default: return GL_FUNC_ADD;
            }
        }

        // XNA CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5,
        // Greater=6, NotEqual=7.
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
            default: return GL_ALWAYS;
            }
        }

        // XNA StencilOperation: Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4,
        // IncrementSaturation=5, DecrementSaturation=6, Invert=7. XNA's Increment/Decrement wrap;
        // the *Saturation pair clamps -- GL's INCR_WRAP/DECR_WRAP and INCR/DECR respectively.
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
            default: return GL_KEEP;
            }
        }

        // TextureAddressMode: Wrap=0 -> GL_REPEAT, Clamp=1 -> GL_CLAMP_TO_EDGE, Mirror=2.
        GLint ToGLWrap(int mode)
        {
            switch (mode)
            {
            case 1: return GL_CLAMP_TO_EDGE;
            case 2: return GL_MIRRORED_REPEAT;
            default: return GL_REPEAT;
            }
        }

        // REMED-GFX-175: every TextureFilter ordinal names a MIP component as well as min and mag.
        // Linear is min/mag/mip LINEAR and Point is min/mag/mip POINT -- FNA's decomposition tables
        // and FNA3D's GL driver agree. Mapping Linear/Point onto plain GL_LINEAR/GL_NEAREST (as
        // this renderer did before GL4-0015) drops the mip term, so a texture with a real chain
        // never mip-filtered under the default filter every game gets. Safe because every
        // sampleable kind clamps GL_TEXTURE_MAX_LEVEL to its real level count.
        void FilterOrdinalToGL(int filter, GLint& minFilter, GLint& magFilter)
        {
            switch (filter)
            {
            case 1: minFilter = GL_NEAREST_MIPMAP_NEAREST; magFilter = GL_NEAREST; break; // Point
            case 2: minFilter = GL_LINEAR_MIPMAP_LINEAR;   magFilter = GL_LINEAR;  break; // Anisotropic
            case 3: minFilter = GL_LINEAR_MIPMAP_NEAREST;  magFilter = GL_LINEAR;  break; // LinearMipPoint
            case 4: minFilter = GL_NEAREST_MIPMAP_LINEAR;  magFilter = GL_NEAREST; break; // PointMipLinear
            case 5: minFilter = GL_LINEAR_MIPMAP_LINEAR;   magFilter = GL_NEAREST; break; // MinLinearMagPointMipLinear
            case 6: minFilter = GL_LINEAR_MIPMAP_NEAREST;  magFilter = GL_NEAREST; break; // MinLinearMagPointMipPoint
            case 7: minFilter = GL_NEAREST_MIPMAP_LINEAR;  magFilter = GL_LINEAR;  break; // MinPointMagLinearMipLinear
            case 8: minFilter = GL_NEAREST_MIPMAP_NEAREST; magFilter = GL_LINEAR;  break; // MinPointMagLinearMipPoint
            default: minFilter = GL_LINEAR_MIPMAP_LINEAR;  magFilter = GL_LINEAR;  break; // Linear
            }
        }

        // DepthFormat -> renderbuffer storage and attachment point (None has none).
        bool MapDepthFormat(int depthFormat, GLenum& outFormat, GLenum& outAttachment)
        {
            using Microsoft::Xna::Framework::Graphics::DepthFormat;
            switch (static_cast<DepthFormat>(depthFormat))
            {
            case DepthFormat::Depth16:
                outFormat = GL_DEPTH_COMPONENT16;
                outAttachment = GL_DEPTH_ATTACHMENT;
                return true;
            case DepthFormat::Depth24:
                outFormat = GL_DEPTH_COMPONENT24;
                outAttachment = GL_DEPTH_ATTACHMENT;
                return true;
            case DepthFormat::Depth24Stencil8:
                outFormat = GL_DEPTH24_STENCIL8;
                outAttachment = GL_DEPTH_STENCIL_ATTACHMENT;
                return true;
            case DepthFormat::None:
            default:
                return false;
            }
        }

        std::string FramebufferStatusName(GLenum status)
        {
            switch (status)
            {
            case GL_FRAMEBUFFER_COMPLETE: return "GL_FRAMEBUFFER_COMPLETE";
            case GL_FRAMEBUFFER_UNDEFINED: return "GL_FRAMEBUFFER_UNDEFINED";
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
            case GL_FRAMEBUFFER_UNSUPPORTED: return "GL_FRAMEBUFFER_UNSUPPORTED";
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
            case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
                return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
            default:
            {
                std::ostringstream os;
                os << "0x" << std::hex << status;
                return os.str();
            }
            }
        }

        std::string DrainGlErrorsDescribed()
        {
            std::ostringstream result;
            for (int i = 0; i < 64; ++i)
            {
                const GLenum error = glGetError();
                if (error == GL_NO_ERROR) break;
                if (result.tellp() > 0) result << ", ";
                result << "0x" << std::hex << error << std::dec;
            }
            return result.str();
        }

        /// XNA converts the signed MaxAnisotropy property to UInt32 before applying the device cap.
        float ClampedMaxAnisotropy(int maxAnisotropy, float cap)
        {
            const float requested = static_cast<float>(static_cast<std::uint32_t>(maxAnisotropy));
            float clamped = (cap > 0.0f && requested > cap) ? cap : requested;
            if (clamped < 1.0f) clamped = 1.0f;
            return clamped;
        }

        // ---- Thread-context lease (REMED: the frame and a background content load share one
        // context, so each must own it exclusively while it issues GL). -----------------------

        class OpenGL4ThreadContextLease final : public IRendererThreadContextLease
        {
        public:
            explicit OpenGL4ThreadContextLease(std::function<void()> release)
                : release_(std::move(release))
            {
            }

            ~OpenGL4ThreadContextLease() override { release_(); }

        private:
            std::function<void()> release_;
        };

        struct ThreadContextLeaseState
        {
            std::size_t depth = 0;
            CNA::Platform::GlContextBinding previousBinding;
            RendererThreadContextLeaseRelease release =
                RendererThreadContextLeaseRelease::RestorePreviousBinding;
        };

        std::unordered_map<const void*, ThreadContextLeaseState>& ThreadContextLeaseStates()
        {
            static thread_local std::unordered_map<const void*, ThreadContextLeaseState> states;
            return states;
        }

        // ---- GL debug output (A8). ------------------------------------------------------------

        const char* DebugSourceName(GLenum source)
        {
            switch (source)
            {
            case GL_DEBUG_SOURCE_API: return "api";
            case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window-system";
            case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader-compiler";
            case GL_DEBUG_SOURCE_THIRD_PARTY: return "third-party";
            case GL_DEBUG_SOURCE_APPLICATION: return "application";
            default: return "other";
            }
        }

        const char* DebugTypeName(GLenum type)
        {
            switch (type)
            {
            case GL_DEBUG_TYPE_ERROR: return "error";
            case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
            case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined-behavior";
            case GL_DEBUG_TYPE_PORTABILITY: return "portability";
            case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
            case GL_DEBUG_TYPE_MARKER: return "marker";
            default: return "other";
            }
        }

        const char* DebugSeverityName(GLenum severity)
        {
            switch (severity)
            {
            case GL_DEBUG_SEVERITY_HIGH: return "high";
            case GL_DEBUG_SEVERITY_MEDIUM: return "medium";
            case GL_DEBUG_SEVERITY_LOW: return "low";
            default: return "notification";
            }
        }

        // plans/plan_opengl4_modern_graphics.md GL4-0016: an error, undefined behaviour or a
        // high-severity message is printed on stderr with the fixed "[OpenGL4 GL Error]" prefix,
        // which the OpenGL4 CTest registrations treat as a failure (FAIL_REGULAR_EXPRESSION) -- the
        // same output-gate shape as Vulkan's "[Vulkan Validation]" gate, because a message
        // reported during teardown is out of reach of any in-process assertion. Everything else
        // (performance, portability, driver notes) is informational and goes to the log only.
        void CNA_GL4_APIENTRY DebugMessageCallback(GLenum source, GLenum type, GLuint id,
                                                   GLenum severity, GLsizei /*length*/,
                                                   const GLchar4* message,
                                                   const void* userParam)
        {
            // A shader that fails to compile is reported to its caller through the program's info
            // log (ShaderEffect's compile error, or the stock program's own "[OpenGL4 GL Error]"
            // line); a game's broken GLSL is not a renderer defect, so the compiler's copy of the
            // message stays off the error channel.
            const bool serious = source != GL_DEBUG_SOURCE_SHADER_COMPILER &&
                                 (type == GL_DEBUG_TYPE_ERROR ||
                                  type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR ||
                                  severity == GL_DEBUG_SEVERITY_HIGH);
            std::ostringstream os;
            os << (serious ? "[OpenGL4 GL Error] " : "[OpenGL4 GL Debug] ")
               << DebugSourceName(source) << '/' << DebugTypeName(type) << '/'
               << DebugSeverityName(severity) << " id=" << id << ": "
               << (message != nullptr ? message : "(null)");
            if (serious)
                CNA::Logger::Error(os.str(), CNA::LogCategory::RENDER);
            else if (userParam != nullptr)
                CNA::Logger::Debug(os.str(), CNA::LogCategory::RENDER);
        }

        [[nodiscard]] bool DebugOutputRequested()
        {
            if (const char* value = std::getenv("CNA_OPENGL4_DEBUG_OUTPUT"))
                return value[0] != '\0' && value[0] != '0';
#if defined(NDEBUG)
            return false;
#else
            return true;
#endif
        }

        /// CNA_OPENGL4_DEBUG_OUTPUT=verbose also logs the informational messages (performance,
        /// portability, driver notes); by default only the serious ones reach the log.
        [[nodiscard]] bool VerboseDebugOutputRequested()
        {
            const char* value = std::getenv("CNA_OPENGL4_DEBUG_OUTPUT");
            return value != nullptr && std::strcmp(value, "verbose") == 0;
        }
    }

    // ------------------------------------------------------------------------------------
    // GLSL adaptation
    // ------------------------------------------------------------------------------------

    std::string AdaptGlslEs300ForDesktopCore(const std::string& source)
    {
        static const std::string versionLine = "#version 300 es";
        const auto versionPos = source.find(versionLine);
        if (versionPos == std::string::npos)
            return source;
        // Only a genuine directive: the version line must start the source's first non-blank line.
        for (std::size_t i = 0; i < versionPos; ++i)
            if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r' && source[i] != '\n')
                return source;

        std::string result = source;
        const std::string replacement = "#version 410 core";
        result.replace(versionPos, versionLine.size(), replacement);

        // GLSL ES requires a default float precision and desktop GLSL does not need one. The
        // statement that immediately follows the version line is blanked -- its TEXT removed, its
        // line kept -- so every compiler diagnostic still names the line the author wrote (a
        // ShaderEffect's structured diagnostics report it). Any later precision statement is legal
        // desktop GLSL and is kept.
        std::size_t lineEnd = result.find('\n', versionPos);
        if (lineEnd == std::string::npos)
            return result;
        const std::size_t next = lineEnd + 1;
        if (result.compare(next, 10, "precision ") == 0)
        {
            const std::size_t precisionEnd = result.find('\n', next);
            result.erase(next, precisionEnd == std::string::npos ? std::string::npos
                                                                 : precisionEnd - next);
        }
        return result;
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

    namespace
    {
        std::string ShaderLog(GLuint shader)
        {
            GLint length = 0;
            gl4_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            gl4_glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            log.resize(std::strlen(log.c_str()));
            return log;
        }

        std::string ProgramLog(GLuint program)
        {
            GLint length = 0;
            gl4_glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            gl4_glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            log.resize(std::strlen(log.c_str()));
            return log;
        }

        GLuint CompileStage(GLenum stage, const std::string& source, std::string& error)
        {
            const GLuint shader = gl4_glCreateShader(stage);
            const char* text = source.c_str();
            gl4_glShaderSource(shader, 1, &text, nullptr);
            gl4_glCompileShader(shader);
            GLint ok = 0;
            gl4_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                // The driver's own log carries "0:LINE(COL)" positions; the stage prefix is what
                // ShaderDiagnosticEXT::parseCompilerLog keys the stage on.
                error = std::string(stage == GL_VERTEX_SHADER ? "VS: " : "FS: ") + ShaderLog(shader);
                gl4_glDeleteShader(shader);
                return 0;
            }
            return shader;
        }
    }

    bool OpenGL4RawProgram::Compile(const std::string& vertSrc, const std::string& fragSrc)
    {
        Destroy();
        error_.clear();

        const GLuint vs = CompileStage(GL_VERTEX_SHADER, vertSrc, error_);
        if (vs == 0) return false;
        const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragSrc, error_);
        if (fs == 0)
        {
            gl4_glDeleteShader(vs);
            return false;
        }

        const GLuint prog = gl4_glCreateProgram();
        gl4_glAttachShader(prog, vs);
        gl4_glAttachShader(prog, fs);
        gl4_glLinkProgram(prog);
        GLint linkOk = 0;
        gl4_glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
        gl4_glDeleteShader(vs);
        gl4_glDeleteShader(fs);
        if (!linkOk)
        {
            error_ = std::string("Link: ") + ProgramLog(prog);
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
        if (program_ == 0) return -1;
        return gl4_glGetUniformLocation(program_, name);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4EffectRenderer
    // ------------------------------------------------------------------------------------

    bool OpenGL4EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        rtFlipVUploaded_ = false;
        return program_.Compile(AdaptGlslEs300ForDesktopCore(vertSrc),
                                AdaptGlslEs300ForDesktopCore(fragSrc));
    }

    void OpenGL4EffectRenderer::Bind()
    {
        if (program_.IsValid())
            program_.Use();
    }

    void OpenGL4EffectRenderer::MakeProgramCurrent()
    {
        if (program_.IsValid())
            program_.Use();
    }

    void OpenGL4EffectRenderer::Unbind()
    {
        // The next bind or sprite flush installs its own program.
    }

    bool OpenGL4EffectRenderer::IsValid() const
    {
        return program_.IsValid();
    }

    std::string OpenGL4EffectRenderer::GetCompileError() const
    {
        return program_.GetError();
    }

    void OpenGL4EffectRenderer::SetUniformFloat(const char* name, float value)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform1f(loc, value);
    }

    void OpenGL4EffectRenderer::SetUniformInt(const char* name, int value)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform1i(loc, value);
    }

    void OpenGL4EffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform2f(loc, x, y);
    }

    void OpenGL4EffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform3f(loc, x, y, z);
    }

    void OpenGL4EffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniform4f(loc, x, y, z, w);
    }

    void OpenGL4EffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        MakeProgramCurrent();
        const int loc = program_.UniformLocation(name);
        if (loc >= 0) gl4_glUniformMatrix4fv(loc, 1, GL_FALSE, matrix);
    }

    int OpenGL4EffectRenderer::ArrayUniformLocation(const char* name) const
    {
        // GLSL names an array uniform by its first element; whether a driver also accepts the bare
        // array name is the driver's choice. Asking for both is the difference between an SSAO
        // kernel that occludes and one that silently stays at the origin.
        const int direct = program_.UniformLocation(name);
        if (direct >= 0) return direct;
        return program_.UniformLocation((std::string(name) + "[0]").c_str());
    }

    void OpenGL4EffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        MakeProgramCurrent();
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0 && count > 0) gl4_glUniform1fv(loc, count, values);
    }

    void OpenGL4EffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        MakeProgramCurrent();
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0 && count > 0) gl4_glUniform2fv(loc, count, values);
    }

    void OpenGL4EffectRenderer::SetUniformVec3Array(const char* name, const float* values, int count)
    {
        MakeProgramCurrent();
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0 && count > 0) gl4_glUniform3fv(loc, count, values);
    }

    void OpenGL4EffectRenderer::SetUniformMat4Array(const char* name, const float* matrices, int count)
    {
        MakeProgramCurrent();
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0 && count > 0) gl4_glUniformMatrix4fv(loc, count, GL_FALSE, matrices);
    }

    void OpenGL4EffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        gl4_glActiveTexture(GL_TEXTURE0);

        // REMED-GFX-147: a custom ShaderEffect's GLSL belongs to the game, so the renderer cannot
        // rewrite its sampling -- but it can say what is being sampled. A shader that declares
        // `uniform vec4 uRtFlipV;` gets the same render-target orientation flag the stock effects
        // get; one that does not pays nothing.
        if (unit >= 0 && unit < 4)
        {
            const float flip = SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
            if (rtFlipV_[unit] != flip || !rtFlipVUploaded_)
            {
                rtFlipV_[unit] = flip;
                MakeProgramCurrent();
                const int loc = program_.UniformLocation("uRtFlipV");
                if (loc >= 0)
                {
                    gl4_glUniform4f(loc, rtFlipV_[0], rtFlipV_[1], rtFlipV_[2], rtFlipV_[3]);
                    rtFlipVUploaded_ = true;
                }
            }
        }
    }

    void OpenGL4EffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        gl4_glActiveTexture(GL_TEXTURE0);
    }

    void OpenGL4EffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        gl4_glActiveTexture(GL_TEXTURE0);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4OcclusionQueryRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4OcclusionQueryRenderer::OpenGL4OcclusionQueryRenderer()
    {
        gl4_glGenQueries(1, &query_);
    }

    OpenGL4OcclusionQueryRenderer::~OpenGL4OcclusionQueryRenderer()
    {
        if (query_ != 0)
            gl4_glDeleteQueries(1, &query_);
    }

    void OpenGL4OcclusionQueryRenderer::Begin()
    {
        if (query_ == 0) return;
        resultCached_ = false;
        issued_ = false;
        gl4_glBeginQuery(GL_SAMPLES_PASSED, query_);
    }

    void OpenGL4OcclusionQueryRenderer::End()
    {
        if (query_ == 0) return;
        gl4_glEndQuery(GL_SAMPLES_PASSED);
        // A query that has ended always becomes available, including one no fragment reached.
        issued_ = true;
    }

    bool OpenGL4OcclusionQueryRenderer::IsComplete() const
    {
        if (query_ == 0 || !issued_) return false;
        if (resultCached_) return true;
        GLuint available = 0;
        gl4_glGetQueryObjectuiv(query_, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) return false;
        GLuint result = 0;
        gl4_glGetQueryObjectuiv(query_, GL_QUERY_RESULT, &result);
        cachedResult_ = static_cast<int>(result);
        resultCached_ = true;
        return true;
    }

    int OpenGL4OcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete()) return 0;
        return cachedResult_;
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4VertexBufferRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4VertexBufferRenderer::OpenGL4VertexBufferRenderer(int vertexCapacity)
        : capacity_(vertexCapacity)
    {
        gl4_glGenVertexArrays(1, &vao_);
        gl4_glGenBuffers(1, &vbo_);
    }

    OpenGL4VertexBufferRenderer::~OpenGL4VertexBufferRenderer()
    {
        if (vbo_ != 0) gl4_glDeleteBuffers(1, &vbo_);
        if (vao_ != 0) gl4_glDeleteVertexArrays(1, &vao_);
    }

    namespace
    {
        struct VertexAttribFormat
        {
            int componentCount;
            GLenum type;
            bool normalized;
        };

        // Task 1080 / SOFTWARE-130 / FX-127: XNA's VertexElementFormat -> GL attribute shape. Every
        // stock and effect input is a floating-point shader register, so all formats -- including
        // Byte4 BLENDINDICES, which XNA also permits as Vector4 -- bind through the converting
        // float path; the skinned programs cast to int when they index the palette.
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:           return { 1, GL_FLOAT,         false };
            case VertexElementFormat::Vector2:          return { 2, GL_FLOAT,         false };
            case VertexElementFormat::Vector3:          return { 3, GL_FLOAT,         false };
            case VertexElementFormat::Vector4:          return { 4, GL_FLOAT,         false };
            case VertexElementFormat::Color:            return { 4, GL_UNSIGNED_BYTE, true  };
            case VertexElementFormat::Byte4:            return { 4, GL_UNSIGNED_BYTE, false };
            case VertexElementFormat::Short2:           return { 2, GL_SHORT,         false };
            case VertexElementFormat::Short4:           return { 4, GL_SHORT,         false };
            case VertexElementFormat::NormalizedShort2: return { 2, GL_SHORT,         true  };
            case VertexElementFormat::NormalizedShort4: return { 4, GL_SHORT,         true  };
            case VertexElementFormat::HalfVector2:      return { 2, GL_HALF_FLOAT,    false };
            case VertexElementFormat::HalfVector4:      return { 4, GL_HALF_FLOAT,    false };
            }
            throw System::NotSupportedException(
                "OpenGL4: the VertexDeclaration contains an unknown VertexElementFormat.");
        }

        void SetFloatAttribute(GLuint location, int size, GLenum type, bool normalized,
                               std::size_t stride, std::size_t offset)
        {
            gl4_glEnableVertexAttribArray(location);
            gl4_glVertexAttribPointer(location, size, type, normalized ? GL_TRUE : GL_FALSE,
                                      static_cast<GLsizei>(stride),
                                      reinterpret_cast<const void*>(offset));
        }
    }

    void OpenGL4VertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declarationElements_ = vertexDeclaration.GetVertexElements();
    }

    void OpenGL4VertexBufferRenderer::ApplyLayout(std::size_t stride)
    {
        gl4_glBindVertexArray(vao_);
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        // The VAO starts from a known state: every location off and every divisor zero, so a
        // previous declaration or draw can never leave a location this layout does not name enabled.
        for (GLuint location = 0; location < 16; ++location)
        {
            gl4_glDisableVertexAttribArray(location);
            gl4_glVertexAttribDivisor(location, 0);
        }

        if (!declarationElements_.empty())
        {
            // Task 1080: generic layout from the caller's own VertexDeclaration -- attribute
            // location N is element N, the "layout(location=N) == Nth field of the ported HLSL
            // input struct" convention every custom effect relies on.
            for (std::size_t i = 0; i < declarationElements_.size() && i < 16; ++i)
            {
                const VertexElement& element = declarationElements_[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                SetFloatAttribute(static_cast<GLuint>(i), desc.componentCount, desc.type,
                                  desc.normalized, stride,
                                  static_cast<std::size_t>(element.getOffsetProperty()));
            }
            gl4_glBindVertexArray(0);
            return;
        }

        const std::size_t s = stride;
        switch (stride)
        {
        case 16:  // VertexPositionColor
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 4, GL_UNSIGNED_BYTE, true, s, 12);
            break;
        case 20:  // VertexPositionTexture
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 2, GL_FLOAT, false, s, 12);
            break;
        case 24:  // VertexPositionColorTexture
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 4, GL_UNSIGNED_BYTE, true, s, 12);
            SetFloatAttribute(2, 2, GL_FLOAT, false, s, 16);
            break;
        case 32:  // VertexPositionNormalTexture
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 2, GL_FLOAT, false, s, 24);
            break;
        case 48:  // VertexPositionNormalTangentTexture (PbrEffect)
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 4, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 2, GL_FLOAT, false, s, 40);
            break;
        case 60:  // GLTF-182/183/462: rigid PBR dual-UV record with packed COLOR_0
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 4, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 2, GL_FLOAT, false, s, 40);
            SetFloatAttribute(4, 2, GL_FLOAT, false, s, 48);
            SetFloatAttribute(5, 4, GL_UNSIGNED_BYTE, true, s, 56);
            break;
        case 52:  // VertexPositionNormalTextureSkinned
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 2, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 4, GL_FLOAT, false, s, 32);
            SetFloatAttribute(4, 4, GL_UNSIGNED_BYTE, false, s, 48);
            break;
        case 56:  // CNB-67: skinned record with a trailing colour
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 2, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 4, GL_FLOAT, false, s, 32);
            SetFloatAttribute(4, 4, GL_UNSIGNED_BYTE, false, s, 48);
            SetFloatAttribute(5, 4, GL_UNSIGNED_BYTE, true, s, 52);
            break;
        case 68:  // PBR + skinning
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 4, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 2, GL_FLOAT, false, s, 40);
            SetFloatAttribute(4, 4, GL_FLOAT, false, s, 48);
            SetFloatAttribute(5, 4, GL_UNSIGNED_BYTE, false, s, 64);
            break;
        case 76:  // GLTF-182/183: skinned PBR with UV1
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 4, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 2, GL_FLOAT, false, s, 40);
            SetFloatAttribute(4, 4, GL_FLOAT, false, s, 48);
            SetFloatAttribute(5, 4, GL_UNSIGNED_BYTE, false, s, 64);
            SetFloatAttribute(6, 2, GL_FLOAT, false, s, 68);
            break;
        case 80:  // GLTF-463: skinned PBR with UV1 and COLOR_0
            SetFloatAttribute(0, 3, GL_FLOAT, false, s, 0);
            SetFloatAttribute(1, 3, GL_FLOAT, false, s, 12);
            SetFloatAttribute(2, 4, GL_FLOAT, false, s, 24);
            SetFloatAttribute(3, 2, GL_FLOAT, false, s, 40);
            SetFloatAttribute(4, 4, GL_FLOAT, false, s, 48);
            SetFloatAttribute(5, 4, GL_UNSIGNED_BYTE, false, s, 64);
            SetFloatAttribute(6, 2, GL_FLOAT, false, s, 68);
            SetFloatAttribute(7, 4, GL_UNSIGNED_BYTE, true, s, 76);
            break;
        default:
            // GLTF-157: a stride does not describe which attributes exist. Binding an unknown
            // record as position-only left the other locations stale and rendered normals, UVs or
            // skin weights from unrelated buffers. A custom layout reaches the declaration path.
            gl4_glBindVertexArray(0);
            throw System::NotSupportedException(
                "OpenGL4VertexBufferRenderer::ApplyLayout: unsupported vertex stride " +
                std::to_string(stride) +
                " without a VertexDeclaration; the upload is refused rather than bound as "
                "position-only.");
        }
        gl4_glBindVertexArray(0);
    }

    void OpenGL4VertexBufferRenderer::Upload(const void* data, std::size_t byteCount,
                                             SetDataOptions options)
    {
        gl4_glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // FX-131: storage is sized by the buffer's CAPACITY. XNA fixes a VertexBuffer's VertexCount
        // at construction and SetData writes a PREFIX, so a short upload must not shrink what a
        // later draw is entitled to read.
        const std::size_t total = static_cast<std::size_t>(std::max(capacity_, 0)) * strideInBytes_;
        if (options == SetDataOptions::Discard)
        {
            // Orphan: new storage without waiting for draws still reading the old contents.
            gl4_glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr4>(std::max(total, byteCount)),
                             nullptr, GL_DYNAMIC_DRAW);
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr4>(byteCount), data);
            gpuAllocated_ = true;
        }
        else if (options == SetDataOptions::NoOverwrite && gpuAllocated_)
        {
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr4>(byteCount), data);
        }
        else if (total > byteCount)
        {
            gl4_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr4>(total), nullptr,
                             GL_DYNAMIC_DRAW);
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr4>(byteCount), data);
            gpuAllocated_ = true;
        }
        else
        {
            gl4_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr4>(byteCount), data,
                             GL_DYNAMIC_DRAW);
            gpuAllocated_ = true;
        }
    }

    void OpenGL4VertexBufferRenderer::SetData(const void* data, int vertex_count,
                                              std::size_t stride_in_bytes)
    {
        SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
    }

    void OpenGL4VertexBufferRenderer::SetDataWithOptions(const void* data, int vertex_count,
                                                         std::size_t stride_in_bytes,
                                                         SetDataOptions options)
    {
        vertexCount_ = vertex_count;
        strideInBytes_ = stride_in_bytes;
        const std::size_t byteCount =
            static_cast<std::size_t>(std::max(vertex_count, 0)) * stride_in_bytes;
        Upload(data, byteCount, options);
        ApplyLayout(stride_in_bytes);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4IndexBufferRenderer
    // ------------------------------------------------------------------------------------

    OpenGL4IndexBufferRenderer::OpenGL4IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit)
        : capacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
        gl4_glGenBuffers(1, &ibo_);
    }

    OpenGL4IndexBufferRenderer::~OpenGL4IndexBufferRenderer()
    {
        if (ibo_ != 0) gl4_glDeleteBuffers(1, &ibo_);
    }

    void OpenGL4IndexBufferRenderer::Upload(const void* data, int indexCount,
                                            std::size_t elementSize, SetDataOptions options)
    {
        indexCount_ = indexCount;
        const std::size_t byteCount = static_cast<std::size_t>(std::max(indexCount, 0)) * elementSize;
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (bytes != nullptr)
            cpuData_.assign(bytes, bytes + byteCount);
        else
            cpuData_.assign(byteCount, 0);

        // GL_ELEMENT_ARRAY_BUFFER is VAO state in a core profile; upload with no VAO bound so this
        // cannot rebind another buffer's element array.
        gl4_glBindVertexArray(0);
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        const std::size_t total = static_cast<std::size_t>(std::max(capacity_, 0)) * elementSize;
        if (options == SetDataOptions::Discard)
        {
            gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr4>(std::max(total, byteCount)), nullptr,
                             GL_DYNAMIC_DRAW);
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                                    static_cast<GLsizeiptr4>(byteCount), data);
            gpuAllocated_ = true;
        }
        else if (options == SetDataOptions::NoOverwrite && gpuAllocated_)
        {
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                                    static_cast<GLsizeiptr4>(byteCount), data);
        }
        else if (total > byteCount)
        {
            gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr4>(total), nullptr,
                             GL_DYNAMIC_DRAW);
            if (byteCount > 0)
                gl4_glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                                    static_cast<GLsizeiptr4>(byteCount), data);
            gpuAllocated_ = true;
        }
        else
        {
            gl4_glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr4>(byteCount), data,
                             GL_DYNAMIC_DRAW);
            gpuAllocated_ = true;
        }
        gl4_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void OpenGL4IndexBufferRenderer::SetData16(const void* data, int index_count)
    {
        SetData16WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferRenderer::SetData16WithOptions(const void* data, int index_count,
                                                          SetDataOptions options)
    {
        Upload(data, index_count, sizeof(std::uint16_t), options);
    }

    void OpenGL4IndexBufferRenderer::SetData32(const void* data, int index_count)
    {
        SetData32WithOptions(data, index_count, SetDataOptions::None);
    }

    void OpenGL4IndexBufferRenderer::SetData32WithOptions(const void* data, int index_count,
                                                          SetDataOptions options)
    {
        Upload(data, index_count, sizeof(std::uint32_t), options);
    }

    // ------------------------------------------------------------------------------------
    // OpenGL4Renderer -- context, presentation, capabilities
    // ------------------------------------------------------------------------------------

    OpenGL4Renderer::OpenGL4Renderer(const GraphicsRendererCreateArgs& args)
        : platformContext_(std::make_shared<PlatformGlContextOwner>(
              RequirePlatformGlContext(args.glContext, "OPENGL4"),
              RequirePlatformGlWindow(args.surface, "OPENGL4"), RequestedContext()))
        , threadContextLeaseControl_(std::make_shared<ThreadContextLeaseControl>())
        , surfaceState_(args.surface, args.virtualWidth, args.virtualHeight, args.presentationMode)
        , swapInterval_(args.swapInterval)
        , sampleCount_(args.multiSampleCount > 1 ? args.multiSampleCount : 1)
        , backBufferDepthFormat_(args.depthStencilFormat)
    {
        threadContextLeaseControl_->platformContext = platformContext_;
        bound_->depthFormat = backBufferDepthFormat_;

        if (!GL4::LoadGL4Functions(platformContext_->GetLoader()))
            throw std::runtime_error("OpenGL4: failed to resolve required GL 4.1 core entry points");

        // A7: the platform may grant something other than what was asked (a GLX fallback context,
        // a compatibility profile). Report the context OPENGL4 actually got and refuse anything
        // below the 4.1 core floor, rather than quietly running as some other GL.
        GLint major = 0, minor = 0, profileMask = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        const auto* versionString = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const auto* rendererString = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const bool versionOk = major > kRequiredMajor ||
                               (major == kRequiredMajor && minor >= kRequiredMinor);
        const bool coreProfile = (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
        if (!versionOk || !coreProfile)
        {
            throw std::runtime_error(
                std::string("OpenGL4: this renderer requires a desktop OpenGL ") +
                std::to_string(kRequiredMajor) + "." + std::to_string(kRequiredMinor) +
                " core profile context; the platform granted OpenGL " + std::to_string(major) +
                "." + std::to_string(minor) + (coreProfile ? " core" : " non-core") + " (\"" +
                (versionString != nullptr ? versionString : "unknown") + "\")");
        }

        modernCapabilities_ = GL4::DiscoverModernCapabilities(platformContext_->GetLoader());
        EnableDebugOutput();

        // GLB-40: desktop core requires this explicitly or gl_PointSize is ignored for GL_POINTS.
        glEnable(GL_PROGRAM_POINT_SIZE);

        // Wine's 63/128-pixel displacement is used unless the rasterizer's subpixel precision
        // cannot represent it below half a pixel.
        GLint subpixelBits = 0;
        glGetIntegerv(GL_SUBPIXEL_BITS, &subpixelBits);
        if (subpixelBits > 1 && subpixelBits < 24)
        {
            const float representableBelowHalf = 1.0f - std::ldexp(1.0f, 1 - subpixelBits);
            xnaPixelCenterScale_ = std::min(xnaPixelCenterScale_, representableBelowHalf);
        }

        GLint maxDrawBuffers = 1, maxColorAttachments = 1;
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
        maxMrtTargets_ = std::max(1, std::min({4, static_cast<int>(maxDrawBuffers),
                                              static_cast<int>(maxColorAttachments)}));

        // Anisotropic filtering: core in 4.6, an extension before it. The ceiling is queried once;
        // a raised GL_INVALID_ENUM means the driver has neither.
        DrainGlErrors();
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        if (GlOperationSucceeded() && maxAniso > 1.0f)
            maxAnisotropy_ = maxAniso;

        // A startup diagnostic belongs in the logger (stderr), never on the program's stdout.
        CNA::Logger::Info(std::string("OpenGL4Renderer initialized with OpenGL ") +
                              (versionString != nullptr ? versionString : "(unknown)") + " on " +
                              (rendererString != nullptr ? rendererString : "(unknown)") +
                              (debugOutputEnabled_ ? " (GL debug output on)" : ""),
                          CNA::LogCategory::RENDER);

        platformContext_->SetSwapInterval(swapInterval_);

        gl4_glGenSamplers(kMaxSamplerSlots, samplers_);
        for (int slot = 0; slot < kMaxSamplerSlots; ++slot)
            gl4_glBindSampler(static_cast<GLuint>(slot), samplers_[slot]);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        if (sampleCount_ > 1)
        {
            int physW = 0, physH = 0;
            surfaceState_.GetDrawableSize(physW, physH);
            CreateMsaaBuffers(physW, physH);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        }
        IGraphicsRenderer::RegisterForWindow(surfaceState_.GetWindowId(), this);
    }

    OpenGL4Renderer::~OpenGL4Renderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surfaceState_.GetWindowId());
        try
        {
            platformContext_->MakeCurrent();
        }
        catch (...)
        {
        }
        gl4_glDeleteSamplers(kMaxSamplerSlots, samplers_);
        for (unsigned int* texture : {&defaultWhiteTexture_, &defaultBlackTexture_,
                                      &defaultBlackCubeTexture_, &defaultFlatNormalTexture_})
            if (*texture) glDeleteTextures(1, texture);
        if (negativeBaseVertexIbo_) gl4_glDeleteBuffers(1, &negativeBaseVertexIbo_);
        if (mrtFbo_) gl4_glDeleteFramebuffers(1, &mrtFbo_);
        DestroyMsaaBuffers();
        // Programs are members and release while the context (declared first) is still current.
    }

    void OpenGL4Renderer::EnableDebugOutput()
    {
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        const bool debugContext = (flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0;
        if (gl4_glDebugMessageCallback == nullptr || gl4_glDebugMessageControl == nullptr)
            return;
        if (!debugContext && !DebugOutputRequested())
            return;
        glEnable(GL_DEBUG_OUTPUT);
        // Synchronous, so the message is reported on the thread and inside the call that caused it.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        // The user parameter only carries the verbosity: non-null logs informational messages too.
        static const int kVerbose = 1;
        const bool verbose = VerboseDebugOutputRequested();
        gl4_glDebugMessageCallback(&DebugMessageCallback, verbose ? &kVerbose : nullptr);
        gl4_glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        if (!verbose)
            gl4_glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0,
                                      nullptr, GL_FALSE);
        // Debug groups are this renderer's own markers, not diagnostics.
        gl4_glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 0, nullptr,
                                  GL_FALSE);
        gl4_glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 0, nullptr,
                                  GL_FALSE);
        debugOutputEnabled_ = true;
    }

    void OpenGL4Renderer::EnsureCallingThreadContext()
    {
        // ContentManager may create graphics resources on a loading thread (the XNA Marble Maze
        // sample does), so the device context is made current on the calling thread first. The
        // loader's function pointers are process-wide, so nothing else needs initialising.
        platformContext_->MakeCurrent();
    }

    std::unique_ptr<IRendererThreadContextLease> OpenGL4Renderer::AcquireThreadContextLeaseEXT(
        const RendererThreadContextLeaseRelease release)
    {
        // A mutual exclusion between a frame and a background content load: both issue GL through
        // one context, and a load's bind/upload pair split by the frame's own binds lands on
        // whatever the frame bound.
        const auto control = threadContextLeaseControl_;
        control->mutex.lock();
        const auto releaseLease = [control]() noexcept {
            auto& states = ThreadContextLeaseStates();
            const auto it = states.find(control.get());
            if (it == states.end() || it->second.depth == 0)
            {
                CNA::Logger::Error("OpenGL4 renderer context lease released without matching "
                                   "acquisition", CNA::LogCategory::RENDER);
                return;
            }
            --it->second.depth;
            if (it->second.depth == 0)
            {
                try
                {
                    control->platformContext->RestoreBinding(it->second.previousBinding,
                                                             it->second.release);
                }
                catch (const std::exception& error)
                {
                    CNA::Logger::Error(std::string("Failed to release OpenGL4 context ownership: ") +
                                           error.what(), CNA::LogCategory::RENDER);
                }
                states.erase(it);
            }
            control->mutex.unlock();
        };
        try
        {
            auto& state = ThreadContextLeaseStates()[control.get()];
            if (state.depth == 0)
            {
                state.previousBinding = control->platformContext->GetCurrentBinding();
                state.release = release;
                EnsureCallingThreadContext();
            }
            ++state.depth;
        }
        catch (...)
        {
            ThreadContextLeaseStates().erase(control.get());
            control->mutex.unlock();
            throw;
        }
        try
        {
            return std::make_unique<OpenGL4ThreadContextLease>(releaseLease);
        }
        catch (...)
        {
            releaseLease();
            throw;
        }
    }

    void OpenGL4Renderer::DestroyMsaaBuffers()
    {
        if (msaaDepthRbo_) gl4_glDeleteRenderbuffers(1, &msaaDepthRbo_);
        if (msaaColorRbo_) gl4_glDeleteRenderbuffers(1, &msaaColorRbo_);
        if (msaaFbo_) gl4_glDeleteFramebuffers(1, &msaaFbo_);
        msaaDepthRbo_ = msaaColorRbo_ = msaaFbo_ = 0;
        msaaW_ = msaaH_ = 0;
        msaaStorageDepthFormat_ = -1;
    }

    void OpenGL4Renderer::CreateMsaaBuffers(int w, int h)
    {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        if (maxSamples > 0 && sampleCount_ > static_cast<int>(maxSamples))
            sampleCount_ = static_cast<int>(maxSamples);

        // SOFTWARE-181: the attachment set is rebuilt atomically so a Reset can add or remove
        // depth/stencil without leaving a stale attachment behind.
        DestroyMsaaBuffers();
        gl4_glGenFramebuffers(1, &msaaFbo_);
        gl4_glGenRenderbuffers(1, &msaaColorRbo_);
        gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRbo_);
        gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount_, GL_RGBA8, w, h);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                      msaaColorRbo_);
        GLenum depthStorage = 0, depthAttachment = 0;
        if (MapDepthFormat(backBufferDepthFormat_, depthStorage, depthAttachment))
        {
            gl4_glGenRenderbuffers(1, &msaaDepthRbo_);
            gl4_glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRbo_);
            gl4_glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount_, depthStorage, w, h);
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER,
                                          msaaDepthRbo_);
        }
        gl4_glBindRenderbuffer(GL_RENDERBUFFER, 0);
        const GLenum status = gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGL4: multisample backbuffer is incomplete (" +
                                     FramebufferStatusName(status) + ") for DepthFormat ordinal " +
                                     std::to_string(backBufferDepthFormat_));
        msaaW_ = w;
        msaaH_ = h;
        msaaStorageDepthFormat_ = backBufferDepthFormat_;
    }

    int OpenGL4Renderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        EnsureCallingThreadContext();
        int newSampleCount = 1;
        if (requestedMultiSampleCount > 1)
        {
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            int candidate = 1;
            while (candidate <= requestedMultiSampleCount / 2) candidate *= 2;
            if (maxSamples > 1)
                newSampleCount = std::min(candidate, static_cast<int>(maxSamples));
        }
        if (newSampleCount == sampleCount_)
        {
            if (bound_->height == 0) BindDefaultFramebuffer();
            return GetMultiSampleCount();
        }
        sampleCount_ = newSampleCount;
        DestroyMsaaBuffers();
        if (bound_->height == 0)
        {
            if (sampleCount_ > 1)
            {
                int physW = 0, physH = 0;
                surfaceState_.GetDrawableSize(physW, physH);
                CreateMsaaBuffers(physW, physH);
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
            }
            else
            {
                gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
        }
        ApplyCurrentDepthStencilAvailability();
        return GetMultiSampleCount();
    }

    void OpenGL4Renderer::BindDefaultFramebuffer()
    {
        if (sampleCount_ > 1)
        {
            int physW = 0, physH = 0;
            surfaceState_.GetDrawableSize(physW, physH);
            if (msaaFbo_ == 0 || physW != msaaW_ || physH != msaaH_ ||
                msaaStorageDepthFormat_ != backBufferDepthFormat_)
                CreateMsaaBuffers(physW, physH);
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        }
        else
        {
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void OpenGL4Renderer::ResolveMsaa()
    {
        if (sampleCount_ <= 1 || msaaFbo_ == 0) return;
        const ScopedScissorTestDisabled fullSurface;
        gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        gl4_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl4_glBlitFramebuffer(0, 0, msaaW_, msaaH_, 0, 0, msaaW_, msaaH_, GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
    }

    bool OpenGL4Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD: return true;
        case CNA::GraphicsCapability::DepthStencilBuffer: return true;
        // Backbuffer and render-target MSAA; GL 4.x core guarantees GL_MAX_SAMPLES >= 4.
        case CNA::GraphicsCapability::MultiSampleAntiAliasing: return true;
        // Up to four independently writable targets, the XNA/FNA ceiling (GL4-0012).
        case CNA::GraphicsCapability::MultipleRenderTargets: return true;
        // Driver-granted: an extension before GL 4.6.
        case CNA::GraphicsCapability::AnisotropicFiltering: return maxAnisotropy_ > 1.0f;
        // Native glPolygonMode, which keeps culling, clipping, depth bias, MSAA and two-sided
        // stencil across every triangle route -- what a GL_LINES expansion cannot promise.
        case CNA::GraphicsCapability::WireFrame: return true;
        // GL_SAMPLES_PASSED with an exact passed-sample count.
        case CNA::GraphicsCapability::OcclusionQuery: return true;
        case CNA::GraphicsCapability::CustomEffects: return true;
        case CNA::GraphicsCapability::Texture3D: return true;
        // GL4-0013: every per-vertex stream is bound at its own locations with its own VBO,
        // stride and offset, exactly as EasyGL's REMED-GFX-201 route does.
        case CNA::GraphicsCapability::MultiStreamVertexInput: return true;
        case CNA::GraphicsCapability::Instancing: return true;
        case CNA::GraphicsCapability::StencilBuffer: return true;
        case CNA::GraphicsCapability::AdditiveBlending: return true;
        // Derived by GraphicsDevice from the dedicated renderer queries (SupportsCompiledEffects,
        // the render-target format probe, SupportsHalfFloatTextureLinearFilteringEXT,
        // SupportsComputeShadersEXT, SupportsIndirectDrawEXT); this switch is never asked.
        case CNA::GraphicsCapability::CompiledEffects:
        case CNA::GraphicsCapability::FloatRenderTargets:
        case CNA::GraphicsCapability::HalfFloatRenderTargets:
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
        case CNA::GraphicsCapability::ComputeShaders:
        case CNA::GraphicsCapability::IndirectDraw:
            return false;
        }
        return false;
    }

    bool OpenGL4Renderer::SupportsShaderLanguageEXT(const int language, const int stage) const
    {
        if (language != static_cast<int>(CNA::ShaderLanguageEXT::GlslDesktop))
            return false;
        return stage == static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
               stage == static_cast<int>(CNA::ShaderStageEXT::Fragment);
    }

    void OpenGL4Renderer::Present()
    {
        if (sampleCount_ > 1) ResolveMsaa();
        platformContext_->SwapBuffers();
        if (sampleCount_ > 1 && bound_->height == 0)
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    }

    void OpenGL4Renderer::SetVirtualResolution(int width, int height)
    {
        surfaceState_.SetVirtualResolution(width, height);
    }

    void OpenGL4Renderer::SetPresentationMode(int mode)
    {
        surfaceState_.SetPresentationMode(static_cast<CnaPresentationMode>(mode));
    }

    void OpenGL4Renderer::SetSwapInterval(int interval)
    {
        // Recorded as well as forwarded: whether the driver honours an interval is the driver's
        // business; whether CNA asked for it is this renderer's (REMED-GFX-243).
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    void OpenGL4Renderer::UpdatePresentationFormatEXT(int backBufferFormat, int depthStencilFormat,
                                                      bool isFullScreen)
    {
        (void)backBufferFormat;
        (void)isFullScreen;
        if (depthStencilFormat < 0 || depthStencilFormat > 3)
            throw std::out_of_range("OpenGL4: invalid DepthFormat ordinal");
        backBufferDepthFormat_ = depthStencilFormat;
        if (bound_->height == 0)
        {
            bound_->depthFormat = backBufferDepthFormat_;
            BindDefaultFramebuffer();
            ApplyCurrentDepthStencilAvailability();
            ApplyCurrentDepthBias();
        }
    }

    void OpenGL4Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != surfaceState_.GetWindowId())
        {
            throw CNA::Platform::PlatformException("OpenGL4Renderer::OnSurfaceChanged",
                                                   "a renderer's platform window identity cannot change");
        }
        surfaceState_.Update(surface);
    }

    void OpenGL4Renderer::GetPhysicalSize(int& width, int& height) const
    {
        surfaceState_.GetDrawableSize(width, height);
    }

    void OpenGL4Renderer::GetLogicalSize(int& width, int& height) const
    {
        surfaceState_.GetLogicalSize(width, height);
    }

    void OpenGL4Renderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void OpenGL4Renderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        surfaceState_.GetDefaultViewportRect(x, y, width, height);
    }

    bool OpenGL4Renderer::TransformWindowToLogical(float windowX, float windowY,
                                                   float& logX, float& logY) const
    {
        return surfaceState_.WindowToLogical(windowX, windowY, logX, logY);
    }

    bool OpenGL4Renderer::TransformLogicalToWindow(float logX, float logY,
                                                   float& windowX, float& windowY) const
    {
        return surfaceState_.LogicalToWindow(logX, logY, windowX, windowY);
    }

    bool OpenGL4Renderer::GetBoundRenderTargetSize(int& width, int& height) const
    {
        if (!bound_->rt2D && !bound_->cube && bound_->mrtCount == 0) return false;
        width = bound_->width;
        height = bound_->height;
        return true;
    }

    std::unique_ptr<ISpriteBatchRenderer> OpenGL4Renderer::CreateSpriteBatch()
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4SpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IOcclusionQueryRenderer> OpenGL4Renderer::CreateOcclusionQuery()
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4OcclusionQueryRenderer>();
    }

    std::unique_ptr<IEffectRenderer> OpenGL4Renderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        EnsureCallingThreadContext();
        auto renderer = std::make_unique<OpenGL4EffectRenderer>();
        renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    std::unique_ptr<IVertexBufferRenderer> OpenGL4Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4VertexBufferRenderer>(vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGL4Renderer::CreateIndexBuffer16(int index_capacity)
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4IndexBufferRenderer>(index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenGL4Renderer::CreateIndexBuffer32(int index_capacity)
    {
        EnsureCallingThreadContext();
        return std::make_unique<OpenGL4IndexBufferRenderer>(index_capacity, true);
    }

    void OpenGL4Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0 || pixels == nullptr) return;
        GLint previousReadFbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
        if (bound_->height == 0)
        {
            if (sampleCount_ > 1)
            {
                // glReadPixels cannot read a multisample attachment: resolve into FBO 0 first.
                ResolveMsaa();
            }
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
        }

        // The bound target's own height when one is bound, the drawable's physical height for
        // the back buffer -- the logical presentation height cannot invert a physical rectangle.
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int physicalWidth = 0;
            GetPhysicalSize(physicalWidth, fbH);
        }
        const int glY = fbH - y - h;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, glY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);

        // GL returned bottom-up rows; XNA's contract is top-down.
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int i = 0; i < h / 2; ++i)
        {
            uint8_t* top = pixels + static_cast<std::size_t>(i) * rowBytes;
            uint8_t* bottom = pixels + static_cast<std::size_t>(h - 1 - i) * rowBytes;
            std::memcpy(tmp.data(), top, rowBytes);
            std::memcpy(top, bottom, rowBytes);
            std::memcpy(bottom, tmp.data(), rowBytes);
        }

        if (bound_->height == 0 && sampleCount_ > 1)
            gl4_glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        else
            gl4_glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
    }

    // ------------------------------------------------------------------------------------
    // Clears -- XNA's Clear covers the whole target and ignores scissor, colour write mask and
    // depth/stencil write masks; glClear respects all of them, so each is neutralised and put back.
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::ApplyCurrentColorWriteMasks()
    {
        for (int i = 0; i < maxMrtTargets_; ++i)
        {
            const int mask = currentColorWriteMasks_[static_cast<std::size_t>(i)];
            gl4_glColorMaski(static_cast<GLuint>(i),
                             ColorWriteHasRed(mask) ? GL_TRUE : GL_FALSE,
                             ColorWriteHasGreen(mask) ? GL_TRUE : GL_FALSE,
                             ColorWriteHasBlue(mask) ? GL_TRUE : GL_FALSE,
                             ColorWriteHasAlpha(mask) ? GL_TRUE : GL_FALSE);
        }
    }

    void OpenGL4Renderer::ForceAllColorWriteMasks()
    {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    bool OpenGL4Renderer::HasRestrictedActiveColorWriteMask() const
    {
        const int activeCount = bound_->mrtCount > 0 ? bound_->mrtCount : 1;
        for (int i = 0; i < activeCount; ++i)
            if (currentColorWriteMasks_[static_cast<std::size_t>(i)] != 15) return true;
        return false;
    }

    bool OpenGL4Renderer::DisableScissorForClear()
    {
        const bool wasEnabled = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
        if (wasEnabled) glDisable(GL_SCISSOR_TEST);
        return wasEnabled;
    }

    void OpenGL4Renderer::RestoreScissorAfterClear(bool wasEnabled)
    {
        if (wasEnabled) glEnable(GL_SCISSOR_TEST);
    }

    void OpenGL4Renderer::RestoreWriteMasksAfterClear(bool depth, bool stencil)
    {
        // REMED-GFX-237: the masks a clear forced open are put back now, not "by the next
        // ApplyDepthStencilState" -- nothing requires the game to reassign its state first.
        if (depth)
            glDepthMask((depthWriteEnabled_ && bound_->depthFormat != 0) ? GL_TRUE : GL_FALSE);
        if (stencil && stencilEnabled_)
        {
            const auto mask = static_cast<GLuint>(stencilWriteMask_);
            gl4_glStencilMaskSeparate(GL_FRONT, mask);
            gl4_glStencilMaskSeparate(GL_BACK, mask);
        }
    }

    void OpenGL4Renderer::Clear(float r, float g, float b, float a)
    {
        // REMED-GFX-142: COLOUR ONLY. Every clear that includes depth has its own entry point.
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearColor(r, g, b, a);
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        glClear(GL_COLOR_BUFFER_BIT);
        if (maskActive) ApplyCurrentColorWriteMasks();
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glDepthMask(GL_TRUE);
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (maskActive) ApplyCurrentColorWriteMasks();
        RestoreWriteMasksAfterClear(true, false);
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearDepth(float depth)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearDepth(depth);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        RestoreWriteMasksAfterClear(true, false);
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearStencil(int stencil)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_STENCIL_BUFFER_BIT);
        RestoreWriteMasksAfterClear(false, true);
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearDepth(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        RestoreWriteMasksAfterClear(true, true);
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glStencilMask(0xFFFFFFFFu);
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (maskActive) ApplyCurrentColorWriteMasks();
        // EasyGL's twin of this route does not put the stencil write mask back; REMED-GFX-237's
        // rule is that every clear does, and a later StencilWriteMask=0 draw depends on it.
        RestoreWriteMasksAfterClear(false, true);
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth,
                                                    int stencil)
    {
        const bool scissorWasEnabled = DisableScissorForClear();
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClearStencil(stencil);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFFFFFFFu);
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        RestoreWriteMasksAfterClear(true, true);
        if (maskActive) ApplyCurrentColorWriteMasks();
        RestoreScissorAfterClear(scissorWasEnabled);
    }

    void OpenGL4Renderer::SetDepthTestEnabled(bool enabled)
    {
        depthEnabled_ = enabled;
        if (enabled && bound_->depthFormat != 0) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        if (enabled)
        {
            glDepthFunc(GL_LEQUAL);
            depthWriteEnabled_ = true;
            glDepthMask(bound_->depthFormat != 0 ? GL_TRUE : GL_FALSE);
        }
    }

    void OpenGL4Renderer::SetBlendEnabled(bool enabled)
    {
        if (enabled)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    void OpenGL4Renderer::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
        glDepthMask((enabled && bound_->depthFormat != 0) ? GL_TRUE : GL_FALSE);
    }

    // ------------------------------------------------------------------------------------
    // Render state
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                          int colorDstBlend, int alphaDstBlend,
                                          int colorBlendFunc, int alphaBlendFunc,
                                          const BlendWriteState& writeState)
    {
        // Blend::One=0 and Blend::Zero=1: the Opaque preset (One/Zero on both channels) is XNA's
        // encoding of "no blending" -- there is no BlendState.Enabled.
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        // The factors and equations are written unconditionally, so disabling blending never
        // leaves a previous state's equation behind for a later Opaque -> blended transition.
        gl4_glBlendFuncSeparate(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend),
                                ToGLBlendFactor(alphaSrcBlend), ToGLBlendFactor(alphaDstBlend));
        gl4_glBlendEquationSeparate(ToGLBlendEquation(colorBlendFunc),
                                    ToGLBlendEquation(alphaBlendFunc));
        for (int i = 0; i < 4; ++i)
            currentColorWriteMasks_[static_cast<std::size_t>(i)] = writeState.colorWriteChannels[i];
        ApplyCurrentColorWriteMasks();
        // SOFTWARE-110: XNA/FNA apply this 32-bit mask to sample coverage; GL 3.2 core has the
        // exact operation. Enabled even for all-ones so a later change cannot inherit a mask.
        glEnable(GL_SAMPLE_MASK);
        gl4_glSampleMaski(0u, static_cast<GLbitfield>(writeState.multiSampleMask));
    }

    void OpenGL4Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                 int depthFunc,
                                                 bool stencilEnable, int stencilFunc,
                                                 int stencilPass, int stencilFail,
                                                 int stencilDepthFail,
                                                 int stencilMask, int stencilWriteMask,
                                                 int referenceStencil,
                                                 bool twoSidedStencilMode,
                                                 int ccwStencilFunc, int ccwStencilPass,
                                                 int ccwStencilFail, int ccwStencilDepthFail)
    {
        depthEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        if (depthEnable && bound_->depthFormat != 0) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthMask((depthWriteEnable && bound_->depthFormat != 0) ? GL_TRUE : GL_FALSE);
        if (depthEnable)
            glDepthFunc(ToGLCompareFunc(depthFunc));

        // Recorded even while the test is off: the reference survives a disabled state, and
        // SetReferenceStencil and topology changes reissue these tuples.
        stencilEnabled_ = stencilEnable;
        stencilWriteMask_ = stencilWriteMask;
        stencilTwoSided_ = twoSidedStencilMode;
        stencilFunc_ = stencilFunc;
        stencilPass_ = stencilPass;
        stencilFail_ = stencilFail;
        stencilDepthFail_ = stencilDepthFail;
        stencilCcwFunc_ = ccwStencilFunc;
        stencilCcwPass_ = ccwStencilPass;
        stencilCcwFail_ = ccwStencilFail;
        stencilCcwDepthFail_ = ccwStencilDepthFail;
        stencilReadMask_ = stencilMask;
        referenceStencil_ = referenceStencil;
        stencilPrimitiveUsesTwoSided_ = twoSidedStencilMode;

        if (stencilEnable && bound_->depthFormat == 3) glEnable(GL_STENCIL_TEST);
        else glDisable(GL_STENCIL_TEST);
        if (!stencilEnable) return;

        const auto readMask = static_cast<GLuint>(stencilMask);
        const auto writeMask = static_cast<GLuint>(stencilWriteMask);
        if (twoSidedStencilMode)
        {
            // XNA's front faces are CLOCKWISE on screen and GL keeps its default GL_CCW front face,
            // so XNA's ordinary tuple belongs to GL_BACK and its CounterClockwiseStencil* tuple to
            // GL_FRONT. Before GL4-0017 this renderer had them the other way round -- which is why
            // DepthStencilState_StencilTwoSided failed on it and passes on EasyGL.
            gl4_glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(stencilFunc), referenceStencil,
                                      readMask);
            gl4_glStencilOpSeparate(GL_BACK, ToGLStencilOp(stencilFail),
                                    ToGLStencilOp(stencilDepthFail), ToGLStencilOp(stencilPass));
            gl4_glStencilMaskSeparate(GL_BACK, writeMask);
            gl4_glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(ccwStencilFunc), referenceStencil,
                                      readMask);
            gl4_glStencilOpSeparate(GL_FRONT, ToGLStencilOp(ccwStencilFail),
                                    ToGLStencilOp(ccwStencilDepthFail),
                                    ToGLStencilOp(ccwStencilPass));
            gl4_glStencilMaskSeparate(GL_FRONT, writeMask);
        }
        else
        {
            glStencilFunc(ToGLCompareFunc(stencilFunc), referenceStencil, readMask);
            glStencilOp(ToGLStencilOp(stencilFail), ToGLStencilOp(stencilDepthFail),
                        ToGLStencilOp(stencilPass));
            glStencilMask(writeMask);
        }
    }

    void OpenGL4Renderer::ApplyStencilPrimitiveTopology(PrimitiveType primitive)
    {
        if (!stencilEnabled_ || !stencilTwoSided_) return;
        // Direct3D 9 applies the CCW tuple only to counter-clockwise TRIANGLES; lines and points
        // use the ordinary tuple, while GL's separate face state still reaches line rasterization
        // (as GL_FRONT). So the ordinary tuple goes on both faces for a non-triangle draw.
        const bool useTwoSided = primitive == PrimitiveType::TriangleList ||
                                 primitive == PrimitiveType::TriangleStrip;
        if (stencilPrimitiveUsesTwoSided_ == useTwoSided) return;
        stencilPrimitiveUsesTwoSided_ = useTwoSided;
        const auto readMask = static_cast<GLuint>(stencilReadMask_);
        const auto writeMask = static_cast<GLuint>(stencilWriteMask_);
        if (!useTwoSided)
        {
            glStencilFunc(ToGLCompareFunc(stencilFunc_), referenceStencil_, readMask);
            glStencilOp(ToGLStencilOp(stencilFail_), ToGLStencilOp(stencilDepthFail_),
                        ToGLStencilOp(stencilPass_));
            glStencilMask(writeMask);
            return;
        }
        gl4_glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(stencilFunc_), referenceStencil_, readMask);
        gl4_glStencilOpSeparate(GL_BACK, ToGLStencilOp(stencilFail_), ToGLStencilOp(stencilDepthFail_),
                                ToGLStencilOp(stencilPass_));
        gl4_glStencilMaskSeparate(GL_BACK, writeMask);
        gl4_glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(stencilCcwFunc_), referenceStencil_,
                                  readMask);
        gl4_glStencilOpSeparate(GL_FRONT, ToGLStencilOp(stencilCcwFail_),
                                ToGLStencilOp(stencilCcwDepthFail_), ToGLStencilOp(stencilCcwPass_));
        gl4_glStencilMaskSeparate(GL_FRONT, writeMask);
    }

    void OpenGL4Renderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
        // GL sets function, reference and mask together, so a new reference means reissuing the
        // remembered functions. Nothing to reissue while the test is off; the value is kept.
        if (!stencilEnabled_) return;
        const auto readMask = static_cast<GLuint>(stencilReadMask_);
        if (stencilPrimitiveUsesTwoSided_)
        {
            gl4_glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(stencilFunc_), referenceStencil_,
                                      readMask);
            gl4_glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(stencilCcwFunc_), referenceStencil_,
                                      readMask);
        }
        else
        {
            glStencilFunc(ToGLCompareFunc(stencilFunc_), referenceStencil_, readMask);
        }
    }

    void OpenGL4Renderer::ApplyCurrentDepthStencilAvailability()
    {
        const bool hasDepth = bound_->depthFormat != 0;
        const bool hasStencil = bound_->depthFormat == 3;
        if (depthEnabled_ && hasDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask((depthWriteEnabled_ && hasDepth) ? GL_TRUE : GL_FALSE);
        if (stencilEnabled_ && hasStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    }

    void OpenGL4Renderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                               float depthBias, float slopeScaleDepthBias)
    {
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. GL's front face stays
        // GL_CCW, and XNA's CLOCKWISE faces are GL's back faces (screen-space winding is the same
        // in both APIs), so CullClockwiseFace culls GL_BACK.
        if (cullMode == 0)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(cullMode == 1 ? GL_BACK : GL_FRONT);
        }
        if (scissorTestEnable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        // SOFTWARE-178: native polygon mode keeps the operation after clipping and culling and
        // gives it polygon offset, MSAA and two-sided stencil.
        fillModeWireframe_ = fillMode == 1;
        glPolygonMode(GL_FRONT_AND_BACK, fillModeWireframe_ ? GL_LINE : GL_FILL);
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
        ApplyCurrentDepthBias();
    }

    void OpenGL4Renderer::ApplyCurrentDepthBias()
    {
        // FNA exposes constant DepthBias in normalized depth units; GL's polygon-offset `units` are
        // minimum-resolvable depth steps, so the active depth format decides the conversion --
        // FNA3D's 16/24-bit scale table. Passing the XNA value through raw (as this renderer did
        // before GL4-0017) is indistinguishable from no bias at all.
        float depthScale = 0.0f;
        switch (bound_->depthFormat)
        {
        case 1: depthScale = 65535.0f; break;
        case 2:
        case 3: depthScale = 16777215.0f; break;
        default: break;
        }
        const bool enabled = slopeScaleDepthBias_ != 0.0f || depthBias_ != 0.0f;
        if (enabled)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glEnable(GL_POLYGON_OFFSET_LINE);
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_POLYGON_OFFSET_LINE);
        }
        glPolygonOffset(slopeScaleDepthBias_, depthBias_ * depthScale);
    }

    void OpenGL4Renderer::ApplyRasterizerMultiSampleState(bool enabled)
    {
        if (enabled) glEnable(GL_MULTISAMPLE);
        else glDisable(GL_MULTISAMPLE);
    }

    void OpenGL4Renderer::SetBlendFactor(float r, float g, float b, float a)
    {
        gl4_glBlendColor(r, g, b, a);
    }

    void OpenGL4Renderer::SetScissorRect(int x, int y, int w, int h)
    {
        // SOFTWARE-310: a zero width or height is a valid XNA scissor that must reach glScissor,
        // whose empty box rejects every fragment. Before GL4-0017 this returned early for it.
        if (w < 0 || h < 0) return;
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int physW = 0;
            GetPhysicalSize(physW, fbH);
        }
        glScissor(x, fbH - y - h, w, h);
        // The scissor TEST is RasterizerState.ScissorTestEnable's alone (ApplyRasterizerState).
    }

    void OpenGL4Renderer::SetGlViewport(int x, int y, int width, int height)
    {
        glViewport(x, y, width, height);
        glViewportShadow_ = {true, x, y, width, height};
    }

    void OpenGL4Renderer::GetGlViewport(int& x, int& y, int& width, int& height) const
    {
        if (!glViewportShadow_.known)
        {
            GLint viewport[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, viewport);
            glViewportShadow_ = {true, viewport[0], viewport[1], viewport[2], viewport[3]};
        }
        x = glViewportShadow_.x;
        y = glViewportShadow_.y;
        width = glViewportShadow_.width;
        height = glViewportShadow_.height;
    }

    void OpenGL4Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return;
        // GL's viewport origin is bottom-left: flip against the bound target's height, or the
        // drawable's for the back buffer.
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int physW = 0;
            GetPhysicalSize(physW, fbH);
        }
        SetGlViewport(x, fbH - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
        // Recorded while the presentation rectangle this call was derived from is still current:
        // the SpriteBatch flush must tell a game's sub-viewport from a default viewport that a
        // later resize has made stale.
        if (bound_->height == 0)
        {
            int defX = 0, defY = 0, defW = 0, defH = 0;
            GetDefaultViewportRect(defX, defY, defW, defH);
            viewportIsDefault_ = defW > 0 && defH > 0 && x == defX && y == defY && w == defW &&
                                 h == defH;
        }
        else
        {
            viewportIsDefault_ = x == 0 && y == 0 && w == bound_->width && h == bound_->height;
        }
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    // ------------------------------------------------------------------------------------
    // Samplers -- one sampler object per XNA slot, bound to its own unit, whose complete state is
    // a function of the latest application (REMED-GFX-174, FX-092).
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                            int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        const GLuint sampler = samplers_[slot];
        GLint minFilter = GL_LINEAR, magFilter = GL_LINEAR;
        FilterOrdinalToGL(filter, minFilter, magFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, magFilter);
        // REMED-GFX-174: anisotropy is a component of the ordinal and is written on every
        // application, or one Anisotropic draw leaves the long-lived slot object anisotropic.
        if (maxAnisotropy_ > 1.0f)
        {
            const float clamped = filter == 2 ? ClampedMaxAnisotropy(maxAnisotropy, maxAnisotropy_)
                                              : 1.0f;
            gl4_glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, clamped);
        }
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, ToGLWrap(addressU));
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, ToGLWrap(addressV));
        // FX-092: the rest of the object's state is also a function of THIS call. W follows U
        // (every XNA preset sets all three axes alike); the mip range and bias return to XNA's
        // defaults, which a device-driven application then overwrites via ApplySamplerMipState and
        // ApplySamplerAddressW. XNA has no comparison sampler, so comparison stays off.
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, ToGLWrap(addressU));
        gl4_glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, 0.0f);
        gl4_glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, 1000.0f);
        gl4_glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, 0.0f);
        gl4_glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        gl4_glBindSampler(static_cast<GLuint>(slot), sampler);
    }

    void OpenGL4Renderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        const GLuint sampler = samplers_[slot];
        // XNA's MaxMipLevel is the MOST detailed level the sampler may use: a lower bound on the
        // LOD, i.e. GL_TEXTURE_MIN_LOD. XNA writes the signed property through D3D9's DWORD
        // channel, so a negative value converts to UInt32 and clamps rather than becoming zero.
        constexpr std::uint32_t kGlDefaultMaxLod = 1000u;
        const auto requested = static_cast<std::uint32_t>(maxMipLevel);
        gl4_glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD,
                                static_cast<float>(std::min(requested, kGlDefaultMaxLod)));
        gl4_glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, lodBias);
        gl4_glBindSampler(static_cast<GLuint>(slot), sampler);
    }

    void OpenGL4Renderer::ApplySamplerAddressW(int slot, int addressW)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        gl4_glSamplerParameteri(samplers_[slot], GL_TEXTURE_WRAP_R, ToGLWrap(addressW));
        gl4_glBindSampler(static_cast<GLuint>(slot), samplers_[slot]);
    }

    // ------------------------------------------------------------------------------------
    // Render targets
    // ------------------------------------------------------------------------------------

    void OpenGL4Renderer::FinalizeCurrentMRT()
    {
        if (bound_->mrtCount <= 0) return;
        const int count = bound_->mrtCount;
        bound_->mrtCount = 0;
        bound_->mrtFramebuffer = 0;
        bound_->width = 0;
        bound_->height = 0;
        for (int i = 0; i < count; ++i)
        {
            const OpenGL4MrtBinding target = bound_->mrt[static_cast<std::size_t>(i)];
            bound_->mrt[static_cast<std::size_t>(i)] = {};
            if (target.rt2D)
                target.rt2D->UnbindAsRenderTarget();
            else if (target.cube)
                target.cube->UnbindMRTFace(target.cubeFace);
        }
    }

    void OpenGL4Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        FinalizeCurrentMRT();
        // Resolve MSAA and regenerate mips for the target being left, before switching away.
        if (bound_->rt2D && bound_->rt2D != rt) bound_->rt2D->UnbindAsRenderTarget();
        if (bound_->cube) bound_->cube->UnbindAsRenderTarget();
        bound_->cube = nullptr;
        bound_->rt2D = rt;
        if (rt)
        {
            bound_->width = rt->GetWidth();
            bound_->height = rt->GetHeight();
            if (const auto* target = dynamic_cast<const OpenGL4RenderTargetRenderer*>(rt))
                bound_->depthFormat = target->GetDepthFormatEXT();
            else
                bound_->depthFormat = 0;
            rt->BindAsRenderTarget();
        }
        else
        {
            bound_->width = 0;
            bound_->height = 0;
            bound_->depthFormat = backBufferDepthFormat_;
            BindDefaultFramebuffer();
        }
        ApplyCurrentDepthStencilAvailability();
        ApplyCurrentDepthBias();
    }

    void OpenGL4Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        if (!rt) { SetRenderTarget2D(nullptr); return; }
        FinalizeCurrentMRT();
        if (bound_->rt2D) bound_->rt2D->UnbindAsRenderTarget();
        // A different face of the same cube is a different subresource: finalize the bound face
        // before the next bind overwrites its record, or face-to-face sequences lose every
        // intermediate resolve and mip generation.
        if (bound_->cube) bound_->cube->UnbindAsRenderTarget();
        bound_->rt2D = nullptr;
        bound_->cube = rt;
        bound_->width = rt->GetSize();
        bound_->height = rt->GetSize();
        if (const auto* target = dynamic_cast<const OpenGL4RenderTargetCubeRenderer*>(rt))
            bound_->depthFormat = target->GetDepthFormatEXT();
        else
            bound_->depthFormat = 0;
        rt->BindAsRenderTargetFace(face);
        ApplyCurrentDepthStencilAvailability();
        ApplyCurrentDepthBias();
    }

    void OpenGL4Renderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                           int count)
    {
        if (count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (!renderTargets)
            throw std::invalid_argument(
                "OpenGL4 SetRenderTargets: nonzero count requires a binding array.");
        if (count == 1)
        {
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                        renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }
        if (count > maxMrtTargets_)
            throw std::runtime_error("OpenGL4 SetRenderTargets: requested " +
                                     std::to_string(count) + " targets, but this context supports " +
                                     std::to_string(maxMrtTargets_) + ".");

        std::array<OpenGL4MrtBinding, 4> targets{};
        for (int i = 0; i < count; ++i)
        {
            auto& slot = targets[static_cast<std::size_t>(i)];
            if (renderTargets[i].IsRenderTargetCubeFace())
            {
                slot.cube = dynamic_cast<OpenGL4RenderTargetCubeRenderer*>(
                    renderTargets[i].GetRenderTargetCube());
                slot.cubeFace = renderTargets[i].GetCubeFace();
                if (!slot.cube)
                    throw std::runtime_error("OpenGL4 SetRenderTargets: binding " + std::to_string(i) +
                                             " is not an OpenGL4 RenderTargetCube face.");
            }
            else
            {
                slot.rt2D = dynamic_cast<OpenGL4RenderTargetRenderer*>(
                    renderTargets[i].GetRenderTarget2D());
                if (!slot.rt2D)
                    throw std::runtime_error("OpenGL4 SetRenderTargets: binding " + std::to_string(i) +
                                             " is not an OpenGL4 RenderTarget2D.");
            }
            if (renderTargets[i].GetWidth() != renderTargets[0].GetWidth() ||
                renderTargets[i].GetHeight() != renderTargets[0].GetHeight())
                throw std::runtime_error(
                    "OpenGL4 SetRenderTargets: render targets must have matching dimensions.");
            if (renderTargets[i].GetAppliedMultiSampleCount() !=
                renderTargets[0].GetAppliedMultiSampleCount())
                throw std::runtime_error(
                    "OpenGL4 SetRenderTargets: render targets must have matching applied sample "
                    "counts.");
            for (int previous = 0; previous < i; ++previous)
                if (renderTargets[i].IsSameSubresource(renderTargets[previous]))
                    throw std::runtime_error(
                        "OpenGL4 SetRenderTargets: the same render-target subresource cannot "
                        "occupy multiple slots.");
        }

        const std::string errorsBeforeSetup = DrainGlErrorsDescribed();
        if (!errorsBeforeSetup.empty())
            throw std::runtime_error(
                "OpenGL4 SetRenderTargets: GL errors were pending before MRT setup: " +
                errorsBeforeSetup);

        FinalizeCurrentMRT();
        if (bound_->rt2D) bound_->rt2D->UnbindAsRenderTarget();
        if (bound_->cube) bound_->cube->UnbindAsRenderTarget();
        bound_->rt2D = nullptr;
        bound_->cube = nullptr;

        if (!mrtFbo_) gl4_glGenFramebuffers(1, &mrtFbo_);
        gl4_glBindFramebuffer(GL_FRAMEBUFFER, mrtFbo_);
        // Every attachment point is replaced, including slots and depth the previous set owned:
        // the FBO is reused, but its identity never stands in for the ordered target set.
        for (int i = 0; i < 4; ++i)
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i),
                                          GL_RENDERBUFFER, 0);
        for (const GLenum attachment : {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT,
                                        GL_DEPTH_STENCIL_ATTACHMENT})
            gl4_glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, 0);

        std::array<GLenum, 4> drawBuffers{};
        for (int i = 0; i < count; ++i)
        {
            const auto attachment = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
            const auto& slot = targets[static_cast<std::size_t>(i)];
            if (slot.rt2D)
                slot.rt2D->AttachColorToMRT(mrtFbo_, attachment);
            else
                slot.cube->AttachColorToMRT(mrtFbo_, attachment, slot.cubeFace);
            drawBuffers[static_cast<std::size_t>(i)] = attachment;
        }
        if (targets[0].rt2D)
            targets[0].rt2D->AttachDepthToMRT(mrtFbo_);
        else
            targets[0].cube->AttachDepthToMRT(mrtFbo_);
        gl4_glDrawBuffers(count, drawBuffers.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        const GLenum status = gl4_glCheckFramebufferStatus(GL_FRAMEBUFFER);
        const std::string setupErrors = DrainGlErrorsDescribed();
        if (status != GL_FRAMEBUFFER_COMPLETE || !setupErrors.empty())
        {
            BindDefaultFramebuffer();
            bound_->width = 0;
            bound_->height = 0;
            throw std::runtime_error("OpenGL4 SetRenderTargets: MRT framebuffer setup failed for " +
                                     std::to_string(count) + " targets; status=" +
                                     FramebufferStatusName(status) + "; GL errors=" +
                                     (setupErrors.empty() ? std::string("none") : setupErrors));
        }

        bound_->mrt = targets;
        bound_->mrtCount = count;
        bound_->mrtFramebuffer = mrtFbo_;
        bound_->width = renderTargets[0].GetWidth();
        bound_->height = renderTargets[0].GetHeight();
        bound_->depthFormat = targets[0].rt2D ? targets[0].rt2D->GetDepthFormatEXT()
                                              : targets[0].cube->GetDepthFormatEXT();
        ApplyCurrentColorWriteMasks();
        ApplyCurrentDepthStencilAvailability();
        ApplyCurrentDepthBias();
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_OPENGL4
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own namespace so
    // several renderer archives can link into one binary.
    namespace OpenGL4 { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> OpenGL4::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenGL4::OpenGL4Renderer>(args);
    }
#endif
}
