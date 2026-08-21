/* Standalone existence-gate spike: prove NanoVG (memononen/nanovg, GL2 backend) renders a real
 * pixel via a GLX context under Xvfb, before wiring it into CNA. Not part of the CNA build -- ad
 * hoc validation only. Mirrors openvg-spike/openvg_smoke.c's own GLX/Xlib context shape.
 *
 * NanoVG's nanovg_gl.h is not a loader (unlike GLAD): it calls gl* functions directly by name,
 * assuming the translation unit that #includes it with NANOVG_GL2_IMPLEMENTATION already has them
 * available. On desktop GLX only up to GL 1.1 is guaranteed statically linkable from libGL.so --
 * everything from GL 1.2 onward (buffer objects, shader objects, glActiveTexture, ...) must be
 * resolved via glXGetProcAddress at runtime. This spike's own tiny loader (below) resolves the
 * exact set nanovg_gl.h's GL2 backend calls and #defines each gl* name to the loaded pointer
 * BEFORE nanovg_gl.h is included, so its plain unqualified calls resolve correctly -- the same
 * macro-substitution shape real loaders (GLAD/GLEW) use, just scoped to nanovg's own needs.
 */
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal GL function-pointer loader for exactly what nanovg_gl.h's GL2 backend calls ---- */

/* Declare pointer-backed shims for every post-GL1.1 entry point nanovg_gl.h's GL2 path calls.
 * Names deliberately shadow the real gl* names via #define below so nanovg_gl.h's own unqualified
 * calls resolve to these. */
typedef void (GLAPIENTRY *PFNACTIVETEXTUREPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNATTACHSHADERPROC)(unsigned int, unsigned int);
typedef void (GLAPIENTRY *PFNBINDATTRIBLOCATIONPROC)(unsigned int, unsigned int, const char*);
typedef void (GLAPIENTRY *PFNBINDBUFFERPROC)(unsigned int, unsigned int);
typedef void (GLAPIENTRY *PFNBLENDFUNCSEPARATEPROC)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (GLAPIENTRY *PFNBUFFERDATAPROC)(unsigned int, long, const void*, unsigned int);
typedef void (GLAPIENTRY *PFNCOMPILESHADERPROC)(unsigned int);
typedef unsigned int (GLAPIENTRY *PFNCREATEPROGRAMPROC)(void);
typedef unsigned int (GLAPIENTRY *PFNCREATESHADERPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNDELETEBUFFERSPROC)(int, const unsigned int*);
typedef void (GLAPIENTRY *PFNDELETEPROGRAMPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNDELETESHADERPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNDISABLEVERTEXATTRIBARRAYPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNENABLEVERTEXATTRIBARRAYPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNGENBUFFERSPROC)(int, unsigned int*);
typedef void (GLAPIENTRY *PFNGENERATEMIPMAPPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNGETPROGRAMINFOLOGPROC)(unsigned int, int, int*, char*);
typedef void (GLAPIENTRY *PFNGETPROGRAMIVPROC)(unsigned int, unsigned int, int*);
typedef void (GLAPIENTRY *PFNGETSHADERINFOLOGPROC)(unsigned int, int, int*, char*);
typedef void (GLAPIENTRY *PFNGETSHADERIVPROC)(unsigned int, unsigned int, int*);
typedef int  (GLAPIENTRY *PFNGETUNIFORMLOCATIONPROC)(unsigned int, const char*);
typedef void (GLAPIENTRY *PFNLINKPROGRAMPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNSHADERSOURCEPROC)(unsigned int, int, const char* const*, const int*);
typedef void (GLAPIENTRY *PFNSTENCILOPSEPARATEPROC)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (GLAPIENTRY *PFNUNIFORM1IPROC)(int, int);
typedef void (GLAPIENTRY *PFNUNIFORM2FVPROC)(int, int, const float*);
typedef void (GLAPIENTRY *PFNUNIFORM4FVPROC)(int, int, const float*);
typedef void (GLAPIENTRY *PFNUSEPROGRAMPROC)(unsigned int);
typedef void (GLAPIENTRY *PFNVERTEXATTRIBPOINTERPROC)(unsigned int, int, unsigned int, unsigned char, int, const void*);

static PFNACTIVETEXTUREPROC              cna_glActiveTexture;
static PFNATTACHSHADERPROC               cna_glAttachShader;
static PFNBINDATTRIBLOCATIONPROC         cna_glBindAttribLocation;
static PFNBINDBUFFERPROC                 cna_glBindBuffer;
static PFNBLENDFUNCSEPARATEPROC          cna_glBlendFuncSeparate;
static PFNBUFFERDATAPROC                 cna_glBufferData;
static PFNCOMPILESHADERPROC              cna_glCompileShader;
static PFNCREATEPROGRAMPROC              cna_glCreateProgram;
static PFNCREATESHADERPROC               cna_glCreateShader;
static PFNDELETEBUFFERSPROC              cna_glDeleteBuffers;
static PFNDELETEPROGRAMPROC              cna_glDeleteProgram;
static PFNDELETESHADERPROC               cna_glDeleteShader;
static PFNDISABLEVERTEXATTRIBARRAYPROC   cna_glDisableVertexAttribArray;
static PFNENABLEVERTEXATTRIBARRAYPROC    cna_glEnableVertexAttribArray;
static PFNGENBUFFERSPROC                 cna_glGenBuffers;
static PFNGENERATEMIPMAPPROC             cna_glGenerateMipmap;
static PFNGETPROGRAMINFOLOGPROC          cna_glGetProgramInfoLog;
static PFNGETPROGRAMIVPROC               cna_glGetProgramiv;
static PFNGETSHADERINFOLOGPROC           cna_glGetShaderInfoLog;
static PFNGETSHADERIVPROC                cna_glGetShaderiv;
static PFNGETUNIFORMLOCATIONPROC         cna_glGetUniformLocation;
static PFNLINKPROGRAMPROC                cna_glLinkProgram;
static PFNSHADERSOURCEPROC               cna_glShaderSource;
static PFNSTENCILOPSEPARATEPROC          cna_glStencilOpSeparate;
static PFNUNIFORM1IPROC                  cna_glUniform1i;
static PFNUNIFORM2FVPROC                 cna_glUniform2fv;
static PFNUNIFORM4FVPROC                 cna_glUniform4fv;
static PFNUSEPROGRAMPROC                 cna_glUseProgram;
static PFNVERTEXATTRIBPOINTERPROC        cna_glVertexAttribPointer;

/* Redirect nanovg_gl.h's plain calls to the loaded pointers. GL 1.0/1.1 core entry points
 * (glActiveTexture is 1.3 -- NOT core 1.1 -- so it is loaded too) stay linked normally against
 * libGL.so. */
#define glActiveTexture            cna_glActiveTexture
#define glAttachShader              cna_glAttachShader
#define glBindAttribLocation        cna_glBindAttribLocation
#define glBindBuffer                cna_glBindBuffer
#define glBlendFuncSeparate         cna_glBlendFuncSeparate
#define glBufferData                cna_glBufferData
#define glCompileShader             cna_glCompileShader
#define glCreateProgram              cna_glCreateProgram
#define glCreateShader               cna_glCreateShader
#define glDeleteBuffers              cna_glDeleteBuffers
#define glDeleteProgram              cna_glDeleteProgram
#define glDeleteShader               cna_glDeleteShader
#define glDisableVertexAttribArray  cna_glDisableVertexAttribArray
#define glEnableVertexAttribArray   cna_glEnableVertexAttribArray
#define glGenBuffers                 cna_glGenBuffers
#define glGenerateMipmap             cna_glGenerateMipmap
#define glGetProgramInfoLog          cna_glGetProgramInfoLog
#define glGetProgramiv                cna_glGetProgramiv
#define glGetShaderInfoLog           cna_glGetShaderInfoLog
#define glGetShaderiv                 cna_glGetShaderiv
#define glGetUniformLocation          cna_glGetUniformLocation
#define glLinkProgram                 cna_glLinkProgram
#define glShaderSource                cna_glShaderSource
#define glStencilOpSeparate          cna_glStencilOpSeparate
#define glUniform1i                   cna_glUniform1i
#define glUniform2fv                  cna_glUniform2fv
#define glUniform4fv                  cna_glUniform4fv
#define glUseProgram                  cna_glUseProgram
#define glVertexAttribPointer         cna_glVertexAttribPointer

static void LoadNanoVgGlFunctions(void)
{
#define CNA_LOAD(var, name) var = (void*)glXGetProcAddress((const unsigned char*)name)
    CNA_LOAD(cna_glActiveTexture, "glActiveTexture");
    CNA_LOAD(cna_glAttachShader, "glAttachShader");
    CNA_LOAD(cna_glBindAttribLocation, "glBindAttribLocation");
    CNA_LOAD(cna_glBindBuffer, "glBindBuffer");
    CNA_LOAD(cna_glBlendFuncSeparate, "glBlendFuncSeparate");
    CNA_LOAD(cna_glBufferData, "glBufferData");
    CNA_LOAD(cna_glCompileShader, "glCompileShader");
    CNA_LOAD(cna_glCreateProgram, "glCreateProgram");
    CNA_LOAD(cna_glCreateShader, "glCreateShader");
    CNA_LOAD(cna_glDeleteBuffers, "glDeleteBuffers");
    CNA_LOAD(cna_glDeleteProgram, "glDeleteProgram");
    CNA_LOAD(cna_glDeleteShader, "glDeleteShader");
    CNA_LOAD(cna_glDisableVertexAttribArray, "glDisableVertexAttribArray");
    CNA_LOAD(cna_glEnableVertexAttribArray, "glEnableVertexAttribArray");
    CNA_LOAD(cna_glGenBuffers, "glGenBuffers");
    CNA_LOAD(cna_glGenerateMipmap, "glGenerateMipmap");
    CNA_LOAD(cna_glGetProgramInfoLog, "glGetProgramInfoLog");
    CNA_LOAD(cna_glGetProgramiv, "glGetProgramiv");
    CNA_LOAD(cna_glGetShaderInfoLog, "glGetShaderInfoLog");
    CNA_LOAD(cna_glGetShaderiv, "glGetShaderiv");
    CNA_LOAD(cna_glGetUniformLocation, "glGetUniformLocation");
    CNA_LOAD(cna_glLinkProgram, "glLinkProgram");
    CNA_LOAD(cna_glShaderSource, "glShaderSource");
    CNA_LOAD(cna_glStencilOpSeparate, "glStencilOpSeparate");
    CNA_LOAD(cna_glUniform1i, "glUniform1i");
    CNA_LOAD(cna_glUniform2fv, "glUniform2fv");
    CNA_LOAD(cna_glUniform4fv, "glUniform4fv");
    CNA_LOAD(cna_glUseProgram, "glUseProgram");
    CNA_LOAD(cna_glVertexAttribPointer, "glVertexAttribPointer");
#undef CNA_LOAD
}

#define NANOVG_GL2_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg.c"

int main(void)
{
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "XOpenDisplay failed\n"); return 1; }

    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                    GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, None };
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attrs);
    if (!vi) { fprintf(stderr, "glXChooseVisual failed\n"); return 1; }

    Window root = RootWindow(dpy, vi->screen);
    XSetWindowAttributes swa;
    swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    swa.border_pixel = 0;
    Window win = XCreateWindow(dpy, root, 0, 0, 128, 128, 0, vi->depth, InputOutput,
                                vi->visual, CWColormap | CWBorderPixel, &swa);

    GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
    if (!ctx) { fprintf(stderr, "glXCreateContext failed\n"); return 1; }
    if (!glXMakeCurrent(dpy, win, ctx)) { fprintf(stderr, "glXMakeCurrent failed\n"); return 1; }

    LoadNanoVgGlFunctions();

    NVGcontext *vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg) { fprintf(stderr, "nvgCreateGL2 failed\n"); return 1; }

    glViewport(0, 0, 128, 128);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg, 128, 128, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 32, 32, 64, 64);
    nvgFillColor(vg, nvgRGBA(255, 0, 0, 255));
    nvgFill(vg);
    nvgEndFrame(vg);

    glFinish();

    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("center pixel RGBA = %d %d %d %d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    unsigned char corner[4] = {0, 0, 0, 0};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    printf("corner pixel RGBA = %d %d %d %d\n", corner[0], corner[1], corner[2], corner[3]);

    int ok = 1;
    if (!(pixel[0] > 200 && pixel[1] < 50 && pixel[2] < 50)) { fprintf(stderr, "FAIL: center not red\n"); ok = 0; }
    if (!(corner[2] > 60 && corner[2] < 100 && corner[0] < 50)) { fprintf(stderr, "FAIL: corner not clear-blue-ish\n"); ok = 0; }

    nvgDeleteGL2(vg);
    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, ctx);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    printf(ok ? "SMOKE TEST PASSED\n" : "SMOKE TEST FAILED\n");
    return ok ? 0 : 1;
}
