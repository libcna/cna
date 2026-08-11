/* Standalone existence-gate spike: prove ShivaVG (OpenVG impl) renders a real
 * pixel via a GLX context under Xvfb, before wiring it into CNA. Not part of
 * the CNA build -- ad hoc validation only. */
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include "openvg.h"
#include "vgu.h"

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
    Window win = XCreateWindow(dpy, root, 0, 0, 64, 64, 0, vi->depth, InputOutput,
                                vi->visual, CWColormap | CWBorderPixel, &swa);

    GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
    if (!ctx) { fprintf(stderr, "glXCreateContext failed\n"); return 1; }
    if (!glXMakeCurrent(dpy, win, ctx)) { fprintf(stderr, "glXMakeCurrent failed\n"); return 1; }

    if (!vgCreateContextSH(64, 64)) { fprintf(stderr, "vgCreateContextSH failed\n"); return 1; }

    /* Clear to a distinctive color, draw a filled square via vgu, read back a pixel. */
    VGfloat clearColor[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
    vgClear(0, 0, 64, 64);

    VGPaint paint = vgCreatePaint();
    vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    VGfloat red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    vgSetParameterfv(paint, VG_PAINT_COLOR, 4, red); /* opaque red */
    vgSetPaint(paint, VG_FILL_PATH);

    VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                                1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);
    vguRect(path, 16, 16, 32, 32);
    vgDrawPath(path, VG_FILL_PATH);
    vgFinish();

    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("center pixel RGBA = %d %d %d %d\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    unsigned char corner[4] = {0, 0, 0, 0};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    printf("corner pixel RGBA = %d %d %d %d\n", corner[0], corner[1], corner[2], corner[3]);

    int ok = 1;
    if (!(pixel[0] > 200 && pixel[1] < 50 && pixel[2] < 50)) { fprintf(stderr, "FAIL: center not red\n"); ok = 0; }
    if (!(corner[2] > 60 && corner[2] < 100)) { fprintf(stderr, "FAIL: corner not clear-blue-ish\n"); ok = 0; }

    vgDestroyPath(path);
    vgDestroyPaint(paint);
    vgDestroyContextSH();
    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, ctx);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    printf(ok ? "SMOKE TEST PASSED\n" : "SMOKE TEST FAILED\n");
    return ok ? 0 : 1;
}
