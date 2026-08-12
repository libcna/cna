# CNA and Game Consoles — Feasibility Analysis

> Document scope: **game consoles** (Nintendo, PlayStation, Xbox, retro platforms), not a text
> console/terminal. This document builds on `cnaplatform.md` (isolating SDL3 behind a platform API)—
> that work is a **prerequisite** for consoles, not an alternative.

## Short answer

1. **CNA can support consoles architecturally.** Compile-time renderer selection (46 identities),
   modular `modules/<name>/` organization, an existing non-desktop port (Emscripten), and current
   per-platform gating (FFmpeg in `modules/media`) are exactly the mechanisms a console port needs.
2. **The question “is that enough, or is an NDK required?” has two different answers depending on
   the console class.** Three meanings of “NDK” must be distinguished because they tend to be
   conflated in this discussion:
   - **Android NDK** — CNA already uses it (sensors through `<android/sensor.h>`, cross-compilation
     according to `docs/devices-build.md`). It is unrelated to game consoles.
   - **Official console devkit / SDK under NDA** (Nintendo SDK, PlayStation SDK, Microsoft GDKX) —
     it is **absolutely required** for current commercial consoles, and without it there is no legal
     route, not even through SDL.
   - **Open homebrew toolchain** (devkitPro, VitaSDK, pspdev, PS2SDK, KallistiOS, nxdk) — it is also
     **required** for older consoles, but it is public, free, and may be included in a public
     repository and CI.
3. In practice: **Switch / Switch 2 / PS4 / PS5 / Xbox One / Series = yes, an NDA SDK and a
   registered developer account are required.** **PS Vita / PSP / PS2 / 3DS / Wii / GameCube /
   Dreamcast / Xbox Classic = no, a public homebrew SDK is sufficient**—and CNA is realistically
   closer to that goal than one might expect because it has the `OPENGL1`, `OPENGLES1`, `SOFTWARE`,
   `PORTABLEGL`, and `SDL_RENDERER` renderers.

---

## 1. Three classes of “console support”

### Class A — current commercial consoles (NDA)

| Platform | Toolchain / SDK | Availability |
|---|---|---|
| Nintendo Switch, Switch 2 | Nintendo SDK (NintendoSDK, custom clang) | registered developers under NDA only |
| PlayStation 4, PlayStation 5 | Sony PS SDK (ORBIS/PROSPERO) | registered developers under NDA only |
| Xbox One, Xbox Series | Microsoft GDKX (Game Development Kit, Xbox extensions) | registered ID@Xbox / licensed developers only |

Key facts:

- SDL3 **supports** these platforms, but the Switch/Switch 2 and PS4/PS5 ports are kept in
  **separate NDA-only repositories**; SDL provides them free of charge under the zlib license, but
  only to developers who already have an NDA with Nintendo/Sony. The public libsdl-org/SDL
  repository does not contain them.
  ([SDL3/README-platforms](https://wiki.libsdl.org/SDL3/README-platforms),
  [SDL3/README-switch](https://wiki.libsdl.org/SDL3/README-switch),
  [SDL3/README-ps5](https://wiki.libsdl.org/SDL3/README-ps5))
- **FNA**, CNA's reference project, uses the same model: console ports exist, but they are
  distributed outside the public repository to licensed developers.
- Implication for CNA: **no line of console SDK code, header content, symbol name, or shader ABI
  may appear in the public `openeggbert/cna` repository.** Only the interface
  (`CNA::Platform::IPlatform` + `IGraphicsRenderer`) can be implemented in the public repository,
  with the implementation kept in a separate private repository.
- Nothing can be verified without devkit hardware either—emulators for these platforms are not
  suitable for development (and their use typically violates the license).

**Answer for Class A: yes, it requires the official SDK (as well as an account and a devkit).
Without them, it is infeasible regardless of how well CNA is written.**

### Class B — consoles with an open homebrew SDK

| Platform | Toolchain | Graphics API | SDL3 in the public tree |
|---|---|---|---|
| PlayStation Vita | VitaSDK | GXM, through `vitaGL` ~ GL1/GLES1 | **yes** |
| PlayStation Portable | pspdev (PSPSDK) | sceGu, through `pspgl` ~ GL1 subset | **yes** |
| PlayStation 2 | PS2SDK / ps2dev | GS, `gsKit`, `ps2gl` | **yes** |
| Nintendo 3DS | devkitARM + libctru | citro3d, through `picaGL` ~ GL1.1 | **yes** |
| Wii / GameCube | devkitPPC + libogc | GX, through `opengx` ~ GL1 | no (community SDL2 only) |
| Dreamcast | KallistiOS | PowerVR, through `GLdc` ~ GL1 | no (SDL 1.2/2 ports) |
| Xbox Classic | nxdk | pbkit / XGU (NV2A, D3D8 generation) | no |
| PlayStation 3 | PSL1GHT | RSX, `rsxgl` | no |

SDL3 has in-tree backends in its **public repository** for Nintendo 3DS, PS2, PSP, and PS Vita
([SDL3/README-platforms](https://wiki.libsdl.org/SDL3/README-platforms)). This is critical for CNA:
on these four platforms, **no new platform implementation needs to be created at all**—CNA only
needs to survive cross-compilation and the runtime environment's constraints.

**Answer for Class B: an NDA SDK is not required. A platform-specific toolchain, a different build
profile, and lower CNA requirements (memory, `std::filesystem`, threads, dependencies) are
required.**

### Class C — “consoles” that are actually PCs

Steam Deck, ROG Ally, Legion Go, and similar devices are x86-64 Linux/Windows systems. **CNA already
works here today**, without a single change; all that is needed is gamepad-first UX, fullscreen,
and a performance profile. If the goal is “the game runs on a living-room console,” this is the
least expensive existing answer.

---

## 2. CNA's current position (measured state, not an estimate)

Determined directly from this tree:

| Fact | Value | Significance for consoles |
|---|---|---|
| SDL3 is a hard dependency of the core layer | `modules/input` 1092 direct `SDL_*`, `modules/devices` 338, `modules/graphics` 208, `modules/devices-ext` 154, `modules/audio` 97, `modules/runtime` 75, `modules/media` 43, `modules/core` 39 | This blocks platforms without an SDL port (Dreamcast, Xbox Classic, Wii)—hence `cnaplatform.md` |
| Renderer selection is compile-time | `CNA_GRAPHICS_RENDERER`, 46 identities, per-family modules | **Strong point**—a console renderer is just another family in `modules/renderers/` |
| A non-desktop port exists | Emscripten (WEBGL1/2, CANVAS, HTML_DOM, custom main-loop spike) | Proof that the codebase can accommodate a foreign lifecycle model |
| Per-platform gating already exists | `modules/media/CMakeLists.txt` excludes FFmpeg sources where FFmpeg is unavailable | A template for a “console profile” |
| Declared standard | `CMAKE_CXX_STANDARD 23`, `CXX_EXTENSIONS OFF` | See below—less painful than it seems |
| C++23 library features actually used | `std::format` 0, `std::print` 0, `std::expected` 0, `std::mdspan` 0, `std::flat_map` 0, `import std` 0 | The code is effectively **C++20** (concepts 74, `requires` 403, `std::optional` 400, `std::bit_cast` 19, `std::span` 14) → GCC 12/13 from devkitPro is realistic |
| `std::filesystem` | 673 occurrences | **Main problem** on PSP/PS2/3DS/Dreamcast—it is either missing or incomplete |
| Threads | `<thread>` 41, `<mutex>` 24 | Limited on newlib/KallistiOS, but manageable |
| Dynamic library loading | `dlopen` 0, `SDL_LoadObject` 0 | **Good news**—consoles generally require static linking |
| Exceptions | used pervasively (`System::Exception`, see the comment in `CMakeLists.txt`) | Work everywhere, but increase binary size and memory footprint |
| Toolchain failure precedent | Android NDK build fails on `std::chrono::clock_cast` and `-Werror` in `sharp-runtime` (`docs/android-graphics-limitations.md`) | Shows that the bottleneck tends to be **sharp-runtime**, not CNA |

The final row is the most important warning: CNA depends on the sibling `sharp-runtime` repository
(a reimplementation of `System.*`). It is written against a full desktop libstdc++. Any console
will require its **restricted profile**—and it will fail before any CNA code does, just as it does
on Android.

---

## 3. What specifically would need to be done

### Phase 0 — scope and legal framework decision

- Select the target class (A / B / C). Mixing them in a single plan is the main source of
  confusion.
- For Class A, establish in advance that the public repository contains **interfaces only**, while
  the implementation lives in a private repository following the same pattern currently used to
  connect `sharp-runtime` (`CNA_SHARP_RUNTIME_ROOT`)—that is, an optional external checkout, not a
  submodule.

### Phase 1 — `CNA::Platform` (an unconditional prerequisite)

Exactly what `cnaplatform.md` proposes: window, event loop, timer, keyboard/mouse/gamepad, native
window handle, and a capability model. Consoles make this step even **more** necessary than SDL2/
SDL1 because:

- there is no SDL3 on Dreamcast, Xbox Classic, or Wii, and there never will be,
- although SDL3 exists on Switch/PS5, its headers must not be visible in the public repository,
- the console lifecycle (suspend/resume, user accounts, system dialogs, HOME menu) has no desktop
  equivalent and must enter the contract as a first-class concept.

At minimum, the following must be added to `PlatformCapabilities` from `cnaplatform.md`:
`supportsWindowResize` (false), `supportsMultipleWindows` (false), `supportsMouse` (false),
`hasSystemBackButton`, `supportsSuspendResume`, `requiresUserAccountSelection`,
`hasWritableUserStorage`, `usesTitleSandboxPaths`.

### Phase 2 — extracting SDL from non-graphics modules

Order by benefit-to-cost ratio (based on the figures above):

1. `modules/runtime` (75)—`Game`, `GameWindow`, `GraphicsDeviceManager`, `TitleContainer`.
2. `modules/core` (39)—`Logger`, `Entrypoint` (currently hard-wired to `SDL_main.h`).
3. `modules/audio` (97)—audio backend behind its own interface (`FillBuffer`, not per-sample).
4. `modules/input` (1092)—the largest part; the gamepad is the only input device on consoles, and
   button mapping is platform-defined (Nintendo's confirm A/B mapping is reversed compared with
   Xbox—that must be platform policy, not a constant in the code).
5. `modules/devices`, `modules/devices-ext` (492 combined)—on consoles these are largely
   **disabled** (`CNA_DEVICES=OFF`), so they do not need to be ported.

### Phase 3 — “console build profile”

A new concept alongside `CNA_GRAPHICS_RENDERER`, for example
`CNA_PLATFORM_PROFILE=CONSOLE_SMALL`:

- `CNA_BUILD_TESTS=OFF`, `CNA_BUILD_EXAMPLES=OFF` (GoogleTest makes no sense on a console),
- `CNA_ENABLE_NET=OFF` (ENet requires BSD sockets; networking on consoles is a platform service
  subject to certification),
- `CNA_DEVICES=OFF`, media/FFmpeg off (already gated), Skia/Blend2D/Magnum/Diligent off,
- static linking, no `dlopen` (already satisfied),
- a VFS layer instead of `std::filesystem`—`TitleContainer`/`StorageDevice` must support
  `host0:`/`ux0:`/`sdmc:`/`umd0:` paths and a read-only title sandbox,
- memory budget: Dreamcast 16 MB, PS2 32 MB, PSP 32/64 MB, 3DS 64/128 MB, Wii 88 MB. Notes in
  `RAM.md` (for example, `cpuPixels_` ~34 MB CPU copies of all textures on the Vulkan path) show
  that the current design targets desktops and would need a dedicated texture-ownership policy
  for small consoles.

### Phase 4 — first real target: a homebrew console with in-tree SDL3

Recommended order: **PS Vita → PSP → 3DS → PS2**.

Why Vita should come first:

- SDL3 supports it in the public tree (no NDA),
- VitaSDK is maintained and a CMake toolchain file exists,
- `vitaGL` provides a GL1/GLES1-level API → it maps to CNA's **existing** `OPENGLES1` / `OPENGL1`
  renderers,
- 512 MB RAM (versus 32 MB on PS2) means a memory diet is not an immediate blocker,
- the `SDL_RENDERER` and `SOFTWARE` fallbacks are available if the GL path causes trouble.

A realistic “hello triangle” milestone: `Game` runs, `SpriteBatch` draws a texture, the gamepad
works, and `ContentManager` reads XNB from a read-only title path. Audio and 3D come later.

### Phase 5 — consoles without an SDL port

This is where an actual new platform implementation is created (`CNA::Platform::KosPlatform`,
`CNA::Platform::NxdkPlatform`, …). CNA has some interesting synergies that other frameworks do not:

- **Dreamcast + `GLdc`** → the `OPENGL1` renderer is ideologically the exact target it was written
  for.
- **Xbox Classic + nxdk** → NV2A is a Direct3D 8-generation GPU. CNA has a `DIRECTX8` renderer,
  but nxdk does not provide `d3d8.dll`—pbkit/XGU must be used. What can be reused is therefore the
  **structure** of the DIRECTX8 renderer (fixed-function + register combiners, same hardware
  generation), not its API calls.
- **Wii/GameCube + `opengx`, 3DS + `picaGL`, PSP + `pspgl`** → again, GL1-level targets for
  `OPENGL1` / `OPENGLES1`.
- For the smallest platforms, `SOFTWARE` and `PORTABLEGL` remain the final fallback (both have only
  four references to SDL in this tree, so they are closest to being SDL-independent).

### Phase 6 — Class A (NDA)

Only after a registered developer account and a devkit are available. Until then, the public
repository contains at most capability flags, empty renderer registrations, and interface
documentation. No SDK function name, header, or build script that refers to devkit paths.

---

## 4. Mapping renderers to consoles

| Console | Recommended CNA renderer | Note |
|---|---|---|
| PS Vita | `OPENGLES1` / `OPENGL1` (vitaGL), `SDL_RENDERER` | lowest barrier to entry in the entire analysis |
| PSP | `OPENGL1` (pspgl) or `SOFTWARE` | 480×272, fixed-function |
| PS2 | custom `gsKit`/`ps2gl` backend, or `SOFTWARE` | 32 MB RAM is a hard limit |
| 3DS | `OPENGL1` (picaGL) | two screens fall outside the XNA model; address as CNAEXT |
| Wii / GameCube | `OPENGL1` (opengx) | libogc GX is fixed-function |
| Dreamcast | `OPENGL1` (GLdc) | 16 MB RAM, the tightest profile |
| Xbox Classic | new `NXDK` renderer (pbkit/XGU) | structure shared with `DIRECTX8` |
| Switch / Switch 2 | NDA (deko3d / Vulkan-like) | outside the public repository |
| PS4 / PS5 | NDA (GNM/GNMX, AGC) | outside the public repository |
| Xbox One / Series | NDA (D3D12X) | structure shared with `DIRECTX12` |

---

## 5. Effort estimate

Rough ranges, measured in person-days of focused work, excluding debugging on real hardware:

| Step | Estimate |
|---|---|
| `CNA::Platform` API + moving SDL3 behind `Sdl3Platform` (Phases 1–2) | the largest single item; more than all the rest combined |
| Console build profile + VFS + memory diet (Phase 3) | medium, but affects `sharp-runtime` |
| PS Vita bring-up on in-tree SDL3 (Phase 4) | small **after** Phases 1–3; unmanageable otherwise |
| Each additional homebrew platform with an SDL port | small increment |
| Platform without an SDL port (Dreamcast, Xbox Classic) | large—an entire platform implementation from scratch |
| NDA platform | impossible to estimate without SDK access |

The key observation: **the cost is not in the consoles, but in removing the assumption that
“platform = SDL3 on desktop.”** Once Phases 1–3 are complete, each additional Class B console is
incremental. Until they are complete, even the simplest console is out of reach.

## 6. Risks

1. **`sharp-runtime` is the real bottleneck.** The Android build failed there, not in CNA. The same
   will recur on a larger scale on consoles (`std::filesystem`, `std::chrono` clock conversions,
   threading, `FileStream`).
2. **Project scope grows faster than testing capacity.** Forty-six renderers × N platforms is a
   combinatorial space that CI cannot cover. Console ports must be explicitly designated as
   restricted profiles with their own conformance suite, not as “another supported platform.”
3. **The legal risk for Class A** is binary: a single commit containing NDA material in a public
   repository is irreversible. Repository separation must therefore be decided **before** work
   starts, not during it.
4. **Memory constraints on small consoles** will not be solved by interface refactoring—they
   require changes to data ownership (see `cpuPixels_` in `RAM.md`).
5. **Certification** (TRC/XR/Lotcheck) applies to the game, not the framework, but the framework can
   make certification impossible (suspend/resume, user selection, controller-disconnect behavior,
   startup time).

## 7. Recommendations

1. Do not start with a console. Start with **Phase 1 from `cnaplatform.md`**—it is a shared
   prerequisite for SDL2, SDL 1.2, and every console, and the only step that remains valuable even
   if a console port never materializes.
2. In parallel, perform a **restricted-environment audit of `sharp-runtime`** (without
   `std::filesystem`, without `clock_cast`, with limited threading). Without that, any console
   bring-up is a dead end.
3. Choose **PS Vita** as the first console target (public SDL3, vitaGL ~ GL1, sufficient RAM).
   Dreamcast is more romantically appealing, but it has the tightest profile and no SDL port.
4. Do not plan Class A as a technical task, but as a **business/legal step**: register with the
   platform holder first, then write code.
5. If the actual goal is “a CNA game on a living-room console as soon as possible,” the answer is
   **Class C** (Steam Deck / handheld PC)—it works today and only requires gamepad-first UX.

---

## 8. The PlayStation and Xbox families specifically

Both families have members in both classes—“PlayStation” and “Xbox” therefore do not inherently
mean “NDA required.”

### Xbox

| Family member | Toolchain | NDA / devkit | Note for CNA |
|---|---|---|---|
| Xbox Classic (2001) | `nxdk` (open source) | no | NV2A = Direct3D 8 generation; however, `nxdk` does not provide `d3d8.dll`, only pbkit/XGU |
| Xbox 360 | XDK | yes, and the program is closed | XNA's original home. XNA Game Studio and XBLIG have long been dead; the only remaining route is retail-console modding—a legal gray area and not recommended |
| Xbox One / Series—route (a) | **Dev Mode + UWP** | **no** (Partner Center ~$19, retail console) | Least expensive legal route to “it runs on Xbox” |
| Xbox One / Series—route (b) | Win32 + **GDKX** | yes (ID@Xbox / licensed partner) | The only route for publishing a full-featured game |

More detail on route (a), since it is the most interesting one for CNA: Developer Mode is enabled
through a Microsoft Store app on a **standard retail console**, registering an individual Partner
Center account costs a one-time fee of about USD 20, and deployment is performed from a PC over
the network. The resulting constraints for CNA are:

- It is a **UWP container**, not Win32—a different entry point, a different lifecycle, different
  file access, and a limited memory budget. Microsoft now describes UWP games as community-
  supported only (partners are expected to use the Win32 + GDK route).
- **SDL3 does not support UWP/WinRT**—the backend was removed from SDL3 before 3.0. The viable
  choices are therefore SDL2 (which has a WinRT backend) or a custom `CNA::Platform` UWP
  implementation. This directly connects to `Sdl2Platform` from `cnaplatform.md`—Xbox Dev Mode is
  the first concrete reason why the SDL2 path is not merely retro nostalgia.
- Graphics: D3D11 and D3D12 are both available in UWP, so the `DIRECTX11` / `DIRECTX12` renderers
  are conceptually usable. Beware of `D3DCompile`, though—`modules/renderers/directx11` currently
  compiles HLSL **at runtime**; console environments generally do not permit runtime shader
  compilation, so a precompiled blob path would be needed (related to
  `docs/fx-bytecode-support-plan.md`). The Vulkan renderer already handles this correctly—it uses
  the pregenerated `spirv_shaders.hpp`, not runtime compilation.
- The public `microsoft/GDK` repository on GitHub is the GDK **for Windows/PC**; its console
  extensions (GDKX) are not public.

### PlayStation

| Family member | Toolchain | NDA / devkit | SDL3 in the public tree |
|---|---|---|---|
| PS1 | PSn00bSDK, psxsdk (open source) | no | no |
| PS2 | PS2SDK / ps2dev | no | **yes** |
| PSP | pspdev | no | **yes** |
| PS3 | PSL1GHT | no | no |
| PS Vita | VitaSDK | no | **yes** |
| PS4 / PS5 | Sony PS SDK | **yes** | NDA-only repository |

PlayStation has **no equivalent to Xbox Dev Mode**. PS4/PS5 offer no legal route for an
unregistered developer—either a licensed account and a devkit, or nothing. On the other hand, the
older half of the family (PS2, PSP, Vita) is open and SDL3 supports it directly in the public tree,
making it the easiest console target for CNA overall.

### What can be done now, without a single SDK

These items are valuable regardless of whether a license is ever obtained, and no console will
work without them:

1. The `CNA::Platform` API and moving SDL3 behind `Sdl3Platform` (Phases 1–2 above).
2. A gamepad-only input profile and a **platform button policy** (confirmation maps to A/B
   differently on Nintendo and Xbox, and to `✕`/`○` by region on PlayStation)—this must not be a
   constant in game code.
3. The lifecycle events `Suspend` / `Resume` / `ConstrainedMode` / `UserSignedOut` in `Game`—
   certification requires them, and desktop systems have no equivalent.
4. A title sandbox + VFS in `TitleContainer` / `StorageDevice` (read-only content, separate
   account-bound user storage, no absolute paths).
5. Precompiled shaders instead of runtime compilation (`D3DCompile` in DIRECTX11 is currently an
   exception; the Vulkan path is the model).
6. A memory budget and texture ownership (see `cpuPixels_` in `RAM.md`).
7. Static linking without `dlopen`—already satisfied today (0 occurrences).

---

## 9. What is required for NDA / licensed access

> Fees, devkit availability, and terms change; the information below reflects public sources as
> of August 2026. Verify it directly on the platform holders' portals before making any decision.

### An NDA is not a separate step

An NDA is not signed separately—it is part of a chain:

```text
portal registration
   → signing an NDA / framework agreement
      → requesting access to a particular console's development environment (with a project description)
         → approval
            → SDK + documentation + devkit
```

The key point: **a project is approved, not curiosity.** An application saying “I want to port my
framework” is much weaker than “I have this game, here are screenshots, and here is the release
plan.”

### Requirements shared by all three platform holders

- legal age and a verifiable identity;
- a **registered legal entity**—practically mandatory for Microsoft and Sony, but not for
  Nintendo;
- a company email address on its own domain and a working studio website (these are genuinely
  reviewed);
- a project description / project plan, often including the team and portfolio for Sony and
  Microsoft;
- **security conditions**: the devkit must be kept in a non-public, lockable area; access must be
  restricted to approved individuals; devkit materials (screenshots, logs, API names) must not be
  disclosed; and SDK content must not be committed to a public repository;
- signed agreements (Sony: GDPA—Global Developer & Publisher Agreement; Microsoft: Partner Center
  + ID@Xbox agreements; Nintendo: NDA directly in the portal).

### By platform

| | Nintendo | Xbox | PlayStation |
|---|---|---|---|
| Portal | [developer.nintendo.com](https://developer.nintendo.com/) | [ID@Xbox](https://developer.microsoft.com/en-US/games/publish/id/welcome) + Partner Center | [partners.playstation.net](https://partners.playstation.net/) |
| Company required | **no**—individual registration is possible | practically yes (legal company name; a DUNS number speeds up verification) | practically yes |
| Released game required | **no** | no, but ability to deliver is assessed | no, but the team and plan are assessed |
| Registration | free | individual Partner Center account, one-time fee of about USD 20 | free |
| NDA | signed online immediately after registration | included in the Partner Center / ID@Xbox agreements | GDPA after approval |
| Devkit | purchased (on the order of hundreds of USD) | **2 units free** after approval | previously purchased; a loan model is now reported—verify |
| Difficulty | **lowest** | medium | **highest** |
| Note | Switch 2: Nintendo is not currently accepting requests for access to the development environment | there is also a route with **no NDA at all**: Dev Mode + UWP (Section 8) | no equivalent to Dev Mode exists |

### What this means specifically for CNA

1. **The application is for a game, not a framework.** The most viable route is to request access
   with a specific game built on CNA and port the framework as a byproduct.
2. **Distributing CNA to other licensed developers is a separate matter.** Middleware/tools
   programs exist for this purpose (Nintendo has a dedicated Middleware section in its portal;
   Sony and Microsoft have equivalents). Registering as a middleware provider is an additional
   step beyond a standard developer account.
3. **SDL and FNA use the same model.** Once an NDA exists, the SDL3 console port is available free
   of charge under the zlib license—one only needs to prove that one has an NDA with the relevant
   platform holder.
4. **The public repository must not touch the SDK.** The structure must be decided in advance:
   public `openeggbert/cna` contains interfaces and capability flags only, while the implementation
   lives in a private repository connected through the same mechanism currently used for
   `CNA_SHARP_RUNTIME_ROOT`.
5. **Without a company**, the only realistically available options are Nintendo (individual
   registration) and Xbox Dev Mode (with no NDA at all). PlayStation is effectively unavailable.

---

## 10. How FNA and MonoGame do it

The question “if FNA and MonoGame can do it, why not CNA?” has a concrete answer: **both do exactly
what Section 9 describes—it is simply not visible because the console portion of their code is not
in the public repository.** Their models differ, however, and offer very useful lessons for CNA.

### MonoGame—the “private repository per platform” model

- It supports PS4, PS5, PS Vita, Xbox One, Xbox Series, and Switch, but **exclusively for licensed
  console developers**.
- Console code lives in access-controlled private repositories, one per platform.
- Access is obtained by first proving one's license with the platform holder and then applying to
  MonoGame: through ID@Xbox membership and a verification email for Xbox, and through Sickhead
  Games for Switch.
  ([Console Access | MonoGame](https://docs.monogame.net/articles/console_access.html))
- The cost of this model: historically, the console runtime was developed separately and lagged
  behind desktop (keeping MonoGame on an older C# language profile for a long time).

### FNA—the “all platform specifics are inside SDL” model

This is the more interesting option for CNA:

- FNA **does not have private branches for individual consoles**. The public master branch is
  exactly what ships on consoles; all platform code is encapsulated in SDL.
  ([Appendix B: FNA on Consoles](https://github.com/FNA-XNA/FNA/wiki/Appendix-B:-FNA-on-Consoles/5b408b86f533fdd89eb103286ad38d2bdeb17fdc))
- The NDA therefore does not concern FNA, but **two things beneath it**: the console port of SDL
  (SDL-switch, SDL-playstation, etc.) and the console .NET/NativeAOT runtime.
- A licensed developer obtains the NDA version of SDL, takes the public FNA source, and compiles
  it. Consulting and documentation are private; the framework itself is not.

### What this means for CNA—and why it is better positioned in one respect

1. **The goal should be the FNA model, not the MonoGame model.** Once all platform behavior is
   behind `CNA::Platform` (and graphics behind `IGraphicsRenderer`), the public
   `openeggbert/cna` repository remains shippable on a console without changes, and only the SDL
   layer beneath it is private. That is exactly the work described by `cnaplatform.md`—consoles
   provide a second, stronger reason for it.
2. **CNA does not have the biggest problem FNA and MonoGame had to solve.** They had to bring the
   .NET runtime (Mono AOT, now NativeAOT) to consoles—platforms that prohibit JIT. CNA is native
   C++; no managed runtime needs to be ported. That entire category of work disappears.
3. **CNA's graphics path already exists.** FNA uses SDL on consoles; CNA has the `SDL_GPU` renderer
   (`plan_sdlgpu.md`: 2D and 3D, the `BasicEffect` family, `AlphaTest`/`DualTexture`/
   `EnvironmentMap`/`SkinnedEffect`, pipeline cache)—in other words, the same architecture. The
   console backend for `SDL_GPU` is part of SDL's NDA port, not something CNA would write.
   The only caveat: `ShaderEffect` in this renderer currently compiles GLSL→SPIR-V **at runtime**
   (linked `libshaderc`), which consoles do not permit—the console profile would need a
   precompiled path (again, `docs/fx-bytecode-support-plan.md`).
4. **The remaining difference is dependency discipline.** FNA needs SDL + a runtime on consoles.
   CNA currently also needs `sharp-runtime` and, depending on the profile, FFmpeg, ENet,
   GoogleTest, Skia, Blend2D, and so on. The console profile from Section 3 is therefore a
   prerequisite in this model too.

In other words, FNA and MonoGame do not support consoles because they have access to something CNA
does not. They support consoles because (a) their authors have licenses and (b) their architecture
fits all platform-specific behavior under a single layer. Point (b) is achievable for CNA right
now, without any NDA—and it is also the only part that can be done in advance.

---

## One-sentence summary

Console support **can** be added to CNA, and the Android NDK has nothing to do with it; on modern
consoles (Switch, PS4/PS5, Xbox), an official SDK under NDA is an **unavoidable and absolute
requirement**, while public homebrew toolchains are sufficient for older consoles—but in both
cases, the real entry ticket is the separation of CNA from SDL3 described in `cnaplatform.md`, not
the console API itself.
