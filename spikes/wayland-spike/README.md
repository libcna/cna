# `wayland-spike` — existence gates for the native Wayland backend

Proven on this machine on 2026-09-15 for `plans/plan_wayland.md` Phase A. Nothing here is built
by CMake; each file's header names its one-line compile command.

| File | Task | What it proved |
|---|---|---|
| `registry_dump.c` | WAYLAND-0001 | Every global and version a compositor advertises. Run against GNOME 48 (`WAYLAND_DISPLAY=wayland-0`), Weston 14 headless and headless gnome-shell 48; the table is plan §0.2. Mutter offers no `zxdg_decoration_manager_v1`; Weston headless has no `wl_seat`. |
| `output_dump.c` | WAYLAND-0001 | What each `wl_output` (v4) and `wl_seat` reports. The desktop has two 2560x1440 outputs at `wl_output.scale` 2 whose logical positions (0 and 2048) show the real 125 % fractional scale. |
| `remote_desktop_probe.c` | WAYLAND-0003 | A headless gnome-shell on a **private** session bus accepts an `org.gnome.Mutter.RemoteDesktop` session; once started, that compositor's seat gains a pointer (after a relative-motion notification) and loses it on `Stop`. This is how the Mutter test suites get deterministic input without touching the user's desktop. The session lives as long as the creating bus connection -- `gdbus call` alone cannot hold one open. |

The built probes are gitignored.
