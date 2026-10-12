# What a CNA program contains: link boundaries and size

CNA *supports* glTF, Draco, avatars, the system Guide, media, video, microphones, XNB, CNB,
every platform and every renderer. That does not mean every CNA program *contains* them. Each
CNA module is a static archive; a static link copies an archive member into the executable only
when something already in the executable references a symbol it defines. A feature a program does
not use is not referenced, so it is not linked. This document is how CNA keeps that true, and how
to check it.

| Question | Answer |
|---|---|
| Does the default build still support everything? | Yes. No feature is disabled, stubbed or moved behind a flag by this design. |
| Does a 2D game pay for glTF, Draco, avatars or MediaPlayer? | No, unless it calls them (`LinkFootprint_2DGame` checks it). |
| Does a game that uses them get them? | Yes. `LinkFootprint_FullFeatures` checks a representative set (glTF, the Model reader, Draco where built, avatars and the catalog, the system Guide, media, capture, streaming audio, cube and volume textures); the full test suite exercises the rest. |
| Do I need to switch features off to get a small game? | No. Options exist for build time and deployment, not for correctness of the closure. |

## Why programs used to contain everything

Measured on Mobile Eggbert, a 2D game using textures, sounds, `Guide.IsTrialMode` and
`Guide.ShowMarketplace` (Linux x86-64 Debug, OPENGLES3, 2026-10-12): 208 MB, 44.6 MB stripped, of
which 30 MB was the embedded avatar catalog. Every one of these was a single reference inside a
translation unit the game legitimately needed:

| The game used | which referenced | and so linked |
|---|---|---|
| `Guide::getIsTrialModeProperty` | `Guide.cpp` held every Guide call; one called `backend()` | the service client, its test fake, libcurl, local profiles, the avatar codec, the 30 MB catalog |
| `cgltf_parse` (avatar GLB reader) | `CGLTF_IMPLEMENTATION` lived in `GltfImportCore.cpp` | the 700 KB glTF importer and the Draco decoder |
| the Guide frame | rail navigation named every panel; the identity rail drew `AvatarRenderer` portraits | the whole system Guide and the avatar editor |
| any `ContentManager` | `RegisterBuiltinLoaders()` named every reader and builder | Model, SkinnedModel, glTF, Draco, Song, Video, FFmpeg |
| `Game` | its constructor called `RegisterAllBuiltInXnbReaders()` | every `.xnb` reader |
| any `.xnb` read | `ContentReader.cpp` instantiated `ReadExternalReference<Effect>` | the Effect content family |
| `FrameworkDispatcher::Update` (every frame) | direct calls into `DynamicSoundEffectInstance`, `Microphone`, `MediaPlayer` | streaming audio, SDL audio capture, the media module |
| the playback device factory | the capture factory in the same file | the SDL recording device |
| `PlatformFactory::Create()` | forwarded to `Create(name)`, which names every implementation | the Headless and Terminal platforms |
| `SpriteBatch::Begin` | `dynamic_cast` to each of the five stock effects | all five effect classes (their typeinfo) |
| `Texture2D`/`SoundEffect` `.xnb` readers | `XnbCanonicalData.cpp` held every decoder | the model and effect conversions |

None of these was a missing flag. Each was a structural reference, and each is now cut where the
dependency is not real.

## The boundaries

| Subsystem | Lives in | Reached only by |
|---|---|---|
| Built-in avatar catalog (~30 MB) | library `cna_gamerservices_avatar_catalog` (`EmbeddedAvatarCatalog.cpp`) | `AvatarCatalog.cpp`: avatar API and the system Guide |
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

This work changed CNA's C++ ABI (new virtual functions on `IPlatform`, `Effect`, `Texture` and the
render targets): rebuild C++ consumers against this revision (CHANGELOG.md). Source compatibility
was kept, except that an `IPlatform` implementation overrides `GetCameraService()` and
`GetTrayService()` instead of `GetCamera()` and `GetTray()`.

## How to add a feature without growing every game

- **No central list of implementations.** A function that names every reader, every platform or
  every panel links all of them into whatever links it. Select at build time (a generated table,
  as the renderer registry does), by type (`detail::BuiltinContent<T>` is a template the caller
  instantiates), or by installation (below).
- **Declare what an API promises, install it when it is needed.** `Game` once registered every
  `.xnb` reader and `ContentManager` every `.cnb` loader. Now they declare the built-ins
  registered; a type's reader is installed by its first load; the registries answer `IsRegistered`
  and type names from the declaration, and install everything only when asked for a reader or
  loader object by hand -- in their own translation units, so only a program that asks links
  everything. The load path uses lookups that never install anything. Removal and
  re-registration keep their old meaning: once everything is installed a load installs nothing,
  so a removed reader stays removed until the built-ins are registered again (a new `Game`, a
  new `ContentManager` or `RegisterBuiltinLoaders()` for `.cnb` loaders). The registries'
  observable answers are pinned by `ContentTypeReaderManagerTests` and the `.cnb` registry tests,
  which cover removal followed by a load.
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

These change what is *built* or how it is optimized. None is needed to keep an unused feature out
of a program.

| Option | Default | Effect |
|---|---|---|
| `CNA_ENABLE_GAMER_SERVICES` | `CNA_ENABLE_NET`'s value | Builds GamerServices (Guide, profiles, avatars). A Guide-only game can build without networking. |
| `CNA_ENABLE_NET` | ON | Builds `CNA_Net` and ENet; requires GamerServices. |
| `CNA_OPTIMIZE_FOR_SIZE` | OFF | GCC/Clang: compiles CNA with `-ffunction-sections -fdata-sections` and gives every program that links CNA `--gc-sections` (`-dead_strip` on Apple). MSVC (`/Gy`, `/OPT:REF`) and wasm-ld already collect unreferenced code in Release. |
| `CNA_ENABLE_IPO` | OFF | LTO for CNA's Release targets. |
| `CNA_SHARED_LIBRARY` | ON on native Linux | Links the runtime into `libcna.so` with every subsystem in it -- the development trees' choice, to stop 800 test executables each carrying a copy. A size-oriented deployment links CNA statically (`OFF`); a program that links module targets (`CNA::Runtime`, `CNA::GamerServices`) is static either way. |
| `CNA_CXX_RUNTIME_LINKAGE` | AUTO | How the programs that link CNA link the C++ runtime; AUTO reads each program's flags. `STATIC`/`SHARED` declare a runtime they do not show ("Linker independence"). |
| `CNA_ENABLE_DRACO`, `CNA_ENABLE_VIDEO` | ON / AUTO | Whether the decoder is built at all; a program that does not load models or video does not link it regardless. |

A size-oriented Release consumer: `-DCMAKE_BUILD_TYPE=MinSizeRel` (or Release)
`-DCNA_SHARED_LIBRARY=OFF -DCNA_OPTIMIZE_FOR_SIZE=ON`, optionally `-DCNA_ENABLE_IPO=ON`.

## A minimal consumer

```cmake
add_subdirectory(path/to/cna CNA)
add_executable(MyGame ...)
target_link_libraries(MyGame PRIVATE CNA::Runtime)            # Game, graphics, input, audio, content
target_link_libraries(MyGame PRIVATE CNA::GamerServices)      # only if it uses the Guide
```

`CNA::Runtime` brings the selected renderer and platform. What the game then contains follows from
what it calls; `tests/link_footprint/footprint_2d_game.cpp` is a complete example.

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
  another (see "Linker independence"). `tools/link_audit/link_closure.py` lists *every*
  reference into a member, so a cut can be planned against all of them.
- **Regression gates.** `LinkFootprint_2DGame` fails if a 2D game links any of the subsystem
  families it does not use (the list is in the CMake file); `LinkFootprint_GamerServicesGame` pins
  what the same game with a `GamerServicesComponent` contains (the system Guide and avatars, and
  none of the families it does not use); `LinkFootprint_FullFeatures` fails if a game that uses the
  listed subsystems lacks any of them -- it is a representative check that the boundaries do not
  cut a used feature, not a test of every CNA feature;
  `LinkFootprint_CapabilityQuery` fails if asking whether a camera or a tray is supported links
  either service, and `LinkFootprint_CameraOnly`/`_TrayOnly` if using one links the other
  (`cmake/Tests/LinkFootprint.cmake`, `scripts/check_link_footprint.py`). The module probes
  (`ModuleLinkClosure_*`) pin which archives each module's consumer links.

## Linker independence

GNU ld, lld and mold must extract the same archive members. They differ in one case that matters
to CNA: a symbol that a shared library needs, defined both by a not-yet-extracted archive member
and by another shared library. From C++20 on, libstdc++ no longer declares `std::string`'s members
instantiated in its shared library, so every CNA object carries weak copies of the ones it uses,
and libstdc++ exports them weakly too. mold resolves such a symbol to whichever file comes first on
the command line, and the compiler driver names the C++ runtime last. In a multi-renderer build,
`libshaderc.so` needs `std::string::_M_erase`; mold extracted `GamerServicesBackend.cpp.o`, the
first member of the first CNA archive that defines it, and the 2D game linked the service client,
the system Guide and the 30 MB avatar catalog (47.1 MB stripped, against 11.7 MB with GNU ld or
lld).

So a program that links CNA's targets with a shared C++ runtime on ELF names the runtime before
CNA's archives: `CNA::BuildConfig` adds `-Wl,--push-state,--as-needed -lstdc++ -Wl,--pop-state`
(`-lc++` for libc++) to its link. A standard-library symbol that no object already in the link
defines is then the shared runtime's, and no archive member is extracted for it alone. A member
extracted for a CNA symbol may still supply one, which links nothing more: in the multi-renderer
2D probe mold resolves libshaderc's `_M_erase` to `GuideStyle.cpp.o`, which the game links anyway
(`GuideContent.cpp.o` uses its styles). With the option mold extracts the members lld does in that
probe (11,740,248 bytes stripped with mold, 11,704,960 with lld, 11,709,040 with GNU ld), and every
footprint gate passes with each linker. GNU ld and lld link the same members with or without it. A
program that names CNA's archives on a hand-written link line instead of linking its targets does
not get it.

The option is only for a shared runtime, as lld and mold would let a shared runtime named first
replace a static one. At the end of the configure CMake reads each program's flags: its directory's
`CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS` (`_SHARED_`/`_MODULE_` for libraries) with their
per-configuration variants, which include a toolchain file's and `LDFLAGS`' initial flags, and the
program's `LINK_OPTIONS`, `LINK_FLAGS` and `LINK_LIBRARIES`. A program with `-static`,
`-static-pie`, `-static-libstdc++`, `-nostdlib`, `-nostdlib++` or `-nodefaultlibs` among them --
inside a generator expression too, which can only cost the option -- does not get it.
`-static-libgcc` alone keeps it: libstdc++ stays shared. A runtime chosen where CMake cannot see it
(a compiler wrapper or spec file, another library's link options) is declared with
`-DCNA_CXX_RUNTIME_LINKAGE=STATIC`, or `SHARED`; the default is `AUTO`.
`CnaCxxRuntimeLinkage` (`cmake/Tests/CxxRuntimeLinkageCase.cmake`) pins each of these.

With a static runtime CNA adds nothing, and where the runtime archive goes is the program's
choice. The driver names it last, so lld and mold resolve a standard-library symbol to the first
archive member on the command line that defines it, which is CNA's: with `-static-libstdc++` the multi-renderer 2D probe linked
glTF and Draco with lld, and the system Guide and the avatar catalog as well with mold (49.7 MB
stripped). Naming the static runtime first,
`target_link_options(MyGame PRIVATE "SHELL:-Wl,--push-state,-Bstatic -lstdc++ -Wl,--pop-state")`,
gives each linker the small closure again (13.2 MB with mold or lld, 13.6 MB with GNU ld, every
footprint gate passing).

## Built-in avatar catalog: embedded, not a separate pack

An external `avatar_catalog` pack was considered and not adopted. The catalog is now its own
library that only avatar code references, so a program that does not draw avatars no longer
contains it, which was the defect. A game with a `GamerServicesComponent` does draw them: its
players open the system Guide with Home or the Guide button, and its panels show their avatars,
in all three catalog versions an existing profile may use (~30 MB, no duplicate files). Keeping it embedded preserves single-file deployment and
offline avatars for programs that do use them. External packs already exist for catalog *updates*
(`installedCatalogRoot()`, `CNA_GAMER_SERVICES_CATALOGS_DIR`), verified by size and SHA-256.

## Measurements

Mobile Eggbert, Linux x86-64, OPENGLES3, GCC 14, mold. "Before" is CNA 3ee6ae3c8, "after" is
this work; the game's source is the same in both.

| Debug executable | Before | After |
|---|---:|---:|
| Unstripped | 208,287,328 | 62,383,864 |
| Debug sections removed (`objcopy --strip-debug`) | 65,007,640 | 13,867,976 |
| Fully stripped | 44,607,184 | 6,191,520 |
| `.text` | 9,363,703 | 4,066,408 |
| `.rodata` | 30,863,125 | 325,973 |
| ... of which the avatar catalog | 29,999,928 | 0 |
| `.data` / `.data.rel.ro` / `.bss` | 13,824 / 127,376 / 56,816 | 9,840 / 53,128 / 23,600 |
| `.eh_frame` | 2,786,336 | 1,111,392 |

Debug information is most of a Debug executable (143 MB before, 48.5 MB after) and shrinks with the
code it describes; the stripped size is what ships.

| Release executable, static CNA (after) | `CNA_OPTIMIZE_FOR_SIZE=OFF` | `ON` |
|---|---:|---:|
| Unstripped | 6,469,312 | 4,259,112 |
| Fully stripped | 4,695,176 | 3,040,712 |
| `.text` | 3,558,042 | 2,249,978 |
| `.rodata` | 93,635 | 74,595 |
| `.data` | 8,144 | 3,504 |

Neither build contains a symbol of the avatar catalog, `AvatarRenderer`, the glTF importer,
cgltf, Draco, `MediaPlayer`, `Microphone`, `Microsoft::Xna::Framework::Net`, ENet, libcurl,
`TextureCube`, `Texture3D`, `RenderTargetCube`, the camera, the tray, `CnbWriter` or the Headless
and Terminal platforms; its shared-library dependencies are SDL3, SDL3_mixer, zstd
and the C/C++ runtime. The footprint consumers stay within the same bounds on MinGW (DIRECTX11)
and with `CNA_ENABLE_NET=OFF`, and the browser 2D demo (wasm, linking the whole `CNA` target)
contains none of them either.

## What a 2D game still contains, and why

The largest members of the 2D consumer are the renderer, image decoding, the device, `Game`,
sharp-runtime's strings and the Guide UI that `ShowMarketplace` and the message box draw. Two more
items can be linked without being used; removing either takes more than a module boundary:

| Linked | Size | Through |
|---|---|---|
| sharp-runtime's `Decimal` | ~30 KB | `System::IO::BinaryReader::ReadDecimal` is virtual, as in .NET, and every content read goes through a `BinaryReader`-derived `ContentReader`, so its vtable names it; dropping it would mean making `ReadDecimal` non-virtual |
| MojoShader, the renderer's compiled-effect support and MojoShader's SPIR-V profile | ~420 KB (EasyGL) | **only in a build that opts into compiled XNA effects** (`CNA_<RENDERER>_COMPILED_EFFECTS`, all OFF by default; Mobile Eggbert's default build contains none of it). Such a build gives every renderer that support, and EasyGL's sprite renderer prepares XNA's SpriteEffect for custom-effect batches. Keeping it out of such a build's 2D games would mean installing each renderer's compiled-effect factory on the first `Effect`, and configuring MojoShader's profiles per build |
