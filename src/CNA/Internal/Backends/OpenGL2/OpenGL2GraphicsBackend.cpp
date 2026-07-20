#include "CNA/Internal/Backends/OpenGL2/OpenGL2GraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

// Windows' opengl32.dll only ever exports the ~350 GL 1.1 entry points (Microsoft never updated
// its own ICD dispatch table beyond that) -- everything this backend calls beyond GL 1.1 (buffer
// objects/GL 1.5, shaders/GL 2.0, multitexture/GL 1.3, framebuffer objects/ARB_framebuffer_object,
// even glTexImage3D/glTexSubImage3D despite being spec-core since GL 1.2) must be resolved at
// runtime via wglGetProcAddress there, or the link step fails outright with unresolved externals.
// GL_GLEXT_PROTOTYPES's plain `extern`/GLAPI declarations (used on every other platform below)
// only link because Linux/Mesa/GLX -- and macOS/other desktop GL ICDs -- export these symbols
// directly for static linking; that is a convenience specific to those platforms' GL loader
// model, not something Windows' opengl32.dll provides. SDL_GL_GetProcAddress() wraps
// wglGetProcAddress portably; see LoadWin32GLExtensions() below, called once after context
// creation. Deliberately NOT build/link-verified against a real Windows toolchain -- none is
// available in this project's own dev/CI sandbox -- but every function pointer below uses the
// STANDARD Khronos PFNGLxxxPROC typedef from glext.h (bundled as SDL3/SDL_opengl_glext.h,
// included transitively by SDL_opengl.h below), not a hand-rolled signature, specifically to
// minimize the chance of a silent type mismatch that only a real compile could otherwise catch.
#if defined(_WIN32)
#include <SDL3/SDL_opengl.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#endif

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
        // Not aliased by IGraphicsBackend.hpp (unlike VertexElement itself) -- only needed
        // locally, for DescribeVertexElementFormat()/VertexElementAttribName() below.
        using VertexElementUsage = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        using VertexElementFormat = Microsoft::Xna::Framework::Graphics::VertexElementFormat;

#if defined(_WIN32)
        // One function pointer per GL entry point beyond GL 1.1 that this file calls -- see the
        // file-header comment above for why this exists only on _WIN32. Grouped in the same order
        // as LoadWin32GLExtensions() below assigns them, not alphabetically, so the two stay easy
        // to diff against each other if a new GL call is ever added to this file.
        PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
        PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = nullptr;
        PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate = nullptr;
        PFNGLBLENDCOLORPROC glBlendColor = nullptr;
        PFNGLSTENCILFUNCSEPARATEPROC glStencilFuncSeparate = nullptr;
        PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate = nullptr;
        PFNGLSTENCILMASKSEPARATEPROC glStencilMaskSeparate = nullptr;
        PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
        PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D = nullptr;
        PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
        PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
        PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
        PFNGLBUFFERDATAPROC glBufferData = nullptr;
        PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
        PFNGLGENQUERIESPROC glGenQueries = nullptr;
        PFNGLDELETEQUERIESPROC glDeleteQueries = nullptr;
        PFNGLBEGINQUERYPROC glBeginQuery = nullptr;
        PFNGLENDQUERYPROC glEndQuery = nullptr;
        PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv = nullptr;
        PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv = nullptr;
        PFNGLCREATESHADERPROC glCreateShader = nullptr;
        PFNGLDELETESHADERPROC glDeleteShader = nullptr;
        PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
        PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
        PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
        PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
        PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
        PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
        PFNGLATTACHSHADERPROC glAttachShader = nullptr;
        PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;
        PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
        PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
        PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
        PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
        PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
        PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = nullptr;
        PFNGLUNIFORM1FPROC glUniform1f = nullptr;
        PFNGLUNIFORM1FVPROC glUniform1fv = nullptr;
        PFNGLUNIFORM1IPROC glUniform1i = nullptr;
        PFNGLUNIFORM2FPROC glUniform2f = nullptr;
        PFNGLUNIFORM2FVPROC glUniform2fv = nullptr;
        PFNGLUNIFORM3FPROC glUniform3f = nullptr;
        PFNGLUNIFORM3FVPROC glUniform3fv = nullptr;
        PFNGLUNIFORM4FPROC glUniform4f = nullptr;
        PFNGLUNIFORM4FVPROC glUniform4fv = nullptr;
        PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv = nullptr;
        PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
        PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
        PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
        PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
        PFNGLVERTEXATTRIB4FPROC glVertexAttrib4f = nullptr;
        PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
        PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
        PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
        PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
        PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
        PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
        PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
        PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
        PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;
        PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample = nullptr;
        PFNGLGENERATEMIPMAPPROC glGenerateMipmap = nullptr;
        PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer = nullptr;
        PFNGLDRAWBUFFERSPROC glDrawBuffers = nullptr;

        // Called once, right after SDL_GL_MakeCurrent() in the constructor -- every function
        // pointer above must be resolved before ensurePrograms() (or any other GL call in this
        // file beyond GL 1.1) runs.
        void LoadWin32GLExtensions()
        {
#define CNA_LOAD_GL(name) name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress(#name))
            CNA_LOAD_GL(glActiveTexture);
            CNA_LOAD_GL(glBlendFuncSeparate);
            CNA_LOAD_GL(glBlendEquationSeparate);
            CNA_LOAD_GL(glBlendColor);
            CNA_LOAD_GL(glStencilFuncSeparate);
            CNA_LOAD_GL(glStencilOpSeparate);
            CNA_LOAD_GL(glStencilMaskSeparate);
            CNA_LOAD_GL(glTexImage3D);
            CNA_LOAD_GL(glTexSubImage3D);
            CNA_LOAD_GL(glGenBuffers);
            CNA_LOAD_GL(glDeleteBuffers);
            CNA_LOAD_GL(glBindBuffer);
            CNA_LOAD_GL(glBufferData);
            CNA_LOAD_GL(glBufferSubData);
            CNA_LOAD_GL(glGenQueries);
            CNA_LOAD_GL(glDeleteQueries);
            CNA_LOAD_GL(glBeginQuery);
            CNA_LOAD_GL(glEndQuery);
            CNA_LOAD_GL(glGetQueryObjectiv);
            CNA_LOAD_GL(glGetQueryObjectuiv);
            CNA_LOAD_GL(glCreateShader);
            CNA_LOAD_GL(glDeleteShader);
            CNA_LOAD_GL(glShaderSource);
            CNA_LOAD_GL(glCompileShader);
            CNA_LOAD_GL(glGetShaderiv);
            CNA_LOAD_GL(glGetShaderInfoLog);
            CNA_LOAD_GL(glCreateProgram);
            CNA_LOAD_GL(glDeleteProgram);
            CNA_LOAD_GL(glAttachShader);
            CNA_LOAD_GL(glBindAttribLocation);
            CNA_LOAD_GL(glLinkProgram);
            CNA_LOAD_GL(glGetProgramiv);
            CNA_LOAD_GL(glGetProgramInfoLog);
            CNA_LOAD_GL(glUseProgram);
            CNA_LOAD_GL(glGetUniformLocation);
            CNA_LOAD_GL(glGetAttribLocation);
            CNA_LOAD_GL(glUniform1f);
            CNA_LOAD_GL(glUniform1fv);
            CNA_LOAD_GL(glUniform1i);
            CNA_LOAD_GL(glUniform2f);
            CNA_LOAD_GL(glUniform2fv);
            CNA_LOAD_GL(glUniform3f);
            CNA_LOAD_GL(glUniform3fv);
            CNA_LOAD_GL(glUniform4f);
            CNA_LOAD_GL(glUniform4fv);
            CNA_LOAD_GL(glUniformMatrix3fv);
            CNA_LOAD_GL(glUniformMatrix4fv);
            CNA_LOAD_GL(glEnableVertexAttribArray);
            CNA_LOAD_GL(glDisableVertexAttribArray);
            CNA_LOAD_GL(glVertexAttribPointer);
            CNA_LOAD_GL(glVertexAttrib4f);
            CNA_LOAD_GL(glGenFramebuffers);
            CNA_LOAD_GL(glDeleteFramebuffers);
            CNA_LOAD_GL(glBindFramebuffer);
            CNA_LOAD_GL(glFramebufferTexture2D);
            CNA_LOAD_GL(glFramebufferRenderbuffer);
            CNA_LOAD_GL(glGenRenderbuffers);
            CNA_LOAD_GL(glDeleteRenderbuffers);
            CNA_LOAD_GL(glBindRenderbuffer);
            CNA_LOAD_GL(glRenderbufferStorage);
            CNA_LOAD_GL(glRenderbufferStorageMultisample);
            CNA_LOAD_GL(glGenerateMipmap);
            CNA_LOAD_GL(glBlitFramebuffer);
            CNA_LOAD_GL(glDrawBuffers);
#undef CNA_LOAD_GL
        }
#endif

        // Hardware instancing (DrawInstancedPrimitivesEx, plan_opengl2.md follow-up): GL_ARB_
        // draw_instanced/GL_ARB_instanced_arrays are NOT GL 2.1 core, and -- unlike this file's own
        // GL 2.0/2.1 CORE calls, which link directly via GL_GLEXT_PROTOTYPES's extern declarations
        // on Linux/macOS (see the file-header comment) -- true ARB extension entry points are not
        // reliably direct-linkable on any platform, Windows included, so these are always resolved
        // at runtime via SDL_GL_GetProcAddress() regardless of platform (LoadInstancingExtensions(),
        // called unconditionally from the constructor, unlike LoadWin32GLExtensions() above which
        // is Windows-only). Null after loading means the driver genuinely lacks the extension --
        // every call site below must check before use (see SupportsCapability(Instancing) and
        // DrawInstancedPrimitivesEx's own guard).
        PFNGLDRAWELEMENTSINSTANCEDARBPROC glDrawElementsInstancedARB = nullptr;
        PFNGLVERTEXATTRIBDIVISORARBPROC glVertexAttribDivisorARB = nullptr;

        void LoadInstancingExtensions()
        {
            glDrawElementsInstancedARB = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDARBPROC>(
                SDL_GL_GetProcAddress("glDrawElementsInstancedARB"));
            glVertexAttribDivisorARB = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORARBPROC>(
                SDL_GL_GetProcAddress("glVertexAttribDivisorARB"));
        }

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
            glBindAttribLocation(program, 3, "aNormal");
            // SkinnedEffect-only attributes (VertexPositionNormalTextureSkinned, stride 52/56).
            // Harmless no-op for every other program, same convention as aNormal above.
            glBindAttribLocation(program, 4, "aBoneWeight");
            glBindAttribLocation(program, 5, "aBoneIndices");
            // PbrEffect/SkinnedPbrEffect-only attribute (VertexPositionNormalTangentTexture(Skinned),
            // stride 48/68). Harmless no-op for every other program, same convention as above.
            glBindAttribLocation(program, 6, "aTangent");
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

        // Mip-aware MIN/MAG filter pair for the general 3D texture-sampling path (applySampler()
        // in drawInternal() below) -- mirrors EasyGLGraphicsBackend::ApplySamplerState's own
        // TextureFilter -> min/mag mapping exactly, including its own documented Linear=0 case
        // ("CNA does not generate mipmaps by default"), for cross-backend consistency. Unlike
        // plain ToGLFilter() above (still used verbatim by SpriteBatch's own draw paths, which
        // don't sample real mip chains), this selects a MIPMAP-suffixed MIN_FILTER for every XNA
        // filter value whose name says "Mip" (or Anisotropic) -- safe even for a texture with no
        // real mip levels beyond 0, because Tex's own GL_TEXTURE_MAX_LEVEL clamp (see Tex's
        // constructor) always keeps the mipmap chain complete per the GL spec.
        // XNA: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3, PointMipLinear=4,
        //      MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        //      MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8.
        void ToGLMinMagFilter(int xnaFilter, GLint& outMin, GLint& outMag)
        {
            switch (xnaFilter)
            {
                case 1: outMin = GL_NEAREST;               outMag = GL_NEAREST; break;
                case 2: outMin = GL_LINEAR_MIPMAP_LINEAR;  outMag = GL_LINEAR;  break;
                case 3: outMin = GL_LINEAR_MIPMAP_NEAREST; outMag = GL_LINEAR;  break;
                case 4: outMin = GL_NEAREST_MIPMAP_LINEAR; outMag = GL_NEAREST; break;
                case 5: outMin = GL_LINEAR_MIPMAP_LINEAR;  outMag = GL_NEAREST; break;
                case 6: outMin = GL_LINEAR_MIPMAP_NEAREST; outMag = GL_NEAREST; break;
                case 7: outMin = GL_NEAREST_MIPMAP_LINEAR; outMag = GL_LINEAR;  break;
                case 8: outMin = GL_NEAREST_MIPMAP_NEAREST;outMag = GL_LINEAR;  break;
                default: outMin = GL_LINEAR; outMag = GL_LINEAR; break; // Linear = 0
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

        // ShaderEffect backend: a user-authored, runtime-compiled GLSL program (Task
        // CreateEffectBackend / plan_opengl2.md follow-up). Reuses LinkProgram() -- so a custom
        // vertex shader binds to the SAME fixed attribute locations as every built-in program
        // here (0=aPosition, 1=aColor, 2=aTexCoord, 3=aNormal, 4=aBoneWeight, 5=aBoneIndices) --
        // required because GLSL 1.10/1.20 (this file's target) has no `layout(location=N)`
        // qualifier, unlike EasyGLEffectBackend's GLES3 shaders, which bind locations directly in
        // the GLSL source instead of via glBindAttribLocation. A custom shader MUST use those
        // exact attribute names to receive vertex data at all.
        class EffectBackend final : public IEffectBackend
        {
        public:
            ~EffectBackend() override { if (program_) glDeleteProgram(program_); }

            // NOXNA: lets drawInternal()'s customEffectBackend path (VertexDeclaration-driven
            // attribute binding, see BindVertexAttributesForDeclaration()) look up this custom
            // program's own attribute locations, same reasoning as
            // EasyGLSpriteBatchBackend::FlushBatch()'s own dynamic_cast to its EasyGLEffectBackend
            // for the identical purpose (reaching the raw program handle through the generic
            // IEffectBackend interface, which deliberately doesn't expose one itself).
            [[nodiscard]] GLuint GetProgramHandle() const { return program_; }

            bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override
            {
                try
                {
                    program_ = LinkProgram(vertSrc.c_str(), fragSrc.c_str());
                    compileError_.clear();
                    return true;
                }
                catch (const std::exception& e)
                {
                    program_ = 0;
                    compileError_ = e.what();
                    return false;
                }
            }

            void Bind() override { if (program_) glUseProgram(program_); }
            void Unbind() override { glUseProgram(0); }
            [[nodiscard]] bool IsValid() const override { return program_ != 0; }
            [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

            // GL 2.1 has no glProgramUniform* (DSA -- ARB_separate_shader_objects is GL 4.1+),
            // so glUniform* always targets whichever program is CURRENTLY bound via glUseProgram,
            // not the program object glGetUniformLocation was queried against. A caller is free
            // to call SetUniformXxx() before ever calling Apply()/Bind() (real usage: XNA/FNA
            // EffectParameter setters are commonly called in any order relative to Apply()), so
            // each setter here re-binds its own program first rather than assuming it is already
            // current -- otherwise the uniform would silently land on whatever OTHER program (a
            // built-in one, or a different ShaderEffect) happened to be bound at that moment.
            void SetUniformFloat(const char* name, float value) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform1f(glGetUniformLocation(program_, name), value);
            }
            void SetUniformInt(const char* name, int value) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform1i(glGetUniformLocation(program_, name), value);
            }
            void SetUniformVec2(const char* name, float x, float y) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform2f(glGetUniformLocation(program_, name), x, y);
            }
            void SetUniformVec3(const char* name, float x, float y, float z) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform3f(glGetUniformLocation(program_, name), x, y, z);
            }
            void SetUniformVec4(const char* name, float x, float y, float z, float w) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform4f(glGetUniformLocation(program_, name), x, y, z, w);
            }
            void SetUniformMat4(const char* name, const float* matrix) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, matrix);
            }
            void SetUniformFloatArray(const char* name, const float* values, int count) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform1fv(glGetUniformLocation(program_, name), count, values);
            }
            void SetUniformVec2Array(const char* name, const float* values, int count) override
            {
                if (!program_) return;
                glUseProgram(program_);
                glUniform2fv(glGetUniformLocation(program_, name), count, values);
            }
            // Matches IEffectBackend::BindTexture's own doc comment: binds the texture object to
            // the given unit only -- does NOT set any sampler uniform (the caller's own
            // SetUniformInt("uSamplerName", unit) call wires that up), same contract as
            // EasyGLEffectBackend's identical BindTexture/BindTextureCube/BindTexture3D.
            void BindTexture(int unit, ITextureBackend* texture) override
            {
                if (!texture) return;
                glActiveTexture(GL_TEXTURE0 + unit);
                texture->BindGL();
                glActiveTexture(GL_TEXTURE0);
            }
            void BindTextureCube(int unit, ITextureCubeBackend* texture) override
            {
                if (!texture) return;
                glActiveTexture(GL_TEXTURE0 + unit);
                texture->BindGL();
                glActiveTexture(GL_TEXTURE0);
            }
            void BindTexture3D(int unit, ITexture3DBackend* texture) override
            {
                if (!texture) return;
                glActiveTexture(GL_TEXTURE0 + unit);
                texture->BindGL();
                glActiveTexture(GL_TEXTURE0);
            }

        private:
            GLuint program_{};
            std::string compileError_;
        };

        // Binds `vboId`'s buffer and sets up all vertex attribute pointers (including position,
        // location 0) for the given stride -- shared by drawInternal()'s normal built-in-effect
        // path and its customEffectBackend early-return path, since a custom ShaderEffect draws
        // through the exact same vertex data / attribute-location convention as every built-in
        // program (see EffectBackend's own doc comment above).
        void BindVertexAttributesForStride(GLuint vboId, std::size_t stride)
        {
            glBindBuffer(GL_ARRAY_BUFFER, vboId);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), nullptr);

            // Stride-based vertex-layout dispatch: VertexPositionColor=16, VertexPositionTexture=20,
            // VertexPositionColorTexture=24, VertexPositionNormalTextureSkinned=52/56 (checked before
            // the >=32 catch-all below, since 52/56 also satisfy >=32), VertexPositionNormalTexture
            // (any other size >=32; normal bound at location 3, read only by litProgram_/
            // envMapProgram_ -- harmless for the other programs, which don't declare an aNormal
            // attribute at all). Locations 4/5 (aBoneWeight/aBoneIndices) are skinned-only and must be
            // explicitly disabled in every other branch, or a previous skinned draw's now-stale
            // pointers would remain enabled (harmless to non-skinned programs, which don't declare
            // those attributes, but left disabled for cleanliness/defensiveness).
            if (stride == 16)
            {
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glDisableVertexAttribArray(2);
                glDisableVertexAttribArray(3);
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else if (stride == 20)
            {
                glDisableVertexAttribArray(1);
                glVertexAttrib4f(1, 1, 1, 1, 1);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glDisableVertexAttribArray(3);
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else if (stride == 24)
            {
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(16));
                glDisableVertexAttribArray(3);
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else if (stride == 52 || stride == 56)
            {
                // VertexPositionNormalTextureSkinned: float3 pos(0) + float3 normal(12) +
                // float2 uv(24) + float4 boneWeight(32) + ubyte4 boneIndices(48) [+ ubyte4 color(52)
                // for the stride-56 variant -- matches EasyGLGraphicsBackend's own stride-56 comment:
                // "the stride-52 layout with a per-vertex Color appended at the end (offset 52)
                // rather than inserted mid-layout"]. Bone indices are read as unnormalized
                // GL_UNSIGNED_BYTE (converted to float 0..255 exactly, no precision loss), NOT
                // normalized to [0,1] -- the vertex shader casts them back to int bone-array indices.
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(24));
                glEnableVertexAttribArray(4);
                glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(32));
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(48));
                glDisableVertexAttribArray(6);
                if (stride == 56)
                {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(52));
                }
                else
                {
                    glDisableVertexAttribArray(1);
                    glVertexAttrib4f(1, 1, 1, 1, 1);
                }
            }
            else if (stride == 48 || stride == 68)
            {
                // VertexPositionNormalTangentTexture(Skinned): float3 pos(0) + float3 normal(12) +
                // float4 tangent(24, xyz + bitangent handedness in w) + float2 uv(40) [+ float4
                // boneWeight(48) + ubyte4 boneIndices(64) for the stride-68 skinned variant --
                // matches EasyGLGraphicsBackend's own identical stride-48/68 layout comments].
                // PbrEffect/SkinnedPbrEffect.
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glEnableVertexAttribArray(6);
                glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(24));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(40));
                glDisableVertexAttribArray(1);
                glVertexAttrib4f(1, 1, 1, 1, 1);
                if (stride == 68)
                {
                    glEnableVertexAttribArray(4);
                    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(48));
                    glEnableVertexAttribArray(5);
                    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(64));
                }
                else
                {
                    glDisableVertexAttribArray(4);
                    glDisableVertexAttribArray(5);
                }
            }
            else if (stride >= 32)
            {
                glDisableVertexAttribArray(1);
                glVertexAttrib4f(1, 1, 1, 1, 1);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(24));
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(12));
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else
            {
                throw std::runtime_error("OPENGL2: unsupported vertex stride");
            }
        }

        // Task 1080 (this backend's own follow-up item -- see plan_opengl2.md): maps a
        // VertexElementFormat to the (component count, GL type, normalized) triple
        // glVertexAttribPointer() needs, mirroring EasyGLGraphicsBackend::
        // DescribeVertexElementFormat's identical table -- except Byte4, which GL 2.1 cannot read
        // as a true integer attribute (glVertexAttribIPointer is GL 3.0+, unlike EasyGL's GLES3
        // path): read as an UNNORMALIZED unsigned-byte-to-float conversion instead (exact for
        // byte-range values, no precision loss), the same convention already used for
        // aBoneIndices throughout this file -- a vertex shader that needs true integer semantics
        // rounds back to int itself (`int(x+0.5)`, as SkinnedEffect's own shader already does).
        struct VertexAttribFormat { int componentCount; GLenum type; GLboolean normalized; };
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:           return {1, GL_FLOAT, GL_FALSE};
                case VertexElementFormat::Vector2:          return {2, GL_FLOAT, GL_FALSE};
                case VertexElementFormat::Vector3:          return {3, GL_FLOAT, GL_FALSE};
                case VertexElementFormat::Vector4:          return {4, GL_FLOAT, GL_FALSE};
                case VertexElementFormat::Color:            return {4, GL_UNSIGNED_BYTE, GL_TRUE};
                case VertexElementFormat::Byte4:            return {4, GL_UNSIGNED_BYTE, GL_FALSE};
                case VertexElementFormat::Short2:           return {2, GL_SHORT, GL_FALSE};
                case VertexElementFormat::Short4:           return {4, GL_SHORT, GL_FALSE};
                case VertexElementFormat::NormalizedShort2: return {2, GL_SHORT, GL_TRUE};
                case VertexElementFormat::NormalizedShort4: return {4, GL_SHORT, GL_TRUE};
                // GL_HALF_FLOAT is core since GL 3.0 (ARB_half_float_vertex on GL 2.1 hardware,
                // not guaranteed) -- accepted for table completeness/EasyGL parity, but real GL
                // 2.1-only hardware without that extension would fail to render a declaration
                // using these two formats specifically. Every other format above is genuinely
                // GL 2.1-safe (core since 1.1).
                case VertexElementFormat::HalfVector2:      return {2, GL_HALF_FLOAT, GL_FALSE};
                case VertexElementFormat::HalfVector4:      return {4, GL_HALF_FLOAT, GL_FALSE};
            }
            return {3, GL_FLOAT, GL_FALSE};
        }

        // Derives a conventional attribute name from a VertexElement's semantic usage, matching
        // this file's own established naming (aPosition/aNormal/aTexCoord/aColor/aBoneWeight/
        // aBoneIndices/aTangent -- the same names LinkProgram() globally binds to fixed locations
        // 0-6 for every built-in program). GLSL 1.10/1.20 (this file's target) has no
        // `layout(location=N)` qualifier, unlike EasyGL's GLES3 declaration path (which binds
        // location=element's own index in the list, name-independent) -- so a genuinely custom
        // vertex layout is matched to a program's attributes by NAME here instead, via
        // glGetAttribLocation() in BindVertexAttributesForDeclaration() below. A custom
        // ShaderEffect vertex shader consuming a custom VertexDeclaration MUST use these same
        // names for the corresponding usage to receive that data at all -- the same
        // attribute-naming contract EffectBackend's own doc comment already establishes for the
        // fixed-stride case, now extended to arbitrary declarations too. usageIndex 0 keeps the
        // bare name; usageIndex>0 (e.g. a second TEXCOORD1 stream) appends the index.
        std::string VertexElementAttribName(VertexElementUsage usage, int usageIndex)
        {
            const char* base = "aUnknown";
            switch (usage)
            {
                case VertexElementUsage::Position:         base = "aPosition"; break;
                case VertexElementUsage::Color:             base = "aColor"; break;
                case VertexElementUsage::TextureCoordinate: base = "aTexCoord"; break;
                case VertexElementUsage::Normal:            base = "aNormal"; break;
                case VertexElementUsage::Binormal:          base = "aBinormal"; break;
                case VertexElementUsage::Tangent:           base = "aTangent"; break;
                case VertexElementUsage::BlendIndices:      base = "aBoneIndices"; break;
                case VertexElementUsage::BlendWeight:       base = "aBoneWeight"; break;
                case VertexElementUsage::Depth:             base = "aDepth"; break;
                case VertexElementUsage::Fog:                base = "aFog"; break;
                case VertexElementUsage::PointSize:         base = "aPointSize"; break;
                case VertexElementUsage::Sample:            base = "aSample"; break;
                case VertexElementUsage::TessellateFactor:  base = "aTessellateFactor"; break;
            }
            if (usageIndex == 0) return base;
            return std::string(base) + std::to_string(usageIndex);
        }

        // Task 1080: generic, program-attribute-name-driven layout binding for a VertexBuffer
        // that was given a genuinely custom VertexDeclaration (via VertexBuffer::SetDataRaw() --
        // see IVertexBufferBackend::SetVertexDeclaration()'s own doc comment), instead of the
        // fixed byte-stride dispatch BindVertexAttributesForStride() recognizes. Elements whose
        // derived name the currently-bound program doesn't declare are silently skipped
        // (glGetAttribLocation returns -1 for an unused/optimized-out attribute -- not an error).
        void BindVertexAttributesForDeclaration(GLuint program, GLuint vboId, std::size_t stride,
                                                const std::vector<VertexElement>& declaration)
        {
            glBindBuffer(GL_ARRAY_BUFFER, vboId);
            // Every location this backend's built-in programs ever use must start disabled, so a
            // stale enabled pointer from a PREVIOUS (stride-based or declaration-based) draw can't
            // leak into this one -- mirrors the same defensiveness BindVertexAttributesForStride()
            // already applies between its own branches.
            for (GLuint loc = 0; loc <= 6; ++loc)
                glDisableVertexAttribArray(loc);

            for (const VertexElement& element : declaration)
            {
                const std::string name = VertexElementAttribName(
                    element.getVertexElementUsageProperty(), element.getUsageIndexProperty());
                const GLint location = glGetAttribLocation(program, name.c_str());
                if (location < 0)
                    continue;
                const VertexAttribFormat desc = DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                glEnableVertexAttribArray(static_cast<GLuint>(location));
                glVertexAttribPointer(static_cast<GLuint>(location), desc.componentCount, desc.type, desc.normalized,
                                      static_cast<GLsizei>(stride),
                                      reinterpret_cast<void*>(static_cast<std::uintptr_t>(element.getOffsetProperty())));
            }
        }

        class Tex final : public ITextureBackend
        {
        public:
            GLuint id{};
            int w{};
            int h{};
            // Task 924 precedent (EasyGLTextureBackend's own identical field): real mip level
            // count this texture was created for (1 = no mipmapping).
            int mipLevels{1};

            explicit Tex(const ImageData& data)
                : w(data.width), h(data.height), mipLevels(data.mipLevels > 0 ? data.mipLevels : 1)
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                // Task 924: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- otherwise a
                // mip-requiring TextureFilter (see ToGLMinMagFilter) treats this as an incomplete
                // mipmap chain (GL's own default max level is 1000) and renders solid black, even
                // for an ordinary single-level (mipLevels==1) texture that never uploads any level
                // beyond 0.
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
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

            // Task 924 follow-up: levels beyond 0 have no storage yet at this point (the
            // constructor above only allocates level 0), so this must be a full glTexImage2D
            // (matching EasyGLTextureBackend::UpdatePixelsLevel's own set_image_2d call), not a
            // glTexSubImage2D into non-existent storage.
            void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override
            {
                glBindTexture(GL_TEXTURE_2D, id);
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            }
        };

        // XNA DepthFormat enum ordinals: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3.
        // Mirrors EasyGLGraphicsBackend's own MapDepthFormat.
        bool MapDepthFormat(int depthFormat, GLenum& outInternalFormat, GLenum& outAttachment)
        {
            switch (depthFormat)
            {
                case 1: outInternalFormat = GL_DEPTH_COMPONENT16; outAttachment = GL_DEPTH_ATTACHMENT; return true;
                case 2: outInternalFormat = GL_DEPTH_COMPONENT24; outAttachment = GL_DEPTH_ATTACHMENT; return true;
                case 3: outInternalFormat = GL_DEPTH24_STENCIL8; outAttachment = GL_DEPTH_STENCIL_ATTACHMENT; return true;
                default: return false; // DepthFormat::None = 0
            }
        }

        int CalculateRenderTargetMipLevels(int w, int h)
        {
            int levels = 1;
            int size = std::max(w, h);
            while (size > 1) { size /= 2; ++levels; }
            return levels;
        }

        // FBO render target: a color texture (sampled later exactly like Tex, same
        // GL_LINEAR/GL_CLAMP_TO_EDGE defaults) plus an optional depth/(stencil) renderbuffer.
        // Optionally multisampled (renders into a separate MSAA color+depth renderbuffer pair,
        // resolved into colorTex via glBlitFramebuffer on unbind) and/or mipmapped (colorTex's
        // full mip chain is pre-allocated, then regenerated from level 0 on unbind) -- mirrors
        // EasyGLRenderTargetBackend's identical resolve-then-mipmap order (FNA3D's own
        // OPENGL_ResolveTarget behavior).
        class RenderTarget final : public IRenderTargetBackend
        {
        public:
            GLuint fbo{};
            GLuint colorTex{};
            GLuint depthRbo{};
            GLuint msaaColorRbo{};
            GLuint msaaDepthRbo{};
            GLuint resolveFbo{};
            int w{};
            int h{};
            int levelCount{1};
            int multiSampleCount{};

            RenderTarget(int width, int height, int depthFormat, bool mipMap, int requestedSamples)
                : w(width), h(height), levelCount(mipMap ? CalculateRenderTargetMipLevels(width, height) : 1),
                  multiSampleCount(requestedSamples)
            {
                if (multiSampleCount > 0)
                {
                    GLint maxSamples = 0;
                    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
                    if (maxSamples > 0 && multiSampleCount > maxSamples)
                        multiSampleCount = maxSamples;
                }

                glGenTextures(1, &colorTex);
                glBindTexture(GL_TEXTURE_2D, colorTex);
                // Pre-allocate GPU storage for every mip level (not just level 0): the chain is
                // regenerated from level 0 via glGenerateMipmap() on unbind, and levels 1+ need
                // defined storage before that write target is GL-complete.
                {
                    int levelW = w, levelH = h;
                    for (int level = 0; level < levelCount; ++level)
                    {
                        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                        levelW = std::max(1, levelW / 2);
                        levelH = std::max(1, levelH / 2);
                    }
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelCount - 1);

                glGenFramebuffers(1, &fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);

                if (multiSampleCount > 0)
                {
                    // Render into a multisampled color renderbuffer; colorTex is only ever the
                    // single-sample resolve target, written by UnbindAsRenderTarget()'s blit.
                    glGenRenderbuffers(1, &msaaColorRbo);
                    glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRbo);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount, GL_RGBA8, w, h);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRbo);

                    glGenFramebuffers(1, &resolveFbo);
                    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
                    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                }
                else
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
                }

                GLenum depthInternalFormat = 0, depthAttachment = 0;
                if (MapDepthFormat(depthFormat, depthInternalFormat, depthAttachment))
                {
                    glGenRenderbuffers(1, &depthRbo);
                    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
                    if (multiSampleCount > 0)
                        glRenderbufferStorageMultisample(GL_RENDERBUFFER, multiSampleCount, depthInternalFormat, w, h);
                    else
                        glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, w, h);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER, depthRbo);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            ~RenderTarget() override
            {
                if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
                if (msaaColorRbo) glDeleteRenderbuffers(1, &msaaColorRbo);
                if (resolveFbo) glDeleteFramebuffers(1, &resolveFbo);
                if (fbo) glDeleteFramebuffers(1, &fbo);
                if (colorTex) glDeleteTextures(1, &colorTex);
            }

            int GetWidth() const override { return w; }
            int GetHeight() const override { return h; }
            SDL_Texture* GetNativeTexture() const override { return nullptr; }
            void BindGL() const override { glBindTexture(GL_TEXTURE_2D, colorTex); }
            unsigned int GetColorGLHandle() const override { return colorTex; }
            int GetMultiSampleCount() const override { return multiSampleCount; }

            void BindAsRenderTarget() override { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }

            void UnbindAsRenderTarget() override
            {
                if (multiSampleCount > 0)
                {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
                    // A multisample-resolving blit (source sample count > 0, destination = 0)
                    // averages every sample per destination pixel regardless of the filter
                    // argument -- GL_NEAREST is used here because GL_LINEAR is invalid for this
                    // exact source/destination sample-count combination on strict implementations.
                    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                if (levelCount > 1)
                {
                    glBindTexture(GL_TEXTURE_2D, colorTex);
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // Texture2D::GetData() only reaches here when its own CPU-side pixel shadow is
            // unavailable (i.e. this is a real RenderTarget2D, not a SetData()-populated
            // texture) -- glGetTexImage reads the whole level, then the requested sub-rect is
            // copied out (same row-major layout convention as every other texture in this
            // backend, verified self-consistent by OpenGL2_2D's own quadrant UV test).
            void GetData(int level, int x, int y, int rw, int rh, void* data, int /*dataLength*/) const override
            {
                int levelW = w, levelH = h;
                for (int i = 0; i < level; ++i) { levelW = std::max(1, levelW / 2); levelH = std::max(1, levelH / 2); }
                std::vector<uint8_t> full(static_cast<std::size_t>(levelW) * levelH * 4);
                glBindTexture(GL_TEXTURE_2D, colorTex);
                glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
                auto* dst = static_cast<uint8_t*>(data);
                for (int row = 0; row < rh; ++row)
                    std::memcpy(dst + static_cast<std::size_t>(row) * rw * 4,
                               full.data() + (static_cast<std::size_t>(y + row) * levelW + x) * 4, rw * 4);
            }
        };

        // Desktop GL 2.1 has the real ARB_occlusion_query GL_SAMPLES_PASSED target (core since
        // GL 1.5), giving an exact pixel count -- unlike EasyGLGraphicsBackend's GLES3
        // GL_ANY_SAMPLES_PASSED, which can only report 0 or 1.
        class OcclusionQuery final : public IOcclusionQueryBackend
        {
        public:
            GLuint id{};

            OcclusionQuery() { glGenQueries(1, &id); }
            ~OcclusionQuery() override { if (id) glDeleteQueries(1, &id); }

            void Begin() override { glBeginQuery(GL_SAMPLES_PASSED, id); }
            void End() override { glEndQuery(GL_SAMPLES_PASSED); }

            bool IsComplete() const override
            {
                GLint available = 0;
                glGetQueryObjectiv(id, GL_QUERY_RESULT_AVAILABLE, &available);
                return available != 0;
            }

            int PixelCount() const override
            {
                // Matches FNA/D3D9's OcclusionQuery.PixelCount contract: blocks until the result
                // is available rather than returning a stale/zero value for an incomplete query.
                GLuint result = 0;
                glGetQueryObjectuiv(id, GL_QUERY_RESULT, &result);
                return static_cast<int>(result);
            }
        };

        // GL_TEXTURE_CUBE_MAP (ARB_texture_cube_map, core since GL 1.3). CubeMapFace's own
        // ordinals (PositiveX=0, NegativeX=1, PositiveY=2, NegativeY=3, PositiveZ=4,
        // NegativeZ=5) already match GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z's own
        // consecutive enum values in the same order, so `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`
        // needs no separate mapping table.
        class TextureCubeBackend final : public ITextureCubeBackend
        {
        public:
            GLuint id{};
            int size{};
            int levelCount{1};

            TextureCubeBackend(int cubeSize, bool mipMap)
                : size(cubeSize), levelCount(mipMap ? CalculateRenderTargetMipLevels(cubeSize, cubeSize) : 1)
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_CUBE_MAP, id);
                for (int face = 0; face < 6; ++face)
                {
                    int levelSize = size;
                    for (int level = 0; level < levelCount; ++level)
                    {
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA,
                                    levelSize, levelSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                        levelSize = std::max(1, levelSize / 2);
                    }
                }
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
            }

            ~TextureCubeBackend() override { if (id) glDeleteTextures(1, &id); }

            void BindGL() const override { glBindTexture(GL_TEXTURE_CUBE_MAP, id); }

            void SetData(int face, int level, int x, int y, int w, int h, const void* data, int /*dataLength*/) override
            {
                glBindTexture(GL_TEXTURE_CUBE_MAP, id);
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
            }

            void GetData(int face, int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
            {
                int levelSize = size;
                for (int i = 0; i < level; ++i) levelSize = std::max(1, levelSize / 2);
                std::vector<uint8_t> full(static_cast<std::size_t>(levelSize) * levelSize * 4);
                glBindTexture(GL_TEXTURE_CUBE_MAP, id);
                glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
                auto* dst = static_cast<uint8_t*>(data);
                for (int row = 0; row < h; ++row)
                    std::memcpy(dst + static_cast<std::size_t>(row) * w * 4,
                               full.data() + (static_cast<std::size_t>(y + row) * levelSize + x) * 4, w * 4);
            }
        };

        // FBO cube-map render target: one shared FBO whose color attachment is re-pointed at
        // whichever face is currently active (glFramebufferTexture2D with
        // GL_TEXTURE_CUBE_MAP_POSITIVE_X+face), plus one depth/(stencil) renderbuffer shared
        // across all 6 faces (matches the common convention of clearing depth per-face rather
        // than needing 6 independent depth buffers). Single-sample only -- MSAA cube render
        // targets are a follow-up item (plan_opengl2.md), unlike RenderTarget2D's own MSAA support.
        class RenderTargetCubeBackend final : public IRenderTargetCubeBackend
        {
        public:
            GLuint fbo{};
            GLuint colorTex{};
            GLuint depthRbo{};
            int size{};
            int levelCount{1};
            int boundFace{-1};

            RenderTargetCubeBackend(int cubeSize, int depthFormat, bool mipMap)
                : size(cubeSize), levelCount(mipMap ? CalculateRenderTargetMipLevels(cubeSize, cubeSize) : 1)
            {
                glGenTextures(1, &colorTex);
                glBindTexture(GL_TEXTURE_CUBE_MAP, colorTex);
                for (int face = 0; face < 6; ++face)
                {
                    int levelSize = size;
                    for (int level = 0; level < levelCount; ++level)
                    {
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA,
                                    levelSize, levelSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                        levelSize = std::max(1, levelSize / 2);
                    }
                }
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levelCount - 1);

                glGenFramebuffers(1, &fbo);
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, colorTex, 0);
                boundFace = 0;

                GLenum depthInternalFormat = 0, depthAttachment = 0;
                if (MapDepthFormat(depthFormat, depthInternalFormat, depthAttachment))
                {
                    glGenRenderbuffers(1, &depthRbo);
                    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
                    glRenderbufferStorage(GL_RENDERBUFFER, depthInternalFormat, size, size);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER, depthRbo);
                }

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            ~RenderTargetCubeBackend() override
            {
                if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
                if (fbo) glDeleteFramebuffers(1, &fbo);
                if (colorTex) glDeleteTextures(1, &colorTex);
            }

            int GetSize() const override { return size; }
            void BindGL() const override { glBindTexture(GL_TEXTURE_CUBE_MAP, colorTex); }
            unsigned int GetGLHandle() const override { return colorTex; }

            void BindAsRenderTargetFace(int face) override
            {
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                if (face != boundFace)
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, colorTex, 0);
                    boundFace = face;
                }
            }

            void UnbindAsRenderTarget() override
            {
                if (levelCount > 1)
                {
                    glBindTexture(GL_TEXTURE_CUBE_MAP, colorTex);
                    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // Texture2D-style GetData isn't part of ITextureCubeBackend (only SetData is
            // pure-virtual there) -- read back via glGetTexImage exactly like TextureCubeBackend.
            void GetData(int face, int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
            {
                int levelSize = size;
                for (int i = 0; i < level; ++i) levelSize = std::max(1, levelSize / 2);
                std::vector<uint8_t> full(static_cast<std::size_t>(levelSize) * levelSize * 4);
                glBindTexture(GL_TEXTURE_CUBE_MAP, colorTex);
                glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
                auto* dst = static_cast<uint8_t*>(data);
                for (int row = 0; row < h; ++row)
                    std::memcpy(dst + static_cast<std::size_t>(row) * w * 4,
                               full.data() + (static_cast<std::size_t>(y + row) * levelSize + x) * 4, w * 4);
            }
        };

        // GL_TEXTURE_3D + glTexImage3D/glTexSubImage3D are core desktop GL since 1.2 (no
        // extension needed, unlike GLES).
        class Texture3DBackend final : public ITexture3DBackend
        {
        public:
            GLuint id{};
            int w{}, h{}, d{};
            int levelCount{1};

            Texture3DBackend(int width, int height, int depth, bool mipMap)
                : w(width), h(height), d(depth)
                , levelCount(mipMap ? CalculateRenderTargetMipLevels(width, height) : 1)
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_3D, id);
                // Task (plan_opengl2.md follow-up, session 8): real FNA3D_Driver_OpenGL.c
                // OPENGL_CreateTexture3D confirms depth halves per level (SDL_max(depth >> i, 1))
                // exactly like width/height, even though Texture3D.cpp's own CalculateMipLevels
                // (matching Texture3D.cs's LevelCount formula) deliberately excludes depth from the
                // LEVEL COUNT itself -- those are two separate facts, both honored here.
                for (int level = 0; level < levelCount; ++level)
                {
                    glTexImage3D(GL_TEXTURE_3D, level, GL_RGBA,
                                std::max(1, w >> level), std::max(1, h >> level), std::max(1, d >> level),
                                0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                }
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                // Task 924 precedent (Tex/TextureCubeBackend's own identical clamp): otherwise a
                // mip-requiring TextureFilter treats this as an incomplete mipmap chain.
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
            }

            ~Texture3DBackend() override { if (id) glDeleteTextures(1, &id); }

            void BindGL() const override { glBindTexture(GL_TEXTURE_3D, id); }

            void SetData(int level, int x, int y, int z, int sw, int sh, int sd, const void* data, int /*dataLength*/) override
            {
                glBindTexture(GL_TEXTURE_3D, id);
                glTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, sw, sh, sd, GL_RGBA, GL_UNSIGNED_BYTE, data);
            }

            void GetData(int level, int x, int y, int z, int sw, int sh, int sd, void* data, int /*dataLength*/) const override
            {
                // Task (plan_opengl2.md follow-up, session 8): glGetTexImage returns THIS level's
                // own (smaller, halved-per-level) dimensions, not level 0's -- both the buffer size
                // and the row/slice stride math below must use the level's real dimensions, not
                // w/h/d (which are always level 0's). Exposed by the mip-storage-allocation fix
                // above: a level>0 read previously always returned zeroed/garbage tail bytes past
                // the level's real (much smaller) data because the stride assumed level 0's size.
                const int levelW = std::max(1, w >> level);
                const int levelH = std::max(1, h >> level);
                const int levelD = std::max(1, d >> level);
                std::vector<uint8_t> full(static_cast<std::size_t>(levelW) * levelH * levelD * 4);
                glBindTexture(GL_TEXTURE_3D, id);
                glGetTexImage(GL_TEXTURE_3D, level, GL_RGBA, GL_UNSIGNED_BYTE, full.data());
                auto* dst = static_cast<uint8_t*>(data);
                for (int slice = 0; slice < sd; ++slice)
                    for (int row = 0; row < sh; ++row)
                        std::memcpy(dst + (static_cast<std::size_t>(slice) * sh + row) * sw * 4,
                                   full.data() + ((static_cast<std::size_t>(z + slice) * levelH + (y + row)) * levelW + x) * 4,
                                   sw * 4);
            }
        };

        class VB final : public IVertexBufferBackend
        {
        public:
            GLuint id{};
            int count{};
            std::size_t stride{};
            // Vertex capacity requested at CreateVertexBuffer() time (0 if unknown/unspecified) --
            // used by SetDataWithOptions(Discard) below to size the orphaned allocation to the
            // buffer's full capacity rather than just the (possibly smaller) count of this upload,
            // matching EasyGLVertexBufferBackend::uploadWithOptions's identical strategy.
            int capacity{};
            bool gpuAllocated{};
            // Task 1080 (this backend's own follow-up item, see plan_opengl2.md): non-empty only
            // when the caller supplied a genuinely custom VertexDeclaration via
            // VertexBuffer::SetDataRaw() -- see SetVertexDeclaration() below and
            // BindVertexAttributesForDeclaration()'s own doc comment for how this is consumed.
            std::vector<VertexElement> declaration;

            explicit VB(int vertexCapacity = 0) : capacity(vertexCapacity) { glGenBuffers(1, &id); }
            ~VB() override { if (id) glDeleteBuffers(1, &id); }

            void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
            {
                SetDataWithOptions(data, vertex_count, stride_in_bytes, SetDataOptions::None);
            }

            void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                    SetDataOptions options) override
            {
                count = vertex_count;
                stride = stride_in_bytes;
                const auto byteCount = static_cast<GLsizeiptr>(vertex_count) * static_cast<GLsizeiptr>(stride_in_bytes);
                glBindBuffer(GL_ARRAY_BUFFER, id);
                if (options == SetDataOptions::Discard)
                {
                    // Orphan strategy: re-specify storage (a fresh, un-aliased allocation on any
                    // real driver) so in-flight draws reading the old data don't stall the pipeline,
                    // then fill it with a sub-data upload.
                    const auto total = capacity > 0
                        ? static_cast<GLsizeiptr>(capacity) * static_cast<GLsizeiptr>(stride_in_bytes)
                        : byteCount;
                    glBufferData(GL_ARRAY_BUFFER, total, nullptr, GL_STREAM_DRAW);
                    if (byteCount > 0) glBufferSubData(GL_ARRAY_BUFFER, 0, byteCount, data);
                    gpuAllocated = true;
                }
                else if (options == SetDataOptions::NoOverwrite && gpuAllocated)
                {
                    // Driver hint that no in-flight data is overwritten -- safe to sub-data into
                    // the existing allocation instead of paying for a full glBufferData.
                    glBufferSubData(GL_ARRAY_BUFFER, 0, byteCount, data);
                }
                else
                {
                    glBufferData(GL_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
                    gpuAllocated = true;
                }
            }

            void SetVertexDeclaration(const std::vector<VertexElement>& elements) override
            {
                declaration = elements;
            }

            int GetVertexCount() const override { return count; }
        };

        class IB final : public IIndexBufferBackend
        {
        public:
            GLuint id{};
            int count{};
            bool thirtyTwoBit{};
            int capacity{};   // index capacity requested at CreateIndexBufferNN() time; see VB::capacity.
            bool gpuAllocated{};

            explicit IB(bool isThirtyTwoBit, int indexCapacity = 0)
                : thirtyTwoBit(isThirtyTwoBit), capacity(indexCapacity) { glGenBuffers(1, &id); }
            ~IB() override { if (id) glDeleteBuffers(1, &id); }

            void uploadWithOptions(const void* data, int index_count, GLsizeiptr elemSize, SetDataOptions options)
            {
                count = index_count;
                const auto byteCount = static_cast<GLsizeiptr>(index_count) * elemSize;
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
                if (options == SetDataOptions::Discard)
                {
                    const auto total = capacity > 0 ? static_cast<GLsizeiptr>(capacity) * elemSize : byteCount;
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, total, nullptr, GL_STREAM_DRAW);
                    if (byteCount > 0) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, byteCount, data);
                    gpuAllocated = true;
                }
                else if (options == SetDataOptions::NoOverwrite && gpuAllocated)
                {
                    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, byteCount, data);
                }
                else
                {
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, byteCount, data, GL_DYNAMIC_DRAW);
                    gpuAllocated = true;
                }
            }

            void SetData16(const void* data, int index_count) override
            {
                thirtyTwoBit = false;
                uploadWithOptions(data, index_count, 2, SetDataOptions::None);
            }

            void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override
            {
                thirtyTwoBit = false;
                uploadWithOptions(data, index_count, 2, options);
            }

            // Desktop OpenGL (unlike GLES2 without an extension) has always accepted
            // GL_UNSIGNED_INT indices in glDrawElements, so 32-bit index buffers just work here.
            void SetData32(const void* data, int index_count) override
            {
                thirtyTwoBit = true;
                uploadWithOptions(data, index_count, 4, SetDataOptions::None);
            }

            void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override
            {
                thirtyTwoBit = true;
                uploadWithOptions(data, index_count, 4, options);
            }

            int GetIndexCount() const override { return count; }
            bool IsThirtyTwoBit() const override { return thirtyTwoBit; }
        };

        // Shared by Sprite::Draw()'s built-in path and DrawWithCustomEffect(): the sprite quad's
        // 4 corners in SCREEN space (destination rect rotated around origin) plus their UVs
        // (source rect, with SpriteEffects flips already applied). Deliberately stops here --
        // the two callers differ in what happens next (clip-space pre-transform on the CPU vs.
        // raw upload + a shader-side MatrixTransform uniform), so that part is NOT shared.
        struct SpriteScreenCorner { float x, y, u, v; };

        void ComputeSpriteScreenCorners(const Rectangle& destination, const Rectangle& source,
                                        int texW, int texH, float rotation, const Vector2& origin,
                                        SpriteEffects effects, SpriteScreenCorner out[4])
        {
            float u0 = static_cast<float>(source.X) / texW;
            float v0 = static_cast<float>(source.Y) / texH;
            float u1 = static_cast<float>(source.X + source.Width) / texW;
            float v1 = static_cast<float>(source.Y + source.Height) / texH;
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
            for (int i = 0; i < 4; ++i)
            {
                out[i].x = localCorners[i][0] * cosR - localCorners[i][1] * sinR + destination.X + origin.X;
                out[i].y = localCorners[i][0] * sinR + localCorners[i][1] * cosR + destination.Y + origin.Y;
                out[i].u = uv[i][0];
                out[i].v = uv[i][1];
            }
        }

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
            // NOXNA follow-up (plan_opengl2.md): only Draw()'s direct 3D counterpart
            // (customEffectBackend in drawInternal()) was implemented first; this wires the same
            // EffectBackend infrastructure into the 2D SpriteBatch path too.
            void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }

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

                EnsureResources();

                if (customEffect_)
                {
                    DrawWithCustomEffect(texture, destination, source, color, rotation, origin, effects, layerDepth);
                    return;
                }

                // Task-1078-equivalent fix (see EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize's
                // own history): a draw into a bound RenderTarget2D must size its screen->clip
                // mapping to the RT, not the window/virtual resolution -- those only coincide when
                // the RT happens to match the window size. Deliberately LOGICAL-only (not
                // physical-window-aware): the actual GL viewport rectangle GraphicsDevice::
                // UpdateViewportFromWindow() applies (backend_->GetDefaultViewportRect()) already
                // maps NDC [-1,1] to the correct Letterbox/Overscan/Stretch physical sub-rectangle
                // by itself -- GL's own viewport transform does the logical->physical mapping, so
                // this CPU-side math only ever needs to reach clip space in LOGICAL units.
                int viewportWidth = 0, viewportHeight = 0;
                if (!backend_->GetCurrentRenderTarget2DSize(viewportWidth, viewportHeight))
                    backend_->GetViewportSize(viewportWidth, viewportHeight);
                if (viewportWidth <= 0 || viewportHeight <= 0)
                    return;

                const float r = color.getRProperty() / 255.0f;
                const float g = color.getGProperty() / 255.0f;
                const float b = color.getBProperty() / 255.0f;
                const float a = color.getAProperty() / 255.0f;

                SpriteScreenCorner sc[4];
                ComputeSpriteScreenCorners(destination, source, texture.GetWidth(), texture.GetHeight(),
                                          rotation, origin, effects, sc);

                struct SpriteVertex { float x, y, z; float r, g, b, a; float u, v; };
                SpriteVertex corners[4];
                for (int i = 0; i < 4; ++i)
                {
                    // SpriteBatch.Begin(transformMatrix, ...)'s camera/scroll transform is applied
                    // in screen space, BEFORE the screen->clip ortho mapping below -- row-vector
                    // convention (v * transform_), matching EasyGLSpriteBatchBackend's own
                    // `transform_ * orthoM` combined-matrix order (only the XY affine part matters;
                    // SpriteBatch's transform is always 2D).
                    const float tx = sc[i].x * transform_.M11 + sc[i].y * transform_.M21 + transform_.M41;
                    const float ty = sc[i].x * transform_.M12 + sc[i].y * transform_.M22 + transform_.M42;
                    corners[i] = {(tx / viewportWidth) * 2.0f - 1.0f, 1.0f - (ty / viewportHeight) * 2.0f, layerDepth,
                                  r, g, b, a, sc[i].u, sc[i].v};
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
                texture.BindGL();
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
                    "void main(){gl_FragData[0]=texture2D(uTex,vTex)*vColor;}";
                program_ = LinkProgram(vertexSrc, fragmentSrc);
                glGenBuffers(1, &vbo_);
            }

            // SpriteBatch.Begin(..., Effect effect) path (Task follow-up, plan_opengl2.md): unlike
            // the built-in shader above (which pre-transforms every vertex to clip space on the
            // CPU, `gl_Position=vec4(aPosition,1.0)` with no projection uniform at all), a custom
            // effect's vertex shader is expected to apply a `MatrixTransform` uniform itself --
            // matches real XNA's own SpriteEffect.fx / SpriteBatch convention exactly (any custom
            // SpriteBatch effect ported from real XNA already has a `float4x4 MatrixTransform`
            // parameter; XNA throws if it's missing). Vertex positions are therefore uploaded RAW
            // (screen pixels), not pre-transformed, and `transform_` is folded into the SAME
            // uniform as the screen->clip ortho projection instead of being applied per-vertex.
            void DrawWithCustomEffect(const ITextureBackend& texture, const Rectangle& destination,
                                      const Rectangle& source, const Color& color, float rotation,
                                      const Vector2& origin, SpriteEffects effects, float layerDepth)
            {
                IEffectBackend* effectBackend = customEffect_->GetEffectBackendPtr();
                // Unlike EasyGLSpriteBatchBackend::FlushBatch() (which silently falls back to ITS
                // OWN built-in program when the custom effect fails to compile), this backend's
                // built-in shader expects a fundamentally different vertex convention (see above)
                // -- falling back would draw garbage, not a plausible approximation. Skip instead.
                if (!effectBackend || !effectBackend->IsValid())
                    return;

                customEffect_->Apply();

                // Deliberately LOGICAL-only, same reasoning as the built-in path above: the
                // actual GL viewport rectangle (backend_->GetDefaultViewportRect(), applied by
                // GraphicsDevice::UpdateViewportFromWindow()) already maps NDC to the correct
                // Letterbox/Overscan/Stretch physical sub-rectangle, so MatrixTransform only ever
                // needs to reach clip space in LOGICAL units.
                int viewportWidth = 0, viewportHeight = 0;
                if (!backend_->GetCurrentRenderTarget2DSize(viewportWidth, viewportHeight))
                    backend_->GetViewportSize(viewportWidth, viewportHeight);
                if (viewportWidth <= 0 || viewportHeight <= 0) return;

                const float r = color.getRProperty() / 255.0f;
                const float g = color.getGProperty() / 255.0f;
                const float b = color.getBProperty() / 255.0f;
                const float a = color.getAProperty() / 255.0f;

                SpriteScreenCorner sc[4];
                ComputeSpriteScreenCorners(destination, source, texture.GetWidth(), texture.GetHeight(),
                                          rotation, origin, effects, sc);

                struct SpriteVertex { float x, y, z; float r, g, b, a; float u, v; };
                const SpriteVertex corners[4] = {
                    {sc[0].x, sc[0].y, layerDepth, r, g, b, a, sc[0].u, sc[0].v},
                    {sc[1].x, sc[1].y, layerDepth, r, g, b, a, sc[1].u, sc[1].v},
                    {sc[2].x, sc[2].y, layerDepth, r, g, b, a, sc[2].u, sc[2].v},
                    {sc[3].x, sc[3].y, layerDepth, r, g, b, a, sc[3].u, sc[3].v},
                };
                const SpriteVertex quad[6] = {corners[0], corners[1], corners[2], corners[0], corners[2], corners[3]};

                const Matrix ortho = Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0f, -1.0f, 1.0f);
                const Matrix combined = transform_ * ortho;
                float matColMajor[16];
                combined.ToColumnMajor(matColMajor);
                effectBackend->SetUniformMat4("MatrixTransform", matColMajor);

                glBindBuffer(GL_ARRAY_BUFFER, vbo_);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, x)));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, r)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), reinterpret_cast<void*>(offsetof(SpriteVertex, u)));

                // Real XNA SpriteBatch always binds the drawn texture to sampler register/unit 0;
                // GLSL default-initializes an unset sampler uniform to 0 too (matching HLSL's
                // implicit register-0 default), so a custom shader doesn't need to explicitly set
                // its own sampler uniform for the common single-texture case.
                glActiveTexture(GL_TEXTURE0);
                texture.BindGL();
                const GLint glFilter = ToGLFilter(filter_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(addressU_));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(addressV_));

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }

            OpenGL2GraphicsBackend* backend_;
            bool begun_ = false;
            GLuint program_ = 0;
            GLuint vbo_ = 0;
            Matrix transform_ = Matrix::getIdentityProperty();
            int filter_ = 0;   // TextureFilter::Linear
            int addressU_ = 1; // TextureAddressMode::Clamp (matches SamplerState::LinearClamp,
            int addressV_ = 1; // the default SpriteBatch.Begin() falls back to)
            Effect* customEffect_ = nullptr;
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
#if defined(_WIN32)
        LoadWin32GLExtensions();
#endif
        LoadInstancingExtensions();
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
        if (dualTextureProgram_) glDeleteProgram(dualTextureProgram_);
        if (litProgram_) glDeleteProgram(litProgram_);
        if (envMapProgram_) glDeleteProgram(envMapProgram_);
        if (skinnedProgram_) glDeleteProgram(skinnedProgram_);
        if (pbrProgram_) glDeleteProgram(pbrProgram_);
        if (pbrSkinnedProgram_) glDeleteProgram(pbrSkinnedProgram_);
        if (defaultWhiteTexture2D_) glDeleteTextures(1, &defaultWhiteTexture2D_);
        if (defaultWhiteTextureCube_) glDeleteTextures(1, &defaultWhiteTextureCube_);
        if (defaultFlatNormalTexture2D_) glDeleteTextures(1, &defaultFlatNormalTexture2D_);
        if (mrtFboReady_) glDeleteFramebuffers(1, &mrtFbo_);
        if (context_) SDL_GL_DestroyContext(context_);
    }

    namespace
    {
        // Shared by every fragment shader below: fog blend (object-space vertex Z, matching
        // EasyGLGraphicsBackend's identical, documented simplification -- see
        // feedback_easygl_fog_object_space_only in this project's own notes) and, for the
        // textured variants, the AlphaTestEffect discard formula from GpuDrawParams::alphaTest's
        // own doc comment. Both default to no-op (uFogEnabled=0, uAlphaTest={0,0,1,1}) so every
        // program works unchanged for draws that don't use either feature.
        const char* kFogVertexChunk =
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;varying float vFogFactor;";
        const char* kFogVertexCompute =
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;";
        const char* kFogFragmentChunk =
            "varying float vFogFactor;uniform vec3 uFogColor;";
        const char* kFogFragmentApply = "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);";
        const char* kAlphaTestFragmentChunk = "uniform vec4 uAlphaTest;";
        const char* kAlphaTestFragmentApply =
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;";
    }

    void OpenGL2GraphicsBackend::ensurePrograms()
    {
        const std::string colorVertexSrc = std::string(
            "attribute vec3 aPosition;attribute vec4 aColor;uniform mat4 uWVP;varying vec4 vColor;") +
            kFogVertexChunk +
            "void main(){gl_Position=uWVP*vec4(aPosition,1.0);vColor=aColor;" + kFogVertexCompute + "}";
        const std::string colorFragmentSrc = std::string(
            "varying vec4 vColor;uniform vec4 uDiffuse;") + kFogFragmentChunk +
            "void main(){gl_FragData[0]=vColor*uDiffuse;" + kFogFragmentApply + "}";

        const std::string texturedVertexSrc = std::string(
            "attribute vec3 aPosition;attribute vec4 aColor;attribute vec2 aTexCoord;uniform mat4 uWVP;"
            "varying vec4 vColor;varying vec2 vTex;") + kFogVertexChunk +
            "void main(){gl_Position=uWVP*vec4(aPosition,1.0);vColor=aColor;vTex=aTexCoord;" + kFogVertexCompute + "}";
        const std::string texturedFragmentSrc = std::string(
            "varying vec4 vColor;varying vec2 vTex;uniform vec4 uDiffuse;uniform sampler2D uTex;") +
            kAlphaTestFragmentChunk + kFogFragmentChunk +
            "void main(){gl_FragData[0]=texture2D(uTex,vTex)*vColor*uDiffuse;" +
            kAlphaTestFragmentApply + kFogFragmentApply + "}";

        // DualTextureEffect: two samplers at the SAME texcoord, the classic lightmap technique
        // (base*2.0 lets a lightmap brighten beyond the base texture, matching
        // EasyGLGraphicsBackend::EnsureDualTextured3DProgram's identical formula).
        const std::string dualTextureFragmentSrc = std::string(
            "varying vec4 vColor;varying vec2 vTex;uniform vec4 uDiffuse;uniform sampler2D uTex;uniform sampler2D uTex2;") +
            kAlphaTestFragmentChunk + kFogFragmentChunk +
            "void main(){vec4 base=texture2D(uTex,vTex);base.rgb*=2.0;"
            "gl_FragData[0]=base*texture2D(uTex2,vTex)*vColor*uDiffuse;" +
            kAlphaTestFragmentApply + kFogFragmentApply + "}";

        // BasicEffect lighting: per-pixel Blinn-Phong, 3 directional lights + ambient + emissive +
        // specular (matches EasyGLGraphicsBackend::EnsureLit3DProgram's formula exactly). Always
        // per-pixel regardless of GpuDrawParams::preferPerPixelLighting -- matches this project's
        // own documented, accepted convention (see that field's doc comment: every backend except
        // D3D9 always renders per-pixel). uNormalMatrix is the real inverse-transpose of World's
        // upper-3x3 (see ComputeNormalMatrix3x3 below), correct under non-uniform scale too.
        const char* litVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat3 uNormalMatrix;"
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;"
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;"
            "void main(){"
            "gl_Position=uWVP*vec4(aPosition,1.0);"
            "vNormal=uNormalMatrix*aNormal;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*vec4(aPosition,1.0)).xyz;"
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;"
            "}";
        const char* litFragmentSrc =
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;"
            "uniform sampler2D uTex;uniform bool uTextureEnabled;uniform vec4 uDiffuse;"
            "uniform vec3 uAmbientColor;"
            "uniform vec3 uLight0Dir;uniform vec3 uLight0Diffuse;uniform vec3 uLight0Specular;"
            "uniform vec3 uLight1Dir;uniform vec3 uLight1Diffuse;uniform vec3 uLight1Specular;"
            "uniform vec3 uLight2Dir;uniform vec3 uLight2Diffuse;uniform vec3 uLight2Specular;"
            "uniform vec3 uSpecularColor;uniform float uSpecularPower;"
            "uniform vec3 uEyePosition;uniform vec3 uEmissiveColor;"
            "uniform vec4 uAlphaTest;uniform vec3 uFogColor;"
            "void main(){"
            "vec3 N=normalize(vNormal);"
            "vec3 E=normalize(uEyePosition-vWorldPos);"
            "float dotL0=dot(N,-uLight0Dir);float zeroL0=step(0.0,dotL0);float NdotL0=max(dotL0,0.0);"
            "float dotL1=dot(N,-uLight1Dir);float zeroL1=step(0.0,dotL1);float NdotL1=max(dotL1,0.0);"
            "float dotL2=dot(N,-uLight2Dir);float zeroL2=step(0.0,dotL2);float NdotL2=max(dotL2,0.0);"
            "vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;"
            "vec3 litRGB=lightSum*uDiffuse.rgb+uEmissiveColor;"
            "vec3 h0=normalize(E-uLight0Dir);float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);"
            "vec3 h1=normalize(E-uLight1Dir);float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);"
            "vec3 h2=normalize(E-uLight2Dir);float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);"
            "vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;"
            "vec4 texColor=uTextureEnabled?texture2D(uTex,vTex):vec4(1.0,1.0,1.0,1.0);"
            "gl_FragData[0]=texColor*vec4(litRGB,uDiffuse.a);"
            "gl_FragData[0].rgb+=specularRGB*gl_FragData[0].a;"
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;"
            "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);"
            "}";

        // EnvironmentMapEffect: reflection-mapped shading (matches
        // EasyGLGraphicsBackend::EnsureEnvMapped3DProgram's formula exactly -- same per-vertex
        // Fresnel evaluation, same litRGB = lightSum*diffuse+emissive composition, same
        // mix(baseColor,envSample*alpha,blendFactor)+specular*envSample.a*alpha blend). Uses the
        // same VertexPositionNormalTexture (stride>=32) layout as litProgram_.
        const char* envMapVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat3 uNormalMatrix;uniform vec3 uEyePosition;"
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;"
            "uniform float uEnvMapAmount;uniform float uFresnelEnabled;uniform float uFresnelFactor;"
            "varying vec3 vWorldNormal;varying vec3 vEyeDir;varying vec2 vTex;"
            "varying float vFogFactor;varying float vFresnel;"
            "void main(){"
            "gl_Position=uWVP*vec4(aPosition,1.0);"
            "vec3 worldPos=(uWorld*vec4(aPosition,1.0)).xyz;"
            "vec3 worldNormal=normalize(uNormalMatrix*aNormal);"
            "vec3 eyeVector=normalize(uEyePosition-worldPos);"
            "vWorldNormal=worldNormal;"
            "vEyeDir=eyeVector;"
            "vTex=aTexCoord;"
            "float viewAngle=dot(eyeVector,worldNormal);"
            "vFresnel=(uFresnelEnabled>0.5)?pow(max(1.0-abs(viewAngle),0.0),uFresnelFactor)*uEnvMapAmount:uEnvMapAmount;"
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;"
            "}";
        const char* envMapFragmentSrc =
            "varying vec3 vWorldNormal;varying vec3 vEyeDir;varying vec2 vTex;"
            "varying float vFogFactor;varying float vFresnel;"
            "uniform sampler2D uTex;uniform samplerCube uEnvMap;"
            "uniform vec4 uDiffuse;uniform vec3 uEmissiveColor;"
            "uniform vec3 uLight0Dir;uniform vec3 uLight0Diffuse;"
            "uniform vec3 uLight1Dir;uniform vec3 uLight1Diffuse;"
            "uniform vec3 uLight2Dir;uniform vec3 uLight2Diffuse;"
            "uniform vec3 uEnvMapSpecular;"
            "uniform vec4 uAlphaTest;uniform vec3 uFogColor;"
            "void main(){"
            "vec3 N=normalize(vWorldNormal);vec3 E=normalize(vEyeDir);"
            "float NdotL0=max(dot(N,-uLight0Dir),0.0);"
            "float NdotL1=max(dot(N,-uLight1Dir),0.0);"
            "float NdotL2=max(dot(N,-uLight2Dir),0.0);"
            "vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;"
            "vec3 litRGB=lightSum*uDiffuse.rgb+uEmissiveColor;"
            "vec4 texColor=texture2D(uTex,vTex);"
            "vec3 reflDir=reflect(-E,N);"
            "vec4 envSample=textureCube(uEnvMap,reflDir);"
            "vec3 baseColor=litRGB*texColor.rgb;"
            "float combinedAlpha=uDiffuse.a*texColor.a;"
            "vec3 rgb=mix(baseColor,envSample.rgb*combinedAlpha,vFresnel)+uEnvMapSpecular*envSample.a*combinedAlpha;"
            "gl_FragData[0]=vec4(rgb,combinedAlpha);"
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;"
            "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);"
            "}";

        // SkinnedEffect: bone-palette vertex skinning (matches
        // EasyGLGraphicsBackend::EnsureSkinnedProgram's formula exactly -- same
        // skinMat=sum(uBones[index]*weight) for the first WeightsPerVertex pairs, same
        // degenerate-blend-normal guard, same litRGB=lightSum*diffuse+emissive composition, same
        // vertex-color-modulates-diffuse-and-specular ordering). GL 2.1 has no integer vertex
        // attributes (glVertexAttribIPointer is GL 3.0+), so bone indices are uploaded as an
        // unnormalized GL_UNSIGNED_BYTE-as-float attribute and rounded back to int in the shader
        // -- exact for byte-range values, no precision loss. Vertex-shader dynamic (non-constant)
        // array indexing of a uniform array is valid GLSL back to 1.10 (only fragment shaders had
        // historical restrictions), so `uBones[i]` with a non-constant `i` is portable GL 2.1.
        const char* skinnedVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;"
            "attribute vec4 aBoneWeight;attribute vec4 aBoneIndices;attribute vec4 aColor;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat4 uBones[72];uniform int uWeightsPerVertex;"
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;"
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;varying vec4 vColor;"
            "void main(){"
            "int i0=int(aBoneIndices.x+0.5);int i1=int(aBoneIndices.y+0.5);"
            "int i2=int(aBoneIndices.z+0.5);int i3=int(aBoneIndices.w+0.5);"
            "mat4 skinMat=uBones[i0]*aBoneWeight.x;"
            "if(uWeightsPerVertex>=2) skinMat+=uBones[i1]*aBoneWeight.y;"
            "if(uWeightsPerVertex>=4) skinMat+=uBones[i2]*aBoneWeight.z+uBones[i3]*aBoneWeight.w;"
            "vec4 skinnedPos=skinMat*vec4(aPosition,1.0);"
            "gl_Position=uWVP*skinnedPos;"
            // mat3(mat4) truncation requires GLSL 1.20 (this file targets 1.10, matching every
            // other program here, which never needs this construct); built manually instead.
            "mat3 skinMat3=mat3(skinMat[0].xyz,skinMat[1].xyz,skinMat[2].xyz);"
            "vec3 skinnedNormal=skinMat3*aNormal;"
            "float skinnedNormalLen=length(skinnedNormal);"
            "vNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*skinnedPos).xyz;"
            "vColor=aColor;"
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;"
            "}";
        const char* skinnedFragmentSrc =
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;varying vec4 vColor;"
            "uniform sampler2D uTex;uniform vec4 uDiffuse;uniform vec3 uEmissiveColor;"
            "uniform vec3 uLight0Dir;uniform vec3 uLight0Diffuse;uniform vec3 uLight0Specular;"
            "uniform vec3 uLight1Dir;uniform vec3 uLight1Diffuse;uniform vec3 uLight1Specular;"
            "uniform vec3 uLight2Dir;uniform vec3 uLight2Diffuse;uniform vec3 uLight2Specular;"
            "uniform vec3 uSpecularColor;uniform float uSpecularPower;uniform vec3 uEyePosition;"
            "uniform vec4 uAlphaTest;uniform vec3 uFogColor;uniform float uVertexColorEnabled;"
            "void main(){"
            "vec3 N=normalize(vNormal);vec3 E=normalize(uEyePosition-vWorldPos);"
            "float dotL0=dot(N,-uLight0Dir);float zeroL0=step(0.0,dotL0);float NdotL0=max(dotL0,0.0);"
            "float dotL1=dot(N,-uLight1Dir);float zeroL1=step(0.0,dotL1);float NdotL1=max(dotL1,0.0);"
            "float dotL2=dot(N,-uLight2Dir);float zeroL2=step(0.0,dotL2);float NdotL2=max(dotL2,0.0);"
            "vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;"
            "vec3 litRGB=lightSum*uDiffuse.rgb+uEmissiveColor;"
            "vec3 h0=normalize(E-uLight0Dir);float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);"
            "vec3 h1=normalize(E-uLight1Dir);float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);"
            "vec3 h2=normalize(E-uLight2Dir);float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);"
            "vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;"
            "vec4 texColor=texture2D(uTex,vTex);"
            "vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);"
            "gl_FragData[0]=vec4(litRGB*texColor.rgb,uDiffuse.a*texColor.a*vc.a);"
            "gl_FragData[0].rgb+=specularRGB*gl_FragData[0].a;"
            "gl_FragData[0].rgb*=vc.rgb;"
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;"
            "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);"
            "}";

        // PbrEffect/SkinnedPbrEffect: metallic-roughness BRDF, matching
        // EasyGLGraphicsBackend::EnsurePbrProgram/EnsurePbrSkinnedProgram's formula exactly --
        // same GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX visibility
        // (direct-lighting k=(roughness+1)^2/8), and Schlick Fresnel (the glTF 2.0 spec's own
        // reference BRDF, Appendix B.3.2-B.3.4), same tangent-space normal mapping via a
        // per-fragment TBN basis. Only the vertex shader differs between the two (skinning vs
        // not) -- the fragment shader (the actual BRDF) is shared verbatim.
        const char* pbrFragmentSrc =
            "varying vec3 vNormal;varying vec3 vTangent;varying float vBitangentSign;"
            "varying vec2 vTex;varying float vFogFactor;varying vec3 vWorldPos;"
            "uniform sampler2D uTex;uniform sampler2D uNormalMap;uniform sampler2D uMetallicRoughnessMap;"
            "uniform sampler2D uEmissiveMap;uniform sampler2D uOcclusionMap;"
            "uniform vec4 uDiffuse;uniform vec3 uAmbientColor;uniform vec3 uEmissiveColor;"
            "uniform float uMetallicFactor;uniform float uRoughnessFactor;"
            "uniform vec3 uLight0Dir;uniform vec3 uLight0Diffuse;"
            "uniform vec3 uLight1Dir;uniform vec3 uLight1Diffuse;"
            "uniform vec3 uLight2Dir;uniform vec3 uLight2Diffuse;"
            "uniform vec3 uEyePosition;uniform vec4 uAlphaTest;uniform vec3 uFogColor;"
            "vec3 PbrLight(vec3 N,vec3 V,vec3 L,vec3 lightColor,vec3 albedo,vec3 F0,float roughness,float metallic){"
            "vec3 H=normalize(V+L);"
            "float NdotL=max(dot(N,L),0.0);float NdotV=max(dot(N,V),1e-4);"
            "float NdotH=max(dot(N,H),0.0);float VdotH=max(dot(V,H),0.0);"
            "float a2=pow(roughness,4.0);"
            "float dTerm=(NdotH*NdotH*(a2-1.0)+1.0);"
            "float D=a2/(3.14159265*dTerm*dTerm+1e-7);"
            "float k=(roughness+1.0);k=k*k/8.0;"
            "float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));"
            "vec3 F=F0+(vec3(1.0)-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);"
            "vec3 specular=(D*G*F)/max(4.0*NdotV*NdotL,1e-4);"
            "vec3 diffuseColor=albedo*(1.0-metallic);"
            "vec3 kd=vec3(1.0)-F;"
            "return (kd*diffuseColor/3.14159265+specular)*lightColor*NdotL;"
            "}"
            "void main(){"
            "vec4 baseColorTex=texture2D(uTex,vTex);"
            "vec3 albedo=baseColorTex.rgb*uDiffuse.rgb;"
            "float alpha=baseColorTex.a*uDiffuse.a;"
            "vec3 N=normalize(vNormal);"
            "vec3 T=normalize(vTangent-N*dot(N,vTangent));"
            "vec3 B=cross(N,T)*vBitangentSign;"
            "mat3 TBN=mat3(T,B,N);"
            "vec3 sampledNormal=texture2D(uNormalMap,vTex).rgb*2.0-1.0;"
            "vec3 finalNormal=normalize(TBN*sampledNormal);"
            "vec4 mr=texture2D(uMetallicRoughnessMap,vTex);"
            "float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);"
            "float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);"
            "vec3 V=normalize(uEyePosition-vWorldPos);"
            "vec3 F0=mix(vec3(0.04),albedo,metallic);"
            "vec3 Lo=vec3(0.0);"
            "Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,roughness,metallic);"
            "Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,roughness,metallic);"
            "Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,roughness,metallic);"
            "float occlusion=texture2D(uOcclusionMap,vTex).r;"
            "vec3 ambient=uAmbientColor*albedo*occlusion;"
            "vec3 emissive=uEmissiveColor*texture2D(uEmissiveMap,vTex).rgb;"
            "gl_FragData[0]=vec4(ambient+Lo+emissive,alpha);"
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;"
            "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);"
            "}";

        const char* pbrVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec4 aTangent;attribute vec2 aTexCoord;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat3 uNormalMatrix;"
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;"
            "varying vec3 vNormal;varying vec3 vTangent;varying float vBitangentSign;"
            "varying vec2 vTex;varying float vFogFactor;varying vec3 vWorldPos;"
            "void main(){"
            "gl_Position=uWVP*vec4(aPosition,1.0);"
            "vNormal=uNormalMatrix*aNormal;"
            // Tangent transforms as a plain direction under the World upper-3x3 (not the
            // inverse-transpose uNormalMatrix the normal itself uses) -- correct for
            // uniform-scale World transforms, a documented simplification for non-uniform scale
            // shared with most real-time engines lacking a full per-tangent inverse-transpose
            // (matches EasyGLGraphicsBackend's own identical comment/formula). mat3(mat4)
            // truncation requires GLSL 1.20 (this file targets 1.10 throughout), so built manually
            // -- same technique already used for SkinnedEffect's skin matrix above.
            "mat3 world3=mat3(uWorld[0].xyz,uWorld[1].xyz,uWorld[2].xyz);"
            "vTangent=world3*aTangent.xyz;"
            "vBitangentSign=aTangent.w;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*vec4(aPosition,1.0)).xyz;"
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;"
            "}";

        const char* pbrSkinnedVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec4 aTangent;attribute vec2 aTexCoord;"
            "attribute vec4 aBoneWeight;attribute vec4 aBoneIndices;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat4 uBones[72];uniform int uWeightsPerVertex;"
            "uniform float uFogEnabled;uniform float uFogStart;uniform float uFogEnd;"
            "varying vec3 vNormal;varying vec3 vTangent;varying float vBitangentSign;"
            "varying vec2 vTex;varying float vFogFactor;varying vec3 vWorldPos;"
            "void main(){"
            "int i0=int(aBoneIndices.x+0.5);int i1=int(aBoneIndices.y+0.5);"
            "int i2=int(aBoneIndices.z+0.5);int i3=int(aBoneIndices.w+0.5);"
            "mat4 skinMat=uBones[i0]*aBoneWeight.x;"
            "if(uWeightsPerVertex>=2) skinMat+=uBones[i1]*aBoneWeight.y;"
            "if(uWeightsPerVertex>=4) skinMat+=uBones[i2]*aBoneWeight.z+uBones[i3]*aBoneWeight.w;"
            "vec4 skinnedPos=skinMat*vec4(aPosition,1.0);"
            "gl_Position=uWVP*skinnedPos;"
            "mat3 skinMat3=mat3(skinMat[0].xyz,skinMat[1].xyz,skinMat[2].xyz);"
            "mat3 world3=mat3(uWorld[0].xyz,uWorld[1].xyz,uWorld[2].xyz);"
            "vNormal=normalize(world3*(skinMat3*aNormal));"
            "vTangent=world3*(skinMat3*aTangent.xyz);"
            "vBitangentSign=aTangent.w;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*skinnedPos).xyz;"
            "vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:"
            "clamp((aPosition.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0;"
            "}";

        colorProgram_ = LinkProgram(colorVertexSrc.c_str(), colorFragmentSrc.c_str());
        texturedProgram_ = LinkProgram(texturedVertexSrc.c_str(), texturedFragmentSrc.c_str());
        dualTextureProgram_ = LinkProgram(texturedVertexSrc.c_str(), dualTextureFragmentSrc.c_str());
        litProgram_ = LinkProgram(litVertexSrc, litFragmentSrc);
        envMapProgram_ = LinkProgram(envMapVertexSrc, envMapFragmentSrc);
        skinnedProgram_ = LinkProgram(skinnedVertexSrc, skinnedFragmentSrc);
        pbrProgram_ = LinkProgram(pbrVertexSrc, pbrFragmentSrc);
        pbrSkinnedProgram_ = LinkProgram(pbrSkinnedVertexSrc, pbrFragmentSrc);
    }

    void OpenGL2GraphicsBackend::ensureDefaultWhiteTextures()
    {
        if (defaultWhiteTexture2D_) return;

        const uint8_t white[4] = {255, 255, 255, 255};

        glGenTextures(1, &defaultWhiteTexture2D_);
        glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &defaultWhiteTextureCube_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, defaultWhiteTextureCube_);
        for (int face = 0; face < 6; ++face)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // (128,128,255,255) decodes (via the PBR shaders' own `*2.0-1.0`) to tangent-space
        // (0,0,1) -- "no perturbation", the correct PbrEffect/SkinnedPbrEffect NormalMap default.
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        glGenTextures(1, &defaultFlatNormalTexture2D_);
        glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture2D_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    void OpenGL2GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL2GraphicsBackend::Present() { SDL_GL_SwapWindow(window_); }

    // Mirrors SdlGpuGraphicsBackend::ComputeLogicalViewport's algorithm exactly -- see this
    // backend's own LogicalViewport doc comment for field meanings and the "why this backend,
    // not EasyGL" rationale. NativeBackBuffer/no-virtual-resolution/zero-size-window all
    // degenerate to the full physical window with no scaling, matching every mode's own prior
    // (pre-this-task) behavior when the game never actually engaged Letterbox/Overscan/Stretch.
    OpenGL2GraphicsBackend::LogicalViewport OpenGL2GraphicsBackend::ComputeLogicalViewport() const
    {
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);

        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physW));
        viewport.height = static_cast<float>(std::max(0, physH));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physW <= 0 || physH <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer || virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physW) / static_cast<float>(physH);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;

        // Letterbox/Overscan: scale uniformly (min for Letterbox -- shrink to fit inside the
        // window, adding bars; max for Overscan -- grow to cover the window, cropping the
        // excess) and centre the result.
        const float sx = static_cast<float>(physW) / logicalWidth;
        const float sy = static_cast<float>(physH) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physW) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physH) - viewport.height) * 0.5f;
        return viewport;
    }

    void OpenGL2GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    // The reference "real Letterbox/Overscan/Stretch" GetDefaultViewportRect() override in this
    // codebase (see the base interface's own doc comment) -- GraphicsDevice::
    // UpdateViewportFromWindow() applies this PHYSICAL rectangle as the actual glViewport,
    // separately from GetViewportSize()'s LOGICAL result above. Every other CnaPresentationMode
    // (FixedHeightDynamicWidth/NativeBackBuffer/Stretch) returns (0, 0, physical window size),
    // identical to the base class default -- only Letterbox/Overscan actually offset/shrink it.
    void OpenGL2GraphicsBackend::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        x = static_cast<int>(std::lround(viewport.x));
        y = static_cast<int>(std::lround(viewport.y));
        width = static_cast<int>(std::lround(viewport.width));
        height = static_cast<int>(std::lround(viewport.height));
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
    std::unique_ptr<IOcclusionQueryBackend> OpenGL2GraphicsBackend::CreateOcclusionQuery() { return std::make_unique<OcclusionQuery>(); }

    std::unique_ptr<ITextureCubeBackend> OpenGL2GraphicsBackend::CreateTextureCube(int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<TextureCubeBackend>(size, mipMap);
    }

    std::unique_ptr<ITexture3DBackend> OpenGL2GraphicsBackend::CreateTexture3D(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<Texture3DBackend>(w, h, depth, mipMap);
    }

    std::unique_ptr<IEffectBackend> OpenGL2GraphicsBackend::CreateEffectBackend(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto backend = std::make_unique<EffectBackend>();
        backend->CompileProgram(vertSrc, fragSrc);
        return backend;
    }

    std::unique_ptr<IRenderTargetBackend> OpenGL2GraphicsBackend::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<RenderTarget>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeBackend> OpenGL2GraphicsBackend::CreateRenderTargetCube(
        int size, int depthFormat, bool mipMap, int /*multiSampleCount*/)
    {
        // plan_opengl2.md follow-up: MSAA cube render targets are not implemented (unlike
        // RenderTarget2D's own MSAA support) -- multiSampleCount is accepted (matching the
        // interface signature) but ignored.
        return std::make_unique<RenderTargetCubeBackend>(size, depthFormat, mipMap);
    }

    void OpenGL2GraphicsBackend::SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
    {
        if (currentRtCube_ != rt)
            unbindCurrentRenderTarget();

        if (rt)
        {
            rt->BindAsRenderTargetFace(face);
            currentRtWidth_ = rt->GetSize();
            currentRtHeight_ = rt->GetSize();
            currentRtCube_ = rt;
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            currentRtWidth_ = 0;
            currentRtHeight_ = 0;
        }
    }

    void OpenGL2GraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (count <= 0) { SetRenderTarget2D(nullptr); return; }
        if (count == 1) { SetRenderTarget2D(rts[0]); return; }

        // MRT: unbind whatever single RT/cube-face was previously active (mip regen if needed) --
        // mirrors EasyGLGraphicsBackend::SetRenderTargets's identical ordering.
        unbindCurrentRenderTarget();

        if (!mrtFboReady_)
        {
            glGenFramebuffers(1, &mrtFbo_);
            mrtFboReady_ = true;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, mrtFbo_);

        // Every IRenderTargetBackend this backend ever creates (CreateRenderTarget2D) is a
        // concrete OpenGL2::RenderTarget -- safe to downcast to reach its raw color-texture
        // handle, matching EasyGLGraphicsBackend::SetRenderTargets's own static_cast precedent.
        constexpr int kMaxMRT = 8;
        const int n = count < kMaxMRT ? count : kMaxMRT;
        GLenum drawBufs[kMaxMRT];
        for (int i = 0; i < n; ++i)
        {
            auto* target = static_cast<RenderTarget*>(rts[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, target->colorTex, 0);
            drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        glDrawBuffers(n, drawBufs);

        // Sized after attachment 0 (matches XNA's own convention -- FNA validates every binding
        // in a MRT set shares the same size, so attachment 0's size represents them all).
        currentRtWidth_ = rts[0]->GetWidth();
        currentRtHeight_ = rts[0]->GetHeight();
        // MRT targets are deliberately NOT tracked as currentRt_/currentRtCube_ -- see this
        // method's own header-comment precedent (mrtFbo_'s doc comment) for the accepted gap
        // this shares with EasyGL: switching away from MRT mode cannot regenerate per-target mips.
    }

    void OpenGL2GraphicsBackend::unbindCurrentRenderTarget()
    {
        // Must run the OUTGOING target's own UnbindAsRenderTarget() first -- that is where
        // RenderTarget/RenderTargetCube resolve their MSAA renderbuffer into the sampleable
        // texture and regenerate their mip chain (mirrors FNA3D's OPENGL_ResolveTarget, invoked
        // when a target stops being the active render target). Skipping this leaves both
        // features silently inert: the color texture keeps whatever empty storage
        // glTexImage2D(..., nullptr) initially gave it.
        if (currentRt_) currentRt_->UnbindAsRenderTarget();
        if (currentRtCube_) currentRtCube_->UnbindAsRenderTarget();
        currentRt_ = nullptr;
        currentRtCube_ = nullptr;
    }

    void OpenGL2GraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        unbindCurrentRenderTarget();

        if (rt)
        {
            rt->BindAsRenderTarget();
            currentRtWidth_ = rt->GetWidth();
            currentRtHeight_ = rt->GetHeight();
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            currentRtWidth_ = 0;
            currentRtHeight_ = 0;
        }
        currentRt_ = rt;
    }

    bool OpenGL2GraphicsBackend::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (currentRtHeight_ == 0) return false;
        width = currentRtWidth_;
        height = currentRtHeight_;
        return true;
    }

    void OpenGL2GraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // When a RenderTarget2D's FBO is currently bound, its single color attachment is
        // already the implicit read source (GL_BACK is only valid for the default framebuffer);
        // mirrors EasyGLGraphicsBackend::ReadBackbuffer's identical currentRtHeight_-gated
        // behavior. Real RenderTarget2D pixel readback normally goes through
        // RenderTarget::GetData() instead -- this path exists for parity with that backend.
        if (currentRtHeight_ == 0)
            glReadBuffer(GL_BACK);

        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            SDL_GetWindowSize(window_, &windowWidth, &fbH);
        }

        // OpenGL origin is bottom-left; flip y so the caller gets top-left origin (mirrors
        // EasyGLGraphicsBackend::ReadBackbuffer's identical convention).
        const int glY = fbH - y - h;
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

    std::unique_ptr<IVertexBufferBackend> OpenGL2GraphicsBackend::CreateVertexBuffer(int vertex_capacity) { return std::make_unique<VB>(vertex_capacity); }
    std::unique_ptr<IIndexBufferBackend> OpenGL2GraphicsBackend::CreateIndexBuffer16(int index_capacity) { return std::make_unique<IB>(false, index_capacity); }
    std::unique_ptr<IIndexBufferBackend> OpenGL2GraphicsBackend::CreateIndexBuffer32(int index_capacity) { return std::make_unique<IB>(true, index_capacity); }

    namespace
    {
        // Inverse-transpose of World's upper-3x3, column-major -- the standard normal-transform
        // matrix, correct under non-uniform scale too (a plain World upper-3x3 only preserves
        // normals under translation/rotation/uniform-scale). Uses Matrix::Invert/Transpose, both
        // already part of the public XNA API, so no separate 3x3 inverse routine is needed.
        void ComputeNormalMatrix3x3(const Matrix& world, float out[9])
        {
            const Matrix invTranspose = Matrix::Transpose(Matrix::Invert(world));
            const float rowMajor[9] = {invTranspose.M11, invTranspose.M12, invTranspose.M13,
                                       invTranspose.M21, invTranspose.M22, invTranspose.M23,
                                       invTranspose.M31, invTranspose.M32, invTranspose.M33};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    out[c * 3 + r] = rowMajor[r * 3 + c];
        }
    }

    void OpenGL2GraphicsBackend::drawInternal(const IVertexBufferBackend& vbi, const IIndexBufferBackend* ibi,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params)
    {
        const auto* vb = dynamic_cast<const VB*>(&vbi);
        if (!vb)
            throw std::runtime_error("OPENGL2: incompatible vertex buffer");
        const auto* ib = dynamic_cast<const IB*>(ibi);

        // Task CreateEffectBackend (plan_opengl2.md): a user-authored ShaderEffect entirely
        // bypasses every built-in program/uniform above -- bind it directly, upload the XNA-HLSL-
        // style "World"/"Projection"/"View" semantic names a custom shader is expected to declare
        // (matches EasyGLGraphicsBackend::BindCustomEffectMatrices's identical uniform names), and
        // draw using the same stride-based vertex layout every built-in program uses (see
        // EffectBackend's own doc comment for why the attribute NAMES matter here, unlike EasyGL).
        if (params && params->customEffectBackend)
        {
            params->customEffectBackend->Bind();
            float worldCM[16], viewCM[16], projCM[16];
            world.ToColumnMajor(worldCM);
            view.ToColumnMajor(viewCM);
            projection.ToColumnMajor(projCM);
            params->customEffectBackend->SetUniformMat4("World", worldCM);
            params->customEffectBackend->SetUniformMat4("View", viewCM);
            params->customEffectBackend->SetUniformMat4("Projection", projCM);

            // Task 1080: a VertexBuffer given a genuinely custom VertexDeclaration (via
            // SetDataRaw()) is bound by attribute NAME (see BindVertexAttributesForDeclaration's
            // own doc comment) against THIS custom program specifically -- dynamic_cast to reach
            // its raw program handle through the generic IEffectBackend interface (same reasoning
            // as EffectBackend::GetProgramHandle()'s own doc comment).
            if (!vb->declaration.empty())
            {
                if (auto* custom = dynamic_cast<EffectBackend*>(params->customEffectBackend))
                    BindVertexAttributesForDeclaration(custom->GetProgramHandle(), vb->id, vb->stride, vb->declaration);
                else
                    BindVertexAttributesForStride(vb->id, vb->stride);
            }
            else
            {
                BindVertexAttributesForStride(vb->id, vb->stride);
            }

            const int customVertexCount = VertexCountForPrimitives(primitive, primitiveCount);
            if (ib)
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
                glDrawElements(ToGLPrimitiveMode(primitive), customVertexCount,
                               ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
            }
            else
            {
                glDrawArrays(ToGLPrimitiveMode(primitive), 0, customVertexCount);
            }
            return;
        }

        const bool pbrSkinned = params && params->pbr && params->skinned && vb->stride == 68;
        const bool pbr = params && params->pbr && !params->skinned && vb->stride == 48;
        const bool skinned = params && params->skinned && !params->pbr && (vb->stride == 52 || vb->stride == 56);
        const bool envMapped = params && params->envMapping && !skinned && !pbr && !pbrSkinned && vb->stride >= 32;
        const bool lit = params && params->lightingEnabled && !envMapped && !skinned && !pbr && !pbrSkinned && vb->stride >= 32;
        const bool dual = params && params->dualTexture && params->texture1 && vb->stride >= 20;
        const bool textured = params && params->texture0 && vb->stride >= 20;

        const GLuint program = pbrSkinned ? pbrSkinnedProgram_ : pbr ? pbrProgram_ : skinned ? skinnedProgram_ : envMapped ? envMapProgram_ : lit ? litProgram_ : dual ? dualTextureProgram_ : textured ? texturedProgram_ : colorProgram_;
        glUseProgram(program);

        float wvp[16];
        ComputeColumnMajorWVP(world, view, projection, wvp);
        glUniformMatrix4fv(glGetUniformLocation(program, "uWVP"), 1, GL_FALSE, wvp);

        float diffuse[4] = {1, 1, 1, 1};
        if (params) std::memcpy(diffuse, params->diffuseColor, sizeof(diffuse));
        glUniform4fv(glGetUniformLocation(program, "uDiffuse"), 1, diffuse);

        float alphaTest[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        if (params) std::memcpy(alphaTest, params->alphaTest, sizeof(alphaTest));
        glUniform4fv(glGetUniformLocation(program, "uAlphaTest"), 1, alphaTest);

        glUniform1f(glGetUniformLocation(program, "uFogEnabled"), (params && params->fogEnabled) ? 1.0f : 0.0f);
        if (params)
        {
            glUniform3fv(glGetUniformLocation(program, "uFogColor"), 1, params->fogColor);
            glUniform1f(glGetUniformLocation(program, "uFogStart"), params->fogStart);
            glUniform1f(glGetUniformLocation(program, "uFogEnd"), params->fogEnd);
        }

        if (lit || envMapped || skinned || pbr || pbrSkinned)
        {
            float worldColMajor[16];
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                {
                    const float rowMajor[16] = {world.M11, world.M12, world.M13, world.M14,
                                                world.M21, world.M22, world.M23, world.M24,
                                                world.M31, world.M32, world.M33, world.M34,
                                                world.M41, world.M42, world.M43, world.M44};
                    worldColMajor[c * 4 + r] = rowMajor[r * 4 + c];
                }
            glUniformMatrix4fv(glGetUniformLocation(program, "uWorld"), 1, GL_FALSE, worldColMajor);
            float normalMat[9];
            ComputeNormalMatrix3x3(world, normalMat);
            glUniformMatrix3fv(glGetUniformLocation(program, "uNormalMatrix"), 1, GL_FALSE, normalMat);

            // Locations a given program doesn't declare resolve to -1 and glUniform* silently
            // no-ops per the GL spec (same established convention as the alphaTest/fog uniforms
            // above, uploaded unconditionally for colorProgram_ too) -- so it's simplest to
            // upload the full lit ∪ env-map ∪ skinned uniform set here rather than branching further.
            glUniform1i(glGetUniformLocation(program, "uTextureEnabled"), params->textureEnabled ? 1 : 0);
            glUniform3fv(glGetUniformLocation(program, "uAmbientColor"), 1, params->ambientColor);
            glUniform3fv(glGetUniformLocation(program, "uLight0Dir"), 1, params->light0Dir);
            glUniform3fv(glGetUniformLocation(program, "uLight0Diffuse"), 1, params->light0Diffuse);
            glUniform3fv(glGetUniformLocation(program, "uLight0Specular"), 1, params->light0Specular);
            glUniform3fv(glGetUniformLocation(program, "uLight1Dir"), 1, params->light1Dir);
            glUniform3fv(glGetUniformLocation(program, "uLight1Diffuse"), 1, params->light1Diffuse);
            glUniform3fv(glGetUniformLocation(program, "uLight1Specular"), 1, params->light1Specular);
            glUniform3fv(glGetUniformLocation(program, "uLight2Dir"), 1, params->light2Dir);
            glUniform3fv(glGetUniformLocation(program, "uLight2Diffuse"), 1, params->light2Diffuse);
            glUniform3fv(glGetUniformLocation(program, "uLight2Specular"), 1, params->light2Specular);
            glUniform3fv(glGetUniformLocation(program, "uSpecularColor"), 1, params->specularColor);
            glUniform1f(glGetUniformLocation(program, "uSpecularPower"), params->specularPower);
            glUniform3fv(glGetUniformLocation(program, "uEyePosition"), 1, params->eyePositionWorld);
            glUniform3fv(glGetUniformLocation(program, "uEmissiveColor"), 1, params->emissiveColor);

            // EnvironmentMapEffect-only uniforms (silently ignored by litProgram_).
            glUniform1f(glGetUniformLocation(program, "uEnvMapAmount"), params->envMapAmount);
            glUniform1f(glGetUniformLocation(program, "uFresnelEnabled"), params->fresnelEnabled ? 1.0f : 0.0f);
            glUniform1f(glGetUniformLocation(program, "uFresnelFactor"), params->fresnelFactor);
            glUniform3fv(glGetUniformLocation(program, "uEnvMapSpecular"), 1, params->envMapSpecular);

            // SkinnedEffect-only uniforms (silently ignored by litProgram_/envMapProgram_).
            if (skinned)
            {
                glUniformMatrix4fv(glGetUniformLocation(program, "uBones[0]"), params->boneCount,
                                   GL_FALSE, params->boneTransforms);
                glUniform1i(glGetUniformLocation(program, "uWeightsPerVertex"), params->weightsPerVertex);
                glUniform1f(glGetUniformLocation(program, "uVertexColorEnabled"), params->vertexColorEnabled ? 1.0f : 0.0f);
            }

            // SkinnedPbrEffect shares SkinnedEffect's own bone-palette uniforms (same names,
            // uploaded unconditionally here rather than duplicating the block above).
            if (pbrSkinned)
            {
                glUniformMatrix4fv(glGetUniformLocation(program, "uBones[0]"), params->boneCount,
                                   GL_FALSE, params->boneTransforms);
                glUniform1i(glGetUniformLocation(program, "uWeightsPerVertex"), params->weightsPerVertex);
            }

            // PbrEffect/SkinnedPbrEffect-only uniforms (silently ignored by every other program).
            if (pbr || pbrSkinned)
            {
                glUniform1f(glGetUniformLocation(program, "uMetallicFactor"), params->pbrMetallicFactor);
                glUniform1f(glGetUniformLocation(program, "uRoughnessFactor"), params->pbrRoughnessFactor);
            }
        }

        // Task 1080: a VertexBuffer given a genuinely custom VertexDeclaration (via
        // SetDataRaw()) is bound by attribute NAME against whichever built-in program was just
        // selected above, instead of this file's own fixed byte-stride dispatch -- see
        // BindVertexAttributesForDeclaration's own doc comment.
        if (!vb->declaration.empty())
            BindVertexAttributesForDeclaration(program, vb->id, vb->stride, vb->declaration);
        else
            BindVertexAttributesForStride(vb->id, vb->stride);

        // GL 2.1 has no sampler objects -- ApplySamplerState() caches the requested filter/wrap
        // per slot; apply it here, right after binding the texture that will actually be sampled
        // (mirrors Sprite::Draw's identical per-draw glTexParameteri approach).
        auto applySampler = [this](int slot)
        {
            GLint glMin, glMag;
            ToGLMinMagFilter(samplerFilter_[slot], glMin, glMag);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glMin);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glMag);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(samplerAddressU_[slot]));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(samplerAddressV_[slot]));

            // TextureFilter::Anisotropic = 2 (mirrors EasyGLGraphicsBackend::ApplySamplerState's
            // own gate-on-extension-presence + clamp-to-device-max pattern exactly) -- maxAnisotropy
            // was accepted but silently discarded before this fix, so SamplerState.MaxAnisotropy
            // never reached the GPU regardless of TextureFilter::Anisotropic being requested.
            if (samplerFilter_[slot] == 2 && SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering))
            {
                GLfloat maxAnisoCap = 1.0f;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisoCap);
                float requested = static_cast<float>(samplerMaxAnisotropy_[slot]);
                float clamped = (maxAnisoCap > 0.0f && requested > maxAnisoCap) ? maxAnisoCap : requested;
                if (clamped < 1.0f) clamped = 1.0f;
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, clamped);
            }
        };

        // Lit draws with textureEnabled=false (VertexPositionNormalTexture but no BasicEffect
        // Texture assigned) legitimately have a null texture0 -- litFragmentSrc's uTextureEnabled
        // branch never samples it in that case, so it is safe (and necessary) to skip the bind.
        if (params && params->texture0 && (lit || textured || dual))
        {
            glActiveTexture(GL_TEXTURE0);
            params->texture0->BindGL();
            applySampler(0);
            glUniform1i(glGetUniformLocation(program, "uTex"), 0);
        }
        // EnvironmentMapEffect::FillGpuDrawParams() always sets textureEnabled=true and always
        // samples uTex/uEnvMap in the fragment shader (unlike lit's conditional uTextureEnabled
        // branch above) -- a real XNA EnvironmentMapEffect with Texture==null or
        // EnvironmentMap==null still renders (flat white base / no reflection tint), so both
        // samplers need something bound even when the corresponding CNA texture is absent.
        if (envMapped)
        {
            ensureDefaultWhiteTextures();

            glActiveTexture(GL_TEXTURE0);
            if (params->texture0) params->texture0->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(0);
            glUniform1i(glGetUniformLocation(program, "uTex"), 0);

            glActiveTexture(GL_TEXTURE1);
            if (params->envMap) params->envMap->BindGL();
            else glBindTexture(GL_TEXTURE_CUBE_MAP, defaultWhiteTextureCube_);
            glUniform1i(glGetUniformLocation(program, "uEnvMap"), 1);
            glActiveTexture(GL_TEXTURE0);
        }
        // SkinnedEffect::FillGpuDrawParams() also always sets textureEnabled=true and
        // skinnedFragmentSrc always samples uTex unconditionally (no uTextureEnabled toggle,
        // matching EasyGLGraphicsBackend::EnsureSkinnedProgram's identical fragment shader) --
        // same null-texture0 fallback reasoning as envMapped above.
        if (skinned)
        {
            ensureDefaultWhiteTextures();

            glActiveTexture(GL_TEXTURE0);
            if (params->texture0) params->texture0->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(0);
            glUniform1i(glGetUniformLocation(program, "uTex"), 0);
        }
        if (dual)
        {
            glActiveTexture(GL_TEXTURE1);
            params->texture1->BindGL();
            applySampler(1);
            glUniform1i(glGetUniformLocation(program, "uTex2"), 1);
            glActiveTexture(GL_TEXTURE0);
        }
        // PbrEffect/SkinnedPbrEffect::FillGpuDrawParams() also always sets textureEnabled=true and
        // pbrFragmentSrc always samples all five textures unconditionally (base color + normal +
        // metallic-roughness + emissive + occlusion) -- same null-texture fallback reasoning as
        // envMapped/skinned above, extended to all five maps (MetallicRoughness/Emissive/Occlusion
        // default to white -- "1.0 when absent", matching FillGpuDrawParams' own doc comment that
        // Metallic/RoughnessFactor alone become the per-material constant; NormalMap defaults to
        // flat -- "no perturbation").
        if (pbr || pbrSkinned)
        {
            ensureDefaultWhiteTextures();

            glActiveTexture(GL_TEXTURE0);
            if (params->texture0) params->texture0->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(0);
            glUniform1i(glGetUniformLocation(program, "uTex"), 0);

            glActiveTexture(GL_TEXTURE1);
            if (params->pbrNormalMap) params->pbrNormalMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultFlatNormalTexture2D_);
            glUniform1i(glGetUniformLocation(program, "uNormalMap"), 1);

            glActiveTexture(GL_TEXTURE2);
            if (params->pbrMetallicRoughnessMap) params->pbrMetallicRoughnessMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            glUniform1i(glGetUniformLocation(program, "uMetallicRoughnessMap"), 2);

            glActiveTexture(GL_TEXTURE3);
            if (params->pbrEmissiveMap) params->pbrEmissiveMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            glUniform1i(glGetUniformLocation(program, "uEmissiveMap"), 3);

            glActiveTexture(GL_TEXTURE4);
            if (params->pbrOcclusionMap) params->pbrOcclusionMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            glUniform1i(glGetUniformLocation(program, "uOcclusionMap"), 4);

            glActiveTexture(GL_TEXTURE0);
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

    void OpenGL2GraphicsBackend::DrawInstancedPrimitivesEx(const IVertexBufferBackend& vbi, const IIndexBufferBackend& ibi,
                                                            const Matrix& world, const Matrix& view, const Matrix& projection,
                                                            PrimitiveType primitive, int primitiveCount, int instanceCount,
                                                            const GpuDrawParams& params)
    {
        // plan_opengl2.md follow-up: hardware instancing is only supported through the custom-
        // ShaderEffect path (mirrors EasyGLGraphicsBackend::DrawInstancedPrimitivesEx's own
        // identical, documented scope reduction -- a non-1 VertexBufferBinding.InstanceFrequency
        // is not threaded through here either) -- the shared base-class default's
        // std::runtime_error is the correct, honest behavior for anything outside that scope
        // (no custom effect bound, or the driver genuinely lacks GL_ARB_draw_instanced/
        // GL_ARB_instanced_arrays), so this override only handles the cases it can do for real.
        if (!params.customEffectBackend || !glDrawElementsInstancedARB || !glVertexAttribDivisorARB)
        {
            IGraphicsBackend::DrawInstancedPrimitivesEx(vbi, ibi, world, view, projection, primitive,
                                                        primitiveCount, instanceCount, params);
            return;
        }

        const auto* vb = dynamic_cast<const VB*>(&vbi);
        const auto* ib = dynamic_cast<const IB*>(&ibi);
        if (!vb || !ib)
            throw std::runtime_error("OPENGL2: incompatible vertex/index buffer");

        auto* custom = dynamic_cast<EffectBackend*>(params.customEffectBackend);
        if (!custom)
            throw std::runtime_error("OPENGL2: DrawInstancedPrimitivesEx requires an OpenGL2 ShaderEffect");

        params.customEffectBackend->Bind();
        float worldCM[16], viewCM[16], projCM[16];
        world.ToColumnMajor(worldCM);
        view.ToColumnMajor(viewCM);
        projection.ToColumnMajor(projCM);
        params.customEffectBackend->SetUniformMat4("World", worldCM);
        params.customEffectBackend->SetUniformMat4("View", viewCM);
        params.customEffectBackend->SetUniformMat4("Projection", projCM);

        const GLuint program = custom->GetProgramHandle();

        // Mesh (per-vertex) attributes -- same declaration-driven or fixed-stride dispatch every
        // other custom-effect draw uses.
        if (!vb->declaration.empty())
            BindVertexAttributesForDeclaration(program, vb->id, vb->stride, vb->declaration);
        else
            BindVertexAttributesForStride(vb->id, vb->stride);

        // Per-instance attributes: bound by NAME (glGetAttribLocation), exactly like the mesh
        // buffer above -- unlike EasyGLGraphicsBackend's own fixed-layout-location approach, name-
        // based binding needs no "continue right after the mesh buffer's own locations" offset
        // arithmetic, since each attribute's location comes straight from the linker regardless of
        // how many mesh attributes exist. glVertexAttribDivisorARB(location, 1) is what makes each
        // of these advance once per INSTANCE instead of once per vertex -- the whole mechanism this
        // task exists to add.
        const auto* instVb = dynamic_cast<const VB*>(params.instanceVb);
        std::vector<GLuint> instanceLocations;
        if (instVb && !instVb->declaration.empty())
        {
            glBindBuffer(GL_ARRAY_BUFFER, instVb->id);
            for (const auto& element : instVb->declaration)
            {
                const std::string name = VertexElementAttribName(
                    element.getVertexElementUsageProperty(), element.getUsageIndexProperty());
                const GLint location = glGetAttribLocation(program, name.c_str());
                if (location < 0) continue;

                const VertexAttribFormat desc = DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const auto offset = reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(element.getOffsetProperty()));
                glEnableVertexAttribArray(static_cast<GLuint>(location));
                glVertexAttribPointer(static_cast<GLuint>(location), desc.componentCount, desc.type,
                                      desc.normalized, static_cast<GLsizei>(instVb->stride), offset);
                glVertexAttribDivisorARB(static_cast<GLuint>(location), 1);
                instanceLocations.push_back(static_cast<GLuint>(location));
            }
        }

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        glDrawElementsInstancedARB(ToGLPrimitiveMode(primitive), indexCount,
                                   ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                                   nullptr, instanceCount);

        // Reset divisor back to 0 (not just disable the attribute) -- otherwise a later,
        // unrelated draw that reuses the same attribute location number would silently inherit
        // per-instance advancement instead of the normal per-vertex behavior.
        for (GLuint location : instanceLocations)
        {
            glVertexAttribDivisorARB(location, 0);
            glDisableVertexAttribArray(location);
        }
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

    void OpenGL2GraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        // Backs BlendState.BlendFactor for the Blend::BlendFactor/InverseBlendFactor blend
        // factors (ToGLBlendFactor maps those to GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR
        // above) -- glBlendColor is the GL-side constant-color register those factors read.
        glBlendColor(r, g, b, a);
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

    void OpenGL2GraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
        // GL 2.1 has no sampler objects -- just cache the request; drawInternal() applies it via
        // glTexParameteri once it knows which texture is actually bound to this slot for the
        // upcoming draw (mirrors Sprite::Draw's identical per-draw approach for SpriteBatch).
        samplerFilter_[slot] = filter;
        samplerAddressU_[slot] = addressU;
        samplerAddressV_[slot] = addressV;
        samplerMaxAnisotropy_[slot] = maxAnisotropy;
    }

    void OpenGL2GraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        // Use the render target's own height for the Y-flip when one is bound (mirrors
        // ReadBackbuffer's identical fbH pattern); fall back to the window's height for the
        // default framebuffer.
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            SDL_GetWindowSize(window_, &windowWidth, &fbH);
        }
        glScissor(x, fbH - y - h, w, h);
    }

    void OpenGL2GraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return;
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            SDL_GetWindowSize(window_, &windowWidth, &fbH);
        }
        glViewport(x, fbH - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
    }

    bool OpenGL2GraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        // Mirrors SdlGpuGraphicsBackend::TransformWindowToLogical exactly: for
        // FixedHeightDynamicWidth/NativeBackBuffer/Stretch (viewport.x=viewport.y=0, width/height
        // equal the physical window) this degenerates to the same pure height-derived uniform
        // scale as before; Letterbox/Overscan additionally subtract the centred sub-rectangle's
        // offset and return false outside its bounds (a window click on a letterbox bar has no
        // corresponding logical position).
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f) return false;
        logX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool OpenGL2GraphicsBackend::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f) return false;
        windowX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logY * viewport.height / viewport.logicalHeight;
        return true;
    }

    bool OpenGL2GraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // Unlike EasyGL/GLES3 (which has no wireframe fill mode at all -- see its own
            // SupportsCapability), desktop GL 2.1's compatibility profile genuinely supports
            // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE), already wired in
            // ApplyRasterizerState() above.
            case CNA::GraphicsCapability::WireFrame:
                return true;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            {
                GLint maxSamples = 0;
                glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
                return maxSamples > 1;
            }
            case CNA::GraphicsCapability::AnisotropicFiltering:
            {
                const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
                return extensions && std::strstr(extensions, "GL_EXT_texture_filter_anisotropic") != nullptr;
            }
            case CNA::GraphicsCapability::Instancing:
                // Both proc addresses must have resolved (LoadInstancingExtensions(), called
                // unconditionally from the constructor) -- DrawInstancedPrimitivesEx's own guard
                // checks the same condition before using either.
                return glDrawElementsInstancedARB != nullptr && glVertexAttribDivisorARB != nullptr;
            default:
                return true;
        }
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
