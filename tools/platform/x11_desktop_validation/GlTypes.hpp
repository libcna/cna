// SPDX-License-Identifier: MS-PL
//
// Function-pointer types for the OpenGL 1.0/1.1 entry points the harness resolves through
// IPlatformGlContext::GetProcAddress. <GL/glext.h> declares PFN types only for what came after
// 1.1, and <GL/glcorearb.h> cannot be included beside the <GL/gl.h> that GLX brings in.
#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

namespace CnaX11Validation::GlTypes {

    using GetString = const GLubyte* (*)(GLenum);
    using GetIntegerv = void (*)(GLenum, GLint*);
    using GetError = GLenum (*)();
    using ClearColor = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
    using Clear = void (*)(GLbitfield);
    using Viewport = void (*)(GLint, GLint, GLsizei, GLsizei);
    using ReadPixels = void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    using Finish = void (*)();
    using DrawArrays = void (*)(GLenum, GLint, GLsizei);

} // namespace CnaX11Validation::GlTypes
