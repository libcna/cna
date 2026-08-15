#include "CNA/Internal/Renderers/OpenGL1/OpenGL1OcclusionQueryRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <stdexcept>

namespace CNA::Internal::Renderers::OpenGL1
{
namespace
{
    PFNGLGENQUERIESPROC glGenQueries_ = nullptr;
    PFNGLDELETEQUERIESPROC glDeleteQueries_ = nullptr;
    PFNGLBEGINQUERYPROC glBeginQuery_ = nullptr;
    PFNGLENDQUERYPROC glEndQuery_ = nullptr;
    PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv_ = nullptr;
    PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv_ = nullptr;
}

bool TryLoadOpenGL1OcclusionQueryFunctions()
{
    auto load = [](const char* name) { return LoadPlatformGlProcAddress(name); };
    glGenQueries_ = reinterpret_cast<PFNGLGENQUERIESPROC>(load("glGenQueries"));
    glDeleteQueries_ = reinterpret_cast<PFNGLDELETEQUERIESPROC>(load("glDeleteQueries"));
    glBeginQuery_ = reinterpret_cast<PFNGLBEGINQUERYPROC>(load("glBeginQuery"));
    glEndQuery_ = reinterpret_cast<PFNGLENDQUERYPROC>(load("glEndQuery"));
    glGetQueryObjectiv_ = reinterpret_cast<PFNGLGETQUERYOBJECTIVPROC>(load("glGetQueryObjectiv"));
    glGetQueryObjectuiv_ = reinterpret_cast<PFNGLGETQUERYOBJECTUIVPROC>(load("glGetQueryObjectuiv"));

    return glGenQueries_ && glDeleteQueries_ && glBeginQuery_ && glEndQuery_
        && glGetQueryObjectiv_ && glGetQueryObjectuiv_;
}

OpenGL1OcclusionQueryRenderer::OpenGL1OcclusionQueryRenderer()
{
    if (!glGenQueries_)
        throw std::runtime_error("OpenGL1OcclusionQueryRenderer: occlusion query functions not loaded");
    glGenQueries_(1, &id_);
}

OpenGL1OcclusionQueryRenderer::~OpenGL1OcclusionQueryRenderer()
{
    if (id_) glDeleteQueries_(1, &id_);
}

void OpenGL1OcclusionQueryRenderer::Begin() { glBeginQuery_(GL_SAMPLES_PASSED, id_); }

void OpenGL1OcclusionQueryRenderer::End() { glEndQuery_(GL_SAMPLES_PASSED); }

bool OpenGL1OcclusionQueryRenderer::IsComplete() const
{
    GLint available = 0;
    glGetQueryObjectiv_(id_, GL_QUERY_RESULT_AVAILABLE, &available);
    return available != 0;
}

// GL_QUERY_RESULT blocks the CPU until the result is ready if called before the query
// genuinely completes -- callers are expected to poll IsComplete() first (its own doc comment:
// "whether ... available WITHOUT blocking"), matching real ARB_occlusion_query semantics.
int OpenGL1OcclusionQueryRenderer::PixelCount() const
{
    GLuint result = 0;
    glGetQueryObjectuiv_(id_, GL_QUERY_RESULT, &result);
    return static_cast<int>(result);
}
}
