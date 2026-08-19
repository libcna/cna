// SPDX-License-Identifier: MS-PL
//
// The one translation unit that instantiates NanoVG's GL2 render backend
// (`NANOVG_GL2_IMPLEMENTATION`, `nanovg_gl.h`). `nanovg_gl.h` is not a loader itself: it calls
// `gl*` entry points unqualified, assuming the includer already has them resolvable. Desktop
// GLX/WGL only guarantee GL 1.1 statically linkable from `libGL`/`opengl32.dll` -- everything
// NanoVG's GL2 path calls beyond that (buffer objects, shader objects, `glActiveTexture`,
// `glBlendFuncSeparate`, `glStencilOpSeparate`, ~28 entry points total) is resolved at runtime by
// LoadNanoVgGlFunctions() through the platform's current GL loader and reached via a `#define`
// rename, not a same-named shadow variable: `<GL/gl.h>`/`<GL/glext.h>` on this toolchain already
// declare a handful of these (e.g. `glActiveTexture`, GL 1.3 core) as real `extern` functions at
// global scope even with `GL_GLEXT_PROTOTYPES` undefined, so a same-named file-scope variable is a
// genuine ambiguous-declaration error (verified empirically -- ODR conflict/ambiguous-lookup, not
// the harmless case `OpenGL2Renderer.cpp`'s own CLASS-MEMBER shadow variables sidestep by being in
// a different scope). Renaming to `cna_nvg_gl*` avoids the collision entirely: `#define glCreateShader
// cna_nvg_glCreateShader` textually rewrites nanovg_gl.h's own calls (and nothing upstream, since
// the real system declarations are already compiled by the time these defines take effect) --
// exactly the mechanism nanovg-spike/nanovg_smoke.c proved end-to-end (real pixels read back)
// before this file was written.
#include "CNA/Internal/Renderers/NanoVg/NanoVgGlLoader.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace
{
    // The exact ~28 entry points nanovg_gl.h's GL2 backend calls beyond GL 1.1 (verified by
    // grepping every `gl[A-Z]\w*(` call site in the pinned nanovg_gl.h and excluding the GL3-only
    // VAO/uniform-buffer calls, which `#if defined NANOVG_GL3`/`#if NANOVG_GL_USE_UNIFORMBUFFER`
    // guard out of the GL2 build entirely). `glGetError`/`glGetIntegerv` and every other call
    // nanovg_gl.h makes are genuine GL 1.0/1.1 core entry points, already real `extern`
    // declarations from the plain `<GL/gl.h>` include above -- no shim needed for those.
    PFNGLACTIVETEXTUREPROC             cna_nvg_glActiveTexture             = nullptr;
    PFNGLATTACHSHADERPROC              cna_nvg_glAttachShader              = nullptr;
    PFNGLBINDATTRIBLOCATIONPROC        cna_nvg_glBindAttribLocation        = nullptr;
    PFNGLBINDBUFFERPROC                cna_nvg_glBindBuffer                = nullptr;
    PFNGLBLENDFUNCSEPARATEPROC         cna_nvg_glBlendFuncSeparate         = nullptr;
    PFNGLBUFFERDATAPROC                cna_nvg_glBufferData                = nullptr;
    PFNGLCOMPILESHADERPROC             cna_nvg_glCompileShader             = nullptr;
    PFNGLCREATEPROGRAMPROC             cna_nvg_glCreateProgram             = nullptr;
    PFNGLCREATESHADERPROC              cna_nvg_glCreateShader              = nullptr;
    PFNGLDELETEBUFFERSPROC             cna_nvg_glDeleteBuffers             = nullptr;
    PFNGLDELETEPROGRAMPROC             cna_nvg_glDeleteProgram             = nullptr;
    PFNGLDELETESHADERPROC              cna_nvg_glDeleteShader              = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC  cna_nvg_glDisableVertexAttribArray  = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC   cna_nvg_glEnableVertexAttribArray   = nullptr;
    PFNGLGENBUFFERSPROC                cna_nvg_glGenBuffers                = nullptr;
    PFNGLGENERATEMIPMAPPROC            cna_nvg_glGenerateMipmap            = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC         cna_nvg_glGetProgramInfoLog         = nullptr;
    PFNGLGETPROGRAMIVPROC              cna_nvg_glGetProgramiv              = nullptr;
    PFNGLGETSHADERINFOLOGPROC          cna_nvg_glGetShaderInfoLog          = nullptr;
    PFNGLGETSHADERIVPROC               cna_nvg_glGetShaderiv               = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC        cna_nvg_glGetUniformLocation        = nullptr;
    PFNGLLINKPROGRAMPROC               cna_nvg_glLinkProgram               = nullptr;
    PFNGLSHADERSOURCEPROC              cna_nvg_glShaderSource              = nullptr;
    PFNGLSTENCILOPSEPARATEPROC         cna_nvg_glStencilOpSeparate         = nullptr;
    PFNGLUNIFORM1IPROC                 cna_nvg_glUniform1i                 = nullptr;
    PFNGLUNIFORM2FVPROC                cna_nvg_glUniform2fv                = nullptr;
    PFNGLUNIFORM4FVPROC                cna_nvg_glUniform4fv                = nullptr;
    PFNGLUSEPROGRAMPROC                cna_nvg_glUseProgram                = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC       cna_nvg_glVertexAttribPointer       = nullptr;
}

namespace CNA::Internal::Renderers::NanoVg
{
    void LoadNanoVgGlFunctions()
    {
#define CNA_NVG_LOAD_GL(name) \
        cna_nvg_##name = reinterpret_cast<decltype(cna_nvg_##name)>( \
            CNA::Internal::Renderers::LoadPlatformGlProcAddress(#name))
        CNA_NVG_LOAD_GL(glActiveTexture);
        CNA_NVG_LOAD_GL(glAttachShader);
        CNA_NVG_LOAD_GL(glBindAttribLocation);
        CNA_NVG_LOAD_GL(glBindBuffer);
        CNA_NVG_LOAD_GL(glBlendFuncSeparate);
        CNA_NVG_LOAD_GL(glBufferData);
        CNA_NVG_LOAD_GL(glCompileShader);
        CNA_NVG_LOAD_GL(glCreateProgram);
        CNA_NVG_LOAD_GL(glCreateShader);
        CNA_NVG_LOAD_GL(glDeleteBuffers);
        CNA_NVG_LOAD_GL(glDeleteProgram);
        CNA_NVG_LOAD_GL(glDeleteShader);
        CNA_NVG_LOAD_GL(glDisableVertexAttribArray);
        CNA_NVG_LOAD_GL(glEnableVertexAttribArray);
        CNA_NVG_LOAD_GL(glGenBuffers);
        CNA_NVG_LOAD_GL(glGenerateMipmap);
        CNA_NVG_LOAD_GL(glGetProgramInfoLog);
        CNA_NVG_LOAD_GL(glGetProgramiv);
        CNA_NVG_LOAD_GL(glGetShaderInfoLog);
        CNA_NVG_LOAD_GL(glGetShaderiv);
        CNA_NVG_LOAD_GL(glGetUniformLocation);
        CNA_NVG_LOAD_GL(glLinkProgram);
        CNA_NVG_LOAD_GL(glShaderSource);
        CNA_NVG_LOAD_GL(glStencilOpSeparate);
        CNA_NVG_LOAD_GL(glUniform1i);
        CNA_NVG_LOAD_GL(glUniform2fv);
        CNA_NVG_LOAD_GL(glUniform4fv);
        CNA_NVG_LOAD_GL(glUseProgram);
        CNA_NVG_LOAD_GL(glVertexAttribPointer);
#undef CNA_NVG_LOAD_GL
    }
}

// Everything below this point sees the RENAMED identifiers -- these #defines must come after both
// the real system GL headers (already compiled above under their real names) and
// LoadNanoVgGlFunctions() (which assigns the cna_nvg_-prefixed variables directly, not through the
// macros -- see CNA_NVG_LOAD_GL's own token-pasting above).
#define glActiveTexture            cna_nvg_glActiveTexture
#define glAttachShader              cna_nvg_glAttachShader
#define glBindAttribLocation        cna_nvg_glBindAttribLocation
#define glBindBuffer                cna_nvg_glBindBuffer
#define glBlendFuncSeparate         cna_nvg_glBlendFuncSeparate
#define glBufferData                cna_nvg_glBufferData
#define glCompileShader             cna_nvg_glCompileShader
#define glCreateProgram              cna_nvg_glCreateProgram
#define glCreateShader               cna_nvg_glCreateShader
#define glDeleteBuffers              cna_nvg_glDeleteBuffers
#define glDeleteProgram              cna_nvg_glDeleteProgram
#define glDeleteShader               cna_nvg_glDeleteShader
#define glDisableVertexAttribArray  cna_nvg_glDisableVertexAttribArray
#define glEnableVertexAttribArray   cna_nvg_glEnableVertexAttribArray
#define glGenBuffers                 cna_nvg_glGenBuffers
#define glGenerateMipmap             cna_nvg_glGenerateMipmap
#define glGetProgramInfoLog          cna_nvg_glGetProgramInfoLog
#define glGetProgramiv                cna_nvg_glGetProgramiv
#define glGetShaderInfoLog           cna_nvg_glGetShaderInfoLog
#define glGetShaderiv                 cna_nvg_glGetShaderiv
#define glGetUniformLocation          cna_nvg_glGetUniformLocation
#define glLinkProgram                 cna_nvg_glLinkProgram
#define glShaderSource                cna_nvg_glShaderSource
#define glStencilOpSeparate          cna_nvg_glStencilOpSeparate
#define glUniform1i                   cna_nvg_glUniform1i
#define glUniform2fv                  cna_nvg_glUniform2fv
#define glUniform4fv                  cna_nvg_glUniform4fv
#define glUseProgram                  cna_nvg_glUseProgram
#define glVertexAttribPointer         cna_nvg_glVertexAttribPointer

#include "nanovg.h"
#define NANOVG_GL2_IMPLEMENTATION
#include "nanovg_gl.h"

namespace CNA::Internal::Renderers::NanoVg
{
    // nvgCreateGL2/nvgDeleteGL2 are declared only inside nanovg_gl.h's own
    // `#if defined NANOVG_GL2` block (set above by NANOVG_GL2_IMPLEMENTATION), so these wrappers
    // -- the only way another translation unit reaches them -- live in this same file. See
    // NanoVgGlLoader.hpp's own doc comment.
    NVGcontext* CreateNanoVgGL2Context()
    {
        return nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    }

    void DeleteNanoVgGL2Context(NVGcontext* ctx)
    {
        if (ctx) nvgDeleteGL2(ctx);
    }
}
