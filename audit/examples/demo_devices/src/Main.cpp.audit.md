# Audit: examples/demo_devices/src/Main.cpp

## Metadata
- Source file: `examples/demo_devices/src/Main.cpp` (18 lines)
- Audit status: AUDITED (full read; confirmed byte-for-byte identical to the Android jni copy via `diff`)
- Subsystem: `examples-demo_devices` shard
- File type: example demo entry point
- XNA/FNA relevance: none (pure `main()`/`SDL_main` wiring)
- Main related tests: none

## Purpose
Standard `Game`-subclass entry point: constructs `DevicesDemo`, runs it, deletes it. The
`<SDL3/SDL_main.h>` include and accompanying comment explain the Android `SDL_main` symbol-lookup
requirement (`SDLActivity.java` uses `dlsym()` to find a literal `SDL_main` symbol) that makes this
plain `main()` do double duty as the Android entry point via a `#define`.

## Executive Verdict
Correct, minimal, and identical between the desktop and Android jni copies of this demo — no drift
here (unlike the sibling `DevicesDemo.cpp`/`.hpp`, see that report).

## Checklist Results
- `new DevicesDemo()` / `game->Run()` / `delete game` — the standard pattern used consistently
  across every other example demo in this project.
- The Android `SDL_main` explanatory comment is accurate and specific (cites the exact mechanism —
  `dlsym()` symbol lookup by `SDLActivity.java` — rather than a vague "needed for Android" note).

## Detailed Findings
None.

## Cross-File Observations
Confirmed via direct `diff` to be byte-for-byte identical to
`android/com.openeggbert.cna.demodevices/app/jni/src/Main.cpp` — the one file in this demo's
desktop/Android pair that has NOT drifted (contrast with `DevicesDemo.cpp`/`.hpp`, which have).

## Missing or Weak Tests
N/A — trivial entry point.

## Positive Findings
The Android `SDL_main` comment is a good example of documenting a genuinely non-obvious
cross-platform mechanism precisely rather than vaguely.

## Final Assessment
No findings.
