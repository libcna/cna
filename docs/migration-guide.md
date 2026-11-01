# Migration guide: porting an XNA 4.0 or FNA game to CNA

**Status: reconciled with the current runtime surface, renderer registry, content pipeline and
known-defect ledger on 2026-11-01.** This guide contains the decisions a game port must make. It
does not duplicate member inventories, renderer capability tables or completed task histories.

CNA is a C++23 reimplementation of the XNA 4.0 programming model. It preserves the
`Microsoft::Xna::Framework` API vocabulary, but it is not a CLR and cannot execute an existing C#
assembly. A port translates the game's source and ownership model to C++, then reuses or rebuilds
its content for the selected CNA configuration.

## Sources of truth

| Question | Current source |
|---|---|
| Is a documented XNA runtime member represented? | [`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md) |
| How does an accepted C++ mapping differ from C# or FNA? | [`CHECKLIST.md`](../CHECKLIST.md) |
| What is broken or limited now? | [`handoff.md` section 5](../handoff.md#5-known-bugs-and-limitations) |
| Which renderer identities exist? | [`renderer-registry.md`](renderer-registry.md) |
| What does each renderer support? | [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md), then the renderer's own page and live capability profile |
| What can CNA load from an existing `.xnb`? | [`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md) |
| How is an existing `.contentproj` built? | [`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md) |
| Which custom-effect format is which? | [`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) |

The generated member report establishes API representation, not behavioral parity. Conversely, a
renderer supporting one feature does not imply that every format, graphics profile, driver or
combination supports it. Use the narrower source for the question being asked.

## 1. Establish the C++ build

Choose the CNA renderer and module set before translating renderer-dependent code. The repository
quick start builds CNA from source:

```bash
git submodule update --init
cmake -S . -B build -DCNA_GRAPHICS_RENDERER=OPENGLES3
cmake --build build --parallel 8
```

CNA also has an opt-in installed C++ package. A consumer can request only the modules it uses and
link their exported `CNA::` targets; [`installed-cpp-sdk.md`](installed-cpp-sdk.md) owns the exact
`find_package`, deployment and binary-compatibility contract. Do not copy private include paths,
renderer libraries or runtime DLL rules into the game.

The source checkout requires the compatible sibling `sharp-runtime`, `easy-gl` and `meta-gl`
checkouts named by the top-level [`README.md`](../README.md). They are not Git submodules. SDL and
the other vendored dependencies that are submodules are initialized by the command above.

## 2. Translate the public API mechanically

### Namespaces, types and headers

XNA namespaces and names stay recognizable:

```text
Microsoft.Xna.Framework.Graphics.Texture2D
Microsoft::Xna::Framework::Graphics::Texture2D
```

Include the public header that declares the type; do not include a renderer implementation header.
Use the signature in that header for ownership, nullability and overload selection rather than
guessing from the C# declaration. Sharp Runtime supplies the `System::*` types and the .NET
primitive aliases used by CNA's public surface, including `String`, `Single`, `intcs` and
`bytecs`.

C# `ref`/`out`, nullable values, arrays, delegates and reference types have explicit C++ mappings.
They may become references, pointers, `std::optional`, spans or project collection types depending
on the API. The generated member report names the mapping classification, while `CHECKLIST.md`
owns the project-wide accepted deviations.

### Properties

C# properties become explicit getter and setter methods:

```csharp
spriteBatch.GraphicsDevice.BlendState = BlendState.AlphaBlend;
var format = texture.Format;
```

```cpp
spriteBatch->getGraphicsDeviceProperty()->setBlendStateProperty(BlendState::AlphaBlend);
auto format = texture->getFormatProperty();
```

Rewrite every property access this way. Do not replace it with direct field access even when the
value type looks simple; the accessor may validate, apply native state or preserve XNA event order.

### Events and disposal

Events use `System::EventHandler<TEventArgs>` and retain XNA's event names:

```cpp
game.Exiting += [](System::Object* sender, const ExitingEventArgs& eventArgs) {
    // Respond to exit.
};
```

Keep explicit `Dispose()` calls where the XNA lifetime is observable, especially for graphics,
audio and content resources. The concrete header states whether the C++ caller owns an object,
shares it or merely borrows it. A disposed CNA object must not be treated as reusable just because
its C++ storage still exists.

### `CNAEXT` is a compatibility marker

`CNAEXT` marks declarations that are not literal XNA 4.0 API. Some are genuine CNA features;
others spell out operations that C# supplies implicitly, such as a zero-initialized value-type
constructor or a compound-assignment operator. The repository's strict XNA declaration checks
deliberately diagnose both groups, so the marker alone is not a behavioral capability query.

The `CNA_CNAEXT` CMake option is a different concept: it enables the optional `graphics-ext`
module and package-oriented shader extensions. It does not control whether every declaration
marked `CNAEXT` exists. Before depending on an extension, check its owning document and build
option.

## 3. Bring content across

### Existing compiled content

Existing `.xnb` assets remain valid inputs. `ContentManager` implements the registered built-in
readers, LZX/LZ4 containers and the general compiled-Effect reader. `ModelReader` is not a partial
placeholder: it loads the bone hierarchy, meshes and mesh parts, shared vertex/index/effect
resources, parent bones, bounds and tags. Its binary `.xnb` path is distinct from CNA's older
`.model.json` route.

The important boundary is registration rather than the container name. CNA cannot discover an
arbitrary C# reader assembly through CLR reflection. Supported built-in readers, registered generic
reader combinations, `ReflectiveReader<T>` setup and explicit refusals are listed in
[`xnb-content-pipeline-support.md`](xnb-content-pipeline-support.md).

### Rebuilding source content

An existing XNA `.contentproj` is an input to `cna-content`, not a file that must first be
converted:

```bash
cna-content build Content/MyGameContent.contentproj -o Content/bin
```

The same pipeline can build a source directory, emit XNA-compatible `.xnb`, or emit CNA's `.cnb`.
Custom C# importers/processors must be ported and registered in a C++ compiler binary because CNA
cannot load their assemblies. The exact commands, CMake helper, processor parameters and deliberate
differences are in [`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md).

CNA can additionally load `.cnb`, editable `.cnj`, native media and glTF/GLB according to the
documented resolution order in [`cnj-format.md`](cnj-format.md). A present but malformed compiled
asset is an error; it does not silently fall through to a source file with the same logical name.

## 4. Choose a renderer by requirement

Start with the host default unless the game has a requirement it cannot meet: `WEBGL2` under
Emscripten, `SDLGPU` on Windows, macOS and iOS, and `OPENGLES3` elsewhere. The default is a build
policy, not a promise that every optional feature is present on the live device.

| Need | Renderer direction | Boundary to check |
|---|---|---|
| Established native GL path | `OPENGLES3` or desktop `OPENGL33` | Context/profile-specific formats, MSAA, queries and extensions |
| Browser GL | `WEBGL2` | Emscripten/browser restrictions and WebGL capability limits |
| Modern native portability | `SDLGPU` | Linux/Vulkan is qualified; native Windows and Apple qualification remains open, and SDL_gpu exposes no occlusion queries |
| Native and browser WebGPU | `WEBGPU` | Qualified on Linux x86-64 and headless Chrome; other native packages remain unverified |
| XNA 4.0 Direct3D 9 authenticity | `DIRECTX9` | Windows-only; the current evidence is MinGW/Wine/DXVK rather than real Windows hardware |
| FNA's graphics abstraction and always-enabled compiled XNA effects | `FNA3D` | Experimental CNA renderer; capabilities depend on its selected OpenGL, Direct3D 11 or SDL_GPU driver |
| Deterministic bounded CPU pixels | `SOFTWARE` | Experimental and intentionally CPU-bound |
| Logic/validation without pixel output | `HEADLESS` or `STUB` | Neither makes a raster-output claim; their validation and diagnostics differ |

Maturity is queryable through `getGraphicsRendererMaturity()`. Feature support must be queried from
the created `GraphicsDevice`: use `SupportsCapability()` for the compact compatibility set and
`GetRendererCapabilityProfileEXT()` plus the per-format queries for structured feature, limit and
format decisions. Do not branch on a renderer name when a capability answer exists.

A normal build contains one renderer. An opt-in multi-renderer build uses
`CNA_GRAPHICS_RENDERERS`, and the application may select before the first successful graphics
device creation. Failure is hard by default; fallback must be enabled explicitly. The complete
precedence, latch and window-recreation rules are in
[`runtime-renderer-selection.md`](runtime-renderer-selection.md).

## 5. Choose the correct effect contract

The five XNA stock effects work across all rasterizing renderer families. Custom effects have two
separate contracts:

- `Effect(GraphicsDevice&, byte[])` and XNB `EffectReader` consume compiled Direct3D 9 Effect
  Framework bytecode (`.fxb` or an XNB Effect payload). FNA3D enables this unconditionally.
  EasyGL, WebGPU, SDLGPU, Software and DirectX9 expose it behind their off-by-default
  `CNA_EASYGL_COMPILED_EFFECTS`, `CNA_WEBGPU_COMPILED_EFFECTS`,
  `CNA_SDLGPU_COMPILED_EFFECTS`, `CNA_SOFTWARE_COMPILED_EFFECTS` and
  `CNA_DIRECTX9_COMPILED_EFFECTS` options.
- `ShaderEffect` is a CNA extension for renderer-language source or an explicitly typed portable
  package. Its accepted languages depend on the renderer and platform.

Raw `.fx` source and MonoGame MGFX are not interchangeable with compiled XNA effect bytecode. CNA
does not silently replace an unsupported custom effect with a stock effect; malformed bytecode and
an unsupported renderer are reported separately. See
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) and
[`fx-compiled-effects.md`](fx-compiled-effects.md) before choosing an asset format.

## 6. Preserve XNA state and profile behavior

Several common surprises are compatibility behavior rather than CNA draw-order defects:

- `SpriteBatch::End()` does not restore prior graphics state. The blend, depth/stencil,
  rasterizer and sampler state selected by `Begin()` remain on the device, matching XNA/FNA.
  Explicitly set the state required by a following 3D pass. The former report that a
  full-backbuffer SpriteBatch draw broke the next 3D draw was traced to the expected default
  `CullCounterClockwiseFace`, and the dedicated order test now distinguishes state from lost
  commands.
- Reach and HiDef restrictions are part of XNA behavior. A native API being able to allocate or
  draw something does not make it legal under the active `GraphicsProfile`.
- `Texture2D::GetData` is not universally a read from an upload shadow. An ordinary texture may
  retain CPU data, but a render target must observe its live rendered surface. Query render-target
  readback support; `HEADLESS` and `STUB` refuse it.
- Texture and render-target support is per `SurfaceFormat` and usage. Query the live format profile
  instead of treating successful `Color` storage as proof for compressed, float, filterable,
  blendable or multisampled storage.
- Use `handoff.md` section 5 for current defects. Historical task notes and fixed-item lists are
  not an additional backlog.

## 7. Port in verifiable slices

1. Build a minimal `Game` with the target renderer and platform.
2. Translate framework types and property/event syntax without introducing CNA extensions.
3. Load one real existing asset through `ContentManager`; decide whether the project will reuse
   `.xnb` or rebuild content only after that proof.
4. Bring up `SpriteBatch` and input, then one representative 3D/effect path if the game has one.
5. Query every renderer-dependent feature and format the game actually needs.
6. Add CNA extensions only after the XNA-shaped path is stable, keeping them isolated so the
   portability cost is visible.
7. Run the game's tests and graphics probes through the private runner, never on the owner's live
   desktop:

   ```bash
   tools/platform/run_gpu_tests_private.sh build --output-on-failure
   ```

8. Compare remaining failures with the generated member report, the relevant subsystem page and
   `handoff.md` section 5. If none owns the behavior, treat it as a new compatibility finding.

For an intentional change to a CNA pixel fixture, `CNA_UPDATE_GOLDEN=1` may regenerate that
fixture's PNG, but the image must be reviewed before it is accepted. Regenerating a golden is not a
way to classify an unexplained port difference.

## Troubleshooting

| Symptom | What to check |
|---|---|
| Configuration reports a missing `sharp-runtime`, `easy-gl` or `meta-gl` | Clone the compatible sibling checkout named by the top-level README; these three are not submodules. |
| Vendored SDL is absent after a fresh clone | Initialize Git submodules, or deliberately select the supported system-SDL mode. |
| A renderer name is refused as retired | Choose a current identity from `renderer-registry.md`; retired names have no compatibility aliases. |
| A renderer works on one machine but a feature throws on another | Inspect the live capability profile, graphics profile, format usages and renderer-specific driver qualification. |
| A 3D draw after `SpriteBatch` is culled or blended unexpectedly | Reapply the 3D pass's blend, depth/stencil, rasterizer and sampler states after `End()`. |
| `Effect(byte[])` or an XNB Effect is rejected | Confirm the payload is D3D9 Effect Framework bytecode and that the renderer's compiled-effect runtime is enabled. |
| An `.xnb` reader cannot be resolved | Check the registered built-in/generic/reflective reader boundary; CNA cannot discover a C# reader assembly. |
| GPU/window tests cannot create a context | Use `tools/platform/run_gpu_tests_private.sh`; do not point tests at the live desktop or substitute Xvfb for Vulkan presentation. |

## See also

- [`docs/README.md`](README.md) — current documentation index.
- [`binding.md`](binding.md) — why the approved cross-language boundary is CNA's native C API.
- [`development-environment.md`](development-environment.md) — build dependencies and private
  display setup.
- [`xna-fna-behavior-decisions.md`](xna-fna-behavior-decisions.md) — measured cases where Microsoft
  XNA wins over FNA.
