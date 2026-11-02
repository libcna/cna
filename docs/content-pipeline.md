# CNA Content Pipeline

The build-time engine imports authoring files, processes canonical CPU data and publishes compiled
content. Default output is [CNB](cnb-format.md); `--format xnb` selects
[XNB](xnb-interoperability.md). A route without a writer for the requested format fails explicitly.
Effect bytecode and generic intermediate XML do not gain a CNB schema by changing an option.

This page documents `CNA::Content::Pipeline` and the `cna-content` coordinator. The XNA-shaped
C++ API in `Microsoft::Xna::Framework::Content::Pipeline` is a façade over that same engine,
with importers/processors, intermediate XML, serialization and task/project adapters; see
[the API contract](xna-content-pipeline-compat-api.md) and
[migration guide](xna-content-pipeline-migration.md).

Native extension/embedding interfaces remain experimental. CNB container and asset schemas have
separate versions; [cnb-format.md](cnb-format.md) owns their contracts. Component cache versions
are not wire-schema versions, and not every built-in output is schema 1.

The pipeline compiles; ContentManager loads runtime content. XNB reader identities belong to
compatibility input/output, not native CNB reader tables. Selection, dependency tracking,
incremental decisions and publication share the coordinator. Processors can inspect output format,
platform/profile where an implemented policy requires it; changing just the writer is not always
sufficient.

XNB options include `--xnb-platform`, `--xnb-version`, `--xnb-profile`, `--xnb-reader-names`
and `--xnb-compress none|lzx|lz4`, and enter build identity. XNA 4.0 loads LZX, while LZ4 is an
extended-ecosystem option refused for XNA targets. Xbox output is refused unless
`--xnb-allow-unverified-xbox` requests the unqualified route. Header identifiers alone do not
prove target payload compatibility; see [xnb-interoperability.md](xnb-interoperability.md).

## Quick start

Given:

```text
ContentSource/
├── Models/
│   └── robot.gltf
├── Textures/
│   └── wall.png
├── Fonts/
│   └── ui.cnj
└── Sounds/
    └── explosion.wav
```

run:

```bash
cna-content build ContentSource -o Content
```

The output preserves extensionless relative content names:

```text
Content/
├── .cna-content.lock
├── .cna-content-manifest.json
├── Models/
│   └── robot.cnb
├── Textures/
│   └── wall.cnb
├── Fonts/
│   └── ui.cnb
└── Sounds/
    └── explosion.cnb
```

A single asset can be built explicitly:

```bash
cna-content build ContentSource/Textures/wall.png -o Content/Textures/wall.cnb
```

A supported legacy built-in XNB is used the same way and produces native CNB, not wrapped XNB:

```bash
cna-content build ContentSource/Legacy/texture.xnb -o Content/Legacy/texture.cnb
```

Configuration remains optional. A directory or single-file build automatically reads
`.cna-content.json` from its source root when present; `--config <path>` selects another file
inside that root. The same strict configuration can explicitly map named, read-only authored
source roots when a project intentionally shares input files between content trees.

Build execution is serial by default. Independent graph nodes can be compiled concurrently with a
strict bounded worker count:

```bash
cna-content build ContentSource -o Content --workers 4
```

`--workers` accepts integers from 1 through 64. `--workers 1` is the explicit serial fallback and
has the same behavior as omitting the option. The worker count changes execution only; it is not
content identity and does not enter CNB bytes or manifest fingerprints.

To show the persisted reason for each incremental decision, add `--explain`:

```bash
cna-content build ContentSource -o Content --explain
```

Each normal `BUILD` or `SKIP` line is followed by sorted `reason:` lines. Paths in those reasons
are source- or output-root-relative. `--quiet` takes precedence over `--explain` and suppresses all
successful build/skip/reason output while retaining failures. `clean` does not accept `--explain`.

### `--xna-compatible`

By default a build refuses anything it cannot honour exactly: a processor parameter naming a
property the processor has not got, a value it cannot convert to that property's type, and a
`.spritefont` character region the resolved font has no glyphs for. Each is a mistake in the project
that nothing else in a build will notice, and refusing is how an author finds out.

Microsoft's XNA warns and carries on for all three, measured case by case in
`XNAPP-267` (`tests/reference/xna40/differential-errors/`). A host that
has to reproduce that behaviour passes `--xna-compatible`:

```bash
cna-content build ContentSource -o Content --format xnb --xna-compatible
```

An unrecognised or unconvertible parameter is then dropped with a warning and the processor keeps
its default; a character with no glyph is drawn with the font's own `.notdef` box and warned about.
Every other refusal stays a refusal in both modes, because an impossible request is impossible
whoever asked: a build with no DXT encoder, a texture whose dimensions block compression cannot
take, a malformed source. The XNA task façade
(`Microsoft::Xna::Framework::Content::Pipeline::Tasks::BuildContent`) selects `--xna-compatible`
for itself. Supported projects therefore use XNA's leniency; this does not run arbitrary CLR
components, project tasks or every possible source feature in C++.

After a valid manifest has established ownership, all unchanged pipeline-owned compiled and
deployment files can be removed without scanning the output tree:

```bash
cna-content clean Content
```

Clean leaves manual/source files and directories alone. Changed former outputs or unsafe owned
candidates make preflight fail before deletion; they are not silently discarded or reported as
successful cleanup. The persistent `.cna-content.lock` coordination file remains so later build
and clean processes continue to serialize safely for that output root.

Runtime loading remains the existing ContentManager API:

```cpp
auto robot = content.Load<Model>("Models/robot");
auto wall = content.Load<Texture2D>("Textures/wall");
auto ui = content.Load<SpriteFont>("Fonts/ui");
auto explosion = content.Load<SoundEffect>("Sounds/explosion");
```

The runtime needs the `.cnb` artifacts plus explicitly deployed Song/Video streaming media, not the
other authoring PNG, WAV, glTF, CNJ, XNB, or intermediate sidecar files.

## Architecture

```text
                         BUILD TIME

source file
    |
    v
ContentImporter + ContentImporterContext
    |
    v
source-oriented imported value
    |
    v
ContentProcessor + ContentProcessorContext
    |
    v
runtime-oriented Cnb*Data / processed value
    |
    v
ContentTypeWriter
    |
    v
existing Encode*ToCnb() codec
    |
    v
primary CNB + bounded named CNB child outputs + bounded deployment-support files
    |
    v
shared atomic publisher
    |
    v
one or more *.cnb artifacts plus explicitly registered external media

-------------------------- RUNTIME --------------------------

*.cnb -> typed CNB decoder + CnbLoaderRegistry -> ContentManager -> runtime object
```

Import and processing are separate even when the first processor is intentionally thin. An image
importer answers “what pixels are in this source?”; a texture processor answers “how should these
pixels become runtime Texture2D content?”; the writer answers “which authoritative CNB codec emits
the compiled representation?”

Built-in compilation paths use CPU data without constructing a GraphicsDevice, opening playback
devices/windows, reading back GPU data or initializing a graphics renderer. Registered C++
components are trusted code; the API is not an OS sandbox. XNB float/ADPCM SoundEffect conversion can call SDL3's
in-memory WAVE decoder when that audio backend is compiled, but does not initialize SDL video/audio
subsystems or open a device. Model import uses CPU-side canonical data and temporary authoring
intermediates, not runtime Model objects.

## XNA conceptual mapping

| XNA 4.0 concept | CNA implementation |
|---|---|
| `ContentImporter<T>` | `ContentImporter`, a checked type-erased C++ component |
| `ContentImporterContext` | focused, call-scoped source/dependency/logging services |
| imported object | explicit source-oriented C++ value such as `ImportedImage` |
| `ContentProcessor<TInput,TOutput>` | `ContentProcessor` with stable input/output identities |
| `ContentProcessorContext` | focused parameters, dependency, XREF, and logging services |
| `ContentTypeWriter<T>` | `ContentTypeWriter` adapter over one existing typed CNB encoder, with bounded named outputs |
| `ContentTypeReader<T>` | existing typed CNB decoder plus `CnbLoaderRegistry` |
| `ExternalReference<T>` | CNB XREF for runtime references; separate records for build inputs |
| XNB | compatibility source and optional compiled output, separate from native CNB |
| XNA project/tasks | bounded .contentproj/BuildContent adapters over the coordinator |
| CMake | cna_add_content() delegates to the CLI |

The native CNB route does not use CLR assembly loading/reflection. The XNA façade explicitly
registers C++ types/properties/writers and handles compatibility names, reader tables and platform
bytes needed by XNB. It reads the supported project subset, without running arbitrary MSBuild.

## Importers

`ContentImporter` declares:

- `Identity()` — an author-controlled stable component name and build version;
- `SourceExtensions()` — lowercase source extensions, including the leading dot;
- `OutputTypes()` — one or more bounded stable imported-type identities;
- `Import(ContentImporterContext&)` — source parsing and validation.

Most importers produce one imported type. `CnjImporter` is intentionally multi-output because the
validated envelope selects one of a fixed set of declared imported types. This is not selection
priority and it is not unrestricted dynamic typing.

### ContentImporterContext

The importer context owns no data beyond one call. It provides the canonical source root, canonical
primary source path, logical asset name, safe source-dependency resolution, and scoped info/warning
logging. An unqualified `ResolveSourceDependency()` reference remains relative to the primary
source and rejects paths outside the source root. An explicit `@alias/path` reference selects one
configured read-only source root. Both forms reject absolute authored paths, traversal and
canonical symlink escapes and record the resolved file as a build input.

Components must not retain references or pointers to a context after `Import()` returns. Component
objects are registry-owned and reusable; they must keep no unsynchronized per-build mutable state
because `--workers` may invoke the same frozen component concurrently.

## Imported and processed data

Imported values preserve source semantics where that distinction is meaningful:

```text
PNG/JPEG/etc. -> ImportedImage -> CnbTextureData
WAV           -> ImportedSound -> ProcessedSoundEffect
glTF          -> ImportedModelDocument -> ProcessedModelBundle
```

`ImportedImage` contains validated RGBA8 pixels and dimensions before texture policy. `ImportedSound`
preserves whether accepted PCM samples were unsigned 8-bit or signed little-endian 16-bit until the
processor creates width-preserving ProcessedSoundEffect data. Its CNB writer emits schema 2 for
PCM8/16. The WAV importer rejects wider/non-PCM sources; compressed-source import is separate. `ImportedModelDocument` is an owned lifetime seam
over CNA's existing canonical glTF-to-CNJ interpretation, avoiding a second glTF parser while the
shared importer is migrated further in memory. `ProcessedModelBundle` always carries one primary
`CnbModelData` and can additionally carry canonical Model, Texture2D, and AnimationClip values for
the existing typed encoders.

CNJ routes similarly produce explicit imported image, sound, font, volume, cube, Curve,
AnimationClip, or Model-document values. A `Cnb*Data` type is used as processor output when it
already is the correct runtime-oriented canonical DTO; it is not forced into the importer stage.

Values cross heterogeneous components through `ContentValue`. Stable strings drive registry and
persistent identity. `std::type_index` is used only inside the running process as a defensive
checked-cast guard; no RTTI name enters CNB, configuration, manifests, or fingerprints.

## Processors and parameters

`ContentProcessor` declares a stable identity, one stable input type, one stable output type,
`ValidateParameters()`, and `Process()`. Parameter validation happens before transformation.

`ContentProcessorParameters` is an ordered map whose bounded values are:

```text
bool, signed 64-bit integer, unsigned 64-bit integer, double, UTF-8 string
```

Unknown, mistyped, non-finite, or invalid values are rejected by the selected processor. Every
effective parameter participates in the build fingerprint. Library callers set parameters on
`ContentBuildRequest`; the optional strict `.cna-content.json` maps the same typed values into CLI
builds. `TextureProcessor` accepts decimal `colorKey` in `R,G,B` or `R,G,B,A` form, the
string parameter `textureFormat`, and the booleans `generateMipmaps`, `premultiplyAlpha` and
`resizeToPowerOfTwo`. They apply in one documented order -- **colour key, resize, premultiply, mip
chain, block compression** -- and `premultiplyAlpha` **defaults to `true`**, exactly as XNA 4.0's
`TextureProcessor.PremultiplyAlpha` does: `BlendState::AlphaBlend`, which `SpriteBatch::Begin()`
selects when given no blend state, is the premultiplied blend, so straight-alpha content renders
with dark fringes under the default state. Two importers override that default because their source
defines its own answer -- a `.cnj` document (CNJ v1 compiles to straight alpha) and an already-built
`.xnb` being transcoded (its pixels have already had one alpha policy applied, and applying a second
would corrupt them). An explicit parameter beats both.
For `Texture2D`, requested DXT compression writes the chosen DXT representation alone to XNB and
writes the matching `Bc1`, `Bc2` or `Bc3` representation followed by the processed `Rgba8` fallback
to CNB. A pass-through compressed DDS preserves its source blocks in the same preferred position.
`SongProcessor` accepts streamReference/name strings and durationMs u64; `ModelProcessor`
accepts generateChildAssets for glTF only. Texture2D CNJ can author its color-key field.
Video parameters include u64 width/height/durationMs/soundtrackType, f64 framesPerSecond and
streamReference. Registered probes can supply frame metadata; parameters override it. Without
usable probe data, dimensions/rate must be supplied.

### ContentProcessorContext

The processor context exposes the logical asset name, validated parameters, safe dependency
resolution, categorized content-build/generated dependency registration, separate runtime XREF
registration, and scoped logging. It does not duplicate the importer's source-path getters or
expose runtime services.

Content-to-content build dependencies are scheduled as explicit graph edges by the CLI coordinator.
`AddContentBuildDependency("Shared/material")` names the stable primary build-node ID, not an output
path or runtime XREF. The target must be another primary node discovered in the same invocation;
additional outputs do not become implicit source nodes. Shared dependencies execute once, complete
before their dependents publish, and propagate failures and effective fingerprint changes.

These graph edges differ from the XNA façade's implemented BuildAsset/BuildAndLoadAsset.
The bridge uses the active pipeline for nested import/process/build work. BuildAndLoadAsset
returns processed content, not a GraphicsDevice-backed runtime object. BuildAsset contributes
compiled nested outputs/references to the containing node, sharing its ownership/publication
rather than creating independent primary graph nodes. An unnamed request matching an already
planned primary sibling instead returns that item's output reference without building it, waiting
for it or adding a build edge. Explicit graph-cycle checks do not establish a recursion guard for
every synchronous nested call. A context outside an active pipeline refuses these operations.

## Content Type Writers and CNB codecs

`ContentTypeWriter` accepts one stable processed type and returns complete CNB bytes plus the
stable CNB asset identity. It does not publish files and it does not parse source formats.

Every built-in writer is a small adapter:

```text
Texture2DContentWriter    -> EncodeTexture2DToCnb()
SoundEffectContentWriter -> EncodeSoundEffectToCnb()
SongContentWriter        -> EncodeSongToCnb()
VideoContentWriter       -> EncodeVideoToCnb()
ModelContentWriter       -> EncodeModelToCnb() or EncodeModelV2ToCnb(), plus generated-child encoders
Texture3DContentWriter    -> EncodeTexture3DToCnb()
TextureCubeContentWriter -> EncodeTextureCubeToCnb()
SpriteFontContentWriter  -> EncodeSpriteFontToCnb()
CurveContentWriter       -> EncodeCurveToCnb()
AnimationClipContentWriter -> EncodeAnimationClipToCnb()
```

There is no second implementation of field order, chunks, CRC, schema versions, asset IDs, or
container construction in the built-in pipeline. `CnbWriter` remains the low-level container
builder used by authoritative codecs and custom schemas; `ContentTypeWriter` is the build-stage
adapter above a codec.

Every writer also declares `OutputSchemaIdentities()` before `Write()` can run. Each sorted entry
contains the stable numeric asset type, canonical runtime type name, asset schema version, and an
explicit codec name/version. These identities have different jobs:

- the writer component version changes when writer orchestration or output-set behavior changes;
- the asset type ID plus canonical name identify runtime dispatch (the name is load-bearing for a
  custom-ID collision);
- the asset schema version changes when the native wire schema changes; and
- the codec version changes when output semantics or bytes can change within the same schema.

The pipeline refuses empty, duplicate, unordered, zero-valued, or incomplete declarations and any
writer result whose asset ID/name/schema tuple was not declared. A writer may declare more than one
schema for one asset identity when the processed value makes an explicit choice; primary and
additional write results report that exact schema version. The declaration is an author-controlled
codec contract—the pipeline does not infer it from RTTI or execute a writer on the skip path.
Built-in codec round trips and the custom compiler's parsed CNB oracle verify that declared schema
versions match the bytes their authoritative encoders emit.

## Runtime reader mapping

Native CNB dispatch uses:

```text
Decode*FromCnb() + CnbLoaderRegistry + ContentManager
```

Custom CNB assets register their ID/type/loader through ContentManager::RegisterCnbLoaderEXT.
The separate XNB path does have XNA's ContentTypeReader hierarchy and registry; see
[xnb-content-pipeline-support.md](xnb-content-pipeline-support.md).

## Built-in routes

| Source | Importer | Imported type | Processor | Writer/output |
|---|---|---|---|---|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga`, `.gif`, `.psd`, `.hdr`, `.pic`, `.pnm`, `.dds`, `.dib`, `.pfm`, `.ppm` | `CNA.ImageImporter/1` | ImportedImage, or DDS cube/volume value | TextureProcessor/4 or matching cube/volume processor | matching native texture writer |
| `.wav` | `CNA.WavImporter/2` | `ImportedSound` | `CNA.SoundEffectProcessor/3` | `CNA.SoundEffectContentWriter/2` (schema 2) |
| `.mp3`, `.ogg`, `.oga`, `.qoa`, `.flac`, `.opus`, `.aac`, `.wma` | `CNA.SongImporter/3` | `ImportedSongSource` | `CNA.SongProcessor/2` | `CNA.SongContentWriter/1` |
| `.wmv`, `.mp4`, `.ogv`, `.webm`, `.mkv`, `.avi`, `.mov` | `CNA.VideoImporter/2` | `ImportedVideoSource` | `CNA.VideoProcessor/2` | `CNA.VideoContentWriter/1` |
| `.gltf`, `.glb` | `CNA.GltfImporter/3` | `ImportedModelDocument` | `CNA.ModelProcessor/4` | `CNA.ModelContentWriter/4` |
| `.cnj` Texture2D | `CNA.CnjImporter/1` | `ImportedImage` | same texture processor | same Texture2D writer |
| `.cnj` SoundEffect | `CNA.CnjImporter/1` | `ImportedSound` | same sound processor | same SoundEffect writer |
| `.cnj` Model | `CNA.CnjImporter/1` | `ImportedModelDocument` | same Model processor | same Model writer |
| `.cnj` SpriteFont | `CNA.CnjImporter/1` | `ImportedSpriteFont` | `CNA.SpriteFontProcessor/1` | `CNA.SpriteFontContentWriter/1` |
| `.cnj` Texture3D | `CNA.CnjImporter/1` | `ImportedTexture3D` | `CNA.Texture3DProcessor/1` | `CNA.Texture3DContentWriter/1` |
| `.cnj` TextureCube | `CNA.CnjImporter/1` | `ImportedTextureCube` | `CNA.TextureCubeProcessor/1` | `CNA.TextureCubeContentWriter/1` |
| `.cnj` Curve | `CNA.CnjImporter/1` | `ImportedCurve` | `CNA.CurveProcessor/1` | `CNA.CurveContentWriter/1` |
| `.cnj` AnimationClip | `CNA.CnjImporter/1` | `ImportedAnimationClip` | `CNA.AnimationClipProcessor/1` | `CNA.AnimationClipContentWriter/1` |
| `.xnb` supported built-in root | `CNA.XnbImporter/3` | existing imported type selected by validated root reader | existing type processor (plus metadata/deployment `CNA.XnbVideoProcessor/2`) | existing native built-in writer/codec; Model selects schema 1 or 2 exactly |
| `.spritefont` | `CNA.FontDescriptionImporter/1` | `ImportedSpriteFont` | `CNA.FontDescriptionProcessor/1` | `CNA.SpriteFontContentWriter/1` |
| `.fxb` | `CNA.CompiledEffectImporter/1` | `ImportedCompiledEffect` | `CNA.CompiledEffectProcessor/1` | `CNA.XnbEffectWriter/1` (`--format xnb` only) |
| `.fx` | `CNA.EffectSourceImporter/1` | `ImportedEffectSource` | `CNA.EffectSourceProcessor/1` | `CNA.XnbEffectWriter/1` (`--format xnb` only) |

The stock compiler also registers .x/.fbx through CNA.XImporter/CNA.FbxImporter and
CNA.XnaModelProcessor. Generic .xml uses XmlImporter plus registered-type pass-through processing/
XNB serialization; see [components](xna-content-pipeline-components.md) for XNA-facing names.

DDS selects 2D/cube/volume by its header. WAV defaults to SoundEffect; explicit
CNA.WavSongImporter is registered by name only. MP3/WMA default to Song; explicit
CNA.CompressedSoundImporter requests SoundEffect decoding when a build-time decoder is available.
Ogg defaults to Song, avoiding automatic route ambiguity.

The table gives native CNB writer identities where applicable; XNB selects its corresponding
CNA.Xnb*Writer. FontDescriptionImporter includes configured font-search paths in its version;
the effect-source processor includes compiler identity and Debug-default policy.

### Effects: `.fxb` and `.fx`

The two effect routes differ in exactly one thing — whether CNA has to run a compiler — and
converge on one processed type and one writer, so nothing downstream can tell them apart:

* **`.fxb` is already-compiled bytecode.** The importer checks that the file begins with an Effect
  Framework 9.1 signature (optionally behind XNA 4.0's own wrapper token) and refuses anything
  else. It needs no source compiler, but malformed bytecode or an incompatible output format
  still fails; recognizing a signature is not runtime shader qualification.
* **`.fx` is source**, and needs an external `fxc`-compatible compiler. CNA does not embed an HLSL
  compiler and will not; the importer resolves and records the `#include` tree (so a changed header
  rebuilds the effects that include it) and the processor drives the compiler across a process
  boundary. Output that is not an Effect Framework 9.1 container is refused rather than written
  into an `.xnb` claiming to be an XNA `Effect`.

Both produce `Effect` **only for `--format xnb`**. The CNB container reserves an `Effect`
identifier and deliberately has no schema for one: CNA has not chosen whether native CNB `Effect`
means XNA Effect Framework bytecode, `ShaderEffect` dialect payloads, or a new renderer-neutral
contract. The XNA bytecode already has the XNB route and optional compiled-effect runtimes across
several renderer families; asking for `--format cnb` refuses instead of silently freezing that
compatibility payload as CNB's native contract.

#### Selecting the compiler

```
cna-content build Content -o bin --format xnb \
    --fx-compiler "/path/to/DirectX-SDK/fxc.exe" \
    --fx-compiler-launcher wine
```

`--fx-compiler-launcher` names a program to run the compiler *through*; `wine` is the ordinary
choice on a non-Windows build machine. Use the host filesystem path to the executable;
on native Windows, use its native path and omit the Wine launcher. Each option resolves independently:

| Rank | Compiler | Launcher |
|---|---|---|
| 1 | `--fx-compiler` | `--fx-compiler-launcher` |
| 2 | `CNA_FXC` in the environment | `CNA_FXC_LAUNCHER` in the environment |
| 3 | `-DCNA_FXC_EXECUTABLE` at configure time | `-DCNA_FXC_LAUNCHER` at configure time |
| 4 | `fxc` / `fxc.exe` on `PATH` | none |

The two are **independent axes**, each resolved through its own order: naming a compiler on the
command line does not also decide the launcher, so a build that passes only `--fx-compiler` on a
machine whose CMake baked in `CNA_FXC_LAUNCHER` still runs that compiler *through* that launcher.

The compiler is probed once per invocation, and the identity it reports enters the build
fingerprint, so switching compilers rebuilds rather than reusing artifacts a different one
produced. A build with no usable compiler fails at the first `.fx` with one complete explanation —
the discovery order above and how to satisfy it — rather than once per asset.

`--fx-compiler` selects a **build-time service**, so it must reach the pipeline while it is being
registered. A library embedding `RunContentCompiler()` therefore passes a
`ContentPipelineRegistryFactory`, which the coordinator calls after parsing; handing it an
already-configured registry together with `--fx-compiler` is refused rather than silently ignored.

Processor parameters are the ordinary generic ones — `profile` (`reach`/`hidef`), `defines` and
`debug` — set per asset in `.cna-content.json` like any other processor's. Each participates in
the fingerprint, so changing one rebuilds.

Tests exercise compiler discovery/fingerprinting/failure using substitutes and real process
boundaries, and a committed genuine fx_2_0 binary covers compiled input. This audit did not run
a new Microsoft fxc compilation. Genuine XNA evidence and its limits belong in
[xnb-interoperability.md](xnb-interoperability.md) and the differential reports.

### glTF scenes and generated children

The shared glTF interpretation imports exactly the declared default scene, or the first scene when
no default is declared; a document with no scenes imports its root nodes. Other scenes are not
silently turned into Models. Within the selected scene the canonical converter groups mesh
placements by skin, with one additional static group where applicable. One group is one Model.

The default pipeline policy remains one primary Model. A single-group animated glTF succeeds and
keeps every clip embedded in Model schema 1; the standalone authoring clip documents are not
published. Every image the model's materials use -- external, buffer-view or data-URI PNG/JPEG --
is always decoded headlessly through the shared image front end, processed into native Texture2D
data and written as a Texture2D child named under the configured logical Model name
(`Models/car_tex0.png` for `Models/car`), and the Model's XREFs name those children, as XNA's
ModelProcessor builds each material texture as an asset of its own. The extracted file exists only
in the build's temporary intermediate directory, so a reference to it could never load
(CNA-DEMOS-ISSUE-022). The XNB Model writer writes the same children as XNB Texture2D assets.
Apart from those references the primary Model is byte-identical to the direct glTF producer's. A
multi-group file is refused unless its asset config explicitly sets:

```json
{
  "parameters": {
    "generateChildAssets": { "type": "bool", "value": true }
  }
}
```

With that option, the lexicographically first canonical Model document becomes the primary logical
asset and the remaining groups become named Model children, whose texture XREFs name the same
Texture2D children. Generated standalone clips become AnimationClip children. The XNB Model writer
has no form for these Model and AnimationClip children and refuses them. All child values still use
the existing schema-1 typed encoders; neither Model nor any other frozen schema changes.

Generated names must retain the converter's source-stem prefix and pass ordinary logical-name
containment. Colliding skin or animation names after filename sanitization are rejected before a
file can overwrite another. Cross-node/primary collisions are rejected by the existing output
reservation pass. The whole bundle remains one graph node: its children share one fingerprint,
staging run, manifest owner, rebuild decision, and garbage-collection lifetime. This mode
improves deployability and reuse by `ContentManager`; it does not claim cache isolation between a
Model and its extracted texture/clip children. CP-060 measured the proposed split and retained this
policy. A valid external PNG pixel edit changed only the generated Texture2D bytes, but the
representative full glTF-node rebuild and a direct texture-only rebuild both took 0.03 seconds at
CLI resolution; the full warm glTF path averaged about 1.47 ms. The importer must still parse
buffers and construct the shared scene/material graph to derive any child. Embedded and data-URI
images do not have separate authored inputs, while schema-1 Model embeds every animation clip and
material XREF choice. Independent nodes would therefore add a generated-source/ownership/partial-
publication contract for a small, uneven cache opportunity rather than eliminating the common
conversion work.

### Streaming Song and Video sources

Streaming routes keep source media external. SongImporter can probe duration; VideoImporter can
probe dimensions, frame rate, duration and soundtrack. The stock compiler registers FFmpeg
build-time probes/decoders when available, separately from the runtime Video backend.

Processors emit metadata, register build dependencies/runtime references, and request deployment
copies. Copying streams bytes rather than buffering the whole file; metadata probing and compressed
SoundEffect decoding have different resource costs. Song duration uses an explicit parameter,
then probed/imported duration, otherwise zero. Name uses its parameter/imported value or remains
empty, allowing the runtime to use the asset name.
Both can be authored per asset:

```json
{
  "parameters": {
    "name": { "type": "string", "value": "Main Theme" },
    "durationMs": { "type": "u64", "value": "185000" },
    "streamReference": { "type": "string", "value": "Music/theme.ogg" }
  }
}
```

Video dimensions/rate must come from a probe or explicit parameters. When probing fails,
supplying all three permits metadata-only compilation. Parameters override probe values;
unknown duration stays zero and absent soundtrack defaults to 0 (Music):

```json
{
  "parameters": {
    "durationMs": { "type": "u64", "value": "42000" },
    "width": { "type": "u64", "value": "1920" },
    "height": { "type": "u64", "value": "1080" },
    "framesPerSecond": { "type": "f64", "value": "29.97" },
    "soundtrackType": { "type": "u64", "value": "2" }
  }
}
```

The compiler publishes the Song/Video metadata `.cnb` and streams a byte-identical media copy to
the content-root-relative XREF path. The copy is not a writer output and is never embedded in CNB.
It has a separate manifest source/path/digest record, participates in output reservation, skip
verification, atomic publication and ownership-safe garbage collection, and uses at most 1 MiB of
copy buffer. A configured `streamReference` changes both the CNB XREF and deployment destination.

For a single-file build whose output root already contains the exact authored media source at the
XREF path, the compiler leaves that source in place and does not claim it as an owned deployment
file. Any other support destination resolving inside the source root is rejected, as are compiled
output collisions, cross-node deployment collisions, traversal and symlink escapes. A separate
output root is therefore required when copying rather than reusing in-place media.

## Registry and selection

Registries are explicitly constructed and owned. There is no hidden global component discovery or
static-initializer registration.

Default selection is a three-step route:

```text
lowercase source extension -> importer
actual imported stable type -> processor
processed stable type + output format -> writer
```

Each component also has a stable name for explicit selection. Duplicate stable names are rejected
at registration. Multiple default-eligible components may declare the same route, but resolution then fails
with a sorted ambiguity diagnostic; name-only routes do not enter automatic extension selection; registration order and “last registered wins” never decide the
result. Explicit selection must name a registered component compatible with the route.

The registry must be fully configured before builds and is permanently frozen when a coordinator
accepts it. Lookups are safe for concurrent readers, and component contracts require reentrant
invocation when more than one worker is selected.

## Optional per-asset configuration

The convention-only command remains the default. `--only-configured-assets` makes the asset
list exclusive instead of using it as overrides over directory discovery; .contentproj builds
select that policy automatically. Configuration exists for content-affecting
choices that cannot be inferred safely, including processor parameters and the metadata needed by
streaming media routes. The version-1 format is strict and deliberately contains no project-wide
build system:

```json
{
  "format": "CNA.ContentPipeline.Config",
  "version": 1,
  "sourceRoots": {
    "shared-textures": "../SharedTextures"
  },
  "assets": {
    "Textures/wall.png": {
      "logicalName": "Environment/stone",
      "importer": "CNA.ImageImporter",
      "processor": "CNA.TextureProcessor",
      "writer": "CNA.Texture2DContentWriter",
      "parameters": {
        "colorKey": { "type": "string", "value": "255,0,255" }
      }
    }
  }
}
```

`sourceRoots` is optional and maps at most 32 stable aliases to machine-local directories. An
alias is 1-64 lowercase ASCII characters matching `[a-z][a-z0-9-]*`. Relative directory values
are resolved against the primary source root; absolute values are valid only in this explicit
configuration map. Every configured directory must exist and canonicalize to a unique location.
The primary source root, all external source roots, and the output root must be pairwise disjoint:
none may equal or contain another. These conservative rules keep reads, publication, clean, orphan
collection, and staging recovery in unambiguous namespaces.

An importer or processor opts into a named root with `@shared-textures/vehicles/truck.png` when it
calls the ordinary dependency resolver. The alias is selected directly; roots are never searched.
Unknown aliases, absolute/rooted remainders, backslashes, repeated separators, `.`/`..`, and
symlink escapes fail. Changing a physical mapping to another checkout with identical relative
bytes preserves cache identity; changing the alias, relative identity, dependency set, or bytes
invalidates the node. Current CNJ and custom components can use this seam. glTF URI loading still
occurs inside its shared converter before the pipeline context receives dependencies, so glTF does
not claim named-root support yet.

Asset keys are normalized generic UTF-8 paths relative to the source root. Backslashes, absolute
paths, `..`, missing files, symlink escapes, and unsupported source extensions are rejected. A
logical-name override changes CNB metadata and, for directory builds, the relative output path; it
must obey the same safe CNB logical-name rules. A single-file build keeps its explicitly supplied
output path.

Root and per-asset `outputFormat` fields accept "cnb" or "xnb": an asset override wins over
the root default, which wins over the CLI default. The selected output path/extension follows that
effective format. Importer, processor, and writer fields are optional stable component-name overrides. Unknown or
route-incompatible names fail at selection. `parameters` is an object keyed by parameter name. Each
value has an explicit `type`: `bool`, `i64`, `u64`, `f64`, or `string`. Boolean/string values use
their corresponding JSON primitive; integer and floating-point values use strings so exact values
survive JSON parsing and persisted fingerprinting:

```json
{
  "durationMs": { "type": "u64", "value": "185000" },
  "framesPerSecond": { "type": "f64", "value": "29.97" },
  "displayName": { "type": "string", "value": "Main Theme" },
  "enabled": { "type": "bool", "value": true }
}
```

Unknown root, asset, or parameter-value fields are errors rather than ignored future behavior.
The selected processor remains authoritative for accepted option names, types, and ranges.
Configuration diagnostics name the file, asset entry, field/parameter, invalid value, and expected
form where applicable.

## Dependencies and runtime XREFs

Build-time dependencies, runtime content references, and deployment outputs are different records:

```text
robot.gltf source/build inputs:
    robot.gltf
    robot.bin
    robot_albedo.png

robot.cnb runtime reference:
    Textures/robot_albedo
```

Build dependency categories are primary source, source file, content-build dependency, generated
dependency and host file. Runtime references carry a logical name and optional expected CNB asset type
ID. A source-file dependency records either the implicit primary root or an explicit stable root
alias plus root-relative identity; the machine-local physical mapping is not semantic identity.
A trusted native processor can record a selected host file outside those roots. Its normalized
absolute path and bytes enter the local build cache; these host-file records are machine-local.
Authored asset references continue to use the contained source-root resolver.
Deployment files pair an authorized input or generated-output file with one contained
output-relative path. Input-backed deployments are byte-hashed build dependencies; a file
generated in the configured output directory is verified as an output, not invented as an input.
All three collections are separately sorted in `ContentBuildResult` and in the manifest.

Reading a source file does not automatically create an XREF. Registering an XREF does not claim
that the referenced runtime asset is enough to reproduce the build. For a custom type,
`AddRuntimeReference()` makes the reference observable to build tooling; the custom processed data
and authoritative codec must also encode the corresponding XREF. Built-in Model handling does both
through the existing canonical Model DTO and encoder. `AddDeploymentFile()` is an explicit copy
request, not an inference from an XREF. An external source may be deployed only after that exact
file was recorded through an explicit aliased dependency resolution. Publication still targets
only the output root; clean, orphan GC, and staging scavenging never treat a source identity as a
deletion target.

`AddOutputFile()` also accepts a processor-written file under the explicitly configured output
directory when that directory is separate from the source root. Its output-relative subdirectories
are preserved, so `Reports/route.txt` is published and owned under that path, not flattened to
`route.txt`. This does not authorize unrelated outside files or escaping symlinks. Source-backed
deployments retain their existing input dependencies and named external-root capability checks.
`AddGeneratedDependency()` remains a byte-hashed input dependency under the source root; it is
distinct from a generated output registered for deployment.

## Build result, logging, and errors

`ContentPipeline::Build()` builds to memory and returns `ContentBuildResult` containing:

- canonical source and logical name;
- selected importer, processor, and writer identities;
- effective parameters;
- sorted categorized build dependencies;
- sorted runtime references;
- separately deployed support files, bounded to 256 files;
- ordered info/warning messages;
- complete primary compiled bytes and stable output identity, for the selected container;
- explicitly named additional compiled outputs, bounded to 256 outputs total.

An optional `ContentBuildLogger` receives messages with source, logical asset, stage, component, and
severity while the result also records them for tests and future build integration.

Failures cross orchestration boundaries as `ContentPipelineError`. Its message and accessors retain
the source, logical asset, stage, selected component, and underlying reason text. Publication errors
are attributed to `Publish (CNA.AtomicPublisher)` by the CLI.

## Incremental builds and manifest

`cna-content` stores an inspectable, versioned `.cna-content-manifest.json` under the output root.
It does not use mtimes to decide correctness. A fingerprint includes:

- logical asset and root-relative source identity;
- primary source bytes;
- every reported source/generated/host-file dependency's bytes;
- content-build dependency fingerprints when graph scheduling is available;
- importer, processor, and writer stable names and versions;
- every selected writer asset ID, canonical type name, schema version, and codec name/version;
- typed processor parameters;
- selected output format and bound container/writer options, including XNB platform/profile/
  version/compression/reader-name policy, plus the CNB container version;
- every owned compiled-output logical identity/type and deployment source/destination identity.

Each compiled output's path, type ID, type name, schema version, and SHA-256 are stored separately.
Each deployment-support file stores its source-root alias, root-relative source, output path,
SHA-256 and `generatedOutput` boolean. When that flag is true, the source is relative to the output
root and the input-root alias must be empty; otherwise it selects the primary or named input root.
The provenance flag participates in the deployment-definition fingerprint. Generated-output bytes
are checked against the published digest, not read as part of the direct input fingerprint.
A missing or tampered primary, child `.cnb`, or deployment file; corrupt/incompatible
manifest; changed output set; changed
component version; changed parameter; or changed dependency forces the owning node to rebuild.
Identical effective inputs and intact outputs produce `SKIP`. Runtime XREF records are outputs
rather than independent inputs; the source/dependency/component inputs that produced them are
fingerprinted. Dependency correctness requires a complete recorded input set.

Selected font files are byte-hashed dependencies, including configured/system-discovered fonts.
Native processors keep root-relative identities for files within declared roots and use explicit
`host-file` records otherwise. Replacing a selected font at the same path invalidates the cache;
changing `--font-directory` also changes importer identity. The manifest is version 11, so older
records rebuild before they can reuse a font whose bytes were previously omitted.
Cache hits rehash the recorded selected font; they do not rescan the candidate set for a newly
installed higher-priority font. Such discovery changes still require an explicit rebuild.

Primary sources, file dependencies, generated-file dependencies, and existing output verification
are hashed in 1 MiB chunks. Hashing therefore uses bounded memory and accepts individual files above
2 GiB while producing the same SHA-256 as the original one-shot implementation. The ordinary test
gate pins cross-chunk equivalence; an opt-in sparse 2 GiB+1-byte
test (`CNA_RUN_LARGE_FILE_TESTS=1`) pins the full large-file path without storing a giant fixture.

Effective configuration is already represented by the manifest's logical name, selected stable
component identities, and typed parameters. Changing one asset's effective configuration therefore
invalidates that asset without treating the entire configuration file as a shared byte dependency;
an unrelated entry change leaves other assets eligible for `SKIP`.

The manifest JSON layout is versioned internal build state, not a hand-edited project format.
Version 10 adds explicit generated-output provenance to deployment records. Version 9 added the
XNB root reader identity. Version 8 introduced stable build-node IDs, ordered compiled and deployment ownership lists,
explicit writer schema/codec declarations, a direct fingerprint, an effective graph fingerprint,
and a bounded `fingerprintState` decomposition. Writer declarations are exact
`(assetTypeId, assetTypeName, assetSchemaVersion)` tuples, so one writer can truthfully advertise
separately versioned schemas for the same runtime asset; each compiled output records the exact
schema it emitted. The decomposition stores canonical SHA-256 domains
for primary bytes; source-dependency identities and bytes; content-dependency identities and
effective fingerprints; typed parameters; writer schema/codec declarations; compiled-output and
runtime-XREF definitions; and deployment definitions. It stores no timestamp or RTTI name in these domains. Aliased source/deployment identities persist as separate
`sourceRoot` and root-relative path fields; those named-root physical mappings do not enter CNB or these persisted source identities.
This is not a universal claim that the manifest has no host paths: configured font-search paths
enter FontDescriptionImporter's component version and therefore its cache identity. Version 7 introduced the aliased source identities and current reason-state decomposition,
but cannot represent multiple schemas for one asset identity under its declaration rule. Versions
1 through 9 are rejected as incompatible and cause a safe rebuild; there is no ambiguous
in-place migration. An incompatible/corrupt manifest grants no deletion authority, so its existing
outputs remain unless the new build replaces the same paths. A corrupt or future incompatible
manifest is handled the same way.

The `BuildContent` adapter likewise treats an unreadable previous manifest as missing cache
state, reports a warning and reports the current outputs as rebuilt. Its newly produced manifest
must still parse successfully; a missing or invalid new manifest remains a build failure.

`build ... --explain` compares the current structured decision inputs with this persisted v10
state; it never guesses a field-level cause from unequal aggregate hashes. The internal decision
is a list of reason codes plus optional root-relative detail, and the CLI renders that structure
only after deterministic scheduling has completed. It classifies:

- missing, incompatible, or corrupt manifests and genuinely new assets;
- logical node/output identity and importer, processor, writer, writer-schema, or codec identity
  changes;
- primary-source bytes, source-dependency identity sets, and source-dependency bytes;
- typed processor parameters;
- content-build dependency identity sets and effective dependency fingerprints;
- compiled-output/XREF definitions and deployment definitions;
- missing, tampered, or unsafe compiled and deployment artifacts; and
- an unchanged effective fingerprint with intact published digests.

The bounded per-domain hashes can identify a changed dependency domain but intentionally do not
duplicate every dependency digest merely to name one leaf. Aggregate direct/effective mismatch
fallbacks remain explicit defensive reasons for a future unknown input rather than being reported
as a fabricated source change. Incompatible older manifests produce a broad incompatible-format reason; subsequent v11 builds
have the current persisted domains.

## Multi-output nodes and ownership

One primary source still defines one build node. Its stable node ID is the configured or
convention-derived primary logical name. A writer may additionally return complete CNB file images
with their own safe logical names and asset type IDs. The total is capped at 256 to bound manifest,
validation, ownership, and publication work.

The primary output keeps the CLI-selected path, including a custom path in a single-file build.
Each additional compiled output maps beneath the output root using its logical name and the
selected container extension. Format-specific writers can reject unsupported child kinds. Before any
artifact from a node is published, the CLI reserves all of its logical names and canonical paths.
It also pre-reserves every discovered primary node, so a generated child cannot shadow another
source asset. Duplicate names, duplicate paths, absolute names, traversal, and output-root escapes
are errors.

The manifest owns all output digests under the producing node. Tampering with any child rebuilds
that node rather than treating the child as an independent source node.

After every requested node succeeds, the compiler compares the previous valid ownership inventory
with the new inventory. A path no longer owned because a source was removed, a logical output was
renamed, a route changed, or a writer's output set contracted is eligible for collection. The
compiler never discovers garbage by scanning for `.cnb`: an unrelated manually placed file is not
an owned output and survives.

Collection is deliberately stricter than path ownership alone. Before deleting any file, the
complete obsolete set is preflighted in sorted manifest-path order. Every candidate must remain
under the canonical output root through real non-symlink parent directories, be a regular
non-symlink file, and still match the SHA-256 recorded by the old manifest. Missing candidates are
already clean. A corrupt/incompatible old manifest authorizes no deletion; changed files,
symlinked parents/targets, directories, containment failures, and I/O errors are preserved and
fail the build with the prior manifest intact.

The explicit `clean <output-directory>` command calls this same preflight/deletion path with an
empty next-ownership set; it is not a second tree cleaner. A missing output root or manifest is a
successful no-op. A corrupt/incompatible/symlinked manifest authorizes nothing. Only after every
owned candidate passes preflight are files removed in sorted order, followed by the ownership
manifest. If removal or process execution stops partway, the retained manifest still proves the
remaining ownership and a later clean can resume. The command never prunes directories and never
infers ownership from an extension.

## Content-to-content build graph

Directory discovery still creates the bounded set of primary nodes in sorted logical-name order.
The graph coordinator validates the complete sorted topology and dispatches only dependency-ready
nodes: each dependency is completed before its dependent, and a shared dependency has one
state/result regardless of how many parents name it. A missing target is a Graph-stage error
identifying the dependent and dependency; a failed target prevents every dependent from publishing.
Independent successful nodes may still publish, but the manifest is replaced only when the complete
requested graph succeeds.

Each manifest node has two hashes:

- `directFingerprint` covers the source bytes, source/generated file dependencies, components,
  parameters, output identities/types, and content-build edge identities;
- `fingerprint` combines that direct hash with every dependency node's effective fingerprint.

If the direct fingerprint still matches, the previous edge set is safe to traverse without
rerunning the importer/processor merely to rediscover it. After dependencies complete, the
effective hash and all owned output digests decide `SKIP`. Changing a shared dependency therefore
rebuilds every transitive dependent even when its own source and direct hash are unchanged. If a
direct input or configuration changes, the node runs first to discover its new edge set, so a
removed stale edge cannot block the rebuild.

The visiting-state guard refuses cycles during graph preflight instead of recursively executing
them. Diagnostics print the exact logical cycle once, for example:

```text
content-build dependency cycle:
  Levels/world
  -> Navigation/world
  -> Levels/world
```

Root traversal and every outgoing edge are sorted, so the chosen chain and its spelling are stable.
Self, two-node, and three-node cycles are covered; the long-cycle subprocess is repeated and must
produce byte-for-byte identical diagnostics. No node in or dependent on a cycle publishes and the
manifest is not replaced.

## Bounded deterministic scheduling

The registry is configured before freeze. A regression invokes sixteen independent Build calls
through one frozen registry. Contexts/dependency collectors/parser data are per invocation;
cgltf uses per-call data and stb has thread-local decoder settings.

This is not general concurrent host reconfiguration support. Font search directories currently live
in a process-global vector: RegisterBuiltInContentPipeline calls SetFontSearchDirectoriesEXT,
while importer Identity/import read it without a lock or registry-local snapshot. Keep that
configuration fixed while builds run. Independently configuring different embedded compilers
concurrently needs a separate synchronization/isolation correction (handoff §5.2).

Filesystem staging is also reservation based. Model/glTF intermediates claim a directory with an
exclusive create, and the one atomic publication helper claims sibling temporary files with the
platform's exclusive-create primitive. Logical/path ownership already prevents two graph nodes
from targeting the same final artifact.

Every build or clean also claims the persistent `.cna-content.lock` beneath its canonical output
root for the complete operation (`flock` on POSIX, an exclusive no-share handle on Windows). An
active owner makes another operation fail before manifest inspection or publication; an unlocked
marker is reclaimed after normal exit or a crash. A symlinked, non-regular, or otherwise unsafe
marker is rejected. This serializes independent processes as well as worker threads and prevents a
clean from racing an active publisher.

The CLI uses dependency-aware ready batches capped by `--workers`. Shared dependencies execute
once; dependent nodes become ready only after all of their content-build inputs succeed. A failed
dependency prevents dependent publication. `--workers 1` takes a synchronous path without
launching worker tasks.

Changed nodes first run a no-publication preparation pass so their complete output/edge topology is
known before cycle validation and scheduling. Prepared output bytes are placed in a private,
owner-only staging directory through the same atomic writer used everywhere else, then released
from memory. This avoids retaining all prepared asset bytes at once. Peak build memory still depends on the
largest active imports/encodes and their intermediates; worker count is not a fixed byte budget. Before final publication, staged size and SHA-256 are checked against
the prepared manifest record.

Compiler staging lives under the private versioned system-temporary parent
`cna_content_staging_v1`. Each run writes an identity marker and holds a separate OS lease for its
whole lifetime (`flock` on POSIX, exclusive no-share file handle on Windows). Startup scavenging is
bounded to 4,096 parent entries and 256 candidate trees. A candidate must have an exact versioned
name and matching <=1 KiB marker, be a same-user non-symlink directory, be at least 24 hours old,
and have a regular lease that can be exclusively claimed. PID is recorded but never used as
liveness evidence, so PID reuse cannot authorize deletion and a genuinely active old build stays
protected by its lease. Removal targets only that validated direct child; nested symlinks are not
followed. Recent, future-dated, malformed, symlinked, owner-mismatched, active, legacy and otherwise
indeterminate entries remain untouched. Non-quiet builds report removals and sorted conservative
diagnostics. Normal success/handled failure still cleans its own tree immediately through RAII;
after a crash, the released lease makes the tree eligible once the age threshold expires.

Updated tools also share the empty `.cna-staging.lock` file in that parent. Its OS lock
(`flock` on POSIX, synchronous `LockFileEx` on Windows) covers reservation initialization,
scavenger inspection/removal and owner teardown, not the content build itself. This prevents
other processes and threads from observing partially published or removed ownership metadata.
The constructor invokes the already-locked scavenger directly; it never recursively waits on
another independently opened parent lock. Per-run leases still protect genuinely active builds.

The parent coordination file is intentionally persistent, singly linked and empty. Tools never
delete or replace it: doing so could leave a waiting process locking a different file identity
from new arrivals. A nonempty, linked, symlinked, non-regular or unopenable coordination file
refuses reservation and produces a diagnostic instead of allowing a scavenger to proceed.
On POSIX it must belong to the current user. An owner unable to acquire the parent lock during
teardown leaves its intact tree for later conservative scavenging rather than removing metadata
without synchronization. Normal cleanup therefore leaves this one housekeeping file in the
staging parent, but no per-run tree. Remove a private staging parent only when all its users are
known to have exited; deleting the coordination file while tools may be running is unsafe.
Older tool binaries do not participate in this new coordination protocol; rebuild all cooperating
tools to obtain the concurrency guarantee. No diagnostic for persistently malformed candidate
metadata is suppressed by this protocol.

Workers read the frozen graph and dependency fingerprint snapshot and return node-local outcomes.
Only the coordinator owns output reservations, graph state, effective fingerprint integration,
manifest mutation, counters, and stdout/stderr. Ready lists and result integration use stable
logical-name order. Tests compare worker counts 1, 2, and 4 across cold, no-op, and shared-input
rebuilds and require identical CNB/output trees, manifest bytes, diagnostics, and summaries.
The reproducible methodology and current Debug/HEADLESS measurements are recorded in
[`content-pipeline-benchmark.md`](content-pipeline-benchmark.md); performance is evidence, not a
cross-machine guarantee or correctness threshold.

## Determinism and publication

Directory discovery is sorted by the UTF-8 logical name. Registries use ordered maps/sets and never
serialize RTTI. Built-in output bytes avoid timestamps, temporary-file identities, PIDs, pointers and RTTI.
Determinism assumes identical source/dependency bytes, component/tool/font/codec versions and
processing settings. Font-search configuration can intentionally put host paths in component
identity; arbitrary custom components must enforce their own deterministic output contract.

Writers finish every CNB image in memory. The CLI then calls the one shared audited atomic publisher
from `tools/common/CnaToolAtomicWrite.hpp` for the primary output followed by additional outputs.
Deployment media uses the same exclusive sibling-temporary/replacement path, but streams from its
source in 1 MiB chunks rather than materializing the file in RAM. Each individual final artifact is
all-or-nothing and an old artifact is never removed before its replacement is ready. Manifest
publication uses the same helper and occurs only after every requested node succeeds.

Portable filesystems do not provide a transaction spanning several paths. If a later output fails,
an earlier output may already contain its new complete bytes, but the previous manifest and any
unreplaced artifacts remain. The next invocation detects the old manifest's digest mismatch and
rebuilds the complete owning node. Thus the manifest never claims a partially published output set,
recovery is deterministic, and no partial file is exposed. A directory build likewise is not a
transaction across unrelated nodes.

Obsolete-output collection occurs after all artifact publications succeed but before the new
manifest is atomically published. A build/node failure performs no collection. A crash, removal
error, or manifest-publication failure can therefore leave some newly published artifacts and/or
some already removed obsolete artifacts beside the old manifest, but that manifest retains the
ownership proof needed to retry: already missing stale paths are accepted, and current output
digest mismatches rebuild their nodes. The compiler never publishes a manifest claiming an output
that was not committed.

## Paths and security

Every build has an explicit source root. The built-in source/sidecar/dependency resolvers check
containment for their supported paths, including glTF buffers/images and CNJ references. Absolute authored references,
`..` escapes, and canonical symlink escapes are rejected. There is no implicit outside-root opt-in.
For dependency-context authored references, outside-root reads require named sourceRoots plus
@alias/path. Build services have separate access: font discovery reads configured/environment/
installed directories, and an external compiler/encoder runs as the host user. Those paths are
not sandboxed by sourceRoots; the font cache gap is documented above. Canonical source/external/output roots cannot overlap, and an
external root grants no write, publication, clean, GC, or staging-scavenger authority.

On Windows, `cna-content` uses `wmain(int, wchar_t**)` and constructs native
`std::filesystem::path` values directly. On POSIX, argv bytes are passed to `std::filesystem`.
Logical names, manifest paths, dependency identities, and diagnostics use explicit generic UTF-8;
manifest reads explicitly convert UTF-8 back to a native path.

Native non-ASCII paths are covered for image, WAV, DDS, CNJ, and Model/glTF sources. The shared glTF
orchestration passes a generic UTF-8 spelling to cgltf, whose CNA-owned file callbacks convert that
spelling back to a native `std::filesystem::path` before opening the primary document or an external
buffer. Authored external image/buffer URIs and generated CNJ sidecar names cross the same explicit
UTF-8/native boundary. `BuildCnbModelFromCnj()` has a native-path overload and opens its document and
sidecars without narrowing; its string overload remains only for existing narrow-path callers.

A POSIX regression test builds a model whose source root, nested directories, `.gltf`, external
`.bin`, and external texture all contain non-ASCII characters, repeats the build byte-identically,
and validates both source dependencies and the resulting CNB. A fresh MinGW-w64 configuration also
compiles and links the complete `cna_content` library, stock `cna-content.exe`, and custom compiler
example. MinGW's Unicode-console startup option is target-local to those two `wmain` executables.
Wine 10 executes both: the stock tool builds through a native `Zażółć/曲線` output path and produces
a Curve CNB byte-identical to Linux, while the custom tool publishes its primary and generated
reply outputs. That observation is MinGW compile/link plus Wine execution. The separately recorded native MSVC
checkpoint below covers its own narrower revision/selection; it does not turn the Wine run into
native evidence. Existing direct glTF producer and pinned Model-byte
equivalence tests remain unchanged.

## Custom extensions

The library can register custom importers, processors, writers, codecs, and runtime loaders without
modifying CNA. The validation test uses a `.level` importer with a collision sidecar, a processor
with a typed `solidBorder` parameter, a custom CNB codec with a Texture2D XREF, a writer adapter,
and a custom ContentManager loader.

The essential registration shape is:

```cpp
auto registry = std::make_shared<CNA::Content::Pipeline::ContentPipelineRegistry>();
registry->RegisterImporter(std::make_shared<WorldLevelImporter>());
registry->RegisterProcessor(std::make_shared<WorldLevelProcessor>());
registry->RegisterWriter(std::make_shared<WorldLevelWriter>());

CNA::Content::Pipeline::ContentPipeline pipeline(registry);
```

Custom components must use deliberate stable strings for component/type identities and increment
the component version whenever component behavior changes. A custom writer must additionally
declare its asset/schema/codec contract through `OutputSchemaIdentities()`; a codec change must bump
the codec version even when its writer and schema versions stay fixed, while an incompatible wire
schema change must bump the schema version. The manifest fingerprints all of them independently,
so forgetting to bump the broad writer component version no longer leaves a schema/codec evolution
eligible for `SKIP`. A custom writer should adapt to one authoritative custom codec, just as
built-ins adapt to `Encode*ToCnb()`; it should not duplicate its schema in multiple front ends.

The stock `cna-content` binary registers built-ins only. CNA now exposes that binary's complete
command coordinator through `CNA::ContentCompiler`, so a game can build a small custom executable
without copying discovery, configuration, incremental manifests, diagnostics, or atomic
publication:

```cpp
#include "CNA/Content/Pipeline/ContentCompiler.hpp"

auto registry = std::make_shared<CNA::Content::Pipeline::ContentPipelineRegistry>();
CNA::Content::Pipeline::RegisterBuiltInContentPipeline(*registry);
registry->RegisterImporter(std::make_shared<WorldLevelImporter>());
registry->RegisterProcessor(std::make_shared<WorldLevelProcessor>());
registry->RegisterWriter(std::make_shared<WorldLevelWriter>());
return CNA::Content::Pipeline::RunContentCompiler(arguments, std::move(registry));
```

```cmake
add_executable(my_content_compiler my_content_compiler.cpp)
target_link_libraries(my_content_compiler PRIVATE CNA::ContentCompiler)
```

[`modules/content/examples/custom-content-compiler.cpp`](../modules/content/examples/custom-content-compiler.cpp)
is a complete executable: it registers all built-ins plus an `ExampleGame.Greeting` `.greeting`
route, uses typed `prefix`, `dependsOn`, and optional external-deployment configuration parameters,
and adapts its writer to the custom type's single `EncodeGreetingToCnb()` codec. The writer
exercises the real multi-output path by emitting a
primary greeting and a generated reply CNB through that same codec. A subprocess test compiles a
mixed custom/PNG directory, checks both custom CNBs and the built-in Texture2D output, verifies
manifest ownership and component/parameter/schema/codec identity, proves a byte-preserving no-op,
forces rebuilds after independently stale asset-ID, type-name, schema-version and codec-version
records, repairs a tampered child, rejects collision with another primary node, and covers recoverable partial
publication failure. Its optional string `dependsOn` parameter also exercises real graph edges:
tests prove dependency-first ordering, one execution of a shared dependency, no-op reuse,
transitive invalidation, missing-target diagnostics, failure propagation, and recovery.

This is a **source/toolchain compatibility model**, not a plugin ABI. The custom executable and CNA
must be compiled and linked as compatible C++; applications should rebuild their compiler when CNA
or the toolchain changes. There is deliberately no arbitrary shared-library search, static
initializer registration, binary version handshake, or claim that separately distributed C++
plugins are stable. `ContentPipelineExtensionApiIsExperimental == true` covers both custom
components and the embedding functions; persistent author-controlled component/type names remain
the configuration and fingerprint identities, but the C++ declarations and ABI may evolve.

Registration has a permanent configure-then-freeze boundary. `ContentPipeline` freezes its shared
registry when constructed, and `RunContentCompiler()` freezes it before source discovery. A later
`Register*()` call through any retained mutable alias fails instead of racing a build. `Freeze()` is
also public and idempotent for callers that want to make the boundary explicit, while `IsFrozen()`
exposes it for integration diagnostics.

The registry owns one shared `const` instance of each component. Importer, processor, and writer
implementations must therefore be reentrant: independent builds may call the same component
concurrently after freeze. Built-ins satisfy that contract with invocation-local values and
contexts. A custom logger shared by concurrent direct `Build()` calls must synchronize itself;
each result still records its own ordered message sequence.

## CNJ, CNB, and XNB

CNJ remains a supported authoring/intermediate front end. Direct image, WAV, and glTF inputs and
their equivalent CNJ inputs converge on the same processors and writers where semantics match.
CNB never depends on JSON internally.

CNB is the CNA-native compiled format produced here. XNB is both an existing runtime compatibility
format and, for the roots below, a Content Pipeline source format. `XnbImporter` validates the XNB
header, None/LZX payload, normalized type-reader table, root reader/version, nested references,
limits and complete consumption. It decodes to canonical CPU data shared with the runtime readers,
then uses the same processors, writers and `Encode*ToCnb()` functions as other sources.

```text
supported built-in XNB
    -> shared Decode*XnbData() CPU representation
       -> existing runtime adapter (old XNB loading)
       -> XnbImporter -> existing processor/writer -> native CNB
```

| Root ContentTypeReader | Native target | Accepted compatibility subset |
|---|---|---|
| `Texture2DReader` | Texture2D | Color and DXT1/3/5, level zero or full mip chain; DXT normalizes to Rgba8 |
| `SpriteFontReader` | SpriteFont | built-in Texture2D/List nested graph and every font field; atlas follows texture limits |
| `SoundEffectReader` | SoundEffect | PCM8/16; float32 and MS/IMA ADPCM when the SDL3 decoder is compiled; loops/rate/channels preserved |
| `Texture3DReader` | Texture3D | Color with its declared levels; the shared decoder rejects compressed volume formats, and other legal native volume formats cannot preserve their identity in the CNB conversion |
| `TextureCubeReader` | TextureCube | Color and DXT1/3/5, six faces and all declared levels, normalized to Rgba8 |
| `CurveReader` | Curve | all loop/key/tangent/continuity fields |
| `SongReader` | Song | path/duration metadata plus contained external media dependency/XREF |
| `VideoReader` | Video | FNA String/Int32/Single object-reference graph plus contained external media dependency/XREF |
| `ModelReader` | Model | frozen schema 1 for its original narrow exact subset; schema 2 for exact general declarations, shared resources, part windows, authored bounds/root, and five stock effects; null tags only |

`ModelReader` first applies the deliberately narrow schema-1 subset proved by `CP-040`/`CP-041`:
canonical CNA vertex declarations; triangle-list parts consuming unique whole buffers; bone 0 as
an identity root; null tags; unique BasicEffect resources with default `SpecularPower`; and a
serialized sphere equal to the deterministic schema-1 reconstruction. Any Model satisfying those
rules still goes through the unchanged schema-1 converter and encoder, with byte-identical output.

When an otherwise valid Model cannot fit those rules, schema 2 preserves all twelve XNA vertex
formats and thirteen usages with exact offsets/stride/usage index; shared or unused supported
vertex/index/effect resources; exact part windows and serialized spheres; explicit root identity,
bone hierarchy and every transform; and every serialized field of `BasicEffect`,
`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, and `SkinnedEffect`. Texture
references remain typed logical Texture2D/TextureCube XREFs. The real MonoGame Blender cube now
uses schema 2 and retains its stride-24 Position+Normal declaration, authored sphere, and
non-default `SpecularPower` exactly.

Non-null Model/Mesh/MeshPart tags, custom Effect readers/material graphs, unknown shared-resource
readers, malformed references/graphs/ranges/windows, invalid stock-effect values, and unsafe
texture names remain explicit failures. CNA does not invent CLR object serialization, Effect
graphs, XNB reader tables, or an approximation. Every other custom/unknown root is rejected with
its normalized reader identity.

Model compilation is headless. One shared field-order graph walker feeds either the existing
runtime ownership/fixup adapter or canonical `XnbModelData`; shared CPU declaration, buffer and all
five stock-effect decoders sit below both paths. The compiler attempts the schema-1 converter
first, then independently validates schema 2 only after a schema-1 fidelity failure. The resulting
schema-tagged carrier follows `ImportedModelDocument -> ModelProcessor -> ModelContentWriter` to
the authoritative matching encoder. It does not instantiate a `GraphicsDevice`, GPU buffer or
runtime Effect and does not preserve XNB bytes. Synthetic and real MonoGame fixtures compare the
runtime XNB and native-CNB results field-by-field, including declarations, bytes, sharing, windows,
bounds, roots/transforms and material values—not merely counts.

None, LZX, and MonoGame raw-block LZ4 compression and XNB versions 4/5 are supported through CNA's
shared container code. The existing 16 platform header
identifiers remain valid, but Xbox-swizzled texture/sample payloads are not transcoded without a
proven byte-order path. Frozen CNB schema 1 cannot preserve BGRA or NormalizedByte2/4 texture format
identity, so those formats are rejected rather than silently changed. XMA2 and unknown audio codecs
are likewise rejected.

For Song and Video, the external media stays external to CNB: it is a source-file dependency for
fingerprinting, a CNB XREF for runtime resolution, and an explicitly owned deployment-support copy
when the output root differs from the source location. Absolute paths, traversal and symlink
escapes fail through the ordinary containment policy. The media bytes are never embedded.

There is no `EmbeddedXnb`, `XNB0` chunk, opaque payload, reader/CLR name in output, second scheduler,
or alternate manifest. Unsupported XNB means a diagnostic and no published artifact.

## Configuration, profiles, parallelism, and CMake

Native builds work without a project file, with optional typed per-asset JSON configuration.
A supported XNA project can also be built directly:

```bash
cna-content build Content/MyGameContent.contentproj -o Content/bin
```

The project adapter reads supported platform/profile/compression/component/parameter properties
and copies supported Content/None items, using the same coordinator with XNA-compatible leniency.
The CLI supports explicit platform/profile/compression overrides for a project;
[cna_add_content](#configuration-profiles-parallelism-and-cmake) deliberately rejects those
conflicting helper arguments with CONTENT_PROJECT.

CNB has no encoded platform/profile header, but the build environment exposes target platform,
profile and configuration to processors. XNB texture restrictions, font-atlas compression and
effect compilation already use implemented format/environment policy. For example, .spritefont
XNB output uses a DXT3 atlas, while native CNB keeps lossless RGBA data. Direct texture routes can
request DXT as well. This is not a universal assertion that all processors are target-neutral.

Service options include `--fx-compiler`/`--fx-compiler-launcher`,
`--xma-encoder`/`--xma-encoder-launcher`/repeated `--xma-encoder-arg`,
repeated `--font-directory` and `--build-configuration`. The latter defaults to Release for
ordinary builds and follows a project's Configuration unless explicitly overridden; it supplies
EffectProcessor's Auto debug policy. Source/encoder availability and XMA qualification remain
bounded; see [xma-encoder-backend.md](xma-encoder-backend.md).

Build-graph scheduling is serial and deterministic unless `--workers` opts into bounded concurrent
execution. Registries are configured explicitly and frozen before discovery. Built-in components
are reentrant; custom components and custom loggers must follow the concurrency contract documented
above when their compiler is invoked with more than one worker.

CMake can create a content target with the helper defined alongside the CNA tool:

```cmake
cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ContentSource"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Content"
)

add_dependencies(MyGame MyGameContent)
```

`SOURCE_DIR` and `CONTENT_PROJECT` are alternatives; the latter must name a .contentproj file.
Those paths and optional `CONFIG_FILE` resolve relative to the caller's source directory;
`OUTPUT_DIR` is relative to its binary directory. `CONFIG_FILE` must exist at configure time and is
passed to the CLI, whose canonical source-root containment check remains authoritative. `WORKERS`
is optional, strictly accepts 1 through 64, and defaults to the CLI-compatible serial value 1.

The custom target intentionally invokes `cna-content` whenever the target is requested; the
pipeline's byte-hashed manifest performs the correct per-asset no-op decisions. CMake therefore
does not duplicate configuration parsing, source discovery, dependency hashing, graph/cache logic,
or publication. `QUIET` forwards quiet output.

For SOURCE_DIR builds, the helper also accepts FORMAT cnb|xnb, XNA_COMPATIBLE, and
XNB_PLATFORM/XNB_PROFILE/XNB_COMPRESS when FORMAT is xnb. With CONTENT_PROJECT it refuses FORMAT,
those XNB overrides, CONFIG_FILE and XNA_COMPATIBLE because the project supplies them; this is
stricter than the CLI's override interface. Examples:

```cmake
cna_add_content(
    TARGET MyLegacyContent
    CONTENT_PROJECT Content/MyGameContent.contentproj
    OUTPUT_DIR Content
)
```

Projects that need per-asset JSON settings can add them to the convention-only flow:

```cmake
cna_add_content(
    TARGET MyConfiguredContent
    SOURCE_DIR ContentSource
    OUTPUT_DIR Content
    CONFIG_FILE ContentSource/pipeline.json
    WORKERS 4
)
```

A custom compiler can drive the same helper through its existing executable override. The explicit
dependency ensures the compiler exists before the content target runs:

```cmake
cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR ContentSource
    OUTPUT_DIR Content
    CONTENT_EXECUTABLE "$<TARGET_FILE:my_content_compiler>"
    CONFIG_FILE ContentSource/pipeline.json
    WORKERS 4
)
add_dependencies(MyGameContent my_content_compiler)
```

For a cross-compiling CMake build, a target-platform `cna-content` executable cannot run on the
host. Such a call must provide an already-built host tool explicitly:

```cmake
cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR ContentSource
    OUTPUT_DIR Content
    CONTENT_EXECUTABLE "/path/to/host/cna-content"
)
```

Without `CONTENT_EXECUTABLE`, cross-compiling fails at configure time with a specific diagnostic.
No `cna_add_game()` convenience layer is defined yet.

## Verification and current limitations

The 2026-11-01 documentation audit rebuilt `cna_content_tool` (executable `cna-content`),
`cna_custom_content_compiler_example`, `CnaContentTests` and the real CMake integration
fixtures in the existing `cmake-build-multi/` tree, plus `CnaContentPipelineTests`, then exercised
the native pipeline, CLI, configuration, manifest, media/texture operations, compiled-effect and
XNB-output selections:
**264 cases, 263 passed and one opt-in >2 GiB file-hash case skipped**. A separate XNA façade
selection passed all **38** project/task/source/texture cases. Temporary probes also confirm
strict unknown-parameter refusal after a lenient build and reproduce the configured-font cache
defect above.

Run window/device-capable tests through the private runner even when a selected case is CPU-only:

```bash
cmake --build cmake-build-multi \
  --target cna_content_tool CnaContentTests --parallel 4
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_GRAPHICS_RENDERER=OPENGLES3 SDL_AUDIODRIVER=dummy \
  cmake-build-multi/CnaContentTests --gtest_filter='*ContentPipeline*:*ContentBuild*'
```

The command is a useful focused selection, not the exact broader filter behind the count above.
Tests cover worker 1/2/4 determinism, source/deployment ownership, graph/cycle decisions,
incremental explanations, no-op bytes, partial publication recovery and clean.
The opt-in hash case requires `CNA_RUN_LARGE_FILE_TESTS=1`.

CP-062 recorded native Windows/MSVC run 33309632114 on
`83807bef64990541e7d41274c11b9562e49112cc`: 177 passes and one large-file skip, plus its Unicode
CLI lifecycle and then-current manifest-v8 rebuild checks. It is historical revision-specific
evidence, not a fresh test of today's manifest v11. Earlier ASan/UBSan and TSan closeouts are in
Git history; no new sanitizer campaign was run for this documentation edit, and runs with
LeakSanitizer disabled do not establish leak freedom.

Multi-file publication is recoverable per-artifact replacement, not one filesystem transaction.
Staging consumes disk space and abrupt termination can leave owner-marked trees; conservative
scavenging does not delete malformed/legacy trees. Ownership-proven cleanup never scans output
extensions to invent deletion authority.

Build-time and runtime modules are physically separate: `modules/content/` owns the canonical
core/codecs and `modules/content-pipeline/` owns build-only XNA components and services.
`CNA::ContentCompiler` links that build-time boundary. C ABI policy/inventory belongs in
[the C API coverage policy](c-api/COVERAGE.md); the owner excludes build-time pipeline
declarations from the runtime C-binding denominator (CBIND-117). Historical header/symbol counts
and “open missing pipeline bindings” are not current requirements.

## Stability summary

**Stable/frozen:**

- CNB container 1.0 and documented per-asset wire versions, IDs/chunks/CRC and typed codecs;
  SoundEffect's current writer uses schema 2 while its decoder retains schema 1;
- Model schema 2's separately versioned wire contract, independent golden vector, and schema-1/
  schema-2 runtime dispatch; schema 1 itself and its existing producer bytes are unchanged;
- the rule that built-in writers reuse those encoders;
- byte equivalence with legacy producers for matching implemented semantics;
- lossless XNB Model transcoding with schema 1 retained byte-for-byte for its original subset and
  schema 2 selected only for the documented exact declaration/resource/window/bounds/root/stock-
  effect matrix; non-null tags, custom effects, and malformed/unsafe semantics remain rejected;
- build/runtime separation, deterministic selection, categorized dependencies versus XREFs,
  content-hashed skips, logical path preservation, bounded output ownership, and per-artifact
  atomic publication;
- manifest-proven orphan collection and explicit clean without output-tree or extension scanning.

**Experimental:**

- the public C++ importer/processor/writer interfaces and stable in-memory type strings;
- the C++ multi-output writer result and custom output generation surface;
- custom registration and the user-built `CNA::ContentCompiler` embedding surface;
- component names/versions as user configuration identifiers, plus explicit writer asset/schema and
  codec identities used by cache fingerprints;
- opt-in glTF Model/Texture2D/AnimationClip generated bundles and their naming policy;
- content-build edges, dependency builds, and bounded parallel scheduling;
- the manifest-v11 persisted fingerprint-domain decomposition and explicit host-file inputs, stable aliased source identities,
  and generated-output provenance used by incremental decisions;
- named, explicit, read-only external source-root capabilities;
- structured incremental decisions and the human-readable `build --explain` rendering;

**Future:**

- a new cross-cutting native-CNB target profile only if existing typed policy cannot represent
  a measured need; current XNB/XNA profiles already have implemented processor policy;
- reconsider independently scheduled glTF children only if a materially larger measured workload
  justifies a generated-source graph contract and supplies stable per-child dependency partitions;
- stable machine-readable build-decision output, if an IDE/build integration contract justifies a
  separately versioned format;
- further link-closure refinement only if measured consumers/dependencies justify it; the
  build-only ContentPipelineBuild module already exists;
- whether built-in authoring formats beyond the context-resolved CNJ/XNB/custom seams need explicit
  external-root reference syntax. glTF's shared converter validates and opens URIs before the
  pipeline context sees them, so it must not claim the named-source-root capability without a
  separately reviewed resolver callback;

**Not provided:**

- a stable dynamic plugin ABI or automatic shared-library discovery;
- binary compatibility for custom C++ compiler components across CNA/toolchain changes.

**Internal/versioned implementation detail:**

- `ContentValue` type-erasure mechanics and process-local RTTI guard;
- the exact manifest JSON layout and cache implementation;
- glTF's temporary canonical CNJ staging representation;
- temporary-file naming used by atomic publication;
- the persistent output-lease filename and exact OS locking mechanism.

Output-root locking is cooperative among current compiler binaries. Concurrently mixing a build or
clean with an older compiler that predates `.cna-content.lock` is unsupported because that older
binary cannot honor the lease. No manifest generation, diagnostic, or check performed by the new
binary can make a concurrently running legacy executable acquire a lock it does not implement;
different compiler generations must not overlap against the same output root.

## Rejected alternatives

The designs this pipeline deliberately does not use, each with the reason, so they are not
rediscovered:

- **`Cnb*Data` as every importer's output.** It would erase the import/process split: an image
  importer would already have made texture policy decisions.
- **One `CompileSourceToCnb` switch.** That was the tool shape the pipeline replaced; it is not
  extensible and hides component choice and dependencies.
- **A pure template/concept pipeline.** It would move selection to compile time, lose registry
  diagnostics and custom discovery, and invite opaque metaprogramming.
- **A `std::variant` of every built-in type.** Every game extension would have to edit CNA's
  closed variant.
- **Persisting `std::type_index` or `typeid(T).name()`.** ABI- and compiler-dependent.
- **Global self-registering components.** Static-initialization order, test isolation,
  registration-order selection and concurrency all argue against it.
- **Writers that rebuild CNB chunks.** The typed CNB encoders are authoritative, without exception.
- **A second native CNB reader hierarchy.** CNB already has typed codec/loader dispatch; XNB's
  separate compatibility ContentTypeReader hierarchy remains intentional.
- **Inferring source dependencies from XREFs.** Source reads and runtime loads have different
  semantics.
- **Timestamp-only incremental builds.** They cannot answer whether the effective inputs changed.
- **A project/configuration format first.** Deferred until implemented processors showed the
  minimum useful semantics; `.cna-content.json` is what that produced.
- **A parallel scheduler in v1.** Deferred until registry, dependency and publication correctness
  were stable; it came later as the bounded `--workers` scheduler.
- **Custom components loaded as arbitrary shared libraries.** Virtual interfaces, STL ownership and
  exceptions cross the C++ ABI and CNA has no stable plugin handshake; a user-built compiler linked
  from source covers the real extension case without implying binary compatibility or searching
  untrusted directories.

## History

The pipeline was built as `CP-001`..`CP-063` (2026-08-29/30) and merged into `next`. Its plan,
`plans/plan_content_pipeline.md`, was deleted on 2026-10-28 (Git history only); source comments
cite the rows as "Content Pipeline CP-nnn".
