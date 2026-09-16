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
| WINCLOSE-0002 | F29 — WGL passes standalone, access-violates in the full suite | 🟨 reproduced, isolating |
| WINCLOSE-0003 | F31 — `RunHostProcess`'s Windows `CreateProcessW` path has no test behind it | ✅ fixed |
| WINCLOSE-0004 | Three Windows text-mode failures (`"hello\r"`) | ✅ fixed |
| WINCLOSE-0005 | UNC: `SongTest.FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt` | ✅ fixed |
| WINCLOSE-0006 | Backslash semantics in an external content dependency | ✅ fixed |
| WINCLOSE-0007 | Native Windows full suite, repeated | ⬜ |
| WINCLOSE-0008 | SDL-free + Unicode-path + D3D11 re-proof | ⬜ |
| WINCLOSE-0009 | Linux / X11 / Wayland / SDL matrix regression | ⬜ |
| WINCLOSE-0010 | Git authorship audit and integration into `next` | ⬜ |

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

**What CNA gained anyway.** `ContextLifetimeSurvivesEveryOwnershipPathWithoutLeavingStaleState`
(`c0d8f2d82`) walks every ownership path the service has in one process — repeated create/destroy,
destroy-while-current, two contexts on one window, a context per window on two windows, and a
window destroyed before the context that was current on it — and then creates one more context and
swaps it, so a leaked `HGLRC`, a released `HDC` or a stale current binding is reported here rather
than as a fault in an unrelated test later. That is the audit Phase 5 asks for, kept as a test
rather than as a paragraph.
