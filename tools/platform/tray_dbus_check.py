#!/usr/bin/env python3
"""Check a real CNA tray through a private StatusNotifierWatcher and dbusmenu host.

Usage: run_gpu_tests_private.sh --exec dbus-run-session -- /usr/bin/python3
       tools/platform/tray_dbus_check.py cmake-build-debug/cna_tray_dbus_harness
Requires PyGObject/Gio. This is a real D-Bus integration check, not the SDL seam.
"""
import os
import subprocess
import sys
from gi.repository import Gio, GLib

address = os.environ.get("DBUS_SESSION_BUS_ADDRESS", "")
if not address or "/run/user/" in address:
    raise SystemExit("Refusing a missing or desktop session bus; use dbus-run-session")
if not os.environ.get("CNA_WAYLAND_TEST_DISPLAY"):
    raise SystemExit("Use run_gpu_tests_private.sh")

xml = """<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<method name="RegisterStatusNotifierHost"><arg type="s" direction="in"/></method>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
</interface></node>"""
loop = GLib.MainLoop()
process = None
service = None
bus = None
ready = False
queried = False
failure = None
finished = False

def fail(reason):
    global failure
    failure = str(reason)
    print("FAIL:", failure, flush=True)
    loop.quit()
    return False

def call(path, interface, name, args):
    return bus.call_sync(service, path, interface, name, args, None,
                         Gio.DBusCallFlags.NONE, 3000, None).unpack()

def inspect(updated=False):
    global queried, finished
    if not service or not ready or (queried and not updated):
        return False
    queried = True
    try:
        menu = call("/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
                    GLib.Variant("(ss)", ("org.kde.StatusNotifierItem", "Menu")))[0]
        layout = call(menu, "com.canonical.dbusmenu", "GetLayout",
                      GLib.Variant("(iias)", (0, -1, [])))[1]
        labels = {child[0]: child[1].get("label", "") for child in layout[2]}
        night = "Updated night mode" if updated else "Night mode"
        expected = ["Say hello", night] + [f"Entry {i}" for i in range(40)]
        if sorted(labels.values()) != sorted(expected):
            raise AssertionError(f"Menu labels differ: {labels!r}")
        print("PASS: exact 42 labels", "after update" if updated else "after temporary storage expired", flush=True)
        target = next(k for k, v in labels.items() if v == (night if updated else "Say hello"))
        call(menu, "com.canonical.dbusmenu", "Event",
             GLib.Variant("(isvu)", (target, "clicked", GLib.Variant("s", ""), 0)))
        if updated:
            finished = True
    except Exception as error:
        fail(error)
    return False

def output(fd, condition):
    global ready
    line = process.stdout.readline()
    if not line:
        if not finished:
            fail("harness exited before both menu/callback checks")
        else:
            loop.quit()
        return False
    print("harness:", line.rstrip(), flush=True)
    if line.strip() == "READY":
        ready = True
        GLib.idle_add(inspect)
    if line.strip() == "UPDATED":
        GLib.idle_add(inspect, True)
    return True

def method(connection, sender, path, interface, name, parameters, invocation):
    global service
    if name == "RegisterStatusNotifierItem":
        value = parameters.unpack()[0]
        service = value if value.startswith((":", "org.")) else sender
        GLib.idle_add(inspect)
    invocation.return_value(None)

def get(connection, sender, path, interface, name):
    return {"RegisteredStatusNotifierItems": GLib.Variant("as", [service] if service else []),
            "IsStatusNotifierHostRegistered": GLib.Variant("b", True),
            "ProtocolVersion": GLib.Variant("i", 0)}[name]

def acquired(connection, name):
    global process, bus
    bus = connection
    connection.register_object("/StatusNotifierWatcher", Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0], method, get, None)
    process = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    GLib.io_add_watch(process.stdout.fileno(), GLib.IO_IN | GLib.IO_HUP, output)

Gio.bus_own_name(Gio.BusType.SESSION, "org.kde.StatusNotifierWatcher", Gio.BusNameOwnerFlags.NONE, acquired, None, None)
GLib.timeout_add_seconds(20, fail, "D-Bus regression deadline expired")
try:
    loop.run()
finally:
    if process is not None:
        if failure and process.poll() is None:
            process.terminate()
        try:
            status = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            status = process.wait()
        if status != 0:
            failure = f"harness exited {status}"
if failure or not finished:
    raise SystemExit(failure or "Incomplete integration check")
print("PASS: real D-Bus menu reads, replacement and two callbacks; clean exit", flush=True)
