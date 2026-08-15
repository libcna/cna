#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "System/NotSupportedException.hpp"

// Windows' opengl32.dll only ever exports the ~350 GL 1.1 entry points (Microsoft never updated
// its own ICD dispatch table beyond that) -- everything this renderer calls beyond GL 1.1 (buffer
// objects/GL 1.5, shaders/GL 2.0, multitexture/GL 1.3, framebuffer objects/ARB_framebuffer_object,
// even glTexImage3D/glTexSubImage3D despite being spec-core since GL 1.2) must be resolved at
// runtime via wglGetProcAddress there, or the link step fails outright with unresolved externals.
// GL_GLEXT_PROTOTYPES's plain `extern`/GLAPI declarations (used on every other platform below)
// only link because Linux/Mesa/GLX -- and macOS/other desktop GL ICDs -- export these symbols
// directly for static linking; that is a convenience specific to those platforms' GL loader
// model, not something Windows' opengl32.dll provides. The platform GL resolver wraps the native
// entry-point lookup portably; see LoadWin32GLExtensions() below, called once after context
// creation. Deliberately NOT build/link-verified against a real Windows toolchain -- none is
// available in this project's own dev/CI sandbox -- but every function pointer below uses the
// STANDARD Khronos PFNGLxxxPROC typedef from glext.h, not a hand-rolled signature, specifically to
// minimize the chance of a silent type mismatch that only a real compile could otherwise catch.
#if defined(__APPLE__)
#define GL_GLEXT_PROTOTYPES 1
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL2
{
    namespace
    {
        CNA::Platform::GlContextDescription RequestedContext()
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 2;
            description.minorVersion = 1;
            description.profile = CNA::Platform::GlProfile::Compatibility;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            return description;
        }
    }

    // plan_opengl2.md (context-loss recovery): mirrors easy-gl's RecoverableResource.hpp exactly
    // (release_gl_handle_only()/recreate_gl_resource()), reimplemented locally rather than
    // depending on EasyGL/easy-gl. Defined here (not inside the anonymous namespace below) so it
    // matches the forward declaration in OpenGL2Renderer.hpp and is visible both to the
    // anonymous-namespace resource classes below and to OpenGL2Renderer's own methods.
    class RecoverableResource
    {
    public:
        virtual ~RecoverableResource() = default;
        // Drop the GPU handle without calling any gl* function -- the context is already gone by
        // the time this runs. The resource must keep whatever CPU-side data it needs intact so
        // recreate_gl_resource() can restore it later.
        virtual void release_gl_handle_only() = 0;
        // Recreate the GPU resource using the stored CPU-side data (if any -- RenderTarget/
        // RenderTargetCubeRenderer/OcclusionQuery have none to restore and simply recreate blank).
        // Called after a fresh GL context is current and this file's own GL function pointers
        // have been reloaded.
        virtual void recreate_gl_resource() = 0;
    };

    namespace
    {
        // Not aliased by IGraphicsRenderer.hpp (unlike VertexElement itself) -- only needed
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

        // Called once, right after the platform context becomes current -- every function
        // pointer above must be resolved before ensurePrograms() (or any other GL call in this
        // file beyond GL 1.1) runs.
        void LoadWin32GLExtensions()
        {
#define CNA_LOAD_GL(name) name = reinterpret_cast<decltype(name)>(LoadPlatformGlProcAddress(#name))
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
        // at runtime via the platform GL resolver regardless of platform (LoadInstancingExtensions(),
        // called unconditionally from the constructor, unlike LoadWin32GLExtensions() above which
        // is Windows-only). Null after loading means the driver genuinely lacks the extension --
        // every call site below must check before use (see SupportsCapability(Instancing) and
        // DrawInstancedPrimitivesEx's own guard).
        PFNGLDRAWELEMENTSINSTANCEDARBPROC glDrawElementsInstancedARB = nullptr;
        PFNGLVERTEXATTRIBDIVISORARBPROC glVertexAttribDivisorARB = nullptr;

        void LoadInstancingExtensions()
        {
            glDrawElementsInstancedARB = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDARBPROC>(
                LoadPlatformGlProcAddress("glDrawElementsInstancedARB"));
            glVertexAttribDivisorARB = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORARBPROC>(
                LoadPlatformGlProcAddress("glVertexAttribDivisorARB"));
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
        // SourceAlphaSaturation=12. Mirrors EasyGLRenderer's ToEasyGLBlendFactor mapping.
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
        // GreaterEqual=5, Greater=6, NotEqual=7. Mirrors EasyGLRenderer's
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
        // (plain GL_INCR/GL_DECR) -- mirrors EasyGLRenderer's ToEasyGLStencilOp mapping.
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
        // MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8. This renderer has no mipmaps
        // (single-level textures only) and no separate min/mag GL sampler control worth adding
        // for that reason -- mirrors SdlRenderer::SetSamplerFilter's own reasoning: since
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
        // in drawInternal() below) -- mirrors EasyGLRenderer::ApplySamplerState's own
        // TextureFilter -> min/mag mapping exactly, including its own documented Linear=0 case
        // ("CNA does not generate mipmaps by default"), for cross-renderer consistency. Unlike
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

        // ShaderEffect renderer: a user-authored, runtime-compiled GLSL program (Task
        // CreateEffectRenderer / plan_opengl2.md follow-up). Reuses LinkProgram() -- so a custom
        // vertex shader binds to the SAME fixed attribute locations as every built-in program
        // here (0=aPosition, 1=aColor, 2=aTexCoord, 3=aNormal, 4=aBoneWeight, 5=aBoneIndices) --
        // required because GLSL 1.10/1.20 (this file's target) has no `layout(location=N)`
        // qualifier, unlike EasyGLEffectRenderer's GLES3 shaders, which bind locations directly in
        // the GLSL source instead of via glBindAttribLocation. A custom shader MUST use those
        // exact attribute names to receive vertex data at all.
        class EffectRenderer final : public IEffectRenderer
        {
        public:
            ~EffectRenderer() override { if (program_) glDeleteProgram(program_); }

            // CNAEXT: lets drawInternal()'s customEffectRenderer path (VertexDeclaration-driven
            // attribute binding, see BindVertexAttributesForDeclaration()) look up this custom
            // program's own attribute locations, same reasoning as
            // EasyGLSpriteBatchRenderer::FlushBatch()'s own dynamic_cast to its EasyGLEffectRenderer
            // for the identical purpose (reaching the raw program handle through the generic
            // IEffectRenderer interface, which deliberately doesn't expose one itself).
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
            // Matches IEffectRenderer::BindTexture's own doc comment: binds the texture object to
            // the given unit only -- does NOT set any sampler uniform (the caller's own
            // SetUniformInt("uSamplerName", unit) call wires that up), same contract as
            // EasyGLEffectRenderer's identical BindTexture/BindTextureCube/BindTexture3D.
            void BindTexture(int unit, ITextureRenderer* texture) override
            {
                if (!texture) return;
                glActiveTexture(GL_TEXTURE0 + unit);
                texture->BindGL();
                glActiveTexture(GL_TEXTURE0);
            }
            void BindTextureCube(int unit, ITextureCubeRenderer* texture) override
            {
                if (!texture) return;
                glActiveTexture(GL_TEXTURE0 + unit);
                texture->BindGL();
                glActiveTexture(GL_TEXTURE0);
            }
            void BindTexture3D(int unit, ITexture3DRenderer* texture) override
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
        // path and its customEffectRenderer early-return path, since a custom ShaderEffect draws
        // through the exact same vertex data / attribute-location convention as every built-in
        // program (see EffectRenderer's own doc comment above).
        // baseByteOffset (default 0): a byte bias added to every attribute pointer -- the
        // software base-vertex technique. GL 2.1 has no glDrawElementsBaseVertex (core 3.2), so a
        // draw's GpuDrawParams::baseVertex is honored exactly by re-basing the attribute pointers
        // baseVertex*stride bytes forward instead of silently drawing from vertex 0.
        void BindVertexAttributesForStride(GLuint vboId, std::size_t stride,
                                           std::size_t baseByteOffset = 0)
        {
            glBindBuffer(GL_ARRAY_BUFFER, vboId);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                                  reinterpret_cast<void*>(baseByteOffset));

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
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
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
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
                glDisableVertexAttribArray(3);
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else if (stride == 24)
            {
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 16));
                glDisableVertexAttribArray(3);
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else if (stride == 52 || stride == 56)
            {
                // VertexPositionNormalTextureSkinned: float3 pos(0) + float3 normal(12) +
                // float2 uv(24) + float4 boneWeight(32) + ubyte4 boneIndices(48) [+ ubyte4 color(52)
                // for the stride-56 variant -- matches EasyGLRenderer's own stride-56 comment:
                // "the stride-52 layout with a per-vertex Color appended at the end (offset 52)
                // rather than inserted mid-layout"]. Bone indices are read as unnormalized
                // GL_UNSIGNED_BYTE (converted to float 0..255 exactly, no precision loss), NOT
                // normalized to [0,1] -- the vertex shader casts them back to int bone-array indices.
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 24));
                glEnableVertexAttribArray(4);
                glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 32));
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 48));
                glDisableVertexAttribArray(6);
                if (stride == 56)
                {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 52));
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
                // matches EasyGLRenderer's own identical stride-48/68 layout comments].
                // PbrEffect/SkinnedPbrEffect.
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
                glEnableVertexAttribArray(6);
                glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 24));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 40));
                glDisableVertexAttribArray(1);
                glVertexAttrib4f(1, 1, 1, 1, 1);
                if (stride == 68)
                {
                    glEnableVertexAttribArray(4);
                    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 48));
                    glEnableVertexAttribArray(5);
                    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 64));
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
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 24));
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), reinterpret_cast<void*>(baseByteOffset + 12));
                glDisableVertexAttribArray(4);
                glDisableVertexAttribArray(5);
                glDisableVertexAttribArray(6);
            }
            else
            {
                throw std::runtime_error("OPENGL2: unsupported vertex stride");
            }
        }

        // Task 1080 (this renderer's own follow-up item -- see plan_opengl2.md): maps a
        // VertexElementFormat to the (component count, GL type, normalized) triple
        // glVertexAttribPointer() needs, mirroring EasyGLRenderer::
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
        // attribute-naming contract EffectRenderer's own doc comment already establishes for the
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
        // see IVertexBufferRenderer::SetVertexDeclaration()'s own doc comment), instead of the
        // fixed byte-stride dispatch BindVertexAttributesForStride() recognizes. Elements whose
        // derived name the currently-bound program doesn't declare are silently skipped
        // (glGetAttribLocation returns -1 for an unused/optimized-out attribute -- not an error).
        void BindVertexAttributesForDeclaration(GLuint program, GLuint vboId, std::size_t stride,
                                                const std::vector<VertexElement>& declaration,
                                                std::size_t baseByteOffset = 0)
        {
            glBindBuffer(GL_ARRAY_BUFFER, vboId);
            // Every location this renderer's built-in programs ever use must start disabled, so a
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
                                      reinterpret_cast<void*>(baseByteOffset
                                          + static_cast<std::uintptr_t>(element.getOffsetProperty())));
            }

            // A declaration without a Color element leaves aColor disabled, and a disabled
            // attribute delivers the CURRENT generic value -- whatever some earlier stride-based
            // bind happened to set. The stride branches pin constant white for exactly this case
            // (XNA: a colorless vertex type modulates by white), so pin the same constant here
            // instead of inheriting stale state.
            bool declaresColor = false;
            for (const VertexElement& element : declaration)
                if (element.getVertexElementUsageProperty() == VertexElementUsage::Color &&
                    element.getUsageIndexProperty() == 0)
                    declaresColor = true;
            if (!declaresColor)
            {
                const GLint colorLocation = glGetAttribLocation(program, "aColor");
                if (colorLocation >= 0)
                    glVertexAttrib4f(static_cast<GLuint>(colorLocation), 1, 1, 1, 1);
            }
        }

        // True when a propagated declaration is exactly the built-in layout this renderer already
        // infers for its stride (same semantics, usage indices, offsets and formats -- order
        // aside). Such buffers keep the fixed-stride dispatch path with its validated
        // constant-attribute handling; anything else is a genuinely custom declaration and is
        // bound name-driven from its own elements (Task 1080), which reads exactly the caller's
        // declared bytes and therefore needs no refusal-style fidelity guard.
        bool DeclarationMatchesInferredStrideLayout(const std::vector<VertexElement>& declaration,
                                                    std::size_t stride)
        {
            namespace G = CNA::Internal::Graphics;
            // RendererRefusesIt: BindVertexAttributesForStride() throws for a stride outside its
            // table, so an unlisted stride can never be "the built-in layout" -- it must take the
            // declaration-driven path (or carry no declaration and hit the established refusal).
            const G::InferredVertexLayout inferred = G::InferredLayoutForStride(
                static_cast<int>(stride), G::UnlistedStrideLayout::RendererRefusesIt);
            if (!inferred.known) return false;
            if (declaration.size() != inferred.count) return false;
            for (const VertexElement& element : declaration)
            {
                bool matched = false;
                for (std::size_t n = 0; n < inferred.count; ++n)
                {
                    const auto& native = inferred.elements[n];
                    if (native.usage == element.getVertexElementUsageProperty() &&
                        native.usageIndex == element.getUsageIndexProperty() &&
                        native.offset == element.getOffsetProperty() &&
                        native.format == element.getVertexElementFormatProperty())
                    {
                        matched = true;
                        break;
                    }
                }
                if (!matched) return false;
            }
            return true;
        }

        class Tex final : public ITextureRenderer, public RecoverableResource
        {
        public:
            GLuint id{};
            int w{};
            int h{};
            // Task 924 precedent (EasyGLTextureRenderer's own identical field): real mip level
            // count this texture was created for (1 = no mipmapping).
            int mipLevels{1};
            // plan_opengl2.md (context-loss recovery): Texture2D.cpp already unconditionally calls
            // renderer_->ShareCpuPixels(cpuPixels_) on every pixel upload -- previously silently
            // discarded by the shared base-class no-op default. Storing it here lets
            // recreate_gl_resource() restore level 0 exactly like EasyGLTextureRenderer::
            // recreate_gl_resource() does (mip levels beyond 0 are NOT restored -- matches that
            // same, real, pre-existing EasyGL limitation exactly, not a new gap introduced here).
            std::shared_ptr<std::vector<uint8_t>> sharedPixels_;
            OpenGL2Renderer* renderer_ = nullptr;

            explicit Tex(OpenGL2Renderer* renderer, const ImageData& data)
                : w(data.width), h(data.height), mipLevels(data.mipLevels > 0 ? data.mipLevels : 1)
                , renderer_(renderer)
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
                if (renderer_) renderer_->RegisterRecoverable(this);
            }

            ~Tex() override
            {
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (id) glDeleteTextures(1, &id);
            }

            int GetWidth() const override { return w; }
            int GetHeight() const override { return h; }

            void BindGL(int /*unit*/) const override { glBindTexture(GL_TEXTURE_2D, id); }

            void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override
            {
                sharedPixels_ = std::move(pixels);
            }

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
            // (matching EasyGLTextureRenderer::UpdatePixelsLevel's own set_image_2d call), not a
            // glTexSubImage2D into non-existent storage.
            void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override
            {
                glBindTexture(GL_TEXTURE_2D, id);
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            }

            void release_gl_handle_only() override { id = 0; }

            void recreate_gl_resource() override
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipLevels - 1);
                if (sharedPixels_ && !sharedPixels_->empty())
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, sharedPixels_->data());
                else
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
        };

        // XNA DepthFormat enum ordinals: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3.
        // Mirrors EasyGLRenderer's own MapDepthFormat.
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
        // EasyGLRenderTargetRenderer's identical resolve-then-mipmap order (FNA3D's own
        // OPENGL_ResolveTarget behavior).
        class RenderTarget final : public IRenderTargetRenderer, public RecoverableResource
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
            int depthFormat{};
            OpenGL2Renderer* renderer_ = nullptr;

            RenderTarget(OpenGL2Renderer* renderer, int width, int height, int depthFmt,
                        bool mipMap, int requestedSamples)
                : w(width), h(height), levelCount(mipMap ? CalculateRenderTargetMipLevels(width, height) : 1),
                  multiSampleCount(requestedSamples), depthFormat(depthFmt), renderer_(renderer)
            {
                CreateResources();
                if (renderer_) renderer_->RegisterRecoverable(this);
            }

            // plan_opengl2.md (context-loss recovery): the constructor's own allocation logic,
            // extracted so recreate_gl_resource() can reuse it verbatim -- mirrors
            // EasyGLRenderTargetRenderer::CreateResources()'s identical role. A render target's
            // GPU-rendered content cannot be CPU-shadowed (unlike VB/IB/Texture2D), so recreation
            // is always blank -- matches EasyGLRenderTargetRenderer::recreate_gl_resource()'s own
            // real, pre-existing limitation exactly, not a new gap introduced here.
            void CreateResources()
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
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
                if (msaaColorRbo) glDeleteRenderbuffers(1, &msaaColorRbo);
                if (resolveFbo) glDeleteFramebuffers(1, &resolveFbo);
                if (fbo) glDeleteFramebuffers(1, &fbo);
                if (colorTex) glDeleteTextures(1, &colorTex);
            }

            void release_gl_handle_only() override
            {
                fbo = colorTex = depthRbo = msaaColorRbo = msaaDepthRbo = resolveFbo = 0;
            }

            void recreate_gl_resource() override { CreateResources(); }

            int GetWidth() const override { return w; }
            int GetHeight() const override { return h; }

            void BindGL(int /*unit*/) const override { glBindTexture(GL_TEXTURE_2D, colorTex); }
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
            // renderer, verified self-consistent by OpenGL2_2D's own quadrant UV test).
            bool GetData(int level, int x, int y, int rw, int rh, void* data, int /*dataLength*/) const override
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
                return true;
            }
        };

        // Desktop GL 2.1 has the real ARB_occlusion_query GL_SAMPLES_PASSED target (core since
        // GL 1.5), giving an exact pixel count -- unlike EasyGLRenderer's GLES3
        // GL_ANY_SAMPLES_PASSED, which can only report 0 or 1.
        class OcclusionQuery final : public IOcclusionQueryRenderer, public RecoverableResource
        {
        public:
            GLuint id{};
            OpenGL2Renderer* renderer_ = nullptr;

            explicit OcclusionQuery(OpenGL2Renderer* renderer) : renderer_(renderer)
            {
                glGenQueries(1, &id);
                if (renderer_) renderer_->RegisterRecoverable(this);
            }
            ~OcclusionQuery() override
            {
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (id) glDeleteQueries(1, &id);
            }

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

            // plan_opengl2.md (context-loss recovery): a query result is inherently ephemeral
            // (valid only for the single draw sequence between Begin/End) -- there is no CPU-side
            // "content" to shadow, matching EasyGLOcclusionQueryRenderer's own identical
            // regen-only recreate_gl_resource().
            void release_gl_handle_only() override { id = 0; }
            void recreate_gl_resource() override { glGenQueries(1, &id); }
        };

        // GL_TEXTURE_CUBE_MAP (ARB_texture_cube_map, core since GL 1.3). CubeMapFace's own
        // ordinals (PositiveX=0, NegativeX=1, PositiveY=2, NegativeY=3, PositiveZ=4,
        // NegativeZ=5) already match GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z's own
        // consecutive enum values in the same order, so `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`
        // needs no separate mapping table.
        class TextureCubeRenderer final : public ITextureCubeRenderer
        {
        public:
            GLuint id{};
            int size{};
            int levelCount{1};

            TextureCubeRenderer(int cubeSize, bool mipMap)
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

            ~TextureCubeRenderer() override { if (id) glDeleteTextures(1, &id); }

            void BindGL(int /*unit*/) const override { glBindTexture(GL_TEXTURE_CUBE_MAP, id); }

            bool SetData(int face, int level, int x, int y, int w, int h, const void* data, int /*dataLength*/) override
            {
                glBindTexture(GL_TEXTURE_CUBE_MAP, id);
                glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
                return true;
            }

            bool GetData(int face, int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
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
                return true;
            }
        };

        // FBO cube-map render target: one shared FBO whose color attachment is re-pointed at
        // whichever face is currently active (glFramebufferTexture2D with
        // GL_TEXTURE_CUBE_MAP_POSITIVE_X+face), plus one depth/(stencil) renderbuffer shared
        // across all 6 faces (matches the common convention of clearing depth per-face rather
        // than needing 6 independent depth buffers). Single-sample only -- MSAA cube render
        // targets are a follow-up item (plan_opengl2.md), unlike RenderTarget2D's own MSAA support.
        class RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer, public RecoverableResource
        {
        public:
            GLuint fbo{};
            GLuint colorTex{};
            GLuint depthRbo{};
            int size{};
            int levelCount{1};
            int boundFace{-1};
            int depthFormat{};
            OpenGL2Renderer* renderer_ = nullptr;

            RenderTargetCubeRenderer(OpenGL2Renderer* renderer, int cubeSize, int depthFmt, bool mipMap)
                : size(cubeSize), levelCount(mipMap ? CalculateRenderTargetMipLevels(cubeSize, cubeSize) : 1)
                , depthFormat(depthFmt), renderer_(renderer)
            {
                CreateResources();
                if (renderer_) renderer_->RegisterRecoverable(this);
            }

            // plan_opengl2.md (context-loss recovery): see RenderTarget::CreateResources's
            // identical doc comment -- content is not shadow-able, recreation is always blank.
            void CreateResources()
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

            ~RenderTargetCubeRenderer() override
            {
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (depthRbo) glDeleteRenderbuffers(1, &depthRbo);
                if (fbo) glDeleteFramebuffers(1, &fbo);
                if (colorTex) glDeleteTextures(1, &colorTex);
            }

            void release_gl_handle_only() override { fbo = colorTex = depthRbo = 0; boundFace = -1; }

            void recreate_gl_resource() override { CreateResources(); }

            int GetSize() const override { return size; }
            void BindGL(int /*unit*/) const override { glBindTexture(GL_TEXTURE_CUBE_MAP, colorTex); }
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

            // Texture2D-style GetData isn't part of ITextureCubeRenderer (only SetData is
            // pure-virtual there) -- read back via glGetTexImage exactly like TextureCubeRenderer.
            bool GetData(int face, int level, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
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
                return true;
            }
        };

        // GL_TEXTURE_3D + glTexImage3D/glTexSubImage3D are core desktop GL since 1.2 (no
        // extension needed, unlike GLES).
        class Texture3DRenderer final : public ITexture3DRenderer
        {
        public:
            GLuint id{};
            int w{}, h{}, d{};
            int levelCount{1};

            Texture3DRenderer(int width, int height, int depth, bool mipMap)
                : w(width), h(height), d(depth)
                , levelCount(mipMap ? CalculateRenderTargetMipLevels(width, height) : 1)
            {
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_3D, id);
                // Task (plan_opengl2.md follow-up, session 8): real FNA3D_Driver_OpenGL.c
                // OPENGL_CreateTexture3D confirms depth halves per level (max(depth >> i, 1))
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
                // Task 924 precedent (Tex/TextureCubeRenderer's own identical clamp): otherwise a
                // mip-requiring TextureFilter treats this as an incomplete mipmap chain.
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, levelCount - 1);
            }

            ~Texture3DRenderer() override { if (id) glDeleteTextures(1, &id); }

            void BindGL(int /*unit*/) const override { glBindTexture(GL_TEXTURE_3D, id); }

            bool SetData(int level, int x, int y, int z, int sw, int sh, int sd, const void* data, int /*dataLength*/) override
            {
                glBindTexture(GL_TEXTURE_3D, id);
                glTexSubImage3D(GL_TEXTURE_3D, level, x, y, z, sw, sh, sd, GL_RGBA, GL_UNSIGNED_BYTE, data);
                return true;
            }

            bool GetData(int level, int x, int y, int z, int sw, int sh, int sd, void* data, int /*dataLength*/) const override
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
                return true;
            }
        };

        class VB final : public IVertexBufferRenderer, public RecoverableResource
        {
        public:
            GLuint id{};
            int count{};
            std::size_t stride{};
            // Vertex capacity requested at CreateVertexBuffer() time (0 if unknown/unspecified) --
            // used by SetDataWithOptions(Discard) below to size the orphaned allocation to the
            // buffer's full capacity rather than just the (possibly smaller) count of this upload,
            // matching EasyGLVertexBufferRenderer::uploadWithOptions's identical strategy.
            int capacity{};
            bool gpuAllocated{};
            // Task 1080 (this renderer's own follow-up item, see plan_opengl2.md): non-empty only
            // when the caller supplied a genuinely custom VertexDeclaration via
            // VertexBuffer::SetDataRaw() -- see SetVertexDeclaration() below and
            // BindVertexAttributesForDeclaration()'s own doc comment for how this is consumed.
            std::vector<VertexElement> declaration;
            // plan_opengl2.md (context-loss recovery): full copy of the last uploaded content,
            // used by recreate_gl_resource() to restore it after a lost GL context -- mirrors
            // EasyGLVertexBufferRenderer's own cpu_data_ member exactly.
            std::vector<uint8_t> cpuShadow_;
            OpenGL2Renderer* renderer_ = nullptr;

            explicit VB(OpenGL2Renderer* renderer, int vertexCapacity = 0)
                : capacity(vertexCapacity), renderer_(renderer)
            {
                glGenBuffers(1, &id);
                if (renderer_) renderer_->RegisterRecoverable(this);
            }
            ~VB() override
            {
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (id) glDeleteBuffers(1, &id);
            }

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
                if (byteCount > 0)
                {
                    cpuShadow_.resize(static_cast<std::size_t>(byteCount));
                    std::memcpy(cpuShadow_.data(), data, static_cast<std::size_t>(byteCount));
                }
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

            void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
            {
                // The shared layer propagates this buffer's complete declaration before every
                // real upload (GFX-043) -- built-in vertex types included, not just the custom
                // declarations the fork-era vector overload received. Binding dispatch stays
                // behavior-preserving: DeclarationMatchesInferredStrideLayout() routes exact
                // built-in layouts through BindVertexAttributesForStride()'s validated constant
                // handling, and only genuinely custom declarations take the name-driven path.
                declaration = vertexDeclaration.GetVertexElements();
            }

            int GetVertexCount() const override { return count; }

            void release_gl_handle_only() override { id = 0; gpuAllocated = false; }

            void recreate_gl_resource() override
            {
                glGenBuffers(1, &id);
                // cpuShadow_ only ever holds the LAST upload's bytes, which can be smaller than
                // the buffer's original full `capacity` (e.g. a SetDataOptions::Discard upload of
                // fewer vertices than the buffer was created for -- the GPU side is still
                // allocated at full capacity, but cpuShadow_ shrinks to match the smaller upload).
                // Recreating at only cpuShadow_.size() would under-allocate relative to the
                // pre-loss buffer, so a later NoOverwrite upload sized against the ORIGINAL
                // capacity could write past the recreated buffer's end. Allocate the larger of
                // the two, matching SetDataWithOptions(Discard)'s own `total` calculation above.
                const auto shadowBytes = static_cast<GLsizeiptr>(cpuShadow_.size());
                const auto capacityBytes = (capacity > 0 && stride > 0)
                    ? static_cast<GLsizeiptr>(capacity) * static_cast<GLsizeiptr>(stride) : 0;
                const auto total = std::max(shadowBytes, capacityBytes);
                if (total > 0)
                {
                    glBindBuffer(GL_ARRAY_BUFFER, id);
                    glBufferData(GL_ARRAY_BUFFER, total, nullptr, GL_DYNAMIC_DRAW);
                    if (shadowBytes > 0)
                        glBufferSubData(GL_ARRAY_BUFFER, 0, shadowBytes, cpuShadow_.data());
                    gpuAllocated = true;
                }
            }
        };

        class IB final : public IIndexBufferRenderer, public RecoverableResource
        {
        public:
            GLuint id{};
            int count{};
            bool thirtyTwoBit{};
            int capacity{};   // index capacity requested at CreateIndexBufferNN() time; see VB::capacity.
            bool gpuAllocated{};
            // plan_opengl2.md (context-loss recovery): see VB::cpuShadow_'s identical doc comment.
            std::vector<uint8_t> cpuShadow_;
            OpenGL2Renderer* renderer_ = nullptr;

            explicit IB(OpenGL2Renderer* renderer, bool isThirtyTwoBit, int indexCapacity = 0)
                : thirtyTwoBit(isThirtyTwoBit), capacity(indexCapacity), renderer_(renderer)
            {
                glGenBuffers(1, &id);
                if (renderer_) renderer_->RegisterRecoverable(this);
            }
            ~IB() override
            {
                if (renderer_) renderer_->UnregisterRecoverable(this);
                if (id) glDeleteBuffers(1, &id);
            }

            void uploadWithOptions(const void* data, int index_count, GLsizeiptr elemSize, SetDataOptions options)
            {
                count = index_count;
                const auto byteCount = static_cast<GLsizeiptr>(index_count) * elemSize;
                if (byteCount > 0)
                {
                    cpuShadow_.resize(static_cast<std::size_t>(byteCount));
                    std::memcpy(cpuShadow_.data(), data, static_cast<std::size_t>(byteCount));
                }
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

            void release_gl_handle_only() override { id = 0; gpuAllocated = false; }

            void recreate_gl_resource() override
            {
                glGenBuffers(1, &id);
                // See VB::recreate_gl_resource()'s identical doc comment: cpuShadow_ can be
                // smaller than the buffer's original full `capacity` after a smaller Discard
                // upload -- allocate the larger of the two so a later NoOverwrite upload sized
                // against the ORIGINAL capacity can't write past the recreated buffer's end.
                const auto shadowBytes = static_cast<GLsizeiptr>(cpuShadow_.size());
                const auto elemSize = thirtyTwoBit ? GLsizeiptr{4} : GLsizeiptr{2};
                const auto capacityBytes = capacity > 0 ? static_cast<GLsizeiptr>(capacity) * elemSize : 0;
                const auto total = std::max(shadowBytes, capacityBytes);
                if (total > 0)
                {
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, total, nullptr, GL_DYNAMIC_DRAW);
                    if (shadowBytes > 0)
                        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, shadowBytes, cpuShadow_.data());
                    gpuAllocated = true;
                }
            }
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

            // FNA's SpriteBatch.Draw() overloads never use `origin` in destination-pixel units
            // directly -- they first normalize it to a FRACTION of the source rectangle
            // (`originX = origin.X / sourceRect.Width`, see SpriteBatch.cs's own `PushSprite`
            // call sites: `origin.X / sourceW / texture.Width` where `sourceW` is itself
            // `sourceRect.Width / texture.Width`, so the two divisions cancel to
            // `origin.X / sourceRect.Width`), THEN scales that fraction back up by the
            // DESTINATION size (`cornerX = -originX * destinationW`). Using `origin.X` verbatim
            // against `destination.Width` below (previously: no scale factor at all) is only
            // correct when destination.Width == source.Width -- i.e. no stretch. Any stretched
            // draw (the `scale`/`Vector2 scale` overloads, or an explicit destinationRectangle
            // whose size differs from sourceRectangle) pivoted around the wrong point.
            const float originScaleX = destination.Width / static_cast<float>(source.Width != 0 ? source.Width : 1);
            const float originScaleY = destination.Height / static_cast<float>(source.Height != 0 ? source.Height : 1);
            const float ox = origin.X * originScaleX;
            const float oy = origin.Y * originScaleY;
            const float localCorners[4][2] = {
                {-ox, -oy},
                {destination.Width - ox, -oy},
                {destination.Width - ox, destination.Height - oy},
                {-ox, destination.Height - oy},
            };
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            // Task (plan_opengl2.md follow-up): the translation term used to be
            // `+ destination.X + origin.X` (and Y) -- origin is already subtracted into
            // localCorners above (matches FNA's own SpriteBatch.cs SpriteBatchItem.Texture
            // formula: cornerX = -originX*destinationW, then position = rotate(corner) +
            // destinationX, with NO extra origin term added back). Adding origin a second time
            // here shifted the whole rotated sprite by the origin vector for any non-zero
            // rotation (harmless at rotation=0, where the two origin terms happen to cancel).
            const float cosR = std::cos(rotation);
            const float sinR = std::sin(rotation);
            for (int i = 0; i < 4; ++i)
            {
                out[i].x = localCorners[i][0] * cosR - localCorners[i][1] * sinR + destination.X;
                out[i].y = localCorners[i][0] * sinR + localCorners[i][1] * cosR + destination.Y;
                out[i].u = uv[i][0];
                out[i].v = uv[i][1];
            }
        }

        // Owns its own tiny shader program + streaming VBO (mirrors EasyGLSpriteBatchRenderer's
        // per-instance GL resources) rather than a function-local static -- a static handle would
        // go stale across DebugSimulateContextLoss()/multiple GraphicsDevice instances in one
        // process.
        class Sprite final : public ISpriteBatchRenderer
        {
        public:
            explicit Sprite(OpenGL2Renderer* renderer) : renderer_(renderer) {}

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
            // CNAEXT follow-up (plan_opengl2.md): only Draw()'s direct 3D counterpart
            // (customEffectRenderer in drawInternal()) was implemented first; this wires the same
            // EffectRenderer infrastructure into the 2D SpriteBatch path too.
            void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }

            void Draw(const ITextureRenderer& texture, float x, float y) override
            {
                const Rectangle destination(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
                const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
                Draw(texture, destination, source, Color::White);
            }

            void Draw(const ITextureRenderer& texture, const Rectangle& destination,
                     const Rectangle& source, const Color& color) override
            {
                Draw(texture, destination, source, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
            }

            void Draw(const ITextureRenderer& texture, const Rectangle& destination, const Rectangle& source,
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

                // Task-1078-equivalent fix (see EasyGLRenderer::GetCurrentRenderTarget2DSize's
                // own history): a draw into a bound RenderTarget2D must size its screen->clip
                // mapping to the RT, not the window/virtual resolution -- those only coincide when
                // the RT happens to match the window size. Deliberately LOGICAL-only (not
                // physical-window-aware): the actual GL viewport rectangle GraphicsDevice::
                // UpdateViewportFromWindow() applies (renderer_->GetDefaultViewportRect()) already
                // maps NDC [-1,1] to the correct Letterbox/Overscan/Stretch physical sub-rectangle
                // by itself -- GL's own viewport transform does the logical->physical mapping, so
                // this CPU-side math only ever needs to reach clip space in LOGICAL units.
                int viewportWidth = 0, viewportHeight = 0;
                if (!renderer_->GetCurrentRenderTarget2DSize(viewportWidth, viewportHeight))
                    renderer_->GetViewportSize(viewportWidth, viewportHeight);
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
                    // convention (v * transform_), matching EasyGLSpriteBatchRenderer's own
                    // `transform_ * orthoM` combined-matrix order (only the XY affine part matters;
                    // SpriteBatch's transform is always 2D).
                    const float tx = sc[i].x * transform_.M11 + sc[i].y * transform_.M21 + transform_.M41;
                    const float ty = sc[i].x * transform_.M12 + sc[i].y * transform_.M22 + transform_.M42;
                    // Render-time Y-flip for 2D render targets: screen-space top maps to clip
                    // +1 for the window, clip -1 while a flipped target is bound -- see
                    // RtFlipActive()'s own doc comment.
                    const float ndcY = renderer_->RtFlipActive()
                        ? (ty / viewportHeight) * 2.0f - 1.0f
                        : 1.0f - (ty / viewportHeight) * 2.0f;
                    corners[i] = {(tx / viewportWidth) * 2.0f - 1.0f, ndcY, layerDepth,
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
                // standard legacy-GL approach and matches this renderer's single-texture-unit,
                // no-sampler-cache design.
                const GLint glFilter = ToGLFilter(filter_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrapMode(addressU_));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrapMode(addressV_));

                // Blending itself is NOT touched here -- GraphicsDevice::setBlendStateProperty()
                // (driven by SpriteBatch::Begin()'s BlendState argument, BlendState::AlphaBlend by
                // default) already reached OpenGL2Renderer::ApplyBlendState() before this
                // draw, exactly like EasyGLSpriteBatchRenderer::Draw(). Hardcoding a blend func here
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
            void DrawWithCustomEffect(const ITextureRenderer& texture, const Rectangle& destination,
                                      const Rectangle& source, const Color& color, float rotation,
                                      const Vector2& origin, SpriteEffects effects, float layerDepth)
            {
                IEffectRenderer* effectRenderer = customEffect_->GetEffectRendererPtr();
                // Unlike EasyGLSpriteBatchRenderer::FlushBatch() (which silently falls back to ITS
                // OWN built-in program when the custom effect fails to compile), this renderer's
                // built-in shader expects a fundamentally different vertex convention (see above)
                // -- falling back would draw garbage, not a plausible approximation. Skip instead.
                if (!effectRenderer || !effectRenderer->IsValid())
                    return;

                customEffect_->Apply();

                // Deliberately LOGICAL-only, same reasoning as the built-in path above: the
                // actual GL viewport rectangle (renderer_->GetDefaultViewportRect(), applied by
                // GraphicsDevice::UpdateViewportFromWindow()) already maps NDC to the correct
                // Letterbox/Overscan/Stretch physical sub-rectangle, so MatrixTransform only ever
                // needs to reach clip space in LOGICAL units.
                int viewportWidth = 0, viewportHeight = 0;
                if (!renderer_->GetCurrentRenderTarget2DSize(viewportWidth, viewportHeight))
                    renderer_->GetViewportSize(viewportWidth, viewportHeight);
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

                // zNear=0, zFar=-1 (NOT the more obvious -1/1) is deliberate: Matrix::
                // CreateOrthographicOffCenter's Z row is M33=1/(zNear-zFar), M43=zNear/(zNear-zFar)
                // -- with (0,-1) that's M33=1, M43=0, i.e. an IDENTITY Z/W mapping (clipZ==inputZ).
                // That matters here because real FNA's own SpriteEffect MatrixTransform (see
                // SpriteBatch.cs's PrepRenderState, "Inlined CreateOrthographicOffCenter *
                // transformMatrix") only ever remaps X/Y through the orthographic projection --
                // its inlined Z/W row is `transformMatrix`'s OWN Z/W row verbatim, with NO
                // projection-driven Z scaling at all, so a real XNA custom SpriteBatch effect's
                // clip-space Z ends up equal to its raw vertex Z (layerDepth) whenever
                // transformMatrix is the identity-in-Z 2D matrix SpriteBatch.Begin() almost always
                // receives. A more "natural"-looking (-1,1) near/far here would instead remap
                // layerDepth through `-0.5*z+0.5`, silently disagreeing with the layerDepth this
                // same file's OWN built-in (non-custom-effect) Sprite::Draw() path writes as a raw,
                // untransformed gl_Position.z (see that code) -- the two paths must produce the
                // SAME clip-space Z for the same layerDepth input, or mixing custom-Effect and
                // built-in SpriteBatch draws under an enabled DepthStencilState (a legitimate XNA
                // pattern for interleaving 2D/3D draw order) would sort inconsistently.
                // Render-time Y-flip for 2D render targets: swap the ortho's top/bottom while a
                // flipped target is bound -- see RtFlipActive()'s own doc comment.
                const Matrix ortho = renderer_->RtFlipActive()
                    ? Matrix::CreateOrthographicOffCenter(
                          0.0f, static_cast<float>(viewportWidth), 0.0f,
                          static_cast<float>(viewportHeight), 0.0f, -1.0f)
                    : Matrix::CreateOrthographicOffCenter(
                          0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
                          0.0f, 0.0f, -1.0f);
                const Matrix combined = transform_ * ortho;
                float matColMajor[16];
                combined.ToColumnMajor(matColMajor);
                effectRenderer->SetUniformMat4("MatrixTransform", matColMajor);

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

            OpenGL2Renderer* renderer_;
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

    OpenGL2Renderer::OpenGL2Renderer(const GraphicsRendererCreateArgs& args)
        : platformContext_(std::make_unique<PlatformGlContextOwner>(
              RequirePlatformGlContext(args.glContext, "OPENGL2"),
              RequirePlatformGlWindow(args.surface, "OPENGL2"), RequestedContext()))
        , surface_(args.surface)
        , swapInterval_(args.swapInterval)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , contextRecoveryEnabled_(args.contextRecoveryEnabled)
    {
#if defined(_WIN32)
        LoadWin32GLExtensions();
#endif
        LoadInstancingExtensions();
        SetSwapInterval(args.swapInterval);
        ensurePrograms();

        // Sane default until the first real GraphicsDevice.BlendState reaches ApplyBlendState()
        // (matches BlendState::AlphaBlend: One / InverseSourceAlpha, premultiplied convention).
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
    }

    OpenGL2Renderer::~OpenGL2Renderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
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
    }

    namespace
    {
        // Shared by every fragment shader below: fog blend (object-space vertex Z, matching
        // EasyGLRenderer's identical, documented simplification -- see
        // feedback_easygl_fog_object_space_only in this project's own notes) and, for the
        // textured variants, the AlphaTestEffect discard formula from GpuDrawParams::alphaTest's
        // own doc comment. Both default to no-op (uFogEnabled=0, uAlphaTest={0,0,1,1}) so every
        // program works unchanged for draws that don't use either feature.
        const char* kFogVertexChunk =
            "uniform float uFogEnabled;uniform vec4 uFogVector;varying float vFogFactor;";
        // REMED-GFX-010: the FNA fog vector is consumed directly -- fogFactor is
        // saturate(dot(objectPosition, uFogVector)) and vFogFactor keeps this file's established
        // "keep" convention (1 = unfogged), so the fragment-side mix() chunks are unchanged. The
        // vector's own encoding carries the special cases: all-zero yields keep=1 (fog disabled)
        // and {0,0,0,1} (FogStart == FogEnd) yields keep=0 (fully fogged), so the scalar-era
        // epsilon branch has no subject anymore.
        const char* kFogVertexCompute =
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(vec4(aPosition,1.0),uFogVector),0.0,1.0)):1.0;";
        const char* kFogFragmentChunk =
            "varying float vFogFactor;uniform vec3 uFogColor;";
        const char* kFogFragmentApply = "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);";
        const char* kAlphaTestFragmentChunk = "uniform vec4 uAlphaTest;";
        const char* kAlphaTestFragmentApply =
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;";
    }

    void OpenGL2Renderer::ensurePrograms()
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
        // EasyGLRenderer::EnsureDualTextured3DProgram's identical formula).
        const std::string dualTextureFragmentSrc = std::string(
            "varying vec4 vColor;varying vec2 vTex;uniform vec4 uDiffuse;uniform sampler2D uTex;uniform sampler2D uTex2;") +
            kAlphaTestFragmentChunk + kFogFragmentChunk +
            "void main(){vec4 base=texture2D(uTex,vTex);base.rgb*=2.0;"
            "gl_FragData[0]=base*texture2D(uTex2,vTex)*vColor*uDiffuse;" +
            kAlphaTestFragmentApply + kFogFragmentApply + "}";

        // BasicEffect lighting: per-pixel Blinn-Phong, 3 directional lights + ambient + emissive +
        // specular (matches EasyGLRenderer::EnsureLit3DProgram's formula exactly). Always
        // per-pixel regardless of GpuDrawParams::preferPerPixelLighting -- matches this project's
        // own documented, accepted convention (see that field's doc comment: every renderer except
        // D3D9 always renders per-pixel). uNormalMatrix is the real inverse-transpose of World's
        // upper-3x3 (see ComputeNormalMatrix3x3 below), correct under non-uniform scale too.
        // Task follow-up: BasicEffect.LightingEnabled=true combined with VertexColorEnabled=true
        // is a real, legal FNA/XNA combination (see BasicEffect.cpp's own unconditional
        // `p.vertexColorEnabled = VertexColorEnabled;`, uploaded regardless of LightingEnabled) --
        // aColor/vColor/uVertexColorEnabled below mirror skinnedVertexSrc/skinnedFragmentSrc's
        // own identical pattern (see those programs' doc comment) so a custom VertexDeclaration
        // carrying both Normal and Color (bound via BindVertexAttributesForDeclaration(), which
        // matches attributes by name) is no longer silently dropped under lighting. Harmless for
        // the far more common VertexPositionNormalTexture case (stride 32, no real color data):
        // BindVertexAttributesForStride()'s own stride>=32 branch still disables location 1 and
        // sets it to a constant white via glVertexAttrib4f, so vColor reads (1,1,1,1) there.
        const char* litVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;attribute vec4 aColor;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat3 uNormalMatrix;"
            "uniform float uFogEnabled;uniform vec4 uFogVector;"
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;varying vec4 vColor;"
            "void main(){"
            "gl_Position=uWVP*vec4(aPosition,1.0);"
            "vNormal=uNormalMatrix*aNormal;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*vec4(aPosition,1.0)).xyz;"
            "vColor=aColor;"
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(vec4(aPosition,1.0),uFogVector),0.0,1.0)):1.0;"
            "}";
        const char* litFragmentSrc =
            "varying vec3 vNormal;varying vec2 vTex;varying vec3 vWorldPos;varying float vFogFactor;varying vec4 vColor;"
            "uniform sampler2D uTex;uniform bool uTextureEnabled;uniform vec4 uDiffuse;"
            "uniform vec3 uAmbientColor;uniform float uVertexColorEnabled;"
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
            "vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);"
            "gl_FragData[0]=vec4(litRGB*texColor.rgb*vc.rgb,uDiffuse.a*texColor.a*vc.a);"
            "gl_FragData[0].rgb+=specularRGB*gl_FragData[0].a;"
            "float _at=(uAlphaTest.y>0.0)?((abs(gl_FragData[0].a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):"
            "((gl_FragData[0].a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);if(_at<0.0)discard;"
            "gl_FragData[0].rgb=mix(uFogColor,gl_FragData[0].rgb,vFogFactor);"
            "}";

        // EnvironmentMapEffect: reflection-mapped shading (matches
        // EasyGLRenderer::EnsureEnvMapped3DProgram's formula exactly -- same per-vertex
        // Fresnel evaluation, same litRGB = lightSum*diffuse+emissive composition, same
        // mix(baseColor,envSample*alpha,blendFactor)+specular*envSample.a*alpha blend). Uses the
        // same VertexPositionNormalTexture (stride>=32) layout as litProgram_.
        const char* envMapVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat3 uNormalMatrix;uniform vec3 uEyePosition;"
            "uniform float uFogEnabled;uniform vec4 uFogVector;"
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
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(vec4(aPosition,1.0),uFogVector),0.0,1.0)):1.0;"
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
        // EasyGLRenderer::EnsureSkinnedProgram's formula exactly -- same
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
            "uniform float uFogEnabled;uniform vec4 uFogVector;"
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
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(skinnedPos,uFogVector),0.0,1.0)):1.0;"
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
        // EasyGLRenderer::EnsurePbrProgram/EnsurePbrSkinnedProgram's formula exactly --
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
            "uniform float uFogEnabled;uniform vec4 uFogVector;"
            "varying vec3 vNormal;varying vec3 vTangent;varying float vBitangentSign;"
            "varying vec2 vTex;varying float vFogFactor;varying vec3 vWorldPos;"
            "void main(){"
            "gl_Position=uWVP*vec4(aPosition,1.0);"
            "vNormal=uNormalMatrix*aNormal;"
            // Tangent transforms as a plain direction under the World upper-3x3 (not the
            // inverse-transpose uNormalMatrix the normal itself uses) -- correct for
            // uniform-scale World transforms, a documented simplification for non-uniform scale
            // shared with most real-time engines lacking a full per-tangent inverse-transpose
            // (matches EasyGLRenderer's own identical comment/formula). mat3(mat4)
            // truncation requires GLSL 1.20 (this file targets 1.10 throughout), so built manually
            // -- same technique already used for SkinnedEffect's skin matrix above.
            "mat3 world3=mat3(uWorld[0].xyz,uWorld[1].xyz,uWorld[2].xyz);"
            "vTangent=world3*aTangent.xyz;"
            "vBitangentSign=aTangent.w;"
            "vTex=aTexCoord;"
            "vWorldPos=(uWorld*vec4(aPosition,1.0)).xyz;"
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(vec4(aPosition,1.0),uFogVector),0.0,1.0)):1.0;"
            "}";

        const char* pbrSkinnedVertexSrc =
            "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec4 aTangent;attribute vec2 aTexCoord;"
            "attribute vec4 aBoneWeight;attribute vec4 aBoneIndices;"
            "uniform mat4 uWVP;uniform mat4 uWorld;uniform mat4 uBones[72];uniform int uWeightsPerVertex;"
            "uniform float uFogEnabled;uniform vec4 uFogVector;"
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
            "vFogFactor=(uFogEnabled>0.5)?"
            "(1.0-clamp(dot(skinnedPos,uFogVector),0.0,1.0)):1.0;"
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

    void OpenGL2Renderer::ensureDefaultWhiteTextures()
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

    void OpenGL2Renderer::Clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGL2Renderer::Present() { platformContext_->SwapBuffers(); }

    // Mirrors SdlGpuRenderer::ComputeLogicalViewport's algorithm exactly -- see this
    // renderer's own LogicalViewport doc comment for field meanings and the "why this renderer,
    // not EasyGL" rationale. NativeBackBuffer/no-virtual-resolution/zero-size-window all
    // degenerate to the full physical window with no scaling, matching every mode's own prior
    // (pre-this-task) behavior when the game never actually engaged Letterbox/Overscan/Stretch.
    OpenGL2Renderer::LogicalViewport OpenGL2Renderer::ComputeLogicalViewport() const
    {
        int physW = 0, physH = 0;
        surface_.GetDrawableSize(physW, physH);

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

    void OpenGL2Renderer::GetViewportSize(int& width, int& height)
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
    void OpenGL2Renderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        x = static_cast<int>(std::lround(viewport.x));
        y = static_cast<int>(std::lround(viewport.y));
        width = static_cast<int>(std::lround(viewport.width));
        height = static_cast<int>(std::lround(viewport.height));
    }

    void OpenGL2Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void OpenGL2Renderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenGL2Renderer::SetPresentationMode(int mode) { presentationMode_ = static_cast<CnaPresentationMode>(mode); }
    void OpenGL2Renderer::SetSwapInterval(int interval) { swapInterval_ = interval; platformContext_->SetSwapInterval(interval); }

    void OpenGL2Renderer::RegisterRecoverable(RecoverableResource* resource)
    {
        if (contextRecoveryEnabled_ && resource) recoverableResources_.push_back(resource);
    }

    void OpenGL2Renderer::UnregisterRecoverable(RecoverableResource* resource)
    {
        auto it = std::find(recoverableResources_.begin(), recoverableResources_.end(), resource);
        if (it != recoverableResources_.end()) recoverableResources_.erase(it);
    }

    void OpenGL2Renderer::SetContextRecoveryEnabled(bool enabled) { contextRecoveryEnabled_ = enabled; }

    void OpenGL2Renderer::DebugSimulateContextLoss()
    {
        // 1. Every registered resource drops its GL handle WITHOUT calling any gl* function --
        // the context is still technically valid here, but about to be destroyed, and per
        // RecoverableResource's own contract no gl* call is safe once this loop starts (matches
        // EasyGLRenderer::DebugSimulateContextLoss's identical ordering/reasoning).
        for (RecoverableResource* resource : recoverableResources_)
            resource->release_gl_handle_only();

        // Built-in lazily-created programs/textures are renderer-owned, not registry-tracked --
        // reset directly so ensurePrograms()/ensureDefaultWhiteTextures() recreate them below
        // (mirrors EasyGLRenderer's own prog_colored_.reset_no_gl() etc. pattern exactly).
        colorProgram_ = texturedProgram_ = dualTextureProgram_ = litProgram_ = 0;
        envMapProgram_ = skinnedProgram_ = pbrProgram_ = pbrSkinnedProgram_ = 0;
        defaultWhiteTexture2D_ = defaultWhiteTextureCube_ = defaultFlatNormalTexture2D_ = 0;
        mrtFboReady_ = false;
        mrtFbo_ = 0;

        // 2. Destroy and recreate the platform GL context.
        platformContext_->Recreate();

        // 3. Reload GL function pointers (matches LoadWin32GLExtensions()/LoadInstancingExtensions()'s
        // own constructor-time call, since a fresh context has no previously-resolved proc
        // addresses -- reassigning these to nullptr first isn't needed; the platform resolver is
        // safe to call again and simply re-resolves the same (or now-current-context) addresses).
#if defined(_WIN32)
        LoadWin32GLExtensions();
#endif
        LoadInstancingExtensions();
        SetSwapInterval(swapInterval_);
        ensurePrograms();
        ensureDefaultWhiteTextures();
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

        // 4. Every registered resource recreates its GL object from whatever CPU-side shadow it
        // kept (VB/IB/Tex) or blank (RenderTarget/RenderTargetCubeRenderer/OcclusionQuery -- see
        // each class's own recreate_gl_resource() doc comment for why).
        for (RecoverableResource* resource : recoverableResources_)
            resource->recreate_gl_resource();

        // 5. Re-select whichever render target was ACTIVE at the moment of loss. Step 4 only
        // rebuilds each resource's OWN fbo handle (RenderTarget::CreateResources()/
        // RenderTargetCubeRenderer::CreateResources() both end with glBindFramebuffer(...,0)) --
        // it does not re-bind it as the CURRENTLY BOUND target. currentRt_/currentRtCube_ are
        // themselves untouched by context loss (nothing at the GraphicsDevice/game layer ran),
        // so without this, the very first draw issued after recovery would silently render into
        // the backbuffer instead of the render target the game still believes is active. This
        // was a real, previously-undocumented gap (EasyGLRenderer::DebugSimulateContextLoss
        // has the identical omission -- not fixed here, out of this renderer's scope).
        if (currentRt_)
            currentRt_->BindAsRenderTarget();
        else if (currentRtCube_)
            currentRtCube_->BindAsRenderTargetFace(currentRtCubeFace_);
        // Winding compensation for the render-time Y-flip survives recovery too.
        glFrontFace(RtFlipActive() ? GL_CW : GL_CCW);
    }

    void OpenGL2Renderer::DebugRestoreContext()
    {
        // Desktop loss+restore is a single atomic operation (matches
        // EasyGLRenderer::DebugRestoreContext's identical delegation) -- there is no
        // separate "restore" step on desktop GL the way there is for WebGL's asynchronous
        // browser-driven context-restore event.
        DebugSimulateContextLoss();
    }

    std::unique_ptr<ITextureRenderer> OpenGL2Renderer::CreateTexture(const ImageData& data) { return std::make_unique<Tex>(this, data); }
    std::unique_ptr<ISpriteBatchRenderer> OpenGL2Renderer::CreateSpriteBatch() { return std::make_unique<Sprite>(this); }
    std::unique_ptr<IOcclusionQueryRenderer> OpenGL2Renderer::CreateOcclusionQuery() { return std::make_unique<OcclusionQuery>(this); }

    std::unique_ptr<ITextureCubeRenderer> OpenGL2Renderer::CreateTextureCube(int size, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<TextureCubeRenderer>(size, mipMap);
    }

    std::unique_ptr<ITexture3DRenderer> OpenGL2Renderer::CreateTexture3D(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
    {
        return std::make_unique<Texture3DRenderer>(w, h, depth, mipMap);
    }

    std::unique_ptr<IEffectRenderer> OpenGL2Renderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<EffectRenderer>();
        renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    std::unique_ptr<IRenderTargetRenderer> OpenGL2Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<RenderTarget>(this, w, h, depthFormat, mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenGL2Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool /*preserveContents*/, bool mipMap, int /*multiSampleCount*/)
    {
        // plan_opengl2.md follow-up: MSAA cube render targets are not implemented (unlike
        // RenderTarget2D's own MSAA support) -- multiSampleCount is accepted (matching the
        // interface signature) but ignored. preserveContents is likewise accepted and needs no
        // storage: this renderer renders cube faces into persistent GL texture storage directly
        // (no transient/discardable face buffer exists), so face contents survive an unbind
        // either way -- preserving under RenderTargetUsage::DiscardContents is permitted
        // behavior, not a divergence.
        return std::make_unique<RenderTargetCubeRenderer>(this, size, depthFormat, mipMap);
    }

    void OpenGL2Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        if (currentRtCube_ != rt)
            unbindCurrentRenderTarget();

        if (rt)
        {
            rt->BindAsRenderTargetFace(face);
            currentRtWidth_ = rt->GetSize();
            currentRtHeight_ = rt->GetSize();
            currentRtCube_ = rt;
            currentRtCubeFace_ = face;
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            currentRtWidth_ = 0;
            currentRtHeight_ = 0;
        }
        // Cube faces keep GL's native orientation (RtFlipActive() is false here), so this
        // restores the default front face after any prior flipped 2D binding.
        glFrontFace(RtFlipActive() ? GL_CW : GL_CCW);
    }

    void OpenGL2Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count <= 0) { SetRenderTarget2D(nullptr); return; }
        if (!renderTargets)
            throw std::invalid_argument(
                "OPENGL2 SetRenderTargets: nonzero count requires a binding array.");
        if (count == 1)
        {
            // Every descriptor kind is explicitly consumed: a cube-face binding routes to the
            // real cube-face setter, never flattened to a RenderTarget2D or to face +X.
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                        renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }

        constexpr int kMaxMrtDescriptors = 8;
        if (count > kMaxMrtDescriptors)
            throw std::runtime_error(
                "OPENGL2 SetRenderTargets: requested " + std::to_string(count)
                + " targets, but this renderer binds at most "
                + std::to_string(kMaxMrtDescriptors) + ".");
        IRenderTargetRenderer* targets[kMaxMrtDescriptors] = {};
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "OPENGL2 SetRenderTargets: cube faces in a multi-target set are not "
                    "implemented by this CNA renderer.");
            targets[i] = renderTargets[i].GetRenderTarget2D();
            if (!targets[i])
                throw std::runtime_error(
                    "OPENGL2 SetRenderTargets: binding " + std::to_string(i)
                    + " carries no RenderTarget2D renderer.");
        }
        SetRenderTargetsArray(targets, count);
    }

    void OpenGL2Renderer::SetRenderTargetsArray(IRenderTargetRenderer* const* rts, int count)
    {
        if (count <= 0) { SetRenderTarget2D(nullptr); return; }
        if (count == 1) { SetRenderTarget2D(rts[0]); return; }

        // MRT: unbind whatever single RT/cube-face (or previous MRT set) was previously active
        // (mip regen/MSAA resolve if needed) -- mirrors EasyGLRenderer::SetRenderTargets's
        // identical ordering.
        unbindCurrentRenderTarget();

        if (!mrtFboReady_)
        {
            glGenFramebuffers(1, &mrtFbo_);
            mrtFboReady_ = true;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, mrtFbo_);

        // Every IRenderTargetRenderer this renderer ever creates (CreateRenderTarget2D) is a
        // concrete OpenGL2::RenderTarget -- safe to downcast to reach its raw color-texture
        // handle, matching EasyGLRenderer::SetRenderTargets's own static_cast precedent.
        constexpr int kMaxMRT = 8;
        const int n = count < kMaxMRT ? count : kMaxMRT;
        GLenum drawBufs[kMaxMRT];
        mrtTargets_.clear();
        for (int i = 0; i < n; ++i)
        {
            auto* target = static_cast<RenderTarget*>(rts[i]);
            // A target created with multiSampleCount>0 renders into its OWN multisampled
            // renderbuffer (see RenderTarget::CreateResources) -- attaching colorTex directly
            // here (its single-sample RESOLVE target) would silently render single-sampled,
            // discarding the requested antialiasing. unbindCurrentRenderTarget() resolves each
            // such target into its own resolveFbo (one glBlitFramebuffer per target) once this
            // MRT set stops being active -- see mrtTargets_ below.
            if (target->multiSampleCount > 0)
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, target->msaaColorRbo);
            else
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, target->colorTex, 0);
            drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;
            mrtTargets_.push_back(rts[i]);
        }
        glDrawBuffers(n, drawBufs);

        // A shared depth/stencil buffer across the whole MRT set, taken from attachment 0's own
        // depthRbo -- matches XNA's own convention (FNA validates every binding in a set shares
        // the same size) and this method's own "sized after attachment 0" precedent below.
        // Previously MISSING entirely (mrtFbo_ never got a depth/stencil attachment at all), so
        // any DepthStencilState with depth/stencil testing enabled had no real depth buffer to
        // test against while MRT was active.
        //
        // Detach whatever depth/stencil renderbuffer a PRIOR SetRenderTargets() call may have
        // left attached before conditionally re-attaching the current one -- mrtFbo_ is one
        // shared, persistent FBO reused across calls, so a stale attachment from a set that HAD a
        // depth format would otherwise survive into a set that doesn't. Left unaddressed, that
        // stale (single-sample) depth attachment sitting alongside a NEW multisampled color
        // attachment violates GL's same-sample-count-across-all-attachments rule
        // (GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE), silently making every subsequent MRT draw a
        // no-op on an incomplete FBO -- caught by this session's own new regression test
        // (OpenGL2_MRT_Depth_MSAA) failing with an all-black readback before this detach was added.
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        auto* target0 = static_cast<RenderTarget*>(rts[0]);
        if (target0->depthRbo)
        {
            GLenum depthInternalFormat = 0, depthAttachment = 0;
            MapDepthFormat(target0->depthFormat, depthInternalFormat, depthAttachment);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, depthAttachment, GL_RENDERBUFFER, target0->depthRbo);
        }

        // Sized after attachment 0 (matches XNA's own convention -- FNA validates every binding
        // in a MRT set shares the same size, so attachment 0's size represents them all).
        currentRtWidth_ = rts[0]->GetWidth();
        currentRtHeight_ = rts[0]->GetHeight();
        // MRT targets are deliberately NOT tracked as currentRt_/currentRtCube_ (a single pointer
        // can't represent a whole set) -- mrtTargets_ above is the dedicated equivalent
        // unbindCurrentRenderTarget() uses for MRT-specific per-target resolve/mip-regen.

        // Winding compensation for the render-time Y-flip -- see RtFlipActive().
        glFrontFace(RtFlipActive() ? GL_CW : GL_CCW);
    }

    void OpenGL2Renderer::unbindCurrentRenderTarget()
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

        // MRT resolve: each attachment that rendered into its own msaaColorRbo (see
        // SetRenderTargets above) needs its OWN per-target blit into its resolveFbo --
        // glBlitFramebuffer can only resolve ONE selected read attachment (via glReadBuffer) into
        // one draw attachment per call, unlike the single-RT UnbindAsRenderTarget() path above,
        // which only ever had one attachment to begin with. Mip regeneration afterward mirrors
        // that same single-RT precedent, per target.
        if (!mrtTargets_.empty())
        {
            for (std::size_t i = 0; i < mrtTargets_.size(); ++i)
            {
                auto* target = static_cast<RenderTarget*>(mrtTargets_[i]);
                if (target->multiSampleCount > 0)
                {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, mrtFbo_);
                    glReadBuffer(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i));
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->resolveFbo);
                    glDrawBuffer(GL_COLOR_ATTACHMENT0);
                    glBlitFramebuffer(0, 0, target->w, target->h, 0, 0, target->w, target->h,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                if (target->levelCount > 1)
                {
                    glBindTexture(GL_TEXTURE_2D, target->colorTex);
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            mrtTargets_.clear();
        }
    }

    void OpenGL2Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
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
        // The render-time Y-flip inverts rasterized winding; flipping the front-face definition
        // restores every CullMode's screen-space meaning -- see RtFlipActive()'s own doc comment.
        glFrontFace(RtFlipActive() ? GL_CW : GL_CCW);
    }

    bool OpenGL2Renderer::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (currentRtHeight_ == 0) return false;
        width = currentRtWidth_;
        height = currentRtHeight_;
        return true;
    }

    bool OpenGL2Renderer::RtFlipActive() const
    {
        // GFX-209 oracle finding: this renderer used to render into 2D render targets in GL's
        // native bottom-up texture order and read them back without a flip, so both
        // RenderTarget2D::GetData and sampling a RenderTarget2D as a texture presented the scene
        // VERTICALLY FLIPPED -- masked by the lane's own orientation-insensitive assertions
        // (centre pixels, solid fills) and caught by the shared wireframe pixel oracle at
        // integration. The fix is FNA's own convention: flip Y at RENDER time while a 2D target
        // (single or MRT) is bound, so texture storage is top-down and matches Texture2D's
        // upload/readback/sampling convention with no flips at any CPU boundary. Cube faces are
        // deliberately excluded: GL cube faces have their own spec-defined orientation and every
        // cube path (env-map sampling, per-face readback) is already self-consistent.
        return currentRt_ != nullptr || !mrtTargets_.empty();
    }

    namespace
    {
        /// Negates the Y row of a column-major clip-space matrix (indices 1, 5, 9, 13) -- the
        /// render-time Y-flip RtFlipActive() selects.
        void FlipClipYColumnMajor(float m[16])
        {
            m[1] = -m[1]; m[5] = -m[5]; m[9] = -m[9]; m[13] = -m[13];
        }
    }

    void OpenGL2Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // When a RenderTarget2D's FBO is currently bound, its single color attachment is
        // already the implicit read source (GL_BACK is only valid for the default framebuffer);
        // mirrors EasyGLRenderer::ReadBackbuffer's identical currentRtHeight_-gated
        // behavior. Real RenderTarget2D pixel readback normally goes through
        // RenderTarget::GetData() instead -- this path exists for parity with that renderer.
        if (currentRtHeight_ == 0)
            glReadBuffer(GL_BACK);

        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            surface_.GetDrawableSize(windowWidth, fbH);
        }

        // While a flipped 2D render target is bound its storage is already top-down (see
        // RtFlipActive()), so caller rows map to texture rows directly and glReadPixels'
        // bottom-up window rows return them in ascending caller order with no swap. The window
        // (and cube-face) path keeps GL's bottom-left origin and flips as before (mirrors
        // EasyGLRenderer::ReadBackbuffer's identical convention).
        if (RtFlipActive())
        {
            glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            return;
        }
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

    void OpenGL2Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGL2Renderer::ClearDepth(float depth)
    {
        glClearDepth(depth);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void OpenGL2Renderer::ClearStencil(int stencil)
    {
        glClearStencil(stencil);
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        glClearDepth(depth);
        glClearStencil(stencil);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        glClearColor(r, g, b, a);
        glClearDepth(depth);
        glClearStencil(stencil);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void OpenGL2Renderer::SetDepthTestEnabled(bool enabled) { enabled ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); }
    void OpenGL2Renderer::SetBlendEnabled(bool enabled) { enabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND); }
    void OpenGL2Renderer::SetDepthWriteEnabled(bool enabled) { glDepthMask(enabled ? GL_TRUE : GL_FALSE); }

    std::unique_ptr<IVertexBufferRenderer> OpenGL2Renderer::CreateVertexBuffer(int vertex_capacity) { return std::make_unique<VB>(this, vertex_capacity); }
    std::unique_ptr<IIndexBufferRenderer> OpenGL2Renderer::CreateIndexBuffer16(int index_capacity) { return std::make_unique<IB>(this, false, index_capacity); }
    std::unique_ptr<IIndexBufferRenderer> OpenGL2Renderer::CreateIndexBuffer32(int index_capacity) { return std::make_unique<IB>(this, true, index_capacity); }

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

    void OpenGL2Renderer::drawInternal(const IVertexBufferRenderer& vbi, const IIndexBufferRenderer* ibi,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams* params)
    {
        const auto* vb = dynamic_cast<const VB*>(&vbi);
        if (!vb)
            throw std::runtime_error("OPENGL2: incompatible vertex buffer");
        const auto* ib = dynamic_cast<const IB*>(ibi);

        // Task CreateEffectRenderer (plan_opengl2.md): a user-authored ShaderEffect entirely
        // bypasses every built-in program/uniform above -- bind it directly, upload the XNA-HLSL-
        // style "World"/"Projection"/"View" semantic names a custom shader is expected to declare
        // (matches EasyGLRenderer::BindCustomEffectMatrices's identical uniform names), and
        // draw using the same stride-based vertex layout every built-in program uses (see
        // EffectRenderer's own doc comment for why the attribute NAMES matter here, unlike EasyGL).
        if (params && params->customEffectRenderer)
        {
            params->customEffectRenderer->Bind();
            float worldCM[16], viewCM[16], projCM[16];
            world.ToColumnMajor(worldCM);
            view.ToColumnMajor(viewCM);
            projection.ToColumnMajor(projCM);
            params->customEffectRenderer->SetUniformMat4("World", worldCM);
            params->customEffectRenderer->SetUniformMat4("View", viewCM);
            // Render-time Y-flip for 2D render targets, applied through the Projection uniform a
            // custom shader multiplies by -- see RtFlipActive()'s own doc comment.
            if (RtFlipActive())
                FlipClipYColumnMajor(projCM);
            params->customEffectRenderer->SetUniformMat4("Projection", projCM);

            // Task 1080: a VertexBuffer whose propagated declaration is genuinely custom (not the
            // built-in layout its stride already implies) is bound by attribute NAME (see
            // BindVertexAttributesForDeclaration's own doc comment) against THIS custom program
            // specifically -- dynamic_cast to reach its raw program handle through the generic
            // IEffectRenderer interface (same reasoning as EffectRenderer::GetProgramHandle()'s own
            // doc comment). A non-zero baseVertex (indexed draws only) is honored by re-basing the
            // attribute pointers -- GL 2.1 has no glDrawElementsBaseVertex.
            const std::size_t customBaseByte = (ib && params->baseVertex > 0)
                ? static_cast<std::size_t>(params->baseVertex) * vb->stride : 0;
            if (!vb->declaration.empty()
                && !DeclarationMatchesInferredStrideLayout(vb->declaration, vb->stride))
            {
                if (auto* custom = dynamic_cast<EffectRenderer*>(params->customEffectRenderer))
                    BindVertexAttributesForDeclaration(custom->GetProgramHandle(), vb->id, vb->stride,
                                                       vb->declaration, customBaseByte);
                else
                    BindVertexAttributesForStride(vb->id, vb->stride, customBaseByte);
            }
            else
            {
                BindVertexAttributesForStride(vb->id, vb->stride, customBaseByte);
            }

            const int customVertexCount = VertexCountForPrimitives(primitive, primitiveCount);
            if (ib)
            {
                const std::size_t indexElemSize = ib->thirtyTwoBit ? 4 : 2;
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
                glDrawElements(ToGLPrimitiveMode(primitive), customVertexCount,
                               ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                               reinterpret_cast<void*>(static_cast<std::uintptr_t>(params->startIndex) * indexElemSize));
            }
            else
            {
                glDrawArrays(ToGLPrimitiveMode(primitive), params->vertexStart, customVertexCount);
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
        // Render-time Y-flip for 2D render targets -- see RtFlipActive()'s own doc comment.
        if (RtFlipActive())
            FlipClipYColumnMajor(wvp);
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
            // REMED-GFX-010: FNA's fog vector, consumed directly by every vertex shader's
            // dot(position, uFogVector) term (post-skin position in the two skinned programs).
            glUniform4fv(glGetUniformLocation(program, "uFogVector"), 1, params->fogVector);
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

            // litProgram_/skinnedProgram_ both declare uVertexColorEnabled (see litFragmentSrc's
            // own doc comment above for why BasicEffect needs this too, not just SkinnedEffect);
            // ignored (-1 location, silent no-op) by every other program in this `if` block.
            if (lit || skinned)
                glUniform1f(glGetUniformLocation(program, "uVertexColorEnabled"), params->vertexColorEnabled ? 1.0f : 0.0f);

            // SkinnedEffect-only uniforms (silently ignored by litProgram_/envMapProgram_).
            if (skinned)
            {
                glUniformMatrix4fv(glGetUniformLocation(program, "uBones[0]"), params->boneCount,
                                   GL_FALSE, params->boneTransforms);
                glUniform1i(glGetUniformLocation(program, "uWeightsPerVertex"), params->weightsPerVertex);
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

        // Task 1080: a VertexBuffer whose propagated declaration is genuinely custom (not the
        // built-in layout its stride already implies) is bound by attribute NAME against
        // whichever built-in program was just selected above, instead of this file's own fixed
        // byte-stride dispatch -- see BindVertexAttributesForDeclaration's own doc comment. A
        // non-zero baseVertex (indexed draws only; the ordinary routes fold their per-vertex
        // binding offset into it) is honored by re-basing the attribute pointers, since GL 2.1
        // has no glDrawElementsBaseVertex.
        const std::size_t stockBaseByte = (ib && params && params->baseVertex > 0)
            ? static_cast<std::size_t>(params->baseVertex) * vb->stride : 0;
        if (!vb->declaration.empty()
            && !DeclarationMatchesInferredStrideLayout(vb->declaration, vb->stride))
            BindVertexAttributesForDeclaration(program, vb->id, vb->stride, vb->declaration,
                                               stockBaseByte);
        else
            BindVertexAttributesForStride(vb->id, vb->stride, stockBaseByte);

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

            // TextureFilter::Anisotropic = 2 (mirrors EasyGLRenderer::ApplySamplerState's
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
        // matching EasyGLRenderer::EnsureSkinnedProgram's identical fragment shader) --
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
            applySampler(1);
            glUniform1i(glGetUniformLocation(program, "uNormalMap"), 1);

            glActiveTexture(GL_TEXTURE2);
            if (params->pbrMetallicRoughnessMap) params->pbrMetallicRoughnessMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(2);
            glUniform1i(glGetUniformLocation(program, "uMetallicRoughnessMap"), 2);

            glActiveTexture(GL_TEXTURE3);
            if (params->pbrEmissiveMap) params->pbrEmissiveMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(3);
            glUniform1i(glGetUniformLocation(program, "uEmissiveMap"), 3);

            glActiveTexture(GL_TEXTURE4);
            if (params->pbrOcclusionMap) params->pbrOcclusionMap->BindGL();
            else glBindTexture(GL_TEXTURE_2D, defaultWhiteTexture2D_);
            applySampler(4);
            glUniform1i(glGetUniformLocation(program, "uOcclusionMap"), 4);

            glActiveTexture(GL_TEXTURE0);
        }

        const int vertexCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (ib)
        {
            // Task (plan_opengl2.md follow-up): params->startIndex/baseVertex were previously
            // ignored entirely (always drew from index 0, no base-vertex offset) -- a real gap
            // EasyGLRenderer does not have (see its own params.startIndex/params.baseVertex
            // handling). glDrawElementsBaseVertex is GL 3.2+/ARB_draw_elements_base_vertex, not
            // guaranteed on GL 2.1, so a non-zero baseVertex is honored by the software
            // base-vertex pointer re-base above (stockBaseByte) instead of a native draw-call
            // parameter (startIndex alone -- the byte offset into the index buffer -- is core
            // GL 1.5 and honored directly).
            const std::size_t indexElemSize = ib->thirtyTwoBit ? 4 : 2;
            const int startIndex = params ? params->startIndex : 0;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
            glDrawElements(ToGLPrimitiveMode(primitive), vertexCount,
                           ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                           reinterpret_cast<void*>(static_cast<std::uintptr_t>(startIndex) * indexElemSize));
        }
        else
        {
            // Task (plan_opengl2.md follow-up): params->vertexStart was previously ignored
            // entirely (always drew from vertex 0) -- a real gap EasyGLRenderer does not
            // have (see its own params.vertexStart-driven draw_arrays call). This broke any
            // DrawPrimitives() sequence that draws multiple sub-ranges of ONE VertexBuffer at
            // different vertexStart offsets (e.g. several quads packed into a single buffer) --
            // every draw after the first silently rendered the FIRST range's geometry again
            // instead of its own.
            const int vertexStart = params ? params->vertexStart : 0;
            glDrawArrays(ToGLPrimitiveMode(primitive), vertexStart, vertexCount);
        }
    }

    void OpenGL2Renderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                                        const Matrix& projection, PrimitiveType primitive, int primitiveCount)
    {
        drawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void OpenGL2Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        drawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void OpenGL2Renderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                                                   const GpuDrawParams& params)
    {
        // REMED-GFX-201: one per-vertex stream only on this renderer; refuse a wider binding set
        // before any program or pointer state is touched, never render from a stream subset
        // (GraphicsCapability::MultiStreamVertexInput is false, and GraphicsDevice already
        // rejects these -- this is the renderer-level backstop for a direct call).
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OPENGL2: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");
        drawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
    }

    void OpenGL2Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        // REMED-GFX-201: one per-vertex stream only on this renderer; refuse a wider binding set
        // before any program or pointer state is touched, never render from a stream subset
        // (GraphicsCapability::MultiStreamVertexInput is false, and GraphicsDevice already
        // rejects these -- this is the renderer-level backstop for a direct call).
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OPENGL2: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");
        drawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
    }

    void OpenGL2Renderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vbi, const IIndexBufferRenderer& ibi,
                                                            const Matrix& world, const Matrix& view, const Matrix& projection,
                                                            PrimitiveType primitive, int primitiveCount, int instanceCount,
                                                            const GpuDrawParams& params)
    {
        // plan_opengl2.md follow-up: hardware instancing is only supported through the custom-
        // ShaderEffect path (mirrors EasyGLRenderer::DrawInstancedPrimitivesEx's own
        // identical, documented scope reduction -- a non-1 VertexBufferBinding.InstanceFrequency
        // is not threaded through here either) -- the shared base-class default's
        // std::runtime_error is the correct, honest behavior for anything outside that scope
        // (no custom effect bound, or the driver genuinely lacks GL_ARB_draw_instanced/
        // GL_ARB_instanced_arrays), so this override only handles the cases it can do for real.
        // REMED-GFX-201: one per-vertex stream only on this renderer; refuse a wider binding set
        // before any program or pointer state is touched, never render from a stream subset
        // (GraphicsCapability::MultiStreamVertexInput is false, and GraphicsDevice already
        // rejects these -- this is the renderer-level backstop for a direct call).
        if (HasMultipleVertexStreams(params) || HasMultipleInstanceStreams(params))
            throw System::NotSupportedException(
                "OPENGL2: multiple per-vertex or per-instance vertex streams are not supported "
                "by this renderer (GraphicsCapability::MultiStreamVertexInput is false).");

        if (!params.customEffectRenderer || !glDrawElementsInstancedARB || !glVertexAttribDivisorARB)
        {
            IGraphicsRenderer::DrawInstancedPrimitivesEx(vbi, ibi, world, view, projection, primitive,
                                                        primitiveCount, instanceCount, params);
            return;
        }

        const auto* vb = dynamic_cast<const VB*>(&vbi);
        const auto* ib = dynamic_cast<const IB*>(&ibi);
        if (!vb || !ib)
            throw std::runtime_error("OPENGL2: incompatible vertex/index buffer");

        auto* custom = dynamic_cast<EffectRenderer*>(params.customEffectRenderer);
        if (!custom)
            throw std::runtime_error("OPENGL2: DrawInstancedPrimitivesEx requires an OpenGL2 ShaderEffect");

        params.customEffectRenderer->Bind();
        float worldCM[16], viewCM[16], projCM[16];
        world.ToColumnMajor(worldCM);
        view.ToColumnMajor(viewCM);
        projection.ToColumnMajor(projCM);
        params.customEffectRenderer->SetUniformMat4("World", worldCM);
        params.customEffectRenderer->SetUniformMat4("View", viewCM);
        // Render-time Y-flip for 2D render targets, applied through the Projection uniform a
        // custom shader multiplies by -- see RtFlipActive()'s own doc comment.
        if (RtFlipActive())
            FlipClipYColumnMajor(projCM);
        params.customEffectRenderer->SetUniformMat4("Projection", projCM);

        const GLuint program = custom->GetProgramHandle();

        // Mesh (per-vertex) attributes -- same declaration-driven or fixed-stride dispatch every
        // other custom-effect draw uses. REMED-GFX-202: the instanced route folds nothing, so the
        // mesh stream's own public VertexOffset arrives in vertexStreams[0].vertexOffset and the
        // draw's baseVertex arrives separately -- both are honored here by the same software
        // pointer re-base the ordinary indexed route uses (GL 2.1 has no glDrawElementsBaseVertex),
        // advancing every per-vertex attribute by that many of this stream's own records.
        const int meshVertexOffset =
            params.vertexStreamCount > 0 ? params.vertexStreams[0].vertexOffset : 0;
        const long long meshBaseRecords =
            static_cast<long long>(meshVertexOffset) + static_cast<long long>(params.baseVertex);
        const std::size_t meshBaseByte =
            meshBaseRecords > 0 ? static_cast<std::size_t>(meshBaseRecords) * vb->stride : 0;
        if (!vb->declaration.empty()
            && !DeclarationMatchesInferredStrideLayout(vb->declaration, vb->stride))
            BindVertexAttributesForDeclaration(program, vb->id, vb->stride, vb->declaration,
                                               meshBaseByte);
        else
            BindVertexAttributesForStride(vb->id, vb->stride, meshBaseByte);

        // Per-instance attributes: bound by NAME (glGetAttribLocation), exactly like the mesh
        // buffer above -- unlike EasyGLRenderer's own fixed-layout-location approach, name-
        // based binding needs no "continue right after the mesh buffer's own locations" offset
        // arithmetic, since each attribute's location comes straight from the linker regardless of
        // how many mesh attributes exist. glVertexAttribDivisorARB is what makes each of these
        // advance once per instanceFrequency INSTANCES instead of once per vertex -- the whole
        // mechanism this task exists to add. REMED-GFX-202: the one per-instance stream arrives as
        // the vertexStreams entry whose instanceFrequency is greater than zero (there is no second
        // representation of "the instance buffer"); its own public VertexOffset offsets every
        // attribute pointer by that many of its own records (GFX-211), and its own
        // InstanceFrequency is the divisor (GFX-213).
        const GpuVertexStreamBinding* instanceStream = FirstInstanceStream(params);
        const auto* instVb = instanceStream
            ? dynamic_cast<const VB*>(instanceStream->buffer) : nullptr;
        std::vector<GLuint> instanceLocations;
        if (instVb && !instVb->declaration.empty())
        {
            const std::size_t instBaseByte = instanceStream->vertexOffset > 0
                ? static_cast<std::size_t>(instanceStream->vertexOffset) * instVb->stride : 0;
            const GLuint divisor = static_cast<GLuint>(
                instanceStream->instanceFrequency > 0 ? instanceStream->instanceFrequency : 1);
            glBindBuffer(GL_ARRAY_BUFFER, instVb->id);
            for (const auto& element : instVb->declaration)
            {
                const std::string name = VertexElementAttribName(
                    element.getVertexElementUsageProperty(), element.getUsageIndexProperty());
                const GLint location = glGetAttribLocation(program, name.c_str());
                if (location < 0) continue;

                const VertexAttribFormat desc = DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const auto offset = reinterpret_cast<const void*>(
                    instBaseByte + static_cast<std::uintptr_t>(element.getOffsetProperty()));
                glEnableVertexAttribArray(static_cast<GLuint>(location));
                glVertexAttribPointer(static_cast<GLuint>(location), desc.componentCount, desc.type,
                                      desc.normalized, static_cast<GLsizei>(instVb->stride), offset);
                glVertexAttribDivisorARB(static_cast<GLuint>(location), divisor);
                instanceLocations.push_back(static_cast<GLuint>(location));
            }
        }

        const int indexCount = VertexCountForPrimitives(primitive, primitiveCount);
        const std::size_t instIndexElemSize = ib->thirtyTwoBit ? 4 : 2;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        glDrawElementsInstancedARB(ToGLPrimitiveMode(primitive), indexCount,
                                   ib->thirtyTwoBit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                                   reinterpret_cast<void*>(
                                       static_cast<std::uintptr_t>(params.startIndex) * instIndexElemSize),
                                   instanceCount);

        // Reset divisor back to 0 (not just disable the attribute) -- otherwise a later,
        // unrelated draw that reuses the same attribute location number would silently inherit
        // per-instance advancement instead of the normal per-vertex behavior.
        for (GLuint location : instanceLocations)
        {
            glVertexAttribDivisorARB(location, 0);
            glDisableVertexAttribArray(location);
        }
    }

    void OpenGL2Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                                                  int colorBlendFunc, int alphaBlendFunc,
                                                  const BlendWriteState& writeState)
    {
        // REMED-GFX-077: glColorMaski is GL 3.0+, so this profile has ONE color mask for every
        // draw buffer. With an MRT set active, distinct per-slot ColorWriteChannels values cannot
        // be expressed -- refuse rather than silently applying slot 0's mask to every attachment
        // (EasyGLRenderer::ApplyBlendState's identical GLES3 refusal).
        if (!mrtTargets_.empty())
        {
            const int activeSlots = static_cast<int>(mrtTargets_.size()) < 4
                ? static_cast<int>(mrtTargets_.size()) : 4;
            for (int i = 1; i < activeSlots; ++i)
                if (writeState.colorWriteChannels[i] != writeState.colorWriteChannels[0])
                    throw std::runtime_error(
                        "OPENGL2 ApplyBlendState: this GL profile cannot express distinct "
                        "ColorWriteChannels values for active MRT slots.");
        }
        const int cwc = writeState.colorWriteChannels[0];
        glColorMask(ColorWriteHasRed(cwc)   ? GL_TRUE : GL_FALSE,
                    ColorWriteHasGreen(cwc) ? GL_TRUE : GL_FALSE,
                    ColorWriteHasBlue(cwc)  ? GL_TRUE : GL_FALSE,
                    ColorWriteHasAlpha(cwc) ? GL_TRUE : GL_FALSE);
        // BlendState.MultiSampleMask: GL_SAMPLE_MASK is GL 3.2+, so a non-default coverage mask
        // is a documented capability gap on this profile (EasyGL's and OPENGL1's same gap) -- the
        // value reaches the renderer and only the rare non-default path is unimplemented.

        // Blend::One=0, Blend::Zero=1 -> Opaque preset: src=One, dst=Zero => effectively no
        // blending (matches EasyGLRenderer::ApplyBlendState's identical detection).
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 && alphaSrcBlend == 0 && alphaDstBlend == 1);
        blendEnabled ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        if (!blendEnabled) return;

        glBlendFuncSeparate(ToGLBlendFactor(colorSrcBlend), ToGLBlendFactor(colorDstBlend),
                            ToGLBlendFactor(alphaSrcBlend), ToGLBlendFactor(alphaDstBlend));
        glBlendEquationSeparate(ToGLBlendEquation(colorBlendFunc), ToGLBlendEquation(alphaBlendFunc));
    }

    void OpenGL2Renderer::SetBlendFactor(float r, float g, float b, float a)
    {
        // Backs BlendState.BlendFactor for the Blend::BlendFactor/InverseBlendFactor blend
        // factors (ToGLBlendFactor maps those to GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR
        // above) -- glBlendColor is the GL-side constant-color register those factors read.
        glBlendColor(r, g, b, a);
    }

    void OpenGL2Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
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
        cachedStencilEnabled_ = stencilEnable;
        cachedTwoSidedStencil_ = twoSidedStencilMode;
        cachedStencilFunc_ = stencilFunc;
        cachedCcwStencilFunc_ = ccwStencilFunc;
        cachedStencilMask_ = static_cast<unsigned>(stencilMask);
        if (!stencilEnable) return;

        const GLenum glSFail = ToGLStencilOp(stencilFail);
        const GLenum glDFail = ToGLStencilOp(stencilDepthFail);
        const GLenum glPass  = ToGLStencilOp(stencilPass);

        // Mirrors EasyGLRenderer::ApplyDepthStencilState's own GL_FRONT/GL_BACK
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

    void OpenGL2Renderer::SetReferenceStencil(int value)
    {
        // GraphicsDevice.ReferenceStencil (Task 870/319, see IGraphicsRenderer.hpp's own doc
        // comment) must take effect standalone, without a full DepthStencilState
        // re-application -- glStencilFunc(Separate) requires func+ref+mask together, so the
        // func/mask most recently applied by ApplyDepthStencilState() above are cached there and
        // reused here unchanged, alongside the new reference value. No-op if stencil testing
        // isn't currently enabled (matches ApplyDepthStencilState's own early-return for that
        // case -- nothing to re-apply).
        if (!cachedStencilEnabled_) return;
        if (cachedTwoSidedStencil_)
        {
            glStencilFuncSeparate(GL_FRONT, ToGLCompareFunc(cachedStencilFunc_), value, cachedStencilMask_);
            glStencilFuncSeparate(GL_BACK, ToGLCompareFunc(cachedCcwStencilFunc_), value, cachedStencilMask_);
        }
        else
        {
            glStencilFunc(ToGLCompareFunc(cachedStencilFunc_), value, cachedStencilMask_);
        }
    }

    void OpenGL2Renderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
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

    void OpenGL2Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
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

    void OpenGL2Renderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        // A flipped 2D render target stores rows top-down (see RtFlipActive()), so the caller's
        // top-left rectangle maps to GL window coordinates directly; the window and cube-face
        // paths keep the bottom-left-origin Y-flip (using the bound target's own height,
        // mirroring ReadBackbuffer's identical fbH pattern).
        if (RtFlipActive())
        {
            glScissor(x, y, w, h);
            return;
        }
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            surface_.GetDrawableSize(windowWidth, fbH);
        }
        glScissor(x, fbH - y - h, w, h);
    }

    void OpenGL2Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (w <= 0 || h <= 0) return;
        // Same top-down mapping as SetScissorRect while a flipped 2D render target is bound.
        if (RtFlipActive())
        {
            glViewport(x, y, w, h);
            glDepthRange(minDepth, maxDepth);
            return;
        }
        int fbH = currentRtHeight_;
        if (fbH == 0)
        {
            int windowWidth = 0;
            surface_.GetDrawableSize(windowWidth, fbH);
        }
        glViewport(x, fbH - y - h, w, h);
        glDepthRange(minDepth, maxDepth);
    }

    bool OpenGL2Renderer::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        // Mirrors SdlGpuRenderer::TransformWindowToLogical exactly: for
        // FixedHeightDynamicWidth/NativeBackBuffer/Stretch (viewport.x=viewport.y=0, width/height
        // equal the physical window) this degenerates to the same pure height-derived uniform
        // scale as before; Letterbox/Overscan additionally subtract the centred sub-rectangle's
        // offset and return false outside its bounds (a window click on a letterbox bar has no
        // corresponding logical position).
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f) return false;
        const float drawableX = surface_.WindowToDrawable(windowX);
        const float drawableY = surface_.WindowToDrawable(windowY);
        logX = (drawableX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (drawableY - viewport.y) * viewport.logicalHeight / viewport.height;
        return drawableX >= viewport.x && drawableX < viewport.x + viewport.width &&
               drawableY >= viewport.y && drawableY < viewport.y + viewport.height;
    }

    bool OpenGL2Renderer::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f) return false;
        windowX = surface_.DrawableToWindow(
            viewport.x + logX * viewport.width / viewport.logicalWidth);
        windowY = surface_.DrawableToWindow(
            viewport.y + logY * viewport.height / viewport.logicalHeight);
        return true;
    }

    bool OpenGL2Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // Every current member answered explicitly, with no default case: a future
        // GraphicsCapability member surfaces as a -Wswitch warning here (and lands on the
        // trailing return false) instead of inheriting a confident wrong answer.
        switch (capability)
        {
            // Real vertex/index buffers, 3D draw routes, depth/stencil clears and state.
            case CNA::GraphicsCapability::ThreeD:
                return true;
            // A real 24-bit depth / 8-bit stencil buffer requested for the window's context
            // (24 depth bits / 8 stencil bits) and created for every FBO render target.
            case CNA::GraphicsCapability::DepthStencilBuffer:
                return true;
            // Real MRT: up to 8 colour attachments on one shared FBO with a real glDrawBuffers
            // call (core GL 2.0; the entry point still resolves at runtime on Windows).
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return glDrawBuffers != nullptr;
            // Real GL_SAMPLES_PASSED query objects with exact passed-sample counts (core GL 1.5;
            // entry points resolved at runtime).
            case CNA::GraphicsCapability::OcclusionQuery:
                return glGenQueries != nullptr;
            // Real caller-supplied GLSL 1.10 compilation via CreateEffectRenderer().
            case CNA::GraphicsCapability::CustomEffects:
                return true;
            // Real GL_TEXTURE_3D storage with mip-level upload (core since GL 1.2; entry points
            // resolved at runtime).
            case CNA::GraphicsCapability::Texture3D:
                return glTexImage3D != nullptr;
            // REMED-GFX-201: not implemented -- the pointer setup binds ONE GL_ARRAY_BUFFER and
            // reads every attribute out of it at stride (or declared) offsets; there is no second
            // per-vertex stream to bind, and every Draw*Ex route refuses a wider binding set up
            // front.
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                return false;
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
            case CNA::GraphicsCapability::StencilBuffer:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
        }
        // Unreachable for current members; a future member lands here (after the -Wswitch
        // warning above) and is reported unsupported until this renderer explicitly claims it.
        return false;
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenGL2::OpenGL2Renderer>(args);
    }
}
