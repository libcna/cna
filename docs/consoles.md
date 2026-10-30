# CNA on game consoles

Can a CNA game run on a game console, and what would it take? This page answers that for Nintendo,
PlayStation and Xbox hardware and for the PC handhelds that look like consoles. No console port
exists today, and none is planned; the page records what is possible, what is in the way, and the
rules any console work has to follow from its first day.

External facts (SDK programmes, SDL's platform list, FNA's and MonoGame's practice) were checked on
**2026-10-25**. Platform holders change their terms; check their portals before deciding anything.

## The short answer

| Class | Consoles | What you need | CNA's position |
|---|---|---|---|
| **A — current commercial consoles** | Switch, Switch 2, PS4, PS5, Xbox One, Xbox Series | The platform holder's SDK under NDA, a developer account and a devkit | Architecturally ready for the FNA model (below); nothing can start without a licence |
| **B — consoles with open homebrew SDKs** | PS Vita, PSP, PS2, 3DS; Wii, GameCube, Dreamcast, Xbox (2001), PS3 | A public homebrew toolchain (VitaSDK, pspdev, PS2SDK, devkitPro, KallistiOS, nxdk, PSL1GHT) | Possible in principle; blocked today by toolchain, filesystem and renderer gaps |
| **C — PC handhelds** | Steam Deck, ROG Ally, Legion Go and similar | Nothing new | Works today: they are x86-64 Linux or Windows PCs |

If the goal is "a CNA game on a living-room device as soon as possible", class C is the answer: it
needs a gamepad-first interface and a fullscreen default, not a port.

The Android NDK has nothing to do with consoles. CNA uses it for Android only.

## Where CNA stands

The hard part of a console port used to be that CNA *was* SDL3 on a desktop. That is no longer
true:

- **The platform is an interface.** Windows, events, input, timing, storage and host services go
  through `CNA::Platform::IPlatform`. SDL3 is one implementation of it, beside `HEADLESS` and
  `TERMINAL`, and SDL calls exist only in the deliberate edges (`modules/platform/src/Sdl3/`, the
  SDL3 audio backend and the `sdl-gpu` and `fna3d` renderers). See
  [`platform-abstraction.md`](platform-abstraction.md), which also describes how to add an
  implementation.
- **SDL3 is the one graphical platform.** Where SDL3 runs, CNA uses it rather than a backend of its
  own. That matters for consoles, because SDL3 already reaches most of them (next section).
- **The graphics stack matches FNA's.** CNA has an `SDLGPU` renderer and an `FNA3D` renderer, the
  graphics path FNA ships with.
- **Builds can leave SDL out.** `CNA_ENABLE_SDL=OFF` gives a windowless build (`HEADLESS`/`TERMINAL`),
  and `CNA_AUDIO_PLATFORM=ALSA` has CNA's own mixer. Neither is a console platform, but both show
  that nothing outside the edges depends on SDL.
- **CNA is native code.** FNA and MonoGame had to bring a .NET runtime to consoles that forbid JIT
  compilation (Mono AOT, now NativeAOT). CNA has no managed runtime to port.

## Class A: consoles under NDA

### What the SDKs and SDL provide

SDL3 lists Switch and Switch 2, PS4 and PS5 as "separate NDA-only forks": the ports exist, under the
zlib licence, but only developers who already have an NDA with Nintendo or Sony can get them. Xbox
goes through the Microsoft GDK, which SDL3 supports. The console extensions of the GDK need a GDK
agreement with Microsoft.

### The FNA model, and why CNA should follow it

There are two ways a framework can ship on NDA consoles:

- **MonoGame** keeps one private repository per console and gives access to developers its vendors
  have approved. The console code can then drift from the public tree.
- **FNA** has no private branches at all: "the public master branch of FNA is exactly what is used
  to ship for these targets". All platform code lives in SDL's NDA port and in the NativeAOT
  bootstrap underneath.

CNA's layering makes the FNA model achievable: a licensed developer would take public CNA, bring
the NDA SDL port for the console, and build. What that has not been tested against, and cannot be
without an SDK, is whether the console's SDL port carries everything the `SDLGPU` renderer needs.

### Rules that apply before any code exists

1. **Nothing from an SDK goes into the public repository**: no header content, no function or
   symbol name, no shader ABI detail, no build script that names devkit paths. A single commit that
   breaks this cannot be undone, so where private code would live (a private repository connected
   like an optional external checkout) is decided before work starts.
2. **NDA material must not reach an AI service.** SDK documentation, headers, devkit logs and
   screenshots may not be disclosed to third parties, and a cloud AI assistant is a third party.
   NDA console work is therefore done without such assistants, whatever CNA's general policy on AI
   is. Confirm the exact wording against the agreement you sign.
3. **Devkits are used, emulators are not.** Emulators of current consoles are no development
   environment, and using them typically breaks the licence.
4. **A licence is granted for a game, not for a framework.** "Here is my game, its screenshots and
   its release plan" is an application; "I want to port my framework" is not. Offering CNA to other
   licensed developers is a further step: the platform holders run separate middleware programmes.

### Getting access

| | Nintendo | Xbox | PlayStation |
|---|---|---|---|
| Portal | [developer.nintendo.com](https://developer.nintendo.com/) | [ID@Xbox](https://developer.microsoft.com/en-US/games/publish/id/welcome) and Partner Center | [partners.playstation.net](https://partners.playstation.net/) |
| Individual developers | Yes: personal details are enough | A company is practically expected | A company is practically expected |
| What follows registration | An access request describing your experience and planned project | Approval of the studio and its plan | Approval of the team and its plan |
| Note | MonoGame reports no open registration for Switch 2 | Developer Mode on a retail console needs no NDA (below) | No equivalent of Xbox Developer Mode |

Expect to provide a verifiable identity, a working studio website and a project description, and to
keep devkits physically secure and their output undisclosed.

**Xbox Developer Mode** is the one route onto a current console without an NDA: a retail Xbox One or
Series console, the Xbox Dev Mode app and a Partner Center app-developer account. It runs UWP apps,
not Win32 games. Microsoft now steers games to the GDK, and SDL3 does not support UWP (SDL2 did).
For CNA it would mean a platform implementation of its own, which is an owner decision, not a plan.

### What a licensed port would still need from CNA

- **Precompiled shaders.** FNA's PlayStation notes say shaders "must be precompiled ahead of time".
  CNA translates at load time today: XNA effects go through MojoShader, and the `SDLGPU` renderer's
  built-in SPIR-V goes through SDL_shadercross where a driver needs another format. A console
  profile needs a precompiled path.
- **The lifecycle and policies in the [certification checklist](#what-certification-asks-of-a-framework).**

## Class B: homebrew consoles

SDL3's public tree supports **PS Vita, PSP, PS2 and 3DS** as homebrew targets, so on those four CNA
needs no platform implementation of its own: its SDL3 platform has to survive the cross-compile and
the hardware. Wii, GameCube, Dreamcast, the original Xbox and PS3 have no SDL3 port; each would need
a new `IPlatform` implementation, a renderer and an audio backend.

**PS Vita is the sensible first target**: a public SDL3 port, a maintained toolchain with a CMake
toolchain file, and 512 MB of RAM. A first milestone is a `Game` that runs, a `SpriteBatch` that
draws a texture, a working gamepad and a `ContentManager` that reads from the read-only title path.
What stands in the way today:

1. **Toolchain.** CNA is C++23 and has begun to use C++23 library features (`std::format`). The
   homebrew toolchain's GCC has to be new enough, and so does its standard library.
2. **`std::filesystem`.** It is CNA's deliberate representation for every path
   ([`filesystem-path-model.md`](filesystem-path-model.md)), with over 4,000 uses. Newlib-based
   homebrew toolchains support it partly or not at all, and console paths (`ux0:`, `sdmc:`,
   `host0:`) and read-only title sandboxes do not look like desktop paths.
3. **Sharp Runtime.** CNA's .NET library layer is written against a complete desktop standard
   library, so a restricted console environment is likely to fail there first: file streams, clock
   conversions, threading.
4. **Rendering.** None of CNA's GPU renderers fits: the GL renderers need OpenGL ES 3 or OpenGL 3.3,
   while these consoles offer fixed-function or GL1-level APIs (vitaGL, pspgl, picaGL, gsKit), and
   `SDLGPU` has no backend for them. `SOFTWARE` is the realistic first renderer. A GPU renderer
   would be a new renderer family, and CNA adds renderers only for clear platform value
   (`renderer-registry.md`).
5. **Memory.** PSP and PS2 have 32 MB (PSP-2000 and later 64 MB), the 3DS 128 MB (New 3DS 256 MB),
   the Dreamcast 16 MB. Consoles do not lose their graphics context, so call
   `GraphicsDevice::SetContextRecoveryEnabled(false)` before the device initializes: a
   non-compressed `Texture2D` then frees its level-0 CPU copy after upload. Beyond that, CNA has no
   memory-budget concept yet.
6. **Build profile.** Tests, examples, networking, devices and video off; static linking. Native
   Linux builds default to a shared `libcna.so`; a cross-compiled console build links statically
   (`CNA_SHARED_LIBRARY=OFF`). The few `dlopen` calls belong to Linux host services (ALSA, the
   credential store, an X11 error handler) that a console build would not contain.

Homebrew toolchains are public, so this work can live in the public repository and its CI. It
never touches a platform holder's SDK, and a homebrew build is not a route to a platform's store.

## What certification asks of a framework

Certification (Nintendo Lotcheck, Sony TRC, Xbox XR) judges the game, but a framework can make
passing it impossible. This is what each requirement needs and what CNA has:

| Requirement | CNA today |
|---|---|
| Suspend and resume | The platform reports `WillEnterBackground`, `DidEnterForeground`, `LowMemory` and `Terminating`; `Game` already waits while suspended on Android and iOS. A console lifecycle would map onto this. |
| Controller disconnect | XNA's `GamePadState.IsConnected` |
| User selection and sign-out | The XNA `GamerServices` API exists; nothing connects it to a console's account system. |
| Confirm and cancel buttons | Missing. Nintendo swaps A and B against Xbox, and PlayStation uses ✕ or ○ by region; this must be platform policy, never a constant in game code. |
| Read-only title content | `TitleContainer` accepts title-relative names only ([`filesystem-path-model.md`](filesystem-path-model.md)). |
| Separate, account-bound save data | XNA's `StorageDevice`/`StorageContainer` exist, mapped to host directories; a console's save service is not connected. |
| Precompiled shaders | Missing (see class A). |
| Startup time, memory budget | Not measured on any console. |

The missing rows are the work that can be done **without any SDK**, and that would help every
console class at once.

## Mapping consoles to renderers

| Console | Renderer |
|---|---|
| Switch, Switch 2, PS4, PS5, Xbox One/Series | `SDLGPU` (or `FNA3D`) over the console's NDA SDL port: to be confirmed with the SDK |
| PS Vita, PSP, PS2, 3DS | `SOFTWARE` through SDL3; a GPU path would be a new renderer family |
| Wii, GameCube, Dreamcast, Xbox (2001), PS3 | `SOFTWARE` at best, after a new platform implementation |
| Steam Deck and other PC handhelds | Any desktop renderer, as today |

## Further reading

- [`platform-abstraction.md`](platform-abstraction.md): the platform contract and how to add an
  implementation.
- [`renderer-registry.md`](renderer-registry.md) and [`removed-renderers.md`](removed-renderers.md):
  the curated renderer set.
- [SDL3 supported platforms](https://wiki.libsdl.org/SDL3/README-platforms).
- [FNA on consoles](https://fna-xna.github.io/docs/appendix/Appendix-B%3A-FNA-on-Consoles/).
- [MonoGame console access](https://docs.monogame.net/articles/console_access.html).
- [Xbox Developer Mode activation](https://learn.microsoft.com/windows/uwp/xbox-apps/devkit-activation).
