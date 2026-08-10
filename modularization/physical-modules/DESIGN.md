# Physical module layout — frozen design (Phase 3)

Base: CNA develop `ea61123e6fda52150a522af8db30023edf4ba1d2`, sharp-runtime develop
`81624983c1e5388cb17e325480fdc2631a5cc653` (not modified). Branch `feature/physical-modules`.

Every physical module owns `modules/<name>/{CMakeLists.txt,include/,src/,tests/}` (subdir omitted
only when genuinely empty). Consumer `#include <...>` spelling is unchanged: each module's
`include/` root reproduces the public path (`Microsoft/...`, `CNA/...`) below it.

## Framework modules (targets unchanged)

| module | target / alias | src from | include from | tests from |
|---|---|---|---|---|
| core | cna_core / CNA::Core | src/Core/* | CNA/{CNAException,CNAHelper,DesktopOS,Entrypoint,LogCategory,Logger,LogLevel,Platform}.hpp; Microsoft/Xna/Framework/{PlayerIndex,NamespaceDocs}.hpp; CNA/Internal/PathContainment.hpp | tests/CNA/LoggerTests.cpp; PlayerIndexTests; PathContainmentTests |
| math | cna_math / CNA::Math | src/Math/* | 22 Framework-root math headers (Bounding*, Color, ContainmentType, Curve*, MathHelper, Matrix, Plane, PlaneIntersectionType, Point, Quaternion, Ray, Rectangle, Vector*) | matching root tests |
| runtime | cna_runtime / CNA::Runtime | src/Runtime/* | 19 Framework-root game-loop headers + CNA/Misc.hpp | matching root tests |
| graphics | cna_graphics_core / CNA::GraphicsCore | src/Graphics/{Xna,Internal}/** | Microsoft/.../Graphics/** (+PackedVector); CNA/Internal/Graphics/**; CNA/Internal/Backends/Common/**; CNA/Internal/Utf8Decode.hpp; CNA/{GraphicsBackendType,GraphicsCapability,Unsupported3DGraphicsCallBehavior}.hpp; Framework/DisplayOrientation.hpp | tests .../Graphics/** (+PackedVector); tests/CNA/Internal/Graphics/**; tests/CNA/GraphicsBackendTypeTests.cpp; DisplayOrientationTests; GraphicsBackendCompileDefinitionTests; tests/PackedVectorGolden.md |
| input | cna_input / CNA::Input | src/Input/** (Xna+Internal+NoXna) | Microsoft/.../Input/** (+Touch); CNA/Input/**; CNA/Internal/Input/** | tests .../Input/** (+Touch); tests/CNA/Input/**; tests/CNA/Internal/Input/** |
| audio | cna_audio / CNA::Audio | src/Audio/** | Microsoft/.../Audio/**; CNA/Internal/Audio/**; Framework/FrameworkDispatcher.hpp | tests .../Audio/**; tests/CNA/Internal/Audio/**; FrameworkDispatcherTests |
| media | cna_media / CNA::Media | src/Media/** | Microsoft/.../Media/** (+Video); CNA/Internal/Media/** | tests .../Media/**; tests/CNA/Internal/Media/** |
| content | cna_content / CNA::Content | src/Content/** | Microsoft/.../Content/**; CNA/Internal/{Xnb,GltfImport}/**; CNA/Internal/{CnjEnvelope,CnjSourceFile,Json}.hpp | tests .../Content/**; tests/CNA/Internal/{Xnb,GltfImport}/**; CnjEnvelopeTests; JsonTests |
| storage | cna_storage / CNA::Storage | src/Storage/* | Microsoft/.../Storage/** | StorageDeviceTests |
| devices | cna_devices / CNA::Devices | src/Devices/Microsoft/** | Microsoft/Devices/** | tests/Microsoft/Devices/** |
| devices-ext | cna_devices_ext / CNA::DevicesExt (NEW, carved from cna_devices) | src/Devices/NoXna/** | CNA/Devices/** | tests/CNA/Devices/** |
| graphics-ext | cna_graphics_ext / CNA::GraphicsExt (NEW = former cna_noxna implementation) | src/NoXna/Graphics/** | CNA/Graphics/** | tests/CNA/Graphics/** |
| net | CNA_Net / CNA::Net | src/Net/** | Microsoft/.../Net/**; CNA/Internal/Net/** | tests .../Net/**; tests/CNA/Internal/Net/** |
| gamer-services | CNA_GamerServices / CNA::GamerServices | src/GamerServices/** | Microsoft/.../GamerServices/**; CNA/Internal/GamerServices/** | tests .../GamerServices/** |

`cna_noxna` / CNA::NoXna becomes an INTERFACE umbrella over CNA::GraphicsExt + CNA::DevicesExt
(consumer-compatible; its former implementation is entirely the graphics extension code).

## Renderer modules — modules/renderers/<family>/

38 implementation families (kebab-case dirs), identity count stays 41 (EasyGL family carries
OPENGLES/OPENGL33/WEBGL1/WEBGL2): ascii, bgfx, canvas, d3d10, d3d11, d3d12, d3d9, diligent,
direct2d, dx1, dx2, dx3, dx5, dx6, dx7, dx8, easygl, freedirect, gdi, glide, headless, html-dom,
llgl, magnum, metal, opengl1, opengl2, opengl4, opengles1, sdl-gpu, sdl-renderer, skia, software,
sokol, stub, vulkan, webgpu, wicked. Common helper (not an identity):
modules/renderers/common/d3d/ = former D3DCommon (consumed by D3D11+D3D12 only; D3D10 confirmed
independent by include+link audit).

Each family: `src/` = former src/Graphics/Backends/<X>/ content (shaders/ subtree preserved,
includer-relative includes untouched); `include/` = former include/CNA/Internal/Backends/<X>/
(spelling preserved below the module include root); `tests/` = former
tests/CNA/Internal/Backends/<X>/ where present (+ tests/opengl1/README.md -> opengl1).

Cross-config header interfaces: metal + glide policy tests deliberately compile in every
configuration (their own headers are host-portable) -> those two modules define an unconditional
`cna_renderer_<x>_headers` INTERFACE target; the STATIC backend target builds only when selected.
GDI compiles 8 software-module TUs (accepted 2D sharing; physical ownership stays with software).
ASCII builds the sdl-renderer module's sources as `cna_backend_graphics_sdl_renderer_core`.
D3D9 keeps its isolated `cna_backend_graphics_d3d9_effect` sub-target inside modules/renderers/d3d9.

## Ownership decisions forced by evidence (recorded, all others are 1:1)

- `PlayerIndex.hpp` -> core; consumers: input, storage, gamer-services. Requires the previously
  hidden include-contract edge `cna_storage -> cna_core` (storage's public API names PlayerIndex;
  the monolithic include root masked this). Declared PUBLIC, documented.
- `DisplayOrientation.hpp` -> graphics (consumers graphics/input/runtime all reach graphics).
- `PathContainment.hpp` -> core (consumers content AND media; media does not link content).
- `Utf8Decode.hpp` -> graphics (SpriteBatch/SpriteFont).
- `Json/CnjEnvelope/CnjSourceFile.hpp` -> content (gamer-services reaches content via runtime).
- `FrameworkDispatcher.hpp` -> audio (follows its accepted cpp ownership).
- `Misc.hpp` (RuntimeOptions) -> runtime; `Entrypoint.hpp`, `NamespaceDocs.hpp` -> core.
- D3D9FormatMapping/D3D9ProfileCapabilities stay physically in renderers/d3d9: every graphics-core
  consumer include is #ifdef CNA_BACKEND_D3D9-guarded (verified), so they resolve only when the
  d3d9 module is configured.

## Accepted relationships preserved unchanged

graphics <-> input, audio <-> media, graphics <-> selected renderer (all declared static-archive
cycles); reverse edges BACKEND -> {graphics,core,math} stay PRIVATE and unconditional; helper
targets (d3dcommon, sdl_renderer_core, d3d9_effect) gain the same PRIVATE reverse edges they
previously satisfied through the global include root. SharpRuntime component sets are unchanged
per target; new modules: graphics-ext -> Core.Base, devices-ext -> Core.Base.

## Tests

All moved tests keep their tests-relative mirror path under modules/<m>/tests/, so every
path-tail regex in cmake/UnitTests.cmake keeps matching. CnaTests remains the single corpus
executable, now globbing modules/*/tests + modules/renderers/*/tests + tests/. tests/assets/**
and tests/modules/** stay top-level (runtime fixture literals; cross-module probes).
