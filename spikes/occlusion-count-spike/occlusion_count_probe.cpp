// SPDX-License-Identifier: MS-PL
// Existence gate for EasyGLOcclusionQueryRenderer's precise-target path: does a desktop OpenGL
// context on this machine accept GL_SAMPLES_PASSED, and does it return a real fragment tally
// rather than the 0/1 an OpenGL ES context answers with?
//
// The renderer picks its query target by asking the driver, so without this the "precise" arm
// could be dead code that never runs anywhere. See spikes/occlusion-count-spike/README.md.
//
// Build: g++ -O1 occlusion_count_probe.cpp -lGL -lX11 -o occlusion_count_probe

#include <GL/glx.h>
#include <GL/gl.h>
#include <X11/Xlib.h>

#include <cstdio>

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

using PFNGENQUERIES = void (*)(GLsizei, GLuint*);
using PFNBEGINQUERY = void (*)(GLenum, GLuint);
using PFNENDQUERY = void (*)(GLenum);
using PFNGETQUERYOBJECTUIV = void (*)(GLuint, GLenum, GLuint*);

int main()
{
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) { std::printf("no display\n"); return 2; }

    int attributes[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
    XVisualInfo* visual = glXChooseVisual(display, DefaultScreen(display), attributes);
    if (visual == nullptr) { std::printf("no visual\n"); return 2; }

    Window root = DefaultRootWindow(display);
    XSetWindowAttributes swa{};
    swa.colormap = XCreateColormap(display, root, visual->visual, AllocNone);
    Window window = XCreateWindow(display, root, 0, 0, 64, 64, 0, visual->depth, InputOutput,
                                  visual->visual, CWColormap, &swa);
    XMapWindow(display, window);

    GLXContext context = glXCreateContext(display, visual, nullptr, GL_TRUE);
    glXMakeCurrent(display, window, context);

    std::printf("GL_VERSION: %s\n", (const char*)glGetString(GL_VERSION));

    auto glGenQueries = (PFNGENQUERIES)glXGetProcAddress((const GLubyte*)"glGenQueries");
    auto glBeginQuery = (PFNBEGINQUERY)glXGetProcAddress((const GLubyte*)"glBeginQuery");
    auto glEndQuery = (PFNENDQUERY)glXGetProcAddress((const GLubyte*)"glEndQuery");
    auto glGetQueryObjectuiv =
        (PFNGETQUERYOBJECTUIV)glXGetProcAddress((const GLubyte*)"glGetQueryObjectuiv");
    if (!glGenQueries || !glBeginQuery || !glEndQuery || !glGetQueryObjectuiv)
    {
        std::printf("query entry points missing\n");
        return 2;
    }

    GLuint query = 0;
    glGenQueries(1, &query);

    while (glGetError() != GL_NO_ERROR) {}
    glBeginQuery(GL_SAMPLES_PASSED, query);
    const bool accepted = glGetError() == GL_NO_ERROR;
    std::printf("GL_SAMPLES_PASSED accepted: %d\n", (int)accepted ? 1 : 0);
    if (!accepted) return 1;

    // A full-viewport quad: 64x64 = 4096 fragments. A boolean target answers 1 here.
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex3f(-1, -1, 0); glVertex3f(1, -1, 0); glVertex3f(-1, 1, 0); glVertex3f(1, 1, 0);
    glEnd();
    glEndQuery(GL_SAMPLES_PASSED);

    GLuint result = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result);
    std::printf("fragments counted: %u (a boolean target would say 1)\n", result);

    glXMakeCurrent(display, None, nullptr);
    glXDestroyContext(display, context);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return result > 1 ? 0 : 1;
}
