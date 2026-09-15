// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0001: what each wl_output (v4) and wl_seat reports, so the
// multi-monitor and scaling rows of the plan start from the real desktop's numbers.
//
//   cc -O2 output_dump.c -o output_dump $(pkg-config --cflags --libs wayland-client)
//   WAYLAND_DISPLAY=wayland-0 ./output_dump

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

static void geometry(void* d, struct wl_output* o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub,
                     const char* make, const char* model, int32_t transform)
{
    (void)o;
    (void)sub;
    printf("  output %s: geometry x=%d y=%d physical=%dx%dmm make=%s model=%s transform=%d\n", (const char*)d, x, y, pw,
           ph, make, model, transform);
}
static void mode(void* d, struct wl_output* o, uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
    (void)o;
    if (flags & WL_OUTPUT_MODE_CURRENT)
        printf("  output %s: current mode %dx%d @ %.3f Hz\n", (const char*)d, w, h, refresh / 1000.0);
}
static void done(void* d, struct wl_output* o) { (void)d; (void)o; }
static void scale(void* d, struct wl_output* o, int32_t s)
{
    (void)o;
    printf("  output %s: integer scale %d\n", (const char*)d, s);
}
static void name(void* d, struct wl_output* o, const char* n)
{
    (void)o;
    printf("  output %s: name %s\n", (const char*)d, n);
}
static void description(void* d, struct wl_output* o, const char* n)
{
    (void)o;
    printf("  output %s: description %s\n", (const char*)d, n);
}
static const struct wl_output_listener output_listener = {geometry, mode, done, scale, name, description};

static void seat_caps(void* d, struct wl_seat* s, uint32_t caps)
{
    (void)d;
    (void)s;
    printf("  seat capabilities: pointer=%d keyboard=%d touch=%d\n", !!(caps & WL_SEAT_CAPABILITY_POINTER),
           !!(caps & WL_SEAT_CAPABILITY_KEYBOARD), !!(caps & WL_SEAT_CAPABILITY_TOUCH));
}
static void seat_name(void* d, struct wl_seat* s, const char* n)
{
    (void)d;
    (void)s;
    printf("  seat name %s\n", n);
}
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

static char labels[8][16];
static int count;

static void on_global(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version)
{
    (void)data;
    if (strcmp(interface, "wl_output") == 0 && count < 8 && version >= 4)
    {
        snprintf(labels[count], sizeof labels[count], "#%u", id);
        struct wl_output* output = wl_registry_bind(registry, id, &wl_output_interface, 4);
        wl_output_add_listener(output, &output_listener, labels[count]);
        ++count;
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        struct wl_seat* seat = wl_registry_bind(registry, id, &wl_seat_interface, version < 8 ? version : 8);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    }
}
static void on_global_remove(void* data, struct wl_registry* registry, uint32_t id)
{
    (void)data;
    (void)registry;
    (void)id;
}
static const struct wl_registry_listener listener = {on_global, on_global_remove};

int main(void)
{
    struct wl_display* display = wl_display_connect(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "no Wayland display\n");
        return 1;
    }
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return 0;
}
