# Win32 platform existence-gate spike

`plans/plan_win32.md` **WIN32-0000**. Written and run before any `modules/platform/src/Win32/`
code existed, to establish that the native Win32 backend is implementable *and testable* in this
repository's development environment rather than only compilable.

## What it proves

| Claim | Measured |
|---|---|
| `RegisterClassExW` + `CreateWindowExW` + a real `WndProc` work | yes |
| `WindowDescription::width/height` is the **client** size and needs `AdjustWindowRectEx` | `client=640x480` for a requested 640×480 client area; the naive form (passing 640×480 as the outer size) measured `632x446` |
| `WM_CLOSE` reaches the window procedure and can be suppressed | `close=1`, no destroy — this is what makes `WindowEventKind::CloseRequested` a *request* |
| `PeekMessageW`/`TranslateMessage`/`DispatchMessageW` pump | `pumped=2` |
| `QueryPerformanceFrequency` answers | `qpf=10000000` |
| `GetDpiForWindow` answers | `dpi=96` |

## Reproducing

```sh
x86_64-w64-mingw32-g++ -std=c++23 -static -o win32_platform_probe.exe \
    win32_platform_probe.cpp -luser32 -lgdi32
Xvfb :97 -screen 0 1280x1024x24 &
DISPLAY=:97 WINEDEBUG=-all wine64 ./win32_platform_probe.exe
```

Exit status 0 means every claim above held. Built binaries are gitignored; the source and this
record are what stay.

Wine is not Windows: `dpi=96` here is Wine's fixed answer, not evidence that per-monitor DPI
transitions work. `plans/plan_win32.md` §16 records that split.
