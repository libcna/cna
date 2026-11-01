# CNA physical module layout

CNA is a module-oriented monorepo. Each subsystem/renderer family owns its
`modules/<name>/{CMakeLists.txt,include,src,tests,examples,benchmarks}` areas where needed.
Public include spelling follows the namespace beneath each module's include root: moving a
header to its owner does not change `#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"`.

The root CMake file enters modules; `modules/CMakeLists.txt` defines shared build flags,
composition and the physical ownership gate. The original physical-module/example migrations
are complete and recorded in Git history.

## Framework and tool modules

The following lists direct CNA target dependencies, omitting the common cna_build_config edge.
Sharp Runtime entries name explicit component requests, not their complete transitive closure.
Native/conditional libraries are described by each module's own CMake file.

| Module | Target / alias | Direct CNA dependencies | Sharp Runtime components |
|---|---|---|---|
| core | cna_core / CNA::Core; cna_core_headers / CNA::CoreHeaders | No SDL edge; separate header-only marker/leaf-enum surface | Core.Base |
| diagnostics | cna_diagnostics / CNA::Diagnostics | — | — |
| inspector | cna_inspector / CNA::Inspector | diagnostics, core-headers | — |
| math | cna_math / CNA::Math | core-headers | Core.Base |
| design | cna_design / CNA::Design | math | ComponentModel |
| platform | cna_platform / CNA::Platform | core-headers; selected native implementation owns host services | Core.Base |
| runtime | cna_runtime / CNA::Runtime | graphics, input, content, audio, media, core, diagnostics, math, platform | Core.Base, Globalization, IO; private Uri |
| graphics | cna_graphics_core / CNA::GraphicsCore | math, core, diagnostics, platform; private input and compiled renderer targets | Core.Base, IO, Collections.Core, Text |
| input | cna_input / CNA::Input | graphics, math, core, platform | Core.Base, Collections.Core |
| audio | cna_audio / CNA::Audio | core, diagnostics, math; private platform, media, input | Core.Base, IO, Runtime |
| media | cna_media / CNA::Media | audio, graphics; private input and optional FFmpeg decoder | Core.Base, IO, Uri |
| video-ffmpeg | cna_video_ffmpeg / CNA::VideoFfmpeg | build flags; private media contract include path and FFmpeg libraries | private Core.Base |
| content | cna_content / CNA::Content | graphics, audio, media, math, core | Core.Base, IO, Resources; private Security.Cryptography |
| content-pipeline | cna_content_pipeline / CNA::ContentPipelineBuild | content; build-time tooling outside runtime umbrella | Core.Base, IO, Collections.Core, Xml |
| storage | cna_storage / CNA::Storage | core-headers | Core.Base, IO, Runtime, Threading; private Storage |
| devices | cna_devices / CNA::Devices | runtime, graphics, core, math | Core.Base |
| devices-ext | cna_devices_ext / CNA::DevicesExt | runtime, graphics, core, math, platform; never devices base | Core.Base |
| phone | cna_phone / CNA::Phone | runtime, core-headers | Core.Base, Collections.Core, Uri |
| graphics-ext | cna_graphics_ext / CNA::GraphicsExt | graphics | Core.Base |
| gamer-services | CNA_GamerServices / CNA::GamerServices | runtime, storage; private avatar catalog helper | Core.Base, IO, Collections.Core, Globalization, Runtime, Threading, Security.Cryptography, TimeZone |
| net | CNA_Net / CNA::Net | gamer-services, enet | Core.Base, IO, Collections.Core, Runtime, Threading |

Diagnostics instrumentation is controlled by CNA_DIAGNOSTICS; Inspector by CNA_BUILD_INSPECTOR.
Device extensions' guarded API uses CNA_DEVICES, although the module target remains registered.
GamerServices is controlled by CNA_ENABLE_GAMER_SERVICES independently of networking;
CNA_ENABLE_NET requires it and adds Net/ENet. FFmpeg is optional; see
[video-backend.md](video-backend.md). Platform/native libraries and build-time FreeType/media/zlib
edges are conditional rather than implied by the table.

The avatar catalog has its own private static helper target,
cna_gamerservices_avatar_catalog / CNA::GamerServicesAvatarCatalog.
The C adapter is an optional separate module under modules/c-api, with cna_c_api / CNA::CApi
and optional static packaging; it is not another Microsoft namespace implementation.
Its contract/build scope is [c-api/README.md](c-api/README.md).

CNA::Runtime above is a CMake target. The unrelated retired C++ CNA::Runtime/RuntimeOptions
declarations from CNA/Misc.hpp are not an initialization API; use XNA Game/GraphicsDeviceManager.

## Composition and installed consumers

- **CNA** is the historical full-runtime INTERFACE consumer target. It supplies aggregate
  runtime includes/definitions and rendering, with the game-template Sharp Runtime XML components
  named at umbrella level. Individual module consumers do not automatically receive that XML stack.
- With **CNA_SHARED_LIBRARY=OFF**, the umbrella composes the static module closure. With it ON,
  the deferred SharedRuntimeLibrary walk composes CNA-owned archives into cna_shared / CNA::Shared
  and the umbrella links that shared runtime. It includes compiled renderer families, not only the
  default. Availability/default policy is in cmake/SharedRuntimePolicy.cmake; this is not a
  universal shared-library option for every toolchain.
- **CNA::Design**, **CNA::ContentPipelineBuild** and **CNA::Inspector** remain opt-in tooling
  outside the ordinary runtime umbrella.
- **cna_cnaext / CNA::CnaExt** is an INTERFACE composition of GraphicsExt and DevicesExt,
  retaining its historical consumer target.
- **cna_build_config / CNA::BuildConfig** carries common public definitions/language requirements.
  It has no include root. ProjectOptions, Instrumentation and LinkerOptions hold private build policy.

GamerServices and Net are optional module consumers, not ordinary core-umbrella entries. When the
C API is built with the shared runtime, its composition additionally places those modules in the
same DSO to preserve singleton state for mixed C/C++ consumers. That is separate from what the
public umbrella promises as usage requirements.

The source-build umbrella is named CNA; installed consumers use the exported package targets
described by [installed-cpp-sdk.md](installed-cpp-sdk.md). The installed layout is its own contract,
not a re-created global repository include directory.

## Renderer families

8 implementation families carry 10 public renderer identities. The easygl family implements
OPENGLES3/OPENGL33/WEBGL2; the others are directx9, fna3d, headless, sdl-gpu, software, stub and webgpu.

`cmake/RendererIdentities.cmake` is the canonical identity list. RendererSelection and
RendererCombinations choose supported combinations; directory names are implementation families,
not additional public identities.

A single-renderer build compiles the selected family. The opt-in CNA_GRAPHICS_RENDERERS list can
compile several identities/families together, with a default and runtime selection.
CNA_RENDERER_TARGETS names the deduplicated family targets; RENDERER_TARGET is the default scalar.
Do not use the scalar when an operation must apply to every compiled family.

Each family's CMake file owns its SDK links. cna_renderer_common_setup applies shared
build/include/Sharp Runtime policy and private reverse dependencies on graphics/core/math.
MojoShader's shared effect adapter and the DirectX9 d3dcompiler effect helper are helper targets,
not public renderer identities.

## Tests and examples

Tests live under their owning module and preserve namespace-relative paths.
CnaTests is the aggregate corpus; focused Cna*Tests targets share the relevant module test objects,
subject to enabled features. Top-level tests also owns shared harnesses, cross-module contracts,
oracle/interop assets, platform/package probes and tests/modules minimal-link consumers.
It is not limited to fixture assets.

Examples belong to the subsystem or renderer family they demonstrate. Shared renderer-neutral
fixtures/support live in modules/graphics/examples, exposed to registrations through
CNA_GRAPHICS_EXAMPLES_DIR. Renderer-specific fixtures live in their family, with explicit paths
for deliberate cross-family reuse. Extension examples live with their extension module.

Module-local examples/CMakeLists files retain platform/feature/renderer gates; source presence does
not mean the target is registered in every build. Executables normally land at the build root.
Root examples/golden holds shared golden assets; it must not become a miscellaneous source tree.
A new genuinely cross-module example needs an explicit ownership justification.

## Deliberate target cycles

- **graphics ↔ input:** standalone GraphicsDevice updates input/window bindings while MouseCursor
  exposes Texture2D construction.
- **audio ↔ media:** FrameworkDispatcher pumps MediaPlayer events and MediaPlayer uses the mixer.
- **graphics ↔ compiled renderers:** generated factory references anchor the compiled archives;
  renderers call common framework implementations. CMake/archive rescan policy must include the
  whole compiled cycle in a multi-renderer build.
- **media → video-ffmpeg**, when enabled, is an optional decoder dependency, not a reverse cycle.

A registration object alone is not enough to force a static linker to retain its archive member.
Do not replace these anchors/edges without checking real minimal-link consumers.

## Validators and evidence

The modules ownership gate checks .cpp/.mm files beneath modules against declared module
src/tests/examples/benchmarks areas. It also refuses legacy src/include directories beneath
CNA_SOURCE_DIR, while allowing an add_subdirectory consumer to have its own such directories.
It does not prohibit intentional top-level tools/spikes/test harnesses.

cmake/Tests/ModuleProbes.cmake registers native minimal-link consumers and paired
scripts/check_module_link_closure.py checks. HEADLESS native-SDK-free closure checks are gated
to a single-renderer HEADLESS configuration; a multi tree with a HEADLESS default legitimately
contains other compiled renderer dependencies. RendererIdentityRegistry and related renderer
discipline gates check canonical identities/target composition.

The 2026-11-01 audit compares current module manifests and reuses the existing multi tree.
All fifteen minimal-link targets build; all 43 module probe/closure checks and three renderer
registry/combination/target checks pass through the private runner. The registry initially
required restoring its numeric count spelling; its recheck passes. These results qualify that
host/configuration, not all static/shared/cross-target combinations.
