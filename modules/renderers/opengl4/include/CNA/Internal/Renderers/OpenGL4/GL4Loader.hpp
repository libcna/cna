// SPDX-License-Identifier: MS-PL
#pragma once

// plan_opengl4.md GL4-2: a small, hand-rolled loader for the subset of desktop OpenGL 4.x core
// profile functions this renderer actually calls. Deliberately NOT a vendored third-party loader
// (no glad/GLEW dependency added to the tree) -- mirrors this project's existing "zero new
// third-party dependency" preference for a from-scratch native renderer (see plan_sdlgpu.md's own
// "Why a GPU renderer" rationale). The handful of pre-1.2 entry points (glClear, glViewport,
// glGenTextures, glTexImage2D, glReadPixels, ...) are declared by the platform's own <GL/gl.h> (or
// macOS/<OpenGL/gl.h>) and are linked directly against libGL/OpenGL.framework -- only functions
// introduced by GL 1.2+ (buffers, VAOs, shaders/programs, GL_TEXTURE0+, separate blend
// funcs/equations, mipmap generation) need a runtime platform GL lookup, done once by
// LoadGL4Functions() right after the GL context is made current.
//
// Every loaded entry point is named gl4_<realName> (e.g. gl4_glCreateShader) rather than shadowing
// the real GL name -- this avoids any ambiguity with the pre-1.2 functions declared by the system
// GL header and linked normally, and matches how a caller reads: "this one came from the loader".

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace CNA::Internal::Renderers::OpenGL4::GL4
{
    // Local aliases for the handful of GL 1.5+ typedefs not guaranteed to be declared by the
    // platform's own (GL 1.1-vintage) <GL/gl.h>. Deliberately distinct names from the real
    // GLchar/GLsizeiptr/GLintptr Khronos tokens to avoid any redefinition risk if a platform
    // header (or something else this renderer later includes) does declare them.
    using GLchar4    = char;
    using GLsizeiptr4 = std::ptrdiff_t;
    using GLintptr4   = std::ptrdiff_t;

    // ---- Tokens not guaranteed to be defined by a GL-1.1-vintage <GL/gl.h> ----
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
// plan_opengl4.md GL4-33: needed by the generic VertexElement-to-GL-attribute mapper's
// HalfVector2/HalfVector4 case (GL 3.0 core / ARB_half_float_vertex).
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_FUNC_SUBTRACT
#define GL_FUNC_SUBTRACT 0x800A
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP 0x8507
#endif
#ifndef GL_DECR_WRAP
#define GL_DECR_WRAP 0x8508
#endif
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

    // ---- Function pointer types for the loaded subset (Khronos-standard PFN names) ----
    using PFNGL4GENBUFFERSPROC              = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDBUFFERPROC              = void (*)(GLenum, GLuint);
    using PFNGL4BUFFERDATAPROC              = void (*)(GLenum, GLsizeiptr4, const void*, GLenum);
    using PFNGL4BUFFERSUBDATAPROC           = void (*)(GLenum, GLintptr4, GLsizeiptr4, const void*);
    using PFNGL4DELETEBUFFERSPROC           = void (*)(GLsizei, const GLuint*);

    using PFNGL4GENVERTEXARRAYSPROC         = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDVERTEXARRAYPROC         = void (*)(GLuint);
    using PFNGL4DELETEVERTEXARRAYSPROC      = void (*)(GLsizei, const GLuint*);
    using PFNGL4VERTEXATTRIBPOINTERPROC     = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    using PFNGL4ENABLEVERTEXATTRIBARRAYPROC = void (*)(GLuint);
    using PFNGL4DISABLEVERTEXATTRIBARRAYPROC= void (*)(GLuint);
    // plan_opengl4.md GL4-22: SkinnedEffect's BlendIndices attribute is a real integer vertex
    // attribute (bone indices, read as uvec4/ivec4 in GLSL) -- glVertexAttribPointer's implicit
    // int-to-float conversion is wrong for this case, so the true GL 3.0 core
    // glVertexAttribIPointer entry point is needed instead.
    using PFNGL4VERTEXATTRIBIPOINTERPROC    = void (*)(GLuint, GLint, GLenum, GLsizei, const void*);

    using PFNGL4CREATESHADERPROC            = GLuint (*)(GLenum);
    using PFNGL4SHADERSOURCEPROC            = void (*)(GLuint, GLsizei, const GLchar4* const*, const GLint*);
    using PFNGL4COMPILESHADERPROC           = void (*)(GLuint);
    using PFNGL4GETSHADERIVPROC             = void (*)(GLuint, GLenum, GLint*);
    using PFNGL4GETSHADERINFOLOGPROC        = void (*)(GLuint, GLsizei, GLsizei*, GLchar4*);
    using PFNGL4DELETESHADERPROC            = void (*)(GLuint);
    using PFNGL4CREATEPROGRAMPROC           = GLuint (*)();
    using PFNGL4ATTACHSHADERPROC            = void (*)(GLuint, GLuint);
    using PFNGL4LINKPROGRAMPROC             = void (*)(GLuint);
    using PFNGL4GETPROGRAMIVPROC            = void (*)(GLuint, GLenum, GLint*);
    using PFNGL4GETPROGRAMINFOLOGPROC       = void (*)(GLuint, GLsizei, GLsizei*, GLchar4*);
    using PFNGL4DELETEPROGRAMPROC           = void (*)(GLuint);
    using PFNGL4USEPROGRAMPROC              = void (*)(GLuint);
    using PFNGL4BINDATTRIBLOCATIONPROC      = void (*)(GLuint, GLuint, const GLchar4*);

    using PFNGL4GETUNIFORMLOCATIONPROC      = GLint (*)(GLuint, const GLchar4*);
    using PFNGL4UNIFORM1IPROC               = void (*)(GLint, GLint);
    using PFNGL4UNIFORM1FPROC               = void (*)(GLint, GLfloat);
    using PFNGL4UNIFORM2FPROC               = void (*)(GLint, GLfloat, GLfloat);
    using PFNGL4UNIFORM3FPROC               = void (*)(GLint, GLfloat, GLfloat, GLfloat);
    using PFNGL4UNIFORM4FPROC               = void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    using PFNGL4UNIFORM1FVPROC              = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORM2FVPROC              = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORMMATRIX4FVPROC        = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);

    using PFNGL4ACTIVETEXTUREPROC           = void (*)(GLenum);
    using PFNGL4GENERATEMIPMAPPROC          = void (*)(GLenum);

    using PFNGL4BLENDFUNCSEPARATEPROC       = void (*)(GLenum, GLenum, GLenum, GLenum);
    using PFNGL4BLENDEQUATIONSEPARATEPROC   = void (*)(GLenum, GLenum);
    using PFNGL4BLENDCOLORPROC              = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);

    using PFNGL4GENSAMPLERSPROC             = void (*)(GLsizei, GLuint*);
    using PFNGL4DELETESAMPLERSPROC          = void (*)(GLsizei, const GLuint*);
    using PFNGL4BINDSAMPLERPROC             = void (*)(GLuint, GLuint);
    using PFNGL4SAMPLERPARAMETERIPROC       = void (*)(GLuint, GLenum, GLint);
    using PFNGL4SAMPLERPARAMETERFPROC       = void (*)(GLuint, GLenum, GLfloat);

    // plan_opengl4.md GL4-14: RenderTarget2D FBO support.
    using PFNGL4GENFRAMEBUFFERSPROC             = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDFRAMEBUFFERPROC             = void (*)(GLenum, GLuint);
    using PFNGL4DELETEFRAMEBUFFERSPROC          = void (*)(GLsizei, const GLuint*);
    using PFNGL4FRAMEBUFFERTEXTURE2DPROC        = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using PFNGL4CHECKFRAMEBUFFERSTATUSPROC      = GLenum (*)(GLenum);
    using PFNGL4GENRENDERBUFFERSPROC            = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDRENDERBUFFERPROC            = void (*)(GLenum, GLuint);
    using PFNGL4DELETERENDERBUFFERSPROC         = void (*)(GLsizei, const GLuint*);
    using PFNGL4RENDERBUFFERSTORAGEPROC         = void (*)(GLenum, GLenum, GLsizei, GLsizei);
    using PFNGL4RENDERBUFFERSTORAGEMULTISAMPLEPROC = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    using PFNGL4FRAMEBUFFERRENDERBUFFERPROC     = void (*)(GLenum, GLenum, GLenum, GLuint);
    using PFNGL4BLITFRAMEBUFFERPROC             = void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    using PFNGL4DRAWBUFFERSPROC                 = void (*)(GLsizei, const GLenum*);

    // plan_opengl4.md GL4-16: two-sided (front/back) stencil state -- GL 2.0 core, not
    // guaranteed to be declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4STENCILFUNCSEPARATEPROC         = void (*)(GLenum, GLenum, GLint, GLuint);
    using PFNGL4STENCILOPSEPARATEPROC           = void (*)(GLenum, GLenum, GLenum, GLenum);
    using PFNGL4STENCILMASKSEPARATEPROC         = void (*)(GLenum, GLuint);
    using PFNGL4COLORMASKIPROC                  = void (*)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);

    // plan_opengl4.md GL4-20: plain Texture3D -- GL 1.2 core, not guaranteed to be declared by a
    // GL-1.1-vintage <GL/gl.h> (same rationale as the other GL4-prefixed entries above).
    using PFNGL4TEXIMAGE3DPROC                  = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    using PFNGL4TEXSUBIMAGE3DPROC               = void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
    // GL 3.0 core -- attaches one Z-layer of a 3D texture to an FBO, used for Texture3D::GetData's
    // per-slice glReadPixels loop (desktop GL has no per-sub-rectangle 3D-texture readback other
    // than this FBO-per-slice approach).
    using PFNGL4FRAMEBUFFERTEXTURELAYERPROC     = void (*)(GLenum, GLenum, GLuint, GLint, GLint);

    // plan_opengl4.md GL4-24: real occlusion queries -- GL 1.5 core, not guaranteed to be
    // declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4GENQUERIESPROC                  = void (*)(GLsizei, GLuint*);
    using PFNGL4DELETEQUERIESPROC               = void (*)(GLsizei, const GLuint*);
    using PFNGL4BEGINQUERYPROC                  = void (*)(GLenum, GLuint);
    using PFNGL4ENDQUERYPROC                    = void (*)(GLenum);
    using PFNGL4GETQUERYOBJECTUIVPROC           = void (*)(GLuint, GLenum, GLuint*);

    // plan_opengl4.md GL4-27: real GpuDrawParams::baseVertex support -- GL 3.2 core
    // (ARB_draw_elements_base_vertex, core since GL 3.2), not guaranteed to be declared by a
    // GL-1.1-vintage <GL/gl.h>.
    using PFNGL4DRAWELEMENTSBASEVERTEXPROC      = void (*)(GLenum, GLsizei, GLenum, const void*, GLint);

    // plan_opengl4.md GL4-33: real GpuDrawParams-driven hardware instancing -- GL 3.1 core
    // (glDrawElementsInstanced) and GL 3.3 core / ARB_instanced_arrays (glVertexAttribDivisor),
    // neither guaranteed to be declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4DRAWELEMENTSINSTANCEDPROC       = void (*)(GLenum, GLsizei, GLenum, const void*, GLsizei);
    using PFNGL4VERTEXATTRIBDIVISORPROC         = void (*)(GLuint, GLuint);

    // ---- Loaded function pointers ----
    extern PFNGL4GENBUFFERSPROC               gl4_glGenBuffers;
    extern PFNGL4BINDBUFFERPROC               gl4_glBindBuffer;
    extern PFNGL4BUFFERDATAPROC               gl4_glBufferData;
    extern PFNGL4BUFFERSUBDATAPROC            gl4_glBufferSubData;
    extern PFNGL4DELETEBUFFERSPROC            gl4_glDeleteBuffers;

    extern PFNGL4GENVERTEXARRAYSPROC          gl4_glGenVertexArrays;
    extern PFNGL4BINDVERTEXARRAYPROC          gl4_glBindVertexArray;
    extern PFNGL4DELETEVERTEXARRAYSPROC       gl4_glDeleteVertexArrays;
    extern PFNGL4VERTEXATTRIBPOINTERPROC      gl4_glVertexAttribPointer;
    extern PFNGL4ENABLEVERTEXATTRIBARRAYPROC  gl4_glEnableVertexAttribArray;
    extern PFNGL4DISABLEVERTEXATTRIBARRAYPROC gl4_glDisableVertexAttribArray;
    extern PFNGL4VERTEXATTRIBIPOINTERPROC     gl4_glVertexAttribIPointer;

    extern PFNGL4CREATESHADERPROC             gl4_glCreateShader;
    extern PFNGL4SHADERSOURCEPROC             gl4_glShaderSource;
    extern PFNGL4COMPILESHADERPROC            gl4_glCompileShader;
    extern PFNGL4GETSHADERIVPROC              gl4_glGetShaderiv;
    extern PFNGL4GETSHADERINFOLOGPROC         gl4_glGetShaderInfoLog;
    extern PFNGL4DELETESHADERPROC             gl4_glDeleteShader;
    extern PFNGL4CREATEPROGRAMPROC            gl4_glCreateProgram;
    extern PFNGL4ATTACHSHADERPROC             gl4_glAttachShader;
    extern PFNGL4LINKPROGRAMPROC              gl4_glLinkProgram;
    extern PFNGL4GETPROGRAMIVPROC             gl4_glGetProgramiv;
    extern PFNGL4GETPROGRAMINFOLOGPROC        gl4_glGetProgramInfoLog;
    extern PFNGL4DELETEPROGRAMPROC            gl4_glDeleteProgram;
    extern PFNGL4USEPROGRAMPROC               gl4_glUseProgram;
    extern PFNGL4BINDATTRIBLOCATIONPROC       gl4_glBindAttribLocation;

    extern PFNGL4GETUNIFORMLOCATIONPROC       gl4_glGetUniformLocation;
    extern PFNGL4UNIFORM1IPROC                gl4_glUniform1i;
    extern PFNGL4UNIFORM1FPROC                gl4_glUniform1f;
    extern PFNGL4UNIFORM2FPROC                gl4_glUniform2f;
    extern PFNGL4UNIFORM3FPROC                gl4_glUniform3f;
    extern PFNGL4UNIFORM4FPROC                gl4_glUniform4f;
    extern PFNGL4UNIFORM1FVPROC               gl4_glUniform1fv;
    extern PFNGL4UNIFORM2FVPROC               gl4_glUniform2fv;
    extern PFNGL4UNIFORMMATRIX4FVPROC         gl4_glUniformMatrix4fv;

    extern PFNGL4ACTIVETEXTUREPROC            gl4_glActiveTexture;
    extern PFNGL4GENERATEMIPMAPPROC           gl4_glGenerateMipmap;

    extern PFNGL4BLENDFUNCSEPARATEPROC        gl4_glBlendFuncSeparate;
    extern PFNGL4BLENDEQUATIONSEPARATEPROC    gl4_glBlendEquationSeparate;
    extern PFNGL4BLENDCOLORPROC               gl4_glBlendColor;

    extern PFNGL4GENSAMPLERSPROC              gl4_glGenSamplers;
    extern PFNGL4DELETESAMPLERSPROC           gl4_glDeleteSamplers;
    extern PFNGL4BINDSAMPLERPROC              gl4_glBindSampler;
    extern PFNGL4SAMPLERPARAMETERIPROC        gl4_glSamplerParameteri;
    extern PFNGL4SAMPLERPARAMETERFPROC        gl4_glSamplerParameterf;

    extern PFNGL4GENFRAMEBUFFERSPROC             gl4_glGenFramebuffers;
    extern PFNGL4BINDFRAMEBUFFERPROC             gl4_glBindFramebuffer;
    extern PFNGL4DELETEFRAMEBUFFERSPROC          gl4_glDeleteFramebuffers;
    extern PFNGL4FRAMEBUFFERTEXTURE2DPROC        gl4_glFramebufferTexture2D;
    extern PFNGL4CHECKFRAMEBUFFERSTATUSPROC      gl4_glCheckFramebufferStatus;
    extern PFNGL4GENRENDERBUFFERSPROC            gl4_glGenRenderbuffers;
    extern PFNGL4BINDRENDERBUFFERPROC            gl4_glBindRenderbuffer;
    extern PFNGL4DELETERENDERBUFFERSPROC         gl4_glDeleteRenderbuffers;
    extern PFNGL4RENDERBUFFERSTORAGEPROC         gl4_glRenderbufferStorage;
    extern PFNGL4RENDERBUFFERSTORAGEMULTISAMPLEPROC gl4_glRenderbufferStorageMultisample;
    extern PFNGL4FRAMEBUFFERRENDERBUFFERPROC     gl4_glFramebufferRenderbuffer;
    extern PFNGL4BLITFRAMEBUFFERPROC             gl4_glBlitFramebuffer;
    extern PFNGL4DRAWBUFFERSPROC                 gl4_glDrawBuffers;

    extern PFNGL4STENCILFUNCSEPARATEPROC         gl4_glStencilFuncSeparate;
    extern PFNGL4STENCILOPSEPARATEPROC           gl4_glStencilOpSeparate;
    extern PFNGL4STENCILMASKSEPARATEPROC         gl4_glStencilMaskSeparate;
    extern PFNGL4COLORMASKIPROC                  gl4_glColorMaski;

    extern PFNGL4TEXIMAGE3DPROC                  gl4_glTexImage3D;
    extern PFNGL4TEXSUBIMAGE3DPROC               gl4_glTexSubImage3D;
    extern PFNGL4FRAMEBUFFERTEXTURELAYERPROC     gl4_glFramebufferTextureLayer;

    extern PFNGL4GENQUERIESPROC                  gl4_glGenQueries;
    extern PFNGL4DELETEQUERIESPROC               gl4_glDeleteQueries;
    extern PFNGL4BEGINQUERYPROC                  gl4_glBeginQuery;
    extern PFNGL4ENDQUERYPROC                    gl4_glEndQuery;
    extern PFNGL4GETQUERYOBJECTUIVPROC           gl4_glGetQueryObjectuiv;

    extern PFNGL4DRAWELEMENTSBASEVERTEXPROC      gl4_glDrawElementsBaseVertex;

    extern PFNGL4DRAWELEMENTSINSTANCEDPROC       gl4_glDrawElementsInstanced;
    extern PFNGL4VERTEXATTRIBDIVISORPROC         gl4_glVertexAttribDivisor;

    /// Generic function-pointer-getter type matching the platform GL service callback.
    using GetProcAddressFn = void* (*)(const char* name);

    /**
     * @brief Resolves every function pointer declared above via @p getProcAddress.
     *
     * Must be called once, with a real current GL context bound, before any other GL4::gl4_*
     * call. Returns false (and leaves a diagnostic on stderr) if any entry point could not be
     * resolved -- every function this renderer actually calls is mandatory in a real GL 4.x core
     * context, so a partial load is treated as a hard failure rather than a capability to probe.
     *
     * @param getProcAddress Function used to resolve each GL entry point by name.
     * @return true if every entry point resolved successfully.
     */
    bool LoadGL4Functions(GetProcAddressFn getProcAddress);
}
