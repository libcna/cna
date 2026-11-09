# What a CNA program contains: link boundaries and size

CNA has optional glTF/Draco, avatars, Guide, media, capture, content, platform and renderer
subsystems. A statically composed consumer can leave unused families out. Archive extraction
works at object-file granularity: any unresolved symbol, including a virtual table or a weak
standard-library definition, can bring the rest of that member's dependencies along. The boundaries
below and the representative footprint gates keep those accidental references under control.

This applies to static module composition. The `CNA` umbrella can instead use `libcna.so`, which
deliberately includes the enabled runtime closure; a small executable then does not imply a small
total deployment. Section garbage collection and LTO are separate from archive extraction, and
a dependency graph or a passing sample is not a universal promise for every caller/linker/toolchain.

| Question | Answer |
|---|---|
| Does this design itself disable features? | No. Actual availability still depends on enabled modules, selected renderer/platform, dependencies and their documented limits. |
| Can a 2D game omit glTF, Draco, avatars and MediaPlayer? | Yes, with the static composition and calls represented by `LinkFootprint_2DGame`; a shared umbrella or an explicit registry query has a broader closure. |
| Does a game that uses them get them? | Yes. `LinkFootprint_FullFeatures` checks a representative set (glTF, the Model reader, Draco where built, avatars and the catalog, the system Guide, media, capture, streaming audio, cube and volume textures); the full test suite exercises the rest. |
| Must optional features be disabled to keep the tested static closure small? | No. Source boundaries are tested with those features built; shared/static deployment, optimization and dependency choices still affect total size. |

## Why programs used to contain everything

Measured on Mobile Eggbert, a 2D game using textures, sounds, `Guide.IsTrialMode` and
`Guide.ShowMarketplace` (Linux x86-64 Debug, OPENGLES3, 2026-10-12), a static program once linked
208 MB (44.6 MB stripped), of which 30 MB was the embedded avatar catalog. No flag was missing:
single structural references inside translation units the game legitimately needed pulled whole
families in. One `Guide` call reached the service client, libcurl and the avatar catalog; the
avatar GLB reader carried the glTF importer and Draco; `ContentManager` and `Game` registration
named every reader; `FrameworkDispatcher::Update` called streaming audio, capture and media
directly; `SpriteBatch::Begin`'s `dynamic_cast` named all five stock effects. Each such reference is
cut where the dependency is not real; the boundaries below record where.

## The boundaries

| Subsystem | Lives in | Reached only by |
|---|---|---|
| Built-in avatar catalog (catalog v3, 22.3 MB deflated at revision 5) | library `cna_gamerservices_avatar_catalog` (`EmbeddedAvatarCatalog.cpp`) | `AvatarCatalog.cpp`: avatar API and the system Guide |
| System Guide: home, friends, party, messages, achievements, leaderboards, players, settings, avatar views | `GuideSocial.cpp`, `GuideParty.cpp`, `GuideLeaderboards.cpp`, `GuidePortraits.cpp`, installed by `installSystemGuide()` (`GuideSystemShell.cpp`) | `GamerServicesDispatcher::Initialize` (hence `GamerServicesComponent`), the social `Guide.Show*` calls, the Guide button |
| Guide frame, dialogs, license | `GuideScreens.cpp`, `GuideDialogs.cpp`, `Guide.cpp`, `GuideMarketplace.cpp`, `GuideContent.cpp` | any Guide use; reaches the system Guide only through installed hooks |
| Gamer Services service client | `GamerServicesBackend.cpp` (test fake: `GamerServicesFakeBackend.cpp`) | service requests (`GamerRequests.cpp`, `SignedInGamerRequests.cpp`), the dispatcher, sign-in |
| Content readers | one `Content*Loaders.cpp` per asset family | `Load<T>` of that family (`detail::BuiltinContent<T>`) |
| Model, glTF, cgltf importer, Draco | `ContentModelLoaders.cpp`, `GltfImportCore.cpp` (cgltf in `CgltfImplementation.cpp`) | `Load<Model>`, `Load<SkinnedModelEXT>`, `Load<AnimationClipEXT>` |
| Every `.xnb` reader | `XnbBuiltInReaders.cpp` | a `Load<T>` of a type CNA does not know, an untyped external reference, `RegisterBuiltinLoaders()`, and the registry queried by name (`ContentTypeReaderManagerQueries.cpp`, `GetXnbReaderUsageSummary`) |
| Every `.cnb` loader | `CnbLoaderRegistryQueries.cpp` | `CnbLoaderRegistry::Find` and `ResolveForDocument` asked for a built-in loader by hand |
| `.xnb` canonical decoders | `XnbCanonicalTextures/Sound/Model/Asset/Data.cpp` | that family's readers; the pipeline for `Asset` |
| `.cnb` encoders and `CnbWriter` | `Cnb*Encode.cpp`, `CnbModelCodec.cpp`, `CnbModelV2Codec.cpp` (decoders: `Cnb*Codec.cpp`, `CnbModel*Decode.cpp`) | content tools; loading never encodes |
| `TextureCube`, `RenderTargetCube`, `Texture3D` | their own translation units; the device binds through `IRenderTargetBindingSource`, renderers ask `Texture::AsTextureCubeEXT`/`AsTexture3DEXT` | a program that creates, loads or binds one |
| Streaming audio, capture, media updates | slots of `FrameworkDispatcher` (`FrameworkUpdates.hpp`) | the subsystem installs its slot on first use |
| Camera, tray | `Sdl3CameraService.cpp`, `Sdl3TrayService.cpp`, one each, which define the non-virtual `IPlatform::GetCamera()`/`GetTray()`; their support probes are `Sdl3ServiceSupport.cpp` | a call of `GetCamera()`/`GetTray()` (as `CNA::Devices::Camera` and `SystemTray` make); `PlatformCapabilities::camera`/`tray` and `Supports()` report support without either service, and only the camera question starts SDL's camera subsystem |
| Audio capture backend | `AudioRecordingDeviceFactory.cpp` | `Microphone` |
| Other platforms | `PlatformFactory.cpp` (by name) | the platform conformance suite; games use `PlatformFactoryDefault.cpp` |
| Renderers | the generated registry lists only the renderers this build compiles | single renderer by default; a multi-renderer build is requested with `CNA_GRAPHICS_RENDERERS` |
| Networking (ENet) | `CNA_Net` | games that link `CNA::Net` |

Each file at a boundary says so in a short `Link boundary:` comment, so that nobody merges it back.

The original LINK campaign changed CNA's C++ ABI (new virtual functions on `IPlatform`, `Effect`,
`Texture` and render targets). Consumers crossing those historical revisions need a rebuild;
this documentation recheck adds no new ABI change. An `IPlatform` implementation supplies
`GetCameraService()`/`GetTrayService()` rather than overriding the non-virtual public accessors.

## How to add a feature without growing every game

- **No central list of implementations.** A function that names every reader, every platform or
  every panel links all of them into whatever links it. Select at build time (a generated table,
  as the renderer registry does), by type (`detail::BuiltinContent<T>` is a template the caller
  instantiates), or by installation (below).
- **Declare what an API promises, install it when it is needed.** `Game` once registered every
  `.xnb` reader and `ContentManager` every `.cnb` loader. Now they declare the built-ins
  registered; a type's reader is installed by its first load. The narrow load path uses the
  registered family without installing unrelated ones. Public by-name XNB queries
  (`IsRegistered`, `CreateReader`, `RemoveTypeCreatorEXT`) live in their own translation unit
  and first install declared readers; explicitly asking them can therefore link every family.
  CNB's public `Find`/`ResolveForDocument` likewise install declared loaders when needed. The load path uses lookups that never install anything. Removal and
  re-registration keep their old meaning: once everything is installed a load installs nothing,
  so a removed reader stays removed until the built-ins are registered again (a new `Game`, a
  new `ContentManager` or `RegisterBuiltinLoaders()` for `.cnb` loaders). The registries'
  observable answers are pinned by `ContentTypeReaderManagerTests` and the `.cnb` registry tests,
  which cover removal followed by a load.
  Standalone `ContentManager`/`ResourceContentManager` loads also install their type's XNB family
  without requiring a `Game` or declaring unrelated families. Lazy installation honors explicitly
  withdrawn reader names even when a later custom graph needs every family. Explicit registration
  or a new `Game` can restore them; a factory registered before a declaration retains priority.
- **A family installs every reader its files may name.** A real pipeline's `.xnb` names value
  readers beside the asset's own -- a Song's duration goes through `Int32Reader`, a Video's name,
  size and frame rate through `StringReader`, `Int32Reader` and `SingleReader`, a Model's Tag may
  hold any value, a TimeSpan included -- and `ContentReader` refuses a table entry with no reader
  rather than installing one. So a family registers them with its own
  (`detail::RegisterValueXnbReaders()`, the Decimal/DateTime/TimeSpan readers for models).
  `ContentManagerGamePathXnbTests` loads a Song, a Video and a Model first from a `Game`'s
  declared-only state -- the three families whose first load used to fail -- and
  `ContentXnbReaderFamilyClosureTests` pins the closures themselves: the families together install
  exactly what `RegisterAllBuiltInXnbReaders()` installs, so no built-in reader is reachable only
  by asking the registry by name, and for every committed `.xnb` fixture the root reader's family
  alone installs every CNA-owned reader the fixture's type-reader table names.
- **Install optional work when it is first used.** `FrameworkDispatcher` and the Guide frame hold
  plain function pointers that the optional subsystem sets on its first use. No static
  "registrar" object: in a static archive nothing references it, so it is dropped, and the usual
  cure, `--whole-archive`, would link everything. No allocation or lookup in the frame loop.
- **One translation unit per family.** Split a large `.cpp` where its functions serve different
  callers. Methods of one class may live in several files.
- **Ask, do not cast, in always-linked code.** `dynamic_cast<Concrete*>` references the concrete
  class's typeinfo and so links the class. Use a virtual query (`Effect::IsStockEffectEXT`,
  `Texture::AsTextureCubeEXT`) or an interface the concrete classes implement
  (`IRenderTargetBindingSource`), whose typeinfo every user emits for itself.
- **An optional platform service is reached through a non-virtual accessor.** A virtual accessor
  links whatever it returns into every program that links the platform. `IPlatform::GetCamera()`
  and `GetTray()` are plain functions, each in the translation unit that creates the service, so
  the call itself links it and a program that never calls them contains neither; the platform
  answers through a protected virtual. Whether it is supported is a separate small probe, so the
  capability keeps its meaning without linking the service. Where a question has a cost (the
  camera's starts SDL's camera subsystem), ask `IPlatform::Supports()` for the capability needed
  rather than reading the set.
- **Large data is its own library**, referenced by the one function that serves it.
- **Test doubles are their own translation unit**, referenced only by tests.
- **Pass capabilities instead of looking them up.** The service client does not ask the avatar
  catalog what it can draw; the avatar code passes `AvatarClientCatalogs` with the request.

## Build options

These choose build availability, optimization and deployment. The representative static probes
keep unrelated families out without disabling those families; the shared umbrella intentionally
has a larger runtime closure.

| Option | Default | Effect |
|---|---|---|
| `CNA_ENABLE_GAMER_SERVICES` | `CNA_ENABLE_NET`'s value | Builds GamerServices (Guide, profiles, avatars). A Guide-only game can build without networking. |
| `CNA_ENABLE_NET` | ON | Builds `CNA_Net` and ENet; requires GamerServices. |
| `CNA_OPTIMIZE_FOR_SIZE` | OFF | Non-MSVC native GCC/Clang: per-function/data sections and a propagated `--gc-sections` (`-dead_strip` on Apple) final-link option. It does not itself choose `-Os`/`-Oz`. MSVC Release and wasm-ld already have their own dead-stripping defaults. |
| `CNA_ENABLE_IPO` | OFF | IPO/LTO on CNA-owned artifacts in Release, RelWithDebInfo and MinSizeRel, after a compiler support check. Refused with sanitizers, cross/Emscripten builds or a packaged static C API. |
| `CNA_SHARED_LIBRARY` | ON for supported native non-Apple Unix GNU/Clang configurations with CMake ≥3.30; OFF otherwise | The `CNA` umbrella links its enabled runtime closure WHOLE_ARCHIVE into `libcna.so` for development/relink efficiency. Module-only consumers remain static. Mixing an umbrella/shared consumer with explicit modules is handled by the shared-runtime ordering/exclusion policy to avoid duplicate singleton state. The C adapter can fold Net/GamerServices into that DSO too. An unsupported explicit ON is refused. |
| `CNA_CXX_RUNTIME_LINKAGE` | AUTO | How the programs that link CNA link the C++ runtime. AUTO reads each program's flags and those of the targets it links, per configuration; `STATIC`/`SHARED` decide instead, for a runtime AUTO cannot read ("Linker independence"). |
| `CNA_ENABLE_DRACO`, `CNA_ENABLE_VIDEO` | ON except Emscripten / AUTO | Decoder build availability. A static consumer represented by the narrow footprint can omit an unused family; the shared umbrella, central queries or other references can include it. |

A size-oriented Release consumer: `-DCMAKE_BUILD_TYPE=MinSizeRel` (or Release)
`-DCNA_SHARED_LIBRARY=OFF -DCNA_OPTIMIZE_FOR_SIZE=ON`, optionally `-DCNA_ENABLE_IPO=ON`.

## A minimal consumer

```cmake
add_subdirectory(path/to/cna CNA)
add_executable(MyGame ...)
target_link_libraries(MyGame PRIVATE CNA::Runtime)            # Game, graphics, input, audio, content
target_link_libraries(MyGame PRIVATE CNA::GamerServices)      # only if it uses the Guide
```

`CNA::Runtime` brings the selected renderer and platform. Its static reference closure depends on
the calls, virtual tables and implementation dependencies, not only the list of module archives.
`tests/link_footprint/footprint_2d_game.cpp` is the representative consumer. Do not additionally
link the shared `CNA` umbrella when the goal is to inspect this static-only closure.

## Checking a binary

- **Sizes.** `size -A game`, `objcopy --strip-debug game game.nodebug`, `strip -o game.stripped
  game`. Debug information is most of a Debug executable and is not shipped; compare stripped
  sizes, and `.rodata` for embedded data.
- **Why is X linked?** Ask the linker that produced the binary, with the same command line: mold
  `-Wl,--print-dependencies` (every reference between input files), lld
  `-Wl,--why-extract=why.txt` (why each archive member was extracted), GNU ld `-Wl,-Map=game.map`
  and then `tools/link_audit/why_linked.py game.map --why X`, which prints the chain of references
  that pulled X in (`--top 40`/`--modules` rank what costs the most). Relinking with a different
  linker only to get a map can hide the problem: an extraction may happen with one linker and not
  another (see "Linker independence"). `tools/link_audit/link_closure.py` models references among
  the supplied objects/archives, grouping archives and choosing the first definer for each symbol.
  It can expose alternative references a first-extraction map hides, but does not model every DSO,
  link-order, LTO or garbage-collection rule; the actual link remains authoritative.
- **Regression gates.** `LinkFootprint_2DGame` fails if a 2D game links any of the subsystem
  families it does not use (the list is in the CMake file); `LinkFootprint_GamerServicesGame` pins
  what the same game with a `GamerServicesComponent` contains (the system Guide and avatars, and
  none of the families it does not use); `LinkFootprint_FullFeatures` fails if a game that uses the
  listed subsystems lacks any of them -- it is a representative check that the boundaries do not
  cut a used feature, not a test of every CNA feature;
  `LinkFootprint_CapabilityQuery` fails if asking whether a camera or a tray is supported links
  either service, and `LinkFootprint_CameraOnly`/`_TrayOnly` if using one links the other
  (`cmake/Tests/LinkFootprint.cmake`, `scripts/check_link_footprint.py`). These consumers are
  built and inspected with the toolchain's `nm`, never run: the checks match defined/imported
  symbol names against selected required/forbidden patterns, with no absolute byte ceiling or
  transitive DSO-size accounting. They require tests and GamerServices, exclude Emscripten/Android,
  and require a usable toolchain `nm` for cross builds; camera/tray probes additionally need SDL3.
  No Python/tool means absent/skipped gates according to the configured conditions, not a pass.
  Module probes (`ModuleLinkClosure_*`) instead inspect generated link lines for archive patterns.

## Linker independence

GNU ld, lld and mold must retain the same tested family closure. On ELF with a shared C++ runtime,
a symbol that a shared library needs can be defined both by an unextracted CNA archive member (CNA
objects emit weak `std::string` definitions that libstdc++ also exports) and by the runtime. mold
resolves such a symbol to whichever file comes first on the command line, and the compiler driver
names the C++ runtime last, so mold could extract an unrelated CNA member — for example, to satisfy
`libshaderc.so`'s need for `std::string::_M_erase` in a multi-renderer build — and drag the service
client, the system Guide and the avatar catalog into a 2D game that GNU ld or lld keep small (LINK-032/040).

So a program that links CNA's targets with a shared C++ runtime on ELF names the runtime before
CNA's archives: `CNA::BuildConfig` adds `-Wl,--push-state,--as-needed -lstdc++ -Wl,--pop-state`
(`-lc++` for libc++) to its link. A standard-library symbol that no object already in the link
defines is then the shared runtime's, and no archive member is extracted for it alone. A member
extracted for a CNA symbol may still supply one, which links nothing more: mold may resolve
libshaderc's `_M_erase` to `GuideStyle.cpp.o`, which the game links anyway (`GuideContent.cpp.o`
uses its styles). With the option the three linkers extract the same members in the footprint
probes. A program that names CNA's archives on a hand-written link line instead of linking its
targets does not get it.

The option is only for a shared runtime, as lld and mold would let a shared runtime named first
replace a static one. On applicable ELF GNU/Clang links (excluding Android), with an implicitly detected stdc++ or c++
runtime, it is decided for each program in each configuration in this order. This option selects
ordering; `STATIC`/`SHARED` here do not themselves select the C++ runtime to link:

1. `-DCNA_CXX_RUNTIME_LINKAGE=SHARED` gives every program the option and `STATIC` gives none,
   whatever their flags say. This is how a runtime that `AUTO` cannot read is declared.
2. `AUTO`, the default, reads at the end of the configure: the program's directory's
   `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS` (`_SHARED_`/`_MODULE_` for libraries) with
   their per-configuration variants, which include a toolchain file's and `LDFLAGS`' initial
   flags; the program's `LINK_OPTIONS`, `LINK_FLAGS` and `LINK_LIBRARIES`; and the
   `INTERFACE_LINK_OPTIONS` and `INTERFACE_LINK_LIBRARIES` of every target those libraries reach,
   through aliases and static libraries. Every item that names `-static`, `-static-pie`,
   `-static-libstdc++`, `-nostdlib`, `-nostdlib++` or `-nodefaultlibs` is copied with generator
   expressions kept, and CMake evaluates the copy for the program in each configuration: a program
   with `$<$<CONFIG:Release>:-static-libstdc++>` has no option in Release and keeps it in Debug
   (reading the flag as text instead would take the option away in Debug too, and mold would then
   link the system Guide and the avatar catalog). `-static-libgcc` alone keeps the option, in a
   generator expression too: libstdc++ stays shared.
3. Where `AUTO` reads only part of the answer, it errs towards static, which only costs the
   option. A target linked in some configurations only is read in all of them. CMake shows an
   imported target only in the directory that defines it and below, so one that a subdirectory
   defines cannot be read: CNA's own (the system libraries it finds) choose no runtime, and a
   program that links any other does not get the option, which configure reports with the
   targets' names (declare `SHARED` if their runtime is shared).
4. What no CMake setting shows -- a compiler wrapper or spec file, a flag that a generator
   expression computes rather than names, a static runtime named by hand
   (`-Wl,-Bstatic -lstdc++` with no `-static-libstdc++`, which is a linker input rather than a
   driver flag) -- `AUTO` cannot see, and the program is taken to link the shared runtime; declare
   `STATIC`. Left to `AUTO`, such a program gets the option after its hand-named static runtime,
   and lld then links `libstdc++.so` it did not ask for (mold and GNU ld keep the static one).

`CnaCxxRuntimeLinkage` (`cmake/Tests/CxxRuntimeLinkageCase.cmake`) configures programs that ask
in the ways 1-3 list, with single- and multi-configuration generators, checks their link lines,
links some with mold and lld in Debug and Release, and reads which C++ runtime each executable
needs.

With a static runtime CNA adds nothing, and where the runtime archive goes is the program's
choice. The driver names it last, so lld and mold resolve a standard-library symbol to the first
archive member on the command line that defines it, which is CNA's, and can link large unrelated
families (glTF and Draco, or the system Guide and the avatar catalog). Which members ride along
depends on the tree — whichever member of whichever archive comes first with the symbol a shared
library needs. Naming the static runtime first,
`target_link_options(MyGame PRIVATE "SHELL:-Wl,--push-state,-Bstatic -lstdc++ -Wl,--pop-state")`,
gives each linker the small closure again, with every footprint gate passing.

## Built-in avatar catalog: embedded, not a separate pack

An external `avatar_catalog` pack was considered and not adopted. The catalog is its own library,
referenced by avatar code; the representative static 2D probe omits it. A shared umbrella that
includes avatar users can carry it even when a particular application does not draw an avatar. A game with a `GamerServicesComponent` does draw them: its
players open the system Guide with Home or the Guide button, and its panels show their avatars,
drawn from the baseline catalog v3 (22.3 MB deflated at revision 5; the 30 MB measured above, on
2026-10-12, also held the development catalogs 1 and 2, no longer shipped). Keeping it embedded preserves single-file deployment and
offline avatars for programs that do use them. External packs already exist for catalog *updates*
(`installedCatalogRoot()`, `CNA_GAMER_SERVICES_CATALOGS_DIR`), verified by size and SHA-256.

## What a 2D game still contains, and why

In a 2D consumer the large members are the renderer, image decoding, the device, `Game`, Sharp
Runtime strings and dialog/marketplace Guide UI. These smaller couplings explain why archive-family
separation alone is not an exact per-method size promise:

| Linked | Size | Through |
|---|---|---|
| sharp-runtime's `Decimal` | ~30 KB | `System::IO::BinaryReader::ReadDecimal` is virtual, as in .NET, and every content read goes through a `BinaryReader`-derived `ContentReader`, so its vtable names it; dropping it would mean making `ReadDecimal` non-virtual |
| MojoShader and compiled-effect support | ~420 KB in the old EasyGL measurement | EasyGL's opt-in compiled path prepares SpriteEffect for custom batches. Current support is renderer-specific: EasyGL/SDLGPU/WEBGPU/SOFTWARE/DIRECTX9 options default OFF, FNA3D has it inherently, HEADLESS/STUB do not. One renderer's option does not enable every other renderer. Profiles and actual closure are build-dependent; see [compiled effects](fx-compiled-effects.md). |
