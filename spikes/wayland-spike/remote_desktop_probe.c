// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0003: existence gate for the Mutter test environment. A headless
// gnome-shell on a PRIVATE session bus (never the desktop's) takes input from
// org.gnome.Mutter.RemoteDesktop; this proves a session can be created and started, after which
// the compositor's wl_seat gains a keyboard and a pointer that exist only inside that compositor.
// The session lives as long as this process's bus connection, so the probe keeps it open.
//
//   cc -O2 remote_desktop_probe.c -o remote_desktop_probe $(pkg-config --cflags --libs dbus-1)
//   DBUS_SESSION_BUS_ADDRESS=unix:path=<private bus> ./remote_desktop_probe <seconds>

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusMessage* call(DBusConnection* bus, const char* path, const char* iface, const char* method,
                         DBusMessage* prepared)
{
    DBusMessage* message =
        prepared != NULL ? prepared
                         : dbus_message_new_method_call("org.gnome.Mutter.RemoteDesktop", path, iface, method);
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(bus, message, 5000, &error);
    dbus_message_unref(message);
    if (reply == NULL)
    {
        fprintf(stderr, "%s.%s failed: %s\n", iface, method, error.message);
        dbus_error_free(&error);
    }
    return reply;
}

int main(int argc, char** argv)
{
    const int seconds = argc > 1 ? atoi(argv[1]) : 5;
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (bus == NULL)
    {
        fprintf(stderr, "no session bus: %s\n", error.message);
        return 1;
    }
    DBusMessage* reply =
        call(bus, "/org/gnome/Mutter/RemoteDesktop", "org.gnome.Mutter.RemoteDesktop", "CreateSession", NULL);
    if (reply == NULL)
        return 1;
    const char* sessionPath = NULL;
    dbus_message_get_args(reply, NULL, DBUS_TYPE_OBJECT_PATH, &sessionPath, DBUS_TYPE_INVALID);
    char session[256];
    snprintf(session, sizeof session, "%s", sessionPath);
    dbus_message_unref(reply);
    printf("session %s\n", session);

    reply = call(bus, session, "org.gnome.Mutter.RemoteDesktop.Session", "Start", NULL);
    if (reply == NULL)
        return 1;
    dbus_message_unref(reply);
    printf("started\n");

    // Relative motion needs no screen-cast stream; it is the one pointer notification that does not.
    DBusMessage* motion = dbus_message_new_method_call("org.gnome.Mutter.RemoteDesktop", session,
                                                       "org.gnome.Mutter.RemoteDesktop.Session",
                                                       "NotifyPointerMotionRelative");
    double dx = 5.0, dy = 3.0;
    dbus_message_append_args(motion, DBUS_TYPE_DOUBLE, &dx, DBUS_TYPE_DOUBLE, &dy, DBUS_TYPE_INVALID);
    reply = call(bus, session, "org.gnome.Mutter.RemoteDesktop.Session", "NotifyPointerMotionRelative", motion);
    if (reply != NULL)
    {
        dbus_message_unref(reply);
        printf("relative motion accepted\n");
    }
    fflush(stdout);
    sleep((unsigned)seconds);
    reply = call(bus, session, "org.gnome.Mutter.RemoteDesktop.Session", "Stop", NULL);
    if (reply != NULL)
        dbus_message_unref(reply);
    dbus_connection_close(bus);
    dbus_connection_unref(bus);
    return 0;
}
