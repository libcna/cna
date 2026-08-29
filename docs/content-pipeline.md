# CNA Content Pipeline

> Status: implemented initial pipeline, updated 2026-08-28. The command-line build workflow and
> built-in byte-compatibility guarantees described here are supported behavior. The custom C++
> component API is explicitly experimental. CNB container 1.0 and the existing built-in CNB
> schemas remain frozen.

The CNA Content Pipeline is the build-time system that turns authoring files into CNB. It is
inspired by XNA 4.0's Importer -> Processor -> Writer separation, but it uses CNA-native C++23
components and the existing CNB codecs instead of CLR reflection, XNB reader tables, or MSBuild.

These three terms describe separate layers:

- **CNA Content Pipeline** imports source data, processes it, records dependencies, chooses
  components, builds incrementally, and publishes artifacts.
- **CNB** is CNA's native compiled runtime content format.
- **ContentManager** loads CNB into runtime objects.

CNB is an output of the Content Pipeline; CNB is not the Content Pipeline itself.

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

Configuration remains optional. A directory or single-file build automatically reads
`.cna-content.json` from its source root when present; `--config <path>` selects another file
inside that root.

Runtime loading remains the existing ContentManager API:

```cpp
auto robot = content.Load<Model>("Models/robot");
auto wall = content.Load<Texture2D>("Textures/wall");
auto ui = content.Load<SpriteFont>("Fonts/ui");
auto explosion = content.Load<SoundEffect>("Sounds/explosion");
```

The runtime needs the `.cnb` artifacts, not the authoring PNG, WAV, glTF, CNJ, or sidecar files.

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
complete CNB bytes
    |
    v
shared atomic publisher
    |
    v
*.cnb

-------------------------- RUNTIME --------------------------

*.cnb -> typed CNB decoder + CnbLoaderRegistry -> ContentManager -> runtime object
```

Import and processing are separate even when the first processor is intentionally thin. An image
importer answers “what pixels are in this source?”; a texture processor answers “how should these
pixels become runtime Texture2D content?”; the writer answers “which authoritative CNB codec emits
the compiled representation?”

No pipeline component constructs a GraphicsDevice, opens an audio device, creates a window, reads
back GPU data, or initializes a renderer. The tested HEADLESS `cna-content` executable has no SDL,
graphics-driver, FFmpeg, or audio shared-library dependency. Model import uses CPU-side canonical
data and temporary authoring intermediates, not runtime Model objects.

## XNA conceptual mapping

| XNA 4.0 concept | CNA implementation |
|---|---|
| `ContentImporter<T>` | `ContentImporter`, a checked type-erased C++ component |
| `ContentImporterContext` | focused, call-scoped source/dependency/logging services |
| imported object | explicit source-oriented C++ value such as `ImportedImage` |
| `ContentProcessor<TInput,TOutput>` | `ContentProcessor` with stable input/output identities |
| `ContentProcessorContext` | focused parameters, dependency, XREF, and logging services |
| `ContentTypeWriter<T>` | `ContentTypeWriter` adapter over one existing typed CNB encoder |
| `ContentTypeReader<T>` | existing typed CNB decoder plus `CnbLoaderRegistry` |
| `ExternalReference<T>` | CNB XREF for runtime references; separate records for build inputs |
| XNB | CNB for CNA-native compiled content; XNB remains the compatibility path |
| MGCB/MSBuild | `cna-content` plus the thin `cna_add_content()` CMake helper |

CNA deliberately does not copy assembly discovery, reflection-driven processor properties,
assembly-qualified type names, CLR generic names, XNB reader tables, XNA platform bytes, or
`.contentproj`/MSBuild machinery.

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
logging. `ResolveSourceDependency()` rejects absolute authored paths and paths that escape the
source root, including canonical symlink escapes, and records the resolved file as a build input.

Components must not retain references or pointers to a context after `Import()` returns. Component
objects are registry-owned and reusable; they should keep no per-build mutable state so future
parallel scheduling remains possible.

## Imported and processed data

Imported values preserve source semantics where that distinction is meaningful:

```text
PNG/JPEG/etc. -> ImportedImage -> CnbTextureData
WAV           -> ImportedSound -> CnbSoundEffectData
glTF          -> ImportedModelDocument -> CnbModelData
```

`ImportedImage` contains validated RGBA8 pixels and dimensions before texture policy. `ImportedSound`
preserves whether accepted PCM samples were unsigned 8-bit or signed little-endian 16-bit until the
processor performs the exact Pcm16 conversion. `ImportedModelDocument` is an owned lifetime seam
over CNA's existing canonical glTF-to-CNJ interpretation, avoiding a second glTF parser while the
shared importer is migrated further in memory.

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
builds. `TextureProcessor` accepts the string parameter `colorKey` in `R,G,B` decimal form.
`SongProcessor` accepts `streamReference` and `name` strings plus a `durationMs` u64. Texture2D CNJ
can also author its existing color-key field.

### ContentProcessorContext

The processor context exposes the logical asset name, validated parameters, safe dependency
resolution, categorized content-build/generated dependency registration, separate runtime XREF
registration, and scoped logging. It does not duplicate the importer's source-path getters or
expose runtime services.

Content-to-content build dependencies have manifest fingerprint semantics, but the serial CLI does
not schedule them yet. It refuses such a result instead of risking an incorrectly ordered build.
`BuildAsset`/`BuildAndLoadAsset` equivalents will not be added until graph identity, cycles,
ownership, and cache behavior are specified.

## Content Type Writers and CNB codecs

`ContentTypeWriter` accepts one stable processed type and returns complete CNB bytes plus the
stable CNB asset identity. It does not publish files and it does not parse source formats.

Every built-in writer is a small adapter:

```text
Texture2DContentWriter    -> EncodeTexture2DToCnb()
SoundEffectContentWriter -> EncodeSoundEffectToCnb()
SongContentWriter        -> EncodeSongToCnb()
ModelContentWriter       -> EncodeModelToCnb()
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

## Runtime reader mapping

CNA does not add a redundant `ContentTypeReader` hierarchy. The conceptual XNA reader role is
already fulfilled by:

```text
Decode*FromCnb() + CnbLoaderRegistry + ContentManager
```

Built-in CNB loaders remain unchanged. A custom asset registers its custom-range asset ID,
canonical type name, and loader through `ContentManager::RegisterCnbLoaderEXT<T>()`.

## Built-in routes

| Source | Importer | Imported type | Processor | Writer/output |
|---|---|---|---|---|
| `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga`, `.gif`, `.psd`, `.hdr`, `.pic`, `.pnm` | `CNA.ImageImporter/1` | `ImportedImage` | `CNA.TextureProcessor/1` | `CNA.Texture2DContentWriter/1` |
| `.wav` | `CNA.WavImporter/1` | `ImportedSound` | `CNA.SoundEffectProcessor/1` | `CNA.SoundEffectContentWriter/1` |
| `.mp3`, `.ogg`, `.oga`, `.qoa`, `.flac`, `.opus`, `.aac`, `.wma` | `CNA.SongImporter/1` | `ImportedSongSource` | `CNA.SongProcessor/1` | `CNA.SongContentWriter/1` |
| `.gltf`, `.glb` | `CNA.GltfImporter/1` | `ImportedModelDocument` | `CNA.ModelProcessor/1` | `CNA.ModelContentWriter/1` |
| `.cnj` Texture2D | `CNA.CnjImporter/1` | `ImportedImage` | same texture processor | same Texture2D writer |
| `.cnj` SoundEffect | `CNA.CnjImporter/1` | `ImportedSound` | same sound processor | same SoundEffect writer |
| `.cnj` Model | `CNA.CnjImporter/1` | `ImportedModelDocument` | same Model processor | same Model writer |
| `.cnj` SpriteFont | `CNA.CnjImporter/1` | `ImportedSpriteFont` | `CNA.SpriteFontProcessor/1` | `CNA.SpriteFontContentWriter/1` |
| `.cnj` Texture3D | `CNA.CnjImporter/1` | `ImportedTexture3D` | `CNA.Texture3DProcessor/1` | `CNA.Texture3DContentWriter/1` |
| `.cnj` TextureCube | `CNA.CnjImporter/1` | `ImportedTextureCube` | `CNA.TextureCubeProcessor/1` | `CNA.TextureCubeContentWriter/1` |
| `.cnj` Curve | `CNA.CnjImporter/1` | `ImportedCurve` | `CNA.CurveProcessor/1` | `CNA.CurveContentWriter/1` |
| `.cnj` AnimationClip | `CNA.CnjImporter/1` | `ImportedAnimationClip` | `CNA.AnimationClipProcessor/1` | `CNA.AnimationClipContentWriter/1` |

DDS is currently a contained TextureCube CNJ sidecar, not a direct default route. `.wav` remains
the unambiguous SoundEffect route; it is not also registered as Song. Video has a frozen CNB codec
and legacy producer but does not yet have a source importer in `cna-content`. Effect remains
intentionally outside this project until CNA's shader/FX architecture is settled.

### Streaming Song sources

`SongImporter` never decodes or buffers the audio payload. It validates that the primary source is
non-empty, retains its normalized root-relative path as the default stream reference, and relies on
the normal primary-source fingerprint for byte dependency tracking. `SongProcessor` produces only
`CnbSongData`, records the media path as a runtime reference with unconstrained asset type, and the
writer delegates to `EncodeSongToCnb()`. This keeps build dependencies and runtime XREFs separate
while preserving bounded compiler memory and HEADLESS operation.

Duration cannot be inferred without introducing a media decoder into the build tool, so it defaults
to zero (unknown). The optional display name defaults empty, which makes the runtime use the asset
name. Both can be authored per asset:

```json
{
  "parameters": {
    "name": { "type": "string", "value": "Main Theme" },
    "durationMs": { "type": "u64", "value": "185000" },
    "streamReference": { "type": "string", "value": "Music/theme.ogg" }
  }
}
```

The current one-output build publishes the `.cnb`, not a second copy of the streaming media. The
referenced media must be deployed at that content-root-relative path. The container XREF makes this
support artifact discoverable. Automatic copying is intentionally deferred until CP-023 defines
multi-output ownership, collision, failure-recovery, and manifest semantics; the compiler does not
pretend two files were atomically published when only one was.

## Registry and selection

Registries are explicitly constructed and owned. There is no hidden global component discovery or
static-initializer registration.

Default selection is a three-step route:

```text
lowercase source extension -> importer
actual imported stable type -> processor
processed stable type       -> writer
```

Each component also has a stable name for explicit selection. Duplicate stable names are rejected
at registration. Multiple components may declare the same route, but default resolution then fails
with a sorted ambiguity diagnostic; registration order and “last registered wins” never decide the
result. Explicit selection must name a registered component compatible with the route.

The registry should be fully configured before builds and then shared as immutable state. The
current coordinator is serial, but component contracts do not require mutable globals.

## Optional per-asset configuration

The convention-only command remains the default. Configuration exists for content-affecting
choices that cannot be inferred safely, including processor parameters and the metadata needed by
streaming media routes. The version-1 format is strict and deliberately contains no project-wide
build system:

```json
{
  "format": "CNA.ContentPipeline.Config",
  "version": 1,
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

Asset keys are normalized generic UTF-8 paths relative to the source root. Backslashes, absolute
paths, `..`, missing files, symlink escapes, and unsupported source extensions are rejected. A
logical-name override changes CNB metadata and, for directory builds, the relative output path; it
must obey the same safe CNB logical-name rules. A single-file build keeps its explicitly supplied
output path.

Importer, processor, and writer fields are optional stable component-name overrides. Unknown or
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

Build-time dependencies and runtime content references are different records:

```text
robot.gltf source/build inputs:
    robot.gltf
    robot.bin
    robot_albedo.png

robot.cnb runtime reference:
    Textures/robot_albedo
```

Build dependency categories are primary source, source file, content-build dependency, and
generated dependency. Runtime references carry a logical name and optional expected CNB asset type
ID. The two collections are separately sorted in `ContentBuildResult` and in the manifest.

Reading a source file does not automatically create an XREF. Registering an XREF does not claim
that the referenced runtime asset is enough to reproduce the build. For a custom type,
`AddRuntimeReference()` makes the reference observable to build tooling; the custom processed data
and authoritative codec must also encode the corresponding XREF. Built-in Model handling does both
through the existing canonical Model DTO and encoder.

## Build result, logging, and errors

`ContentPipeline::Build()` builds to memory and returns `ContentBuildResult` containing:

- canonical source and logical name;
- selected importer, processor, and writer identities;
- effective parameters;
- sorted categorized build dependencies;
- sorted runtime references;
- ordered info/warning messages;
- complete CNB bytes and stable output asset identity.

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
- every reported source/generated dependency's bytes;
- content-build dependency fingerprints when graph scheduling is available;
- importer, processor, and writer stable names and versions;
- typed processor parameters;
- CNB container version and written asset type ID.

The output's SHA-256 is stored separately. A missing or tampered `.cnb`, corrupt/incompatible
manifest, changed component version, changed parameter, or changed dependency forces a rebuild.
Identical effective inputs and intact output produce `SKIP`. Runtime XREF records are outputs rather
than independent inputs; the source/dependency/component inputs that produced them are fingerprinted.

Primary sources, file dependencies, generated-file dependencies, and existing output verification
are hashed in 1 MiB chunks. Hashing therefore uses bounded memory and accepts individual files above
2 GiB while producing the same SHA-256 and version-1 fingerprint semantics as the original one-shot
implementation. The ordinary test gate pins cross-chunk equivalence; an opt-in sparse 2 GiB+1-byte
test (`CNA_RUN_LARGE_FILE_TESTS=1`) pins the full large-file path without storing a giant fixture.

Effective configuration is already represented by the manifest's logical name, selected stable
component identities, and typed parameters. Changing one asset's effective configuration therefore
invalidates that asset without treating the entire configuration file as a shared byte dependency;
an unrelated entry change leaves other assets eligible for `SKIP`.

The manifest JSON layout is versioned internal build state, not a hand-edited project format. A
corrupt or incompatible manifest is ignored safely and rebuilt.

## Determinism and publication

Directory discovery is sorted by the UTF-8 logical name. Registries use ordered maps/sets and never
serialize RTTI. CNB bytes and fingerprints do not contain timestamps, absolute temporary paths,
random values, PIDs, pointers, memory addresses, or temporary file names.

Writers finish the complete CNB image in memory. The CLI then calls the one shared audited atomic
publisher from `tools/common/CnaToolAtomicWrite.hpp`. Each final artifact is all-or-nothing: a failed
import, process, encode, or publish operation leaves that asset's previous valid `.cnb` unchanged
and exposes no partial final output. Manifest publication uses the same helper. A directory build
is not a transaction across unrelated assets; independently successful assets may publish even if
another asset fails, while the old manifest remains and the next run repairs any mismatch.

## Paths and security

Every build has an explicit source root. Primary sources, sidecars, generated dependencies, glTF
external buffers/images, and CNJ references are containment checked. Absolute authored references,
`..` escapes, and canonical symlink escapes are rejected. There is no implicit outside-root opt-in.

On Windows, `cna-content` uses `wmain(int, wchar_t**)` and constructs native
`std::filesystem::path` values directly. On POSIX, argv bytes are passed to `std::filesystem`.
Logical names, manifest paths, dependency identities, and diagnostics use explicit generic UTF-8;
manifest reads explicitly convert UTF-8 back to a native path.

Native non-ASCII paths are covered for image, WAV, DDS, and non-Model CNJ paths. The current shared
glTF orchestration still narrows its input for cgltf, and the canonical Model CNJ compiler has a
legacy narrow-path boundary. Windows non-ASCII glTF and Model CNJ paths are therefore not currently
advertised. Fixing that boundary must preserve the pinned direct glTF/CNJ/CNB byte oracles and must
not introduce a second glTF parser.

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
the component version whenever content-affecting behavior changes. A custom writer should adapt to
one authoritative custom codec, just as built-ins adapt to `Encode*ToCnb()`; it should not duplicate
its schema in multiple front ends.

The stock `cna-content` binary currently registers built-ins only. A game using custom components
must currently provide a small custom build front end linked to the pipeline library. Dynamic
component loading and a declarative CLI extension mechanism are unresolved. The entire custom C++
component surface is marked by `ContentPipelineExtensionApiIsExperimental == true`; no source or
ABI compatibility promise is made yet.

## CNJ, CNB, and XNB

CNJ remains a supported authoring/intermediate front end. Direct image, WAV, and glTF inputs and
their equivalent CNJ inputs converge on the same processors and writers where semantics match.
CNB never depends on JSON internally.

CNB is the CNA-native compiled format produced here. XNB remains the compatibility format for XNA,
MonoGame, and FNA assets. The CNA Content Pipeline does not claim XNA binary compatibility and does
not replace CNA's XNB readers.

## Configuration, profiles, parallelism, and CMake

The initial command still works without a project file. The optional strict per-asset JSON format
described above supplies only proven selection/parameter/logical-name needs; it is not a
`.cnaproj`, `.contentproj`, profile system, or second build graph.

There is no platform ID or target profile in CNB v1 processing. Current schemas use their existing
portable representations. A future profile can be added only when a demonstrated policy needs it;
it must participate in fingerprints.

Build scheduling is serial. Registries are configured explicitly and components are intended to be
reentrant, leaving room for later parallel scheduling without committing to thread complexity now.

CMake can create a content target with the helper defined alongside the CNA tool:

```cmake
cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ContentSource"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Content"
)

add_dependencies(MyGame MyGameContent)
```

`SOURCE_DIR` is resolved relative to the caller's source directory and `OUTPUT_DIR` relative to its
binary directory. The custom target intentionally invokes `cna-content` whenever the target is
requested; the pipeline's byte-hashed manifest performs the correct per-asset no-op decisions. CMake
therefore does not duplicate source discovery, dependency hashing, cache logic, or publication.
`QUIET` forwards quiet output.

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

## Stability summary

**Stable/frozen:**

- CNB container 1.0, built-in schema-1 bytes, asset IDs, chunk IDs, CRC behavior, and existing
  typed encoders/decoders;
- the rule that built-in writers reuse those encoders;
- byte equivalence with legacy producers for matching implemented semantics;
- build/runtime separation, deterministic selection, categorized dependencies versus XREFs,
  content-hashed skips, logical path preservation, and per-artifact atomic publication.

**Experimental:**

- the public C++ importer/processor/writer interfaces and stable in-memory type strings;
- custom registration and custom component source/ABI compatibility;
- component names/versions as user configuration identifiers;
- future recursive builds, dynamic extensions, target profiles, and parallel scheduling.

**Internal/versioned implementation detail:**

- `ContentValue` type-erasure mechanics and process-local RTTI guard;
- the exact manifest JSON layout and cache implementation;
- glTF's temporary canonical CNJ staging representation;
- temporary-file naming used by atomic publication.

The engineering decisions, rejected alternatives, current risks, and CP task ledger are maintained
separately in [`plans/plan_content_pipeline.md`](../plans/plan_content_pipeline.md).
