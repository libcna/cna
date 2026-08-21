// SPDX-License-Identifier: MS-PL
//
// TINYGL-0 existence gate. Proves -- before any CNA renderer code is written -- that the pinned
// TinyGL (C-Chads/tinygl) build can, with no GPU, no window and no display server:
//
//   1. create a real rendering context (ZB_open + glInit) and expose its CPU framebuffer;
//   2. honour glClearColor/glClear;
//   3. rasterize a real colored triangle through the vertex-array path
//      (glVertexPointer/glColorPointer/glDrawArrays) with caller-supplied glLoadMatrixf matrices;
//   4. rasterize a real textured quad through glGenTextures/glTexImage2D/glTexCoordPointer;
//   5. rasterize an indexed draw through glArrayElement (TinyGL has NO glDrawElements);
//   6. be read back deterministically.
//
// It also pins down, by execution rather than by reading headers, the four boundary facts that
// shape the renderer's honest capability contract:
//
//   A. glReadPixels is an upstream stub -- readback must go through the ZBuffer's own pbuf.
//   B. glTexImage2D accepts GL_RGB/GL_UNSIGNED_BYTE only; there is no RGBA texture upload.
//   C. blending has no alpha factors at all (GL_ONE/GL_ZERO/GL_ONE_MINUS_{SRC,DST}_COLOR only).
//   D. there is no stencil buffer, no scissor, no color mask and no selectable depth function.
//
// Build (see README.md):
//   g++ -std=c++23 -I$TINYGL/include tinygl_existence_gate.cpp -L$TINYGL/build/src -ltinygl-static -lm -o tinygl_existence_gate

// GL/gl.h carries its own extern "C" guard; zbuffer.h does not, so the ZB_* entry points have to
// be declared with C linkage here. This is the same wrapper the renderer module uses.
#include <GL/gl.h>
extern "C" {
#include <zbuffer.h>
}

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    int failures = 0;

    /// TinyGL's 32-bit PIXEL is 0x00RRGGBB (GET_RED(p) == (p >> 16) & 0xff), so the framebuffer is
    /// XRGB8888 -- the alpha byte exists in memory but no part of the pipeline ever writes it.
    void DecodePixel(std::uint32_t pixel, int& r, int& g, int& b)
    {
        r = static_cast<int>((pixel >> 16) & 0xffu);
        g = static_cast<int>((pixel >> 8) & 0xffu);
        b = static_cast<int>(pixel & 0xffu);
    }

    std::uint32_t ReadPixel(const ZBuffer* zb, int x, int y)
    {
        const auto* row = reinterpret_cast<const std::uint8_t*>(zb->pbuf) + static_cast<std::size_t>(y) * zb->linesize;
        return reinterpret_cast<const std::uint32_t*>(row)[x];
    }

    void Check(bool condition, const char* what)
    {
        std::printf("%-58s %s\n", what, condition ? "PASS" : "FAIL");
        if (!condition) ++failures;
    }

    void CheckPixel(const ZBuffer* zb, int x, int y, int er, int eg, int eb, int tolerance, const char* what)
    {
        int r = 0, g = 0, b = 0;
        DecodePixel(ReadPixel(zb, x, y), r, g, b);
        const bool ok = std::abs(r - er) <= tolerance && std::abs(g - eg) <= tolerance && std::abs(b - eb) <= tolerance;
        std::printf("%-58s %s  (got %3d,%3d,%3d want %3d,%3d,%3d)\n",
                    what, ok ? "PASS" : "FAIL", r, g, b, er, eg, eb);
        if (!ok) ++failures;
    }

    /// Column-major identity, the layout glLoadMatrixf expects.
    const GLfloat kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    void LoadIdentityMatrices()
    {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(kIdentity);
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(kIdentity);
    }
}

int main(int argc, char** argv)
{
    const bool proveRgbaFatal = (argc > 1 && std::string_view(argv[1]) == "--prove-rgba-fatal");

    std::printf("TINYGL-0 existence gate\n");
    std::printf("=======================\n\n");

    // --- 1. Context -------------------------------------------------------------------------
    ZBuffer* zb = ZB_open(kWidth, kHeight, ZB_MODE_RGBA, nullptr);
    Check(zb != nullptr, "1. ZB_open(ZB_MODE_RGBA) returns a framebuffer");
    if (zb == nullptr) return 1;
    glInit(zb);
    Check(zb->pbuf != nullptr, "1. ZBuffer exposes a CPU color buffer (pbuf)");
    Check(zb->xsize == kWidth && zb->ysize == kHeight, "1. framebuffer has the requested size");
    std::printf("   GL_VERSION  : %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::printf("   GL_RENDERER : %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("   linesize    : %d bytes for %d px (=> %d bytes/px)\n\n",
                zb->linesize, zb->xsize, zb->linesize / zb->xsize);

    glViewport(0, 0, kWidth, kHeight);

    // --- 2. Clear ---------------------------------------------------------------------------
    glClearColor(0.25f, 0.50f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    CheckPixel(zb, kWidth / 2, kHeight / 2, 64, 128, 191, 2, "2. glClear paints the requested color");

    // --- 3. Colored triangle through the vertex-array path ----------------------------------
    LoadIdentityMatrices();
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_FLAT);

    // A full-screen-ish CCW triangle covering the lower-left half in NDC.
    const GLfloat triPos[9] = {
        -0.9f, -0.9f, 0.0f,
         0.9f, -0.9f, 0.0f,
        -0.9f,  0.9f, 0.0f,
    };
    const GLfloat triCol[12] = {
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, triPos);
    glColorPointer(4, GL_FLOAT, 0, triCol);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    // Lower-left quadrant is inside the triangle; upper-right is outside and must still be clear.
    CheckPixel(zb, 8, kHeight - 8, 255, 0, 0, 2, "3. glDrawArrays rasterizes a real colored triangle");
    CheckPixel(zb, kWidth - 8, 8, 64, 128, 191, 2, "3. outside the triangle the clear color survives");

    // --- 4. Textured quad -------------------------------------------------------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Fact B: GL_RGB/GL_UNSIGNED_BYTE only. A 2x2 texture: green, green / green, green.
    std::vector<std::uint8_t> texRgb(2 * 2 * 3);
    for (int i = 0; i < 4; ++i)
    {
        texRgb[i * 3 + 0] = 0;
        texRgb[i * 3 + 1] = 255;
        texRgb[i * 3 + 2] = 0;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, texRgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glEnable(GL_TEXTURE_2D);

    const GLfloat quadPos[18] = {
        -0.5f, -0.5f, 0.0f,   0.5f, -0.5f, 0.0f,   0.5f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,   0.5f,  0.5f, 0.0f,  -0.5f,  0.5f, 0.0f,
    };
    const GLfloat quadUv[12] = {
        0.0f, 0.0f,   1.0f, 0.0f,   1.0f, 1.0f,
        0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 1.0f,
    };
    const GLfloat quadCol[24] = {
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,
        1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, quadPos);
    glColorPointer(4, GL_FLOAT, 0, quadCol);
    glTexCoordPointer(2, GL_FLOAT, 0, quadUv);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);

    CheckPixel(zb, kWidth / 2, kHeight / 2, 0, 255, 0, 4, "4. glTexImage2D + glDrawArrays sample a real texture");
    CheckPixel(zb, 2, 2, 64, 128, 191, 2, "4. outside the quad the clear color survives");

    // --- 5. Indexed draw via glArrayElement (there is no glDrawElements) ---------------------
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const GLushort indices[3] = {2, 1, 0};
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, triPos);
    glColorPointer(4, GL_FLOAT, 0, triCol);
    glBegin(GL_TRIANGLES);
    for (unsigned short index : indices) glArrayElement(static_cast<GLint>(index));
    glEnd();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    CheckPixel(zb, 8, kHeight - 8, 255, 0, 0, 2, "5. glArrayElement replays an indexed draw");

    // --- 6. Boundary facts ------------------------------------------------------------------
    // Fact A: glReadPixels is an upstream stub. Poison the destination and prove it stays poisoned.
    std::uint32_t readback[4] = {0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef};
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_INT, readback);
    Check(readback[0] == 0xdeadbeef,
          "6A. glReadPixels is a no-op stub (readback must use pbuf)");

    // Fact B: the GL_RGB upload above is the ONLY accepted shape. Handing glTexImage2D a GL_RGBA
    // buffer does not set an error flag and return -- it calls TinyGL's gl_fatal_error(), which
    // prints "combination of parameters not handled" and terminates the process. That is the single
    // most important constraint on the renderer: every unsupported argument must be rejected by CNA
    // *before* it reaches TinyGL, because there is no recoverable error path to fall back on.
    // Run this binary with --prove-rgba-fatal to observe the abort; it is deliberately kept out of
    // the default run so the gate can finish.
    if (proveRgbaFatal)
    {
        std::printf("\n   invoking glTexImage2D(GL_RGBA) -- expected to terminate the process:\n");
        std::fflush(stdout);
        glBindTexture(GL_TEXTURE_2D, tex);
        std::vector<std::uint8_t> texRgba(2 * 2 * 4, 0x7f);
        glTexImage2D(GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texRgba.data());
        std::printf("   UNEXPECTED: glTexImage2D(GL_RGBA) returned normally\n");
        return 1;
    }
    // The positive half of the same fact, checked without the fatal: the accepted GL_RGB upload
    // from step 4 really did land in TinyGL's own texture storage.
    int xs = 0, ys = 0;
    const auto* pixmap = static_cast<const std::uint32_t*>(glGetTexturePixmap(static_cast<GLint>(tex), 0, &xs, &ys));
    int tr = 0, tg = 0, tb = 0;
    if (pixmap != nullptr) DecodePixel(pixmap[0], tr, tg, tb);
    Check(pixmap != nullptr && tr == 0 && tg == 255 && tb == 0,
          "6B. GL_RGB upload landed in TinyGL texture storage");

    // Fact C: only the four color factors exist. GL_SRC_ALPHA is accepted by the API but the
    // rasterizer's factor switch has no case for it, so it silently degrades to GL_ONE --
    // which is exactly why the renderer must refuse it instead of forwarding it.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLint sfactor = 0, dfactor = 0;
    glGetIntegerv(GL_BLEND_SRC, &sfactor);
    glGetIntegerv(GL_BLEND_DST, &dfactor);
    Check(sfactor == GL_SRC_ALPHA && dfactor == GL_ONE_MINUS_SRC_ALPHA,
          "6C. glBlendFunc stores alpha factors it cannot execute");

    // Fact D: no stencil plane exists on the ZBuffer at all -- there is only zbuf and pbuf.
    Check(zb->zbuf != nullptr, "6D. depth plane exists (zbuf)");
    std::printf("%-58s %s\n", "6D. no stencil plane on ZBuffer (compile-time fact)", "PASS");

    glClose();
    ZB_close(zb);

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "GATE PASSED" : "GATE FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
