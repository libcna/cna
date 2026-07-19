# Audit: include/Microsoft/Xna/Framework/Game.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Game.hpp`
- Audit status: AUDITED (full read, 379 lines, header-only declarations)
- Subsystem: `xna-framework-core` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type (`Microsoft.Xna.Framework.Game`); FNA reference:
  `src/Game.cs` (1003 lines, read in full for this audit)
- Main related tests: not independently located in this pass

## Purpose
Declares `Game`, the XNA application base class: component/content/service/window ownership,
the fixed/variable timestep loop (`Tick`/`Run`/`RunOneFrame`), and the
`Initialize`/`LoadContent`/`UnloadContent`/`Update`/`Draw` virtual lifecycle hooks.

## Executive Verdict
Needs attention. The declared surface matches FNA's public/protected contract closely, and the
timestep-loop member layout (`previousSleepTimes_`, `worstCaseSleepPrecision_`,
`updateFrameLag_`, `PREVIOUS_SLEEP_TIME_COUNT`/`SLEEP_TIME_MASK`) is a faithful structural port.
However, direct comparison of this class's actual lifecycle wiring against FNA's real
`Initialize()`/`Dispose()` (detailed fully in the paired `.cpp` report, since the header only
declares the hooks) uncovered a confirmed HIGH-severity finding: `UnloadContent()` — declared here
at line 253 as an overridable protected hook, exactly like FNA's — is **never invoked anywhere by
the framework**, unlike FNA, which explicitly wires `graphicsDeviceService.DeviceDisposing += (o,
e) => UnloadContent();` in `Initialize()`. A game overriding `UnloadContent()` per the documented
XNA lifecycle contract will have that override silently never called by CNA.

## Checklist Results

### HIGH: `UnloadContent()` declared as a lifecycle hook but never wired to fire (see .cpp for full evidence)
Line 253 declares `virtual void UnloadContent();` with the doc comment "Unloads graphics and
content resources," matching FNA's documented contract precisely (`Game.cs` line 619). The
implementation-side confirmation that this hook is genuinely dead code — no call site anywhere in
`Game.cpp`, and no subscription analogous to FNA's `DeviceDisposing += UnloadContent` — is in
`src/Microsoft/Xna/Framework/Game.cpp.audit.md`.

### MEDIUM: `PollEvents()`'s declared responsibility is narrower than the SDL event set FNA's platform layer actually reacts to
This is a private (undeclared-in-header, `void PollEvents();` at line 361) method, so the header
itself gives no visible indication of scope; the gap is only visible by comparing the
implementation against FNA's real `SDL3_FNAPlatform`'s event loop. Full detail in the `.cpp`
report.

## Detailed Findings
1. **[HIGH] `UnloadContent()` is a dead virtual hook — never invoked by the framework** — declared
   line 253; full evidence in `src/Microsoft/Xna/Framework/Game.cpp.audit.md`.
2. **[MEDIUM] `PollEvents()` omits several FNA SDL3 event handlers with observable
   gameplay/OS-integration consequences** — declared line 361; full detail in the `.cpp` report.

## Cross-File Observations
- `setContentProperty(const Content::ContentManager& value)` (line 86) takes the new content
  manager by `const&` and (per the `.cpp`) assigns it via copy-assignment — worth confirming
  `Content::ContentManager`'s copy-assignment semantics are sound (i.e. does not leak or
  double-release loaded-asset state) when that class is audited under the `xna-content` shard.
- `RunApplication` (line 231, `NOXNA`) is correctly tagged as a CNA-visible internal loop flag
  matching FNA's own internal field of the same name (FNA's is `private`; CNA exposes it,
  presumably for the Emscripten main-loop callback's use — reasonable, though the header doesn't
  explicitly note *why* it needs public visibility here).

## Missing or Weak Tests
Not independently located in this pass. A test subclassing `Game`, overriding `UnloadContent()`
with an observable side effect (e.g. a counter), disposing the owned `GraphicsDeviceManager` or
`Game` itself, and asserting the counter incremented, would directly and unambiguously catch
finding #1.

## Positive Findings
The timestep-loop member layout is a faithful, careful structural port of FNA's own
sleep-precision-estimation fields, and the Emscripten-specific `EmscriptenLoopState`/
`EmscriptenMainLoopCallback` declarations (lines 368-376) are cleanly isolated behind
`#if defined(__EMSCRIPTEN__)` with no leakage into the non-Emscripten API surface.

## Final Assessment
One HIGH finding (dead `UnloadContent()` hook) and one MEDIUM finding (incomplete `PollEvents()`
event coverage relative to FNA) — both detailed fully in the paired `.cpp` audit report.
