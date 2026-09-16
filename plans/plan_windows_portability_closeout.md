# Windows portability — closing pass

The Windows workstream (`plans/plan_win32.md`, `plans/plan_win32_native_validation.md`,
`plans/plan_windows_portability.md`) is finished apart from a small, bounded set of known gaps.
This file closes them and records the integration into `next`. It is deliberately short: it is a
closeout, not another expansion plan.

**Baselines, measured rather than assumed (2026-09-16):**

| | |
|---|---|
| CNA `windows-portability` | `9aab60f093c6e8cc710d608415b4cf88926e9e18` |
| CNA `origin/next` | `4cf33c2b7968cb0d335e18faf2d9c0c32d730a0e` — 117 behind, **0 ahead**, so a fast-forward exists |
| sharp-runtime `next` | `a655630fa796402dfe93007e4daccb355250b5cf` (already pushed) |
| Windows VM | `win10_local`, Windows 10 22H2 19045.2965, MSVC 19.44, clean checkout at CNA HEAD |
| Working trees | both clean at the start of this pass |

## Tasks

| ID | Item | Status |
|---|---|---|
| WINCLOSE-0001 | sharp-runtime and CNA did not build together at their two HEADs | ✅ fixed |
| WINCLOSE-0002 | F29 — WGL passes standalone, access-violates in the full suite | ✅ explained and closed — CNA's WGL validated; the residual fault is the guest GPU driver in F24's state |
| WINCLOSE-0003 | F31 — `RunHostProcess`'s Windows `CreateProcessW` path has no test behind it | ✅ fixed |
| WINCLOSE-0004 | Three Windows text-mode failures (`"hello\r"`) | ✅ fixed |
| WINCLOSE-0005 | UNC: `SongTest.FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt` | ✅ fixed |
| WINCLOSE-0006 | Backslash semantics in an external content dependency | ✅ fixed |
| WINCLOSE-0007 | Native Windows full suite, repeated | ✅ three identical runs |
| WINCLOSE-0008 | SDL-free + Unicode-path + ASan re-proof | ✅ done; D3D11 ⛔ environment |
| WINCLOSE-0009 | Linux / X11 / Wayland / SDL matrix regression | ✅ zero new failures |
| WINCLOSE-0010 | Git authorship audit and integration into `next` | ✅ clean, fast-forwarded |
| WINCLOSE-0011 | **Post-closeout:** DirectX11 drew nothing after the first `Present` | ✅ fixed — found by running the demo, see below |

---

## WINCLOSE-0001 — the two HEADs did not build together *(fixed)*

**Symptom.** The first full MSVC build of this closing pass failed outright:

```
XmlReader.cpp(12): error C2220: the following warning is treated as an error
XmlReader.cpp(12): warning C4005: 'NOMINMAX': macro redefinition
XmlReader.cpp(12): note: 'NOMINMAX' previously declared on the command line
```

**Root cause.** sharp-runtime's newest commit (`a655630f`) defined `WIN32_LEAN_AND_MEAN` and
`NOMINMAX` unconditionally before `<windows.h>`. CNA compiles the whole tree with `-DNOMINMAX` and
with `/W4 /WX`, so the redefinition is an error. Every other Windows translation unit in
sharp-runtime already uses the `#ifndef` guard — `Environment.cpp`, from the same commit series,
included. `XmlReader.cpp` was the one that missed it.

This is worth recording beyond its one-line fix: **the two repositories' final states had never
been built together.** Each workstream validated its own repository, and the pair was broken at the
exact commits this pass was asked to close. Mutual compatibility is not implied by two green
repositories.

**Fix.** The house `#ifndef` guard, sharp-runtime `827c34fc5708949943db3e7dcda9c76428e299d7`.
Linux is unaffected — the block is inside `#if defined(_WIN32)`.

---

## WINCLOSE-0002 — F29, the WGL crash: which side of the boundary

The preceding workstream recorded F29 with an explicit admission: WGL "works in isolation and
crashes in company, and **which side of the boundary the fault is on has not been established**".
It is established now, by measurement rather than by suspicion.

**Reproduced first, unchanged.** `Win32GraphicsServices.AContextEitherIsCreatedAndUsableOrFails-`
`Explicitly`, in the full `CnaTests` run on native Windows 10 (MSVC, 8 187 tests):

```
SEH exception with code 0xc0000005 thrown in the test body.
```

**The isolation, by binary search over test order.** The suite runs 837 test suites; 754 of them
precede this one. Bisecting the prefix that has to run first:

| Preceding suites | WGL |
|---|---|
| none (the test alone, from the same binary) | **passes** — 13 passed, 2 Vulkan skips |
| the 5 neighbouring `Win32*` suites | passes |
| every `GraphicsDevice*` / `RenderTarget*` / `Renderer*` / `Presentation*` suite | passes |
| 47 / 70 / 82 / 85 | passes |
| **86** | **crashes** |

The 86th suite is `GltfConformanceL6`, and `GltfConformanceL6.* : Win32GraphicsServices.*` alone
is a complete reproduction. But no single test of it reproduces, and the pure-maths tests in it do
not either — so it is not a poisoning event, it is cumulative.

**What is accumulating, pinned to one test.** Run alone, `GltfConformanceL6` is 15 passed then 11
failed — every test after the 15th fails, and the one that turns (`EveryImportedPartOwnsIts-`
`BuffersSoItsOffsetsAreZero`) spends 4 438 ms doing it, which is a device creation timing out
rather than an assertion. In the full run the boundary sits at 12 instead of 15, because other
suites had already spent some of the same budget. That is the F24 state, reached from inside one
suite.

The decisive experiment is one test wide:

| Filter | Graphics stack | WGL |
|---|---|---|
| the first **15** `GltfConformanceL6` tests + WGL | still healthy — all 15 pass | **passes** |
| the first **16** + WGL | the 16th is the first that cannot get a device | **crashes** |

**One test is the entire difference**, and that test is the one at which the virtual GPU stops
handing out Direct3D 11 devices. WGL does not fail because of what ran before it; it fails because
the graphics stack underneath it has already stopped working.

**Whose leak is it?** The same single test, repeated 20 times in one process with
`--gtest_repeat`: **7 passed, 13 failed.** Identical work, same order, stops working after seven
create/destroy cycles — so something is not being given back per device. CNA's own lifetime is
deterministic here: `GraphicsDevice::~GraphicsDevice()` calls `Dispose()`, which disposes owned
resources, calls `destroyNativeResources()` and releases the video subsystem. Whether the
DirectX11 family's COM references balance underneath that is **F24**, which this closing pass was
instructed not to open, and which this measurement now describes far better than it was described
before.

**What this means for F29 specifically.** The fault is not in CNA's WGL code:

- the WGL test passes alone, and passes after 15 device create/destroy cycles;
- only `DIRECTX11` is compiled into this build, so no other CNA code creates a GL context and
  none can have left one current;
- `Win32GlContext` checks the result of `wglCreateContext`, `wglCreateContextAttribsARB`,
  `wglMakeCurrent` and every `GetDC`, and throws rather than continuing;
- the access violation arrives *inside* the guest's OpenGL implementation (Mesa SVGA3D over
  VBoxSVGA), earlier in the test than a healthy run gets (25 ms against 60 ms), before CNA has a
  return value to test.

A crash inside a third-party DLL cannot be null-checked away from outside it, and wrapping GL
entry points in SEH to survive a virtual driver would be distorting CNA's semantics to
accommodate an objectively broken one. So the honest verdict is: **CNA's WGL implementation is
not implicated; the remaining full-suite crash is the virtual GPU driver faulting once the VM's
device budget is exhausted.**

### A hypothesis that was measured and rejected

Worth recording because the reasoning was right and the answer was still no.

`~DirectX11Renderer()` reset `lifetimeToken_`, destroyed the MojoShader context, and left its
`ComPtr` members to unwind on their own — no `ClearState()`, no `Flush()`, no explicit release of
the swap chain. The swap chain is `DXGI_SWAP_EFFECT_FLIP_DISCARD`, and a flip-model swap chain is
not destroyed while the immediate context still references its buffers: the runtime defers it, and
holds the device with it. The same file already performs exactly that sequence in
`RecreateDeviceEXT()` and in `EnsureSwapChainSize()`, for exactly that reason. Every part of that
reading was verified in the source.

It is not what exhausts the budget. With the destructor doing
`ReleaseWindowSizeDependentViews()` → `ClearState()` → `Flush()` → release, the measurement is
**7 passed / 13 failed** — identical to without it. Reverted (`bfd05ebda`) rather than kept: F24 is
not this pass's scope, and a renderer teardown edit whose own measurement contradicts its argument
is the speculative patch this work was told not to make.

**The more useful half of that dead end** is the control. `tools/platform/standalone_tests/`
`win32_d3d11_cycle.cpp` was written to reproduce F24 and ran 400 cycles clean — but it builds a
`DXGI_SWAP_EFFECT_DISCARD`, `BufferCount = 1` swap chain, which is the BLT model, and it already
calls `ClearState()`/`Flush()` before releasing. It cannot exercise the renderer's actual
flip-model teardown by construction. **A control that cannot fail is why F24 has stayed "reproduced
by neither control"**, and whoever opens F24 should fix the control first.

**What CNA gained anyway.** `ContextLifetimeSurvivesEveryOwnershipPathWithoutLeavingStaleState`
(`c0d8f2d82`) walks every ownership path the service has in one process — repeated create/destroy,
destroy-while-current, two contexts on one window, a context per window on two windows, and a
window destroyed before the context that was current on it — and then creates one more context and
swaps it, so a leaked `HGLRC`, a released `HDC` or a stale current binding is reported here rather
than as a fault in an unrelated test later. That is the audit Phase 5 asks for, kept as a test
rather than as a paragraph.

---

## WINCLOSE-0003 — F31, the untested `CreateProcessW` path *(fixed)*

**Symptom.** Eighteen Windows-only failures, and behind them the finding the preceding workstream
asked to be carried forward: `RunHostProcess` has a complete Windows implementation — command-line
construction, `CommandLineToArgvW` quoting, UTF-8 → UTF-16, a threaded pipe drain, exit status —
and **no test exercised any of it**, because every `HostProcessTest` asked for `/bin/echo` or
`/bin/sh` and got `started == false` on Windows.

**Root cause of the gap, not of a bug.** Testing argument quoting needs a child that reports the
argv it actually received. Routing through `cmd.exe` would measure cmd's quoting instead, which is
why the earlier workstream recorded this rather than doing it badly.

**Fix.** `tools/content/argv_echo.cpp`, next to the existing `cna_fake_effect_compiler` and linking
nothing for the same reason. Two details are what make it trustworthy:

- it reads arguments from `CommandLineToArgvW(GetCommandLineW())`, not the CRT's narrow `argv`,
  which is converted through the ANSI code page and cannot spell the characters the Unicode cases
  exist to test — and `CommandLineToArgvW` is the exact contract `QuoteArgument` names;
- it puts stdout in **binary** mode, because text mode would expand `\n` to CRLF and make the
  length prefixes disagree with the bytes. That is the F27 defect, in the one place that would have
  made these tests lie about what they measured.

**Coverage**, on both platforms: no arguments at all; spaces; embedded quotes; backslashes,
trailing backslashes and the backslash-before-quote case the doubling rule exists for; an empty
argument; tab and newline; `;&|^%` characters a shell would have eaten; non-ASCII arguments spelled
so no single Windows ANSI code page can represent them; an executable under a non-ASCII directory;
a non-zero exit; a failed lookup; the over-a-pipe-buffer drain, which on Windows is a thread that
had never run under test; and handle hygiene across 40 launches.

**Two defects it found**, both on the Windows path:

| | |
|---|---|
| the two `CreatePipe` calls shared one `if`, so the output pipe's two handles leaked whenever the **second** call failed — the path a process near the handle limit actually takes | fixed |
| `Narrow()` was dead, kept alive only by a `static_cast<void>` of its own name | removed |

**Deliberately not tested:** working directory and environment. `RunHostProcess` takes neither —
"no shell, no environment manipulation, no streaming, no timeout" — and a test for a feature the
API does not claim describes something that does not exist.

**Commit** `1470ae208`. Linux: all 10 `HostProcessTest` cases pass.

---

## WINCLOSE-0004 — the three text-mode failures *(fixed)*

**Symptom.** `XnaPipelineBridge` ×2 and `XnaContentImporter` ×1 compared against `"hello"` and got
`"hello\r"`.

**Root cause.** Entirely test-side, and an inconsistency rather than a platform assumption: the
fixtures were **written** with a bare `std::ofstream` (text mode, so the Windows CRT expands `\n`
to CRLF) and **read** by the test's own `GreetingImporter` with `std::ios::binary` and
`std::getline`, which splits on `\n` and leaves the `\r`.

**Contract decision.** These bytes are the measurement, not console output, so binary is the right
mode — the same answer `WINNATIVE-F27` reached for `fake_effect_compiler`'s argument record. No
production code and no on-disk format is involved; the fixtures are temporary files the tests
create and delete.

**Fix.** A `WriteFixture()` helper, applied to all **eight** fixture writes rather than the three
that happened to assert on the text, so the next assertion added there cannot inherit the bug.
Commit `f05630232`.

---

## WINCLOSE-0005 — the UNC case *(fixed — and it was not about UNC)*

**Symptom.** `SongTest.FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt` failed on
Windows only.

**Root cause.** The URI resolution was **right**: `file://remotehost/…` does become
`//remotehost/…`, the remote authority is not dropped, and the drive-letter strip correctly does
not fire on it. What was wrong was the error channel. `Song`'s constructor documents that text
which cannot name a path here "takes the same exit as a genuinely missing one rather than surfacing
a `filesystem_error`" — and implemented that only for the UTF-8 conversion. The existence check
used `std::filesystem::exists()`'s **throwing** overload.

On Windows the resolved path is `//remotehost/C:/…`, and `C:` is not a legal share name, so the
call fails with `ERROR_INVALID_NAME` rather than as not-found — and a `filesystem_error` escaped
past every caller's `catch`. On POSIX the same input is merely a missing file, which is why it had
never shown.

**Fix.** The `error_code` overload, which is what actually delivers the documented guarantee.
Measured rather than assumed: for a path the platform refuses, the throwing overload throws and the
`error_code` one returns `false`.

**Regression test** spells the case with an over-long component (`ENAMETOOLONG`) so it runs on
every platform rather than only where the defect was first seen. Commit `74464d43a`.

---

## WINCLOSE-0006 — backslash semantics *(decided by contract)*

**Symptom.** `ContentPipelineCoreTest.ExternalReferencesRejectUnknownTraversalAbsoluteAndSymlink-`
`Escape` failed on Windows for the case `@shared/folder\escape.bin`.

**Root cause, and the earlier analysis was one step short.** `ResolveDependency()` does reject a
`\` in an external source dependency — but it looked for it in the dependency's **generic** UTF-8
spelling, and `ContentPathToUtf8()` is `PathToGenericUtf8()`, which on Windows rewrites `\` to `/`.
The character the check searched for had already been replaced before the check ran. **The
rejection was dead code on Windows**, which is why the path was contained there and refused on
POSIX — one authored manifest with two meanings.

**Contract decision**, stated rather than inferred from the failing test: an external content
dependency is a **logical asset identifier, not a native path**. Its one separator is `/` on every
platform, because the same authored manifest must resolve to the same thing on Linux and Windows.
A platform-dependent reading of a content identifier is exactly what the path model exists to
prevent, and it is now written down in `docs/filesystem-path-model.md` question 7.

**Fix.** The check reads the authored text as written — which is also where the adjacent-separator
check two lines above already looks. Commit `07aab6f5d`.

The regression test uses a target that **exists**, so the correct and incorrect behaviours give
opposite results. It cannot fail on POSIX, where `\` is an ordinary filename character and the two
spellings are identical, and it is named and commented so that is not mistaken for coverage.

---

## WINCLOSE-0007 — the native Windows suite, three times

`CnaTests.exe` on Windows 10, MSVC 19.44, run from the repository root on the interactive desktop
(session 1 — an SSH session is session 0 and has no desktop), three consecutive times:

| Run | Ran | Passed | Failed |
|---|---|---|---|
| 1 | 8 195 | 6 481 | 1 520 |
| 2 | 8 195 | 6 481 | 1 520 |
| 3 | 8 195 | 6 481 | 1 520 |

**Byte-identical.** Phase 17 exists because F29 looked order-dependent; it is not intermittent, it
is deterministic, and three runs say so.

### What this pass changed, by name

The failure **sets** before (`9aab60f09`) and after (`ed1418601`), compared as sets:

**Nine fixed, and they are exactly the nine this pass set out to fix:**

```
ContentPipelineCoreTest.ExternalReferencesRejectUnknownTraversalAbsoluteAndSymlinkEscape   WINCLOSE-0006
HostProcessTest.StandardOutputAndAZeroExitAreCapturedFromARealProcess                      WINCLOSE-0003
HostProcessTest.ANonZeroExitIsAResultRatherThanAFailureToStart                             WINCLOSE-0003
HostProcessTest.AnArgumentContainingSpacesIsNotResplit                                     WINCLOSE-0003
HostProcessTest.OutputLargerThanAPipeBufferIsNotTruncatedOrDeadlocked                      WINCLOSE-0003
SongTest.FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt                    WINCLOSE-0005
XnaContentImporter.TypedImportAndUntypedInterfaceAgree                                     WINCLOSE-0004
XnaPipelineBridge.RegisteredXnaComponentsImportAndProcessThroughCanonicalContexts          WINCLOSE-0004
XnaPipelineBridge.BuildAssetAndBuildAndLoadAssetRunNestedBuildsOnTheCanonicalGraph         WINCLOSE-0004
```

**One newly failing**, and it is this pass's own new test:
`Win32GraphicsServices.ContextLifetimeSurvivesEveryOwnershipPathWithoutLeavingStaleState`. It
passes in the standalone platform suite (390 ran, 0 failed) and under MSVC ASan, and in the full
run it takes the **same** `SEH 0xc0000005` as the test it was written to explain — because by then
the graphics stack is in F24's exhausted state and any GL context creation faults inside the
driver. Two tests now show that one environmental fact instead of one. The alternative — not
writing the lifetime audit at all, or making it skip on a condition it cannot detect before the
driver crashes — would buy a greener number and less knowledge.

**The remaining 1 520** are the F24 cascade and the inherited classes the preceding workstream
already accounted for (no FreeType on this VM, the case-insensitive-filesystem divergence, the
POSIX-only tool invocations in `CnbGltfDirectToolTest` and `LargeModelScalingTest`). None of them
is new, and none is a Windows portability defect in the library.

---

## WINCLOSE-0008 — SDL independence, Unicode paths, sanitizer

| Check | Result |
|---|---|
| `nosdl.artifacts` | **PASS** — the SDL-free configuration produced no SDL binary |
| `nosdl.imports` | **PASS** — **40 executables** inspected with `dumpbin /dependents`; none imports `SDL2`, `SDL3` or `SDL3_mixer` |
| standalone platform suite | **390 ran, 0 failed, 3 skipped** |
| MSVC AddressSanitizer | **390 ran, 387 passed, exit 0, no AddressSanitizer diagnostic** — covers the WGL lifecycle and Win32 platform lifetime, the pointer-heavy areas this pass touched |
| `directx.probe` | `ENVIRONMENT` — see below |

`directx.probe` reporting "no Direct3D on this machine" is itself evidence rather than a gap: it is
a **separate process**, and a process cannot inherit another process's leaked GPU objects. That the
probe cannot get a device after the suite has run says the exhaustion outlives the process that
caused it, which is not something CNA can do — an application's D3D objects are released by the
operating system at process exit whatever the application did. It belongs to the virtualised GPU
stack, and it is the strongest single piece of evidence that F24/F29 are below CNA rather than in
it.

---

## WINCLOSE-0009 — the regression matrix

**Linux**, repository root, `Xvfb :99`, the two suites the branch has always excluded for cause
(`Sdl3XErrorHandlerTest`, which ends the process; `XnaDifferentialBuildTest`, whose Wine prefix
wedges):

```
                      tests   failures
baseline (9aab60f09)   8990         26
this pass              8997         25
```

Compared as **sets**, name by name. The 25 are exactly the preceding workstream's 25; the one that
is gone is `TerminalRestoration.SighupGivesTheTerminalBack`, which that workstream recorded as
carrying an "obvious environmental dependence" and asked to be re-checked rather than attributed.
**Zero newly failing tests.** The 7 extra tests are this pass's own: 5 `HostProcessTest`, 1
`SongTest`, 1 `ContentPipelineCoreTest`.

**Platform matrix**, each the platform module and its own suite:

| Configuration | Ran | Passed | Failed | Skipped |
|---|---|---|---|---|
| SDL3 (full `CnaTests`, above) | 8 997 | 8 493 | 25 (baseline) | 479 |
| SDL3 (platform suite) | 451 | 445 | **0** | 6 |
| SDL2 | 307 | 306 | **0** | 1 |
| HEADLESS | 273 | 272 | **0** | 1 |
| Wayland | 582 | 551 | **0** | 31 |
| X11 | 731 | 608 | 1 | 122 |

The single X11 failure, `X11Live.CapabilitiesDescribeThisServerRatherThanX11InGeneral`, **passes in
isolation** and is an ordering flake inside that suite. It is not attributable to this pass, and
not by assertion: none of the production code changed here is even linked into that binary —
`RunHostProcess`, `Song::FromUri` and `ResolveDependency` all resolve to zero symbols in
`cmake-build-x11/CnaPlatformModuleTests`.

**sharp-runtime**, Linux: 5 failures, all in `Xml.Linq::XLinqNamespaceTests`, all pre-existing.
Not by assumption — the entire sharp-runtime change is six lines strictly inside
`#if defined(_WIN32)`, so on Linux the preprocessor discards the block and the translation unit is
identical to `a655630f`.

### Unicode paths, re-proved after the changes

`tools/platform/win32_unicode_paths.ps1` in the guest, where **`ACP = 1252` can represent none of**
Czech, Japanese, Cyrillic or emoji — which is what makes it a hard test rather than an easy one:

| Check | Result |
|---|---|
| `host.filesystem` | **PASS** — NTFS stores and returns every path class, proved by reading a known payload back |
| `unicode.app` | **PASS** — `cna_platform_tests.exe` copied to `…/CNA test žluťoučký 日本語 Кир 😀/` and run with its working directory deliberately set elsewhere: **exit 0, 386 tests passed** |
| `unicode.nosdl` | **PASS** — the PE import table names no SDL DLL |

One harness defect fixed on the way, in the tradition of the two the previous step found in itself:
the `app` step's fallback looked for `cna_win32_platform_tests.exe` while the standalone harness
produces `cna_platform_tests.exe`, so run on its own it reported `NOT-RUN` — which reads as "there
was nothing to test" rather than "the candidate list is spelled wrong", and silently skipped the
one check that proves a CNA binary runs from a path the machine cannot spell.

---

## What is left, and why it is not this workstream's to close

**F24 — the Direct3D 11 device budget.** Still open, and now measured rather than described: the
same test repeated in one process passes 7 times and fails 13; the boundary moves with what ran
before; and — the strongest single fact — a **separate process** cannot get a device afterwards
either. A process's D3D objects are released by the operating system at exit whatever the
application did, so an exhaustion that outlives the process is below CNA. Physical Windows GPU
validation is the environment task that settles it, and it is a hardware gap rather than an open
implementation question.

**D3D12** stays `DXGI_ERROR_UNSUPPORTED` on VBoxSVGA. Unchanged, unattempted, and correctly
classified as an environment limitation.

**The test corpus is still not Unicode-clean** (WINPORT-F5): roughly 1 100 `.string()` calls in the
content and content-pipeline *tests* narrow a fixture path before handing it to CNA. That is the
F31 class at a larger scale and its own pass; the library is not implicated by it.

Neither keeps the Windows portability **implementation** open. What this pass was asked to close —
F29, F31, three text-mode failures, one UNC case and one backslash question — is closed.

---

## WINCLOSE-0011 — DirectX11 drew nothing after the first Present *(found after the closeout, fixed)*

**This corrects the closeout above.** It reported Direct3D 11 as green on the strength of the
`directx.probe` stages (device, swap chain, clear, present, resize) and of the earlier record that
`cna_demo_2d` was "rendering frames". Both were true and neither was what they were read as:
frames were being *presented*, and nothing was ever checked to be *drawn* in them. Asked to run a
demo on the VM after integration, `cna_demo_2d` showed an animated clear colour and none of its
fifty sprites.

**Symptom.** From its second frame on, every draw on DirectX11 — sprites and primitives alike —
reached no pixel. `Clear` kept working. No error, no log line.

**Root cause.** The swap chain is `DXGI_SWAP_EFFECT_FLIP_DISCARD` (DX-45), and a flip-model
`Present` unbinds the back buffer from the output merger. `DirectX11Renderer::Present()` never
rebound it. `Clear()` names its render target view explicitly and so never noticed; `DrawIndexed`
renders into whatever `OMSetRenderTargets` last bound, which after a `Present` is nothing.

**How it was isolated**, each step measured on native Windows with MSVC before the next:

| Experiment | DirectX11 | Conclusion |
|---|---|---|
| sprite drawn, pixel read back (64 px buffer) | fails | the renderer, not the demo |
| plain `BasicEffect` primitive, same device | passes | not the device, target or viewport |
| sprite with `CullNone` | fails | not winding |
| red clear, white sprite, `Opaque` | reads red | never rasterized, not a zero texture |
| same cases at 256 px | **all pass** | the 64 px failure was the letterbox path |
| `cna_demo_2d` with `FlushBatch` instrumented | no log line at all | **the demo binary on the VM was stale** |
| demo rebuilt, instrumented | 306 flushes, correct vertices/viewport/constants/SRV, no sprites | the draw is right and goes nowhere |
| depth buffer; rotation with centre origin | pass | neither |
| draw in the frame **after** `Present` — sprite, and primitive | **both fail**; OpenGL33 passes | **the defect** |

**Fix.** `Present()` rebinds the tracked render target set after a successful present — not the
back buffer, so the viewport and any game-bound render target stay as they were.

**Validation.** `SpriteBatchRasterizationTest` 9/9 on DirectX11 in three consecutive runs (the first
run after the build had one intermittent failure in the red/white/opaque case, not reproduced in
the three runs after it — recorded, not hidden); 9/9 on OPENGL33; skipped on HEADLESS.
`cna_demo_2d` draws its sprites, captured **from inside the guest with GDI** on the interactive
desktop rather than taken from the VirtualBox framebuffer, whose screenshots of a 3D-accelerated
guest were not trusted as evidence here.

**Three things this leaves on record:**

- **No readback test in the repository covered DirectX11.** Every pixel-reading suite was gated to
  `Software, OpenGL33, OpenGLES3`. The new suite includes DirectX11; the older suites still do not,
  and are worth re-gating on their own merits.
- **The validation harness builds with `CNA_BUILD_EXAMPLES=OFF`**, so an example binary left in the
  build tree by an earlier configuration survives every later run untouched. `cna_demo_2d.exe` on
  the VM was from 12:01 while the tests beside it were from 19:34, and several measurements were
  taken against it before that was noticed.
- **Open, not fixed:** with a back buffer narrower than the narrowest captioned window (64 px), the
  presentation layer letterboxes it, and a sprite at logical (8..56) did not appear at logical
  (32, 32) on readback. Whether `GetBackBufferData` or the sprite placement fails to apply the
  letterbox offset was not determined.

### Choppy animation on the VM is the VM's, measured without CNA

With WINCLOSE-0011 fixed, `cna_demo_2d` draws its sprites but animates visibly choppily. Measured
in the demo: 10–15 draws per second with vsync on, ~270 with it off, and `Draw` itself costing
**0.07 ms** — the rest is waiting for vblank. Because a CNA pacing defect would look identical from
inside the demo, the question was settled with `spikes/d3d11-vsync-spike/vsync_probe.cpp`, a D3D11
program containing no CNA at all:

| No CNA, same guest | fps | p50 frame |
|---|---|---|
| flip model, `Present(1)` | **8.3** | 101 ms |
| flip model, `Present(0)` | 149 | 0.17 ms |
| BLT model, `Present(1)` | **6.9** | 153 ms |

Any D3D11 application with vsync runs at 7–8 fps in this headless VBoxSVGA session, under either
swap model, although the guest reports 60 Hz. **Not a CNA defect.** Frame pacing on a physical
Windows GPU is unmeasured and belongs to the real-hardware Windows test phase, where this probe
should run first as the CNA-free baseline.

Re-measured the same day: `cna_win32_directx_probe` — all seven D3D11 stages pass; **D3D12 device
`hr=0x887A0004` (`DXGI_ERROR_UNSUPPORTED`)**, unchanged.

---

## WINCLOSE-0012 — DirectX11 in the pixel-readback suites *(post-closeout)*

Every pixel-reading suite was gated to Software/OpenGL33/OpenGLES3, so DirectX11 rasterization had
no coverage until WINCLOSE-0011's first readback test found a real defect. DirectX11 was added to
106 such gates (not to the Software-only point-list gates, the two EasyGL pins, or ~39
`CNA_RENDERER_IS` capability predicates, each of which needs its own judgement). Each affected suite
was then run on native Windows in **its own process**, with `SpriteBatchRasterizationTest` last as
a canary for an exhausted GPU (9/9 every time).

| | Tests |
|---|---|
| previously skipped on DX11, now **passing** | 72 |
| newly exposed and failing | 23 → **7** after the two fixes below |
| already failing on DX11 before, unchanged | 20 |

**Two defects fixed** (`modules/renderers/directx11/src/DirectX11Renderer.cpp`):

1. **Letterboxed back buffer readback read the letterbox bars.** Windows will not create a captioned
   window narrower than ~120 px, so a 16×16 back buffer is presented inside a 120×16 surface.
   Draws are placed through `GetDefaultViewportRect()`; `ReadBackbuffer` read physical `(x, y)`.
   Measured: a left-half draw read back as `########........` on OpenGL33 and `................` on
   DirectX11. It now samples the same presentation geometry at logical pixel centres, and is the
   untouched direct copy whenever logical and physical agree. This also affected any game whose
   window is not the size of its back buffer.
2. **The back buffer ignored `PresentationParameters.DepthStencilFormat`** and always allocated
   D24S8, while reporting Depth24Stencil8 as applied. So `DepthFormat::None` still depth-tested,
   Depth24 carried a usable stencil, and depth/stencil clears on surfaces without them never threw.
   It now allocates what was asked (render targets already did), reports it honestly, recreates it
   on a Reset that changes it, and — because DXGI has no 24-bit depth-only format — disables the
   stencil test while a Depth24 back buffer is bound.

Fixed by those two: `StateEnumFallbackTest` 6, `StateNumericFallbackTest` 1,
`GraphicsProfileDrawStateFormatTest` 2, `BackBufferDepthStencilContractTest` 7.

**Still failing, a third and separate cause (not fixed):** 7 `BackBufferDepthStencilContractTest`
SpriteBatch cases — `layerDepth` in depth testing, a `Begin` transform's W, near-plane clipping,
perspective-correct interpolation, viewport depth range, float-domain source endpoints. With
`CNA_DIRECTX11_COMPILED_EFFECTS=OFF` the D3D11 sprite path uses its own shader, whose vertex is
`(x, y, u, v, rgba)` and whose output is `float4(ndc.x, -ndc.y, 0, 1)`: depth is always 0 and W
always 1, so this XNA SpriteBatch semantics cannot be expressed on that path at all.

**Pre-existing on DX11, unchanged:** Texture3D/TextureCube reader 6 (incomplete 3D/cube storage for
classic formats), `UnsupportedFormatConstruction` 4 (DX11 accepts NormalizedByte2/4 and Bgra5551
where the test expects a refusal — whether the test or the renderer is wrong is not determined),
`VertexDeclarationLayout` 6 + `DeclarationGuard` 4 (`rendered == true` where `false` is expected;
not analysed).

### A finding that reframes the Windows full-suite numbers

The full Windows run with these changes showed 104 newly failing tests. Every one of them failed
with `DIRECTX11: initialization failed (forced by CNA_DEBUG_FAIL_RENDERER_INIT)` — and **so did
1 214 of the 1 520 failures in the baseline run**, before any change today. They are not F24.

`GraphicsRendererFallbackTest`/`GraphicsDeviceSubsystemLifecycleTest` force a renderer failure by
setting that variable and clear it in `TearDown` with an empty string. sharp-runtime's Windows
`Environment::SetEnvironmentVariable` writes an empty value **only** to the Win32 environment block
(`_wputenv_s` would delete it), so the CRT copy keeps `"DIRECTX11"` — and `GraphicsDevice` reads
the variable with `std::getenv`, i.e. from the CRT copy. From that test on, every device in the
process fails to initialise. POSIX `setenv(name, "")` leaves nothing stale, so it is Windows-only.
The 104 were simply tests that now run on DX11, or that a relink moved, after that point; none is
a regression from the two fixes (the per-suite isolated runs above are clean). The real F24 GPU
exhaustion is still genuine where it was measured, much earlier in the run (`GltfConformanceL6`).
**Not fixed here** — it belongs to sharp-runtime.
