// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0001: lists every global a compositor advertises, with its
// version, so the protocol inventory in the plan is measured rather than assumed. Binds nothing.
//
//   cc -O2 registry_dump.c -o registry_dump $(pkg-config --cflags --libs wayland-client)
//   WAYLAND_DISPLAY=wayland-0 ./registry_dump

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

static void on_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface,
                      uint32_t version)
{
    (void)data;
    (void)registry;
    printf("%-3u %-48s v%u\n", name, interface, version);
}

static void on_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)data;
    (void)registry;
    printf("removed %u\n", name);
}

static const struct wl_registry_listener listener = {on_global, on_global_remove};

int main(void)
{
    struct wl_display* display = wl_display_connect(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "no Wayland display (WAYLAND_DISPLAY unset or unreachable)\n");
        return 1;
    }
    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
