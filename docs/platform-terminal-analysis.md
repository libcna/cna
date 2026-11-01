# Terminal platform: implemented behavior and limits

The former PLAT-5b feasibility proposal became the TERMINAL implementation (PLAT-129…140,
with the later TCR presenter repair). This page describes current source on 2026-11-01.
It demonstrates a non-SDL output/input model, but does not make terminal input equivalent to
a graphical desktop or promise deployment on every POSIX host.

## Selection and presentation route

Select CNA_PLATFORM=TERMINAL and a compatible renderer. SOFTWARE produces visible CPU frames
through IPlatformSurfacePresenter; HEADLESS/STUB are compatible windowless choices, not ways to
display a game. GPU families requiring native windows/GL services are refused.

The implementation uses termios, descriptors, ioctl and signals and is excluded on Windows.
A Windows console provider is not implemented. Compiler/platform availability does not remove
CNA's C++23/toolchain requirements. An SDL-free composition additionally selects SDL-free audio
and renderer options; TERMINAL alone changes only the platform axis.

The visible path is:

```text
Game → GraphicsDevice → SOFTWARE RGBA8 frame
                    → TerminalSurfacePresenter → quantized cells → ANSI output
```

GraphicsDevice owns the presenter and its virtual window. SOFTWARE's descriptor requests a
presenter and Present hands over its completed/resolved buffer without a readback-copy pipeline.
The device creates a CPU window when surfacePresentation is available and nativeWindowHandle is
not: the presenter is that platform's only display route. No terminal-specific renderer identity
or per-primitive platform drawing API is needed.

TerminalFrameGrid implements its own quantization in modules/platform/src/Terminal, using the
same luma weights and ten-glyph ramp as the ASCII effect. It does not link graphics-ext's
QuantizeFrameToGrid or use arbitrary renderers' ReadBackbuffer as the presentation contract.
The retired ASCII renderer/effect history is in [ASCII post-processing](ascii-post-process-effect.md).

## Capability and virtual window behavior

Construction checks whether the output descriptor is a TTY without taking it over. Full capability
detection is lazy: it temporarily probes the terminal and caches the result. Services/leases are
constructed only after successful allocations. A non-TTY output reports no presentation and
CreateSurfacePresenter refuses rather than emitting ANSI frames into a log.

| Current capability/service | Boundary |
|---|---|
| surfacePresentation | True only for a TTY output descriptor |
| exactKeyboardState | True only after the required Kitty enhancements are detected |
| Keyboard/mouse services | Available when terminal input can be queried; lower-quality services may exist while quality flags are false |
| pixelAccurateMouse, relativeMouse, cursorShapes, globalPointer | False |
| multipleWindows, nativeWindowHandle, openGlContext | False |
| TextInput/IME, clipboard/OSC 52, device enumeration, gamepad/joystick, haptic/sensor, camera, tray/dialog/display services | Not supplied; accessors return null |
| Filesystem/system information | Portable common services, with ordinary unknown-value limits |

Only one live virtual window is allowed per platform instance. It has a stable WindowId and
NativeWindowSystem::Terminal with no native pointer payload. Initial bounds preserve the game's
requested pixel resolution; they are not automatically replaced by the terminal's cell grid.
SetSize is a request observed on Sync.

On SIGWINCH, PollEvents coalesces pending changes, reads the current grid and emits both Resized
and PixelSizeChanged. The new virtual size uses nominal 8×16 units per cell with overflow clamps.
This is an approximation, not a measurement of the emulator's font/DPI. Display scale is one.
The presenter independently queries the current cell grid; an unavailable/zero grid falls back
to 80×24.

## Output, scaling and bandwidth

Presentation supports Stretch, Letterbox, Overscan, None and Native placement/cropping. Aspect
preservation assumes cells twice as tall as wide; PresentFilter does not add a separate filtering
mode, and SetVSync returns false.

The ANSI writer tracks changed cells and omits redundant color changes. Color depth is selected
from terminal environment heuristics: truecolor, indexed 256, ANSI 16 or monochrome. This is not a
universal protocol-negotiated color guarantee. Initial presentation, resizing and session-generation
changes force a full redraw.

A five-observation median write-rate estimate refills a bounded byte budget. An unaffordable diff
is dropped without updating the remembered on-screen grid, so the next diff includes all changes
since the last actual write. An unchanged frame emits nothing. Full redraws bypass dropping.

This reduces avoidable output; it does not make writes nonblocking or bound every frame's latency.
The first/full redraw can block on a slow TTY, and measured write duration reflects host buffering/
scheduling as well as transport. SSH/tmux behavior and throughput need their own qualification.
Presenter teardown reports color/grid/dropped-frame/Kitty diagnostics to stderr after releasing
its terminal lease. Keep logging and other output away from the ANSI frame channel on stdout.

## Keyboard and mouse

The capability probe first asks for device attributes, then pushes Kitty flags 15, queries the
active enhancements and pops the stack. Merely seeing any Kitty response is insufficient.
A session also uses flags 15: disambiguation, event types, alternate keys and all keys as CSI-u.
The old proposal's flags-1 example would not provide complete key-release reporting.

Kitty press/repeat/release events support held-key snapshots. Without them, the legacy decoder
synthesizes release 100 ms after the latest press/repeat, and waits 30 ms to disambiguate a lone
Escape from a split sequence. The repeat delay can make a held key appear released; taps can
appear held briefly. exactKeyboardState remains false. Keys/modifiers/encodings the terminal
cannot distinguish are not recoverable from this heuristic.

SGR-1006 mouse reporting supplies button, movement and wheel events at cell granularity,
expanded into nominal client coordinates. It cannot warp the emulator's graphical pointer.
TerminalMouse rejects SetPosition/cursor visibility changes, and refuses cursor shapes, relative
mode and global-pointer controls with the appropriate platform exceptions. Relative displacement
is empty. Keyboard and mouse share one decoder/read path so one consumer does not steal the
other's byte stream. There is no native controller/sensor channel composed into this provider.

## Session ownership and restoration

Presentation and input use leases on one shared process terminal session. Their combined options
control raw input, alternate screen, hidden text cursor, mouse reporting and Kitty mode. A changed
option union rebuilds the session; the presenter detects its generation and invalidates its diff.
Subsystem acquire/release itself is bookkeeping and does not take over the terminal.

Session teardown restores saved termios and emits the epilogue. Signal handlers cover INT, TERM,
HUP, QUIT, ABRT, SEGV, BUS, FPE and ILL, restoring before re-raising through the previous action;
the terminate handler restores before delegating. SIGKILL, power loss, a blocked/broken output or
arbitrary memory corruption cannot be promised recoverable. Restoration tests are bounded
evidence for their exercised paths, not a guarantee that every crash can repair a user's shell.

## Verification and use

Terminal* platform suites cover pseudo-TTY probing, keyboard/mouse decoding, shared sessions,
normal/signal/terminate restoration, resizing, grid/ANSI output and frame budgets. Run them through
the private runner even though they create their own pseudo-terminals:

```sh
tools/platform/run_gpu_tests_private.sh --exec cmake-build-multi/CnaPlatformModuleTests \
  '--gtest_filter=Terminal*.*'
```

TerminalSoftwareDemoIntegration is registered only when tests/examples are enabled and the
configured default pair is TERMINAL/SOFTWARE. Its runner owns a pseudo-TTY, launches cna_demo_2d,
injects keyboard/resize input and checks restoration/diagnostics. It is enabled after the TCR
repair; the former DISABLED registration was historical. The 2026-11-01 documentation audit ran
the focused pseudo-TTY suites, not a new terminal-demo build or remote/emulator campaign.

See [platform contract](platform-abstraction.md), [input behavior](platform-input-notes.md) and
[renderer boundary](platform-renderer-boundary.md) for shared contracts.
