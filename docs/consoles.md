# CNA on consoles: current boundary

**No CNA console port is implemented or qualified.** This page records portability considerations,
not a porting commitment, supported-target list or certification result. It was reconciled on
2026-11-01 against CNA's build/platform sources, the pinned SDL documentation and public framework
references.

## Target classes

| Target class | Current CNA position |
|---|---|
| Commercial Nintendo, PlayStation and Xbox consoles | No CNA toolchain, package, renderer/account/save integration or device result is qualified. Platform access and applicable agreements are prerequisites for investigating an actual port. |
| Homebrew consoles | No CNA port is qualified. Public toolchains/upstream SDL ports can supply starting components, but do not establish CNA C++23, runtime-library, renderer or memory support. |
| PC handhelds such as Steam Deck or Windows handhelds | Use the corresponding Linux/Windows desktop path. No distinct console port is required by that architecture, but this page records no device-specific compatibility, controls or performance run. |

An Android NDK build is Android evidence, not a console qualification.

## Existing architectural seams

CNA routes host windows/events/input/timing/filesystem/services through IPlatform.
Available CNA_PLATFORM identities are SDL3, HEADLESS and POSIX TERMINAL; there is no console
identity. SDL3 is the graphical platform implementation. See
[platform-abstraction.md](platform-abstraction.md) and
[platform-renderer-boundary.md](platform-renderer-boundary.md).

SDLGPU and FNA3D exist as CNA renderer families, but a console's SDL implementation does not
automatically provide the GPU API, shader path or presentation behavior either renderer needs.
CNA_ENABLE_SDL=OFF permits compatible windowless platform/rendering/audio compositions; it does
not create a console platform. CNA's native C++ runtime avoids a managed-runtime deployment
dependency, while still requiring its C++/Sharp Runtime library and host-service dependencies.

## Upstream console paths and access

The pinned SDL3 README-platforms lists separate NDA-only Nintendo Switch/Switch 2 and
PlayStation 4/5 forks, Microsoft GDK support, and homebrew targets including Vita, PSP, PS2 and
Nintendo 3DS. This is upstream SDL source/documentation evidence, not CNA support.

FNA describes shipping its public framework branch with platform work in SDL and a NativeAOT
bootstrap. MonoGame describes authorized developers accessing separate private console
integrations. These are examples of different project arrangements, not proof that CNA can
build against either arrangement unchanged.
[FNA console architecture](https://fna-xna.github.io/docs/appendix/Appendix-B:-FNA-on-Consoles/),
[MonoGame console access](https://docs.monogame.net/articles/console_access.html).

A future owner-authorized port would first select a concrete target and integration boundary.
Use the platform holder's current developer portal for eligibility, tooling, permitted disclosure,
testing and distribution terms; this page does not infer those rules from another framework's
experience. Do not place third-party restricted SDK material into the public CNA tree.
[Nintendo Developer Portal](https://developer.nintendo.com/),
[PlayStation Partners](https://partners.playstation.net/),
[ID@Xbox](https://developer.microsoft.com/en-US/games/publish/id/welcome).

Retail Xbox Developer Mode/UWP and Xbox GDK deployment are distinct routes. Neither is an
implemented CNA console target; a desktop Windows build does not establish either one.
No current account eligibility or SDK-licence interpretation is claimed here.

## Work an actual port must qualify

| Area | Existing starting point / unresolved console boundary |
|---|---|
| Toolchain and standard library | C++23, Sharp Runtime, threading/clocks, exceptions and filesystem must build and run with the selected toolchain. |
| Title and save access | TitleContainer and StorageDevice/StorageContainer provide XNA APIs; host-directory behavior does not implement a console's title sandbox or account-bound save service. |
| Presentation and rendering | Window/native-handle/surface contracts and a supported renderer must work together on that target. A SOFTWARE path also requires a working presentation service. |
| Effects and shaders | Precompiled asset/effect paths exist for some current profiles, but there is no qualified console shader package/pipeline. FNA's PlayStation notes specifically require shader preprocessing. |
| Audio and input | An actual audio implementation, controllers, disconnection behavior and target confirmation/cancellation policy need validation. GamePad.IsConnected and button-label helpers alone are not certification. |
| Accounts and privileges | XNA-shaped GamerServices APIs exist; no console account/sign-out/privilege service is connected. |
| Lifecycle | Game consumes platform suspension/foreground/termination events; a console event mapping and recovery run are absent. |
| Memory, loading and startup | No console budget/performance result exists. Do not assume context/device loss is impossible or disable recovery based only on device class. |
| Packaging and certification | No console package, store submission or certification result exists. Platform requirements must be evaluated for the selected SDK/game. |

For homebrew, an upstream SDL port narrows some host work; it does not remove the need to validate
the exact graphics API and presentation path. Older hardware/library constraints can rule out a
renderer even when SDL itself builds. The old recommendation of one first console and fixed RAM/
filesystem-use counts was not backed by a CNA port measurement and is not a current plan.

## Verification boundary and related documents

This audit checks repository architecture and public upstream descriptions. It builds no console
binary, uses no private SDK, and claims no handheld hardware run. Current native renderer
qualification is in the individual renderer pages; no console is implied by that qualification.

- [physical-modules.md](physical-modules.md): library composition and dependency boundaries.
- [filesystem-path-model.md](filesystem-path-model.md): current path representation.
- [renderer-registry.md](renderer-registry.md): renderer identities and addition policy.
- [SDL3 platform overview](https://wiki.libsdl.org/SDL3/README-platforms): upstream inventory
  (the matching pinned local README was checked; the live wiki could not be fetched in this audit).
