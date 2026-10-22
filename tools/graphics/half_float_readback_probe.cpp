// SPDX-License-Identifier: MS-PL
// Driver-only diagnostic for the museum's Android half-float readback finding.
// Default mode exposes the old RGBA/HALF_FLOAT transfer; --portable validates RGBA/FLOAT
// followed by CNA's existing half conversion. Neither mode creates a CNA device or uses SDL.
// Build in an existing tree: c++ -std=c++20 -Imodules/graphics/include this.cpp
//     -lEGL -lGLESv2 -o <build>/probe
// Android: NDK x86_64-linux-android24-clang++ -static-libstdc++, -lGLESv3 instead.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

int main(int argc,char** argv)
{
    const bool portable = argc == 2 && std::strcmp(argv[1],"--portable") == 0;
    if (argc != 1 && !portable) return 2;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        std::printf("eglInitialize failed: %x\n", eglGetError());
        return 77;
    }
    const EGLint attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
        return 77;
    const EGLint surfaceAttributes[]{EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    const EGLint contextAttributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (!eglMakeCurrent(display, surface, surface, context)) {
        std::printf("eglMakeCurrent failed: %x\n", eglGetError());
        return 77;
    }
    std::printf("GL vendor=%s renderer=%s version=%s\n",
        glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint texture = 0, framebuffer = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 8, 8, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    std::printf("framebuffer=%x allocationError=%x\n", status, glGetError());
    const GLfloat clear[]{-2.0f, 0.5f, 2.0f, 1.0f};
    glClearBufferfv(GL_COLOR, 0, clear);
    std::printf("clearError=%x\n", glGetError());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    GLint readType = 0, readFormat = 0;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &readType);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &readFormat);
    std::printf("preferred read type=%x format=%x\n", readType, readFormat);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // Oversized sentinel buffers make an erroneous driver transfer visible without overrunning.
    std::array<std::uint16_t, 16> half;
    half.fill(0xa55a);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_HALF_FLOAT, half.data());
    const GLenum halfError = glGetError();
    std::printf("HALF_FLOAT error=%x words:", halfError);
    for (auto word : half) std::printf(" %04x", static_cast<unsigned>(word));
    std::puts("");
    std::array<float, 8> full;
    full.fill(-1234.0f);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, full.data());
    const GLenum fullError = glGetError();
    std::printf("FLOAT error=%x values:", fullError);
    for (auto value : full) std::printf(" %.9g", static_cast<double>(value));
    std::puts("");
    const std::array<std::uint16_t, 4> expected{0xc000, 0x3800, 0x4000, 0x3c00};
    bool halfCorrect = halfError == GL_NO_ERROR;
    bool fullCorrect = fullError == GL_NO_ERROR;
    for (unsigned i = 0; i < 4; ++i) {
        halfCorrect = halfCorrect && half[i] == expected[i];
        fullCorrect = fullCorrect && full[i] == clear[i];
    }
    for (unsigned i = 4; i < half.size(); ++i) halfCorrect = halfCorrect && half[i] == 0xa55a;
    for (unsigned i = 4; i < full.size(); ++i) fullCorrect = fullCorrect && full[i] == -1234.0f;
    std::printf("exact half=%s float=%s\n", halfCorrect ? "PASS" : "FAIL", fullCorrect ? "PASS" : "FAIL");
    bool packedCorrect=fullCorrect;
    for (unsigned i=0;i<4;++i) {
        const auto packed=Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper::Convert(full[i]);
        packedCorrect=packedCorrect && packed==expected[i];
    }
    std::printf("portable float-to-half=%s\n",packedCorrect ? "PASS" : "FAIL");
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return status == GL_FRAMEBUFFER_COMPLETE && (portable ? packedCorrect : halfCorrect && fullCorrect) ? 0 : 1;
}
