# CNA Content Pipeline

> Status: implemented and extended pipeline, updated 2026-08-29. The command-line build workflow and
> built-in byte-compatibility guarantees described here are supported behavior. The custom C++
> component API is explicitly experimental. CNB container 1.0 and the existing built-in CNB
> schemas remain frozen.

The CNA Content Pipeline is the build-time system that turns authoring files into CNB. It is
inspired by XNA 4.0's Importer -> Processor -> Writer separation, but it uses CNA-native C++23
components and the existing CNB codecs instead of CLR reflection or MSBuild. XNB reader tables are
consulted only at the compatibility-source boundary; they never enter native CNB.

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

A supported legacy built-in XNB is used the same way and produces native CNB, not wrapped XNB:

```bash
cna-content build ContentSource/Legacy/texture.xnb -o Content/Legacy/texture.cnb
```

Configuration remains optional. A directory or single-file build automatically reads
`.cna-content.json` from its source root when present; `--config <path>` selects another file
inside that root.

Build execution is serial by default. Independent graph nodes can be compiled concurrently with a
strict bounded worker count:

```bash
cna-content build ContentSource -o Content --workers 4
```

`--workers` accepts integers from 1 through 64. `--workers 1` is the explicit serial fallback and
has the same behavior as omitting the option. The worker count changes execution only; it is not
content identity and does not enter CNB bytes or manifest fingerprints.

Runtime loading remains the existing ContentManager API:

```cpp
auto robot = content.Load<Model>("Models/robot");
auto wall = content.Load<Texture2D>("Textures/wall");
auto ui = content.Load<SpriteFont>("Fonts/ui");
auto explosion = content.Load<SoundEffect>("Sounds/explosion");
```

The runtime needs the `.cnb` artifacts, not the authoring PNG, WAV, glTF, CNJ, XNB, or sidecar
files.

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
primary CNB + bounded named CNB child outputs
    |
    v
shared atomic publisher
    |
    v
one or more *.cnb artifacts

-------------------------- RUNTIME --------------------------

*.cnb -> typed CNB decoder + CnbLoaderRegistry -> ContentManager -> runtime object
```

Import and processing are separate even when the first processor is intentionally thin. An image
importer answers “what pixels are in this source?”; a texture processor answers “how should these
pixels become runtime Texture2D content?”; the writer answers “which authoritative CNB codec emits
the compiled representation?”

No pipeline component constructs a GraphicsDevice, opens an audio device, creates a window, reads
back GPU data, or initializes a renderer. XNB float/ADPCM SoundEffect conversion can call SDL3's
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
| XNB | a validated compatibility source for explicitly supported built-in roots; output is ordinary CNB |
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
objects are registry-owned and reusable; they must keep no unsynchronized per-build mutable state
because `--workers` may invoke the same frozen component concurrently.

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
can also author its existing color-key field. `VideoProcessor` requires u64 `width`/`height` and
f64 `framesPerSecond`; `streamReference`, u64 `durationMs`, and u64 `soundtrackType` are optional.

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

The pipeline still does not expose an XNA-style `BuildAsset`/`BuildAndLoadAsset` operation that
returns a dependency's runtime object during processing. Current processors declare the edge; the
orchestrator schedules and fingerprints it after the writer has produced the in-memory result.

## Content Type Writers and CNB codecs

`ContentTypeWriter` accepts one stable processed type and returns complete CNB bytes plus the
stable CNB asset identity. It does not publish files and it does not parse source formats.

Every built-in writer is a small adapter:

```text
Texture2DContentWriter    -> EncodeTexture2DToCnb()
SoundEffectContentWriter -> EncodeSoundEffectToCnb()
SongContentWriter        -> EncodeSongToCnb()
VideoContentWriter       -> EncodeVideoToCnb()
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
| `.mp4`, `.ogv`, `.webm`, `.mkv`, `.avi`, `.mov` | `CNA.VideoImporter/1` | `ImportedVideoSource` | `CNA.VideoProcessor/1` | `CNA.VideoContentWriter/1` |
| `.gltf`, `.glb` | `CNA.GltfImporter/1` | `ImportedModelDocument` | `CNA.ModelProcessor/1` | `CNA.ModelContentWriter/1` |
| `.cnj` Texture2D | `CNA.CnjImporter/1` | `ImportedImage` | same texture processor | same Texture2D writer |
| `.cnj` SoundEffect | `CNA.CnjImporter/1` | `ImportedSound` | same sound processor | same SoundEffect writer |
| `.cnj` Model | `CNA.CnjImporter/1` | `ImportedModelDocument` | same Model processor | same Model writer |
| `.cnj` SpriteFont | `CNA.CnjImporter/1` | `ImportedSpriteFont` | `CNA.SpriteFontProcessor/1` | `CNA.SpriteFontContentWriter/1` |
| `.cnj` Texture3D | `CNA.CnjImporter/1` | `ImportedTexture3D` | `CNA.Texture3DProcessor/1` | `CNA.Texture3DContentWriter/1` |
| `.cnj` TextureCube | `CNA.CnjImporter/1` | `ImportedTextureCube` | `CNA.TextureCubeProcessor/1` | `CNA.TextureCubeContentWriter/1` |
| `.cnj` Curve | `CNA.CnjImporter/1` | `ImportedCurve` | `CNA.CurveProcessor/1` | `CNA.CurveContentWriter/1` |
| `.cnj` AnimationClip | `CNA.CnjImporter/1` | `ImportedAnimationClip` | `CNA.AnimationClipProcessor/1` | `CNA.AnimationClipContentWriter/1` |
| `.xnb` supported built-in root | `CNA.XnbImporter/1` | existing imported type selected by validated root reader | existing type processor (plus metadata-only `CNA.XnbVideoProcessor/1`) | existing native built-in writer/codec |

DDS is currently a contained TextureCube CNJ sidecar, not a direct default route. `.wav` remains
the unambiguous SoundEffect route; it is not also registered as Song. `.ogg` remains the
unambiguous Song route even though FNA's legacy Video reader can probe one; ordinary Video formats
use the non-colliding route above. Effect remains intentionally outside this project until CNA's
shader/FX architecture is settled.

### Streaming Song and Video sources

`SongImporter` and `VideoImporter` never decode or buffer the media payload. They validate that the
primary source is non-empty, retain its normalized root-relative path as the default stream
reference, and rely on the normal primary-source fingerprint for byte dependency tracking. Their
processors produce only `CnbSongData`/`CnbVideoData`, record the media path as a runtime reference
with unconstrained asset type, and their writers delegate to the existing media encoders. This
keeps build dependencies and runtime XREFs separate while preserving bounded compiler memory and
HEADLESS operation.

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

Video frame dimensions and rate cannot safely default, so the route refuses to build until they are
configured. Duration remains zero when unknown, and soundtrack type defaults to `0` (Music):

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

Song and Video writers publish the metadata `.cnb`, not a second copy of the streaming media. The
referenced media must be deployed at that content-root-relative path. The container XREF makes this
support artifact discoverable. Multi-output writers deliberately return complete CNB artifacts;
raw deployment-file copying remains a separate policy question and is not silently inferred from
an XREF.

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

The registry must be fully configured before builds and is permanently frozen when a coordinator
accepts it. Lookups are safe for concurrent readers, and component contracts require reentrant
invocation when more than one worker is selected.

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
- complete primary CNB bytes and stable output asset identity;
- zero or more explicitly named additional CNB outputs, bounded to 256 outputs total.

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
- CNB container version;
- every owned output logical identity and written asset type ID.

Each output's path, type ID, and SHA-256 are stored separately. A missing or tampered primary or
child `.cnb`, corrupt/incompatible manifest, changed output set, changed component version, changed
parameter, or changed dependency forces the owning node to rebuild. Identical effective inputs and
intact outputs produce `SKIP`. Runtime XREF records are outputs rather than independent inputs; the
source/dependency/component inputs that produced them are fingerprinted.

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
Version 3 gives each entry a stable build-node ID, an ordered output ownership list, a direct
fingerprint, and an effective graph fingerprint. Versions 1 and 2 cannot represent all of those
relationships, so they are rejected as incompatible and cause a safe rebuild; there is no
ambiguous in-place migration. A corrupt or future incompatible manifest is handled the same way.

## Multi-output nodes and ownership

One primary source still defines one build node. Its stable node ID is the configured or
convention-derived primary logical name. A writer may additionally return complete CNB file images
with their own safe logical names and asset type IDs. The total is capped at 256 to bound manifest,
validation, ownership, and publication work.

The primary output keeps the CLI-selected path, including a custom path in a single-file build.
Each additional output maps deterministically to `<output-root>/<logical-name>.cnb`. Before any
artifact from a node is published, the CLI reserves all of its logical names and canonical paths.
It also pre-reserves every discovered primary node, so a generated child cannot shadow another
source asset. Duplicate names, duplicate paths, absolute names, traversal, and output-root escapes
are errors.

The manifest owns all output digests under the producing node. Tampering with any child rebuilds
that node rather than treating the child as an independent source node.

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

The registry is protected during configuration and permanently read-only once a coordinator
accepts it. A concurrency regression invokes sixteen independent `ContentPipeline::Build()` calls
through one frozen registry and verifies their distinct results. The built-in component audit found
no component-owned mutable state: contexts, dependency collectors, parser data and codec inputs are
per invocation. cgltf receives per-call options/data; the vendored stb decoder uses thread-local
failure/configuration state.

Filesystem staging is also reservation based. Model/glTF intermediates claim a directory with an
exclusive create, and the one atomic publication helper claims sibling temporary files with the
platform's exclusive-create primitive. Logical/path ownership already prevents two graph nodes
from targeting the same final artifact.

The CLI uses dependency-aware ready batches capped by `--workers`. Shared dependencies execute
once; dependent nodes become ready only after all of their content-build inputs succeed. A failed
dependency prevents dependent publication. `--workers 1` takes a synchronous path without
launching worker tasks.

Changed nodes first run a no-publication preparation pass so their complete output/edge topology is
known before cycle validation and scheduling. Prepared output bytes are placed in a private,
owner-only staging directory through the same atomic writer used everywhere else, then released
from memory. Thus retained cold-build memory is bounded by the active worker count rather than the
number of discovered assets. Before final publication, staged size and SHA-256 are checked against
the prepared manifest record.

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
serialize RTTI. CNB bytes and fingerprints do not contain timestamps, absolute temporary paths,
random values, PIDs, pointers, memory addresses, or temporary file names.

Writers finish every CNB image in memory. The CLI then calls the one shared audited atomic publisher
from `tools/common/CnaToolAtomicWrite.hpp` for the primary output followed by additional outputs.
Each individual final artifact is all-or-nothing and an old artifact is never removed before its
replacement is ready. Manifest publication uses the same helper and occurs only after every
requested node succeeds.

Portable filesystems do not provide a transaction spanning several paths. If a later output fails,
an earlier output may already contain its new complete bytes, but the previous manifest and any
unreplaced artifacts remain. The next invocation detects the old manifest's digest mismatch and
rebuilds the complete owning node. Thus the manifest never claims a partially published output set,
recovery is deterministic, and no partial file is exposed. A directory build likewise is not a
transaction across unrelated nodes. Removed or no-longer-produced child files are retained as
unclaimed stale artifacts; automatic garbage collection is intentionally not part of this protocol.

## Paths and security

Every build has an explicit source root. Primary sources, sidecars, generated dependencies, glTF
external buffers/images, and CNJ references are containment checked. Absolute authored references,
`..` escapes, and canonical symlink escapes are rejected. There is no implicit outside-root opt-in.

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
compiles and links the complete `cna_content` Windows-target static library, including the native
path and scheduler sources. Neither result is evidence of a native MSVC/Windows runtime execution;
that platform run remains an explicit verification gap rather than an advertised result. Existing
direct glTF producer and pinned Model-byte equivalence tests remain unchanged.

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
route, uses a typed `prefix` configuration parameter, and adapts its writer to the custom type's
single `EncodeGreetingToCnb()` codec. The writer exercises the real multi-output path by emitting a
primary greeting and a generated reply CNB through that same codec. A subprocess test compiles a
mixed custom/PNG directory, checks both custom CNBs and the built-in Texture2D output, verifies
manifest ownership and component/parameter identity, proves a byte-preserving no-op, repairs a
tampered child, rejects collision with another primary node, and covers recoverable partial
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
| `Texture3DReader` | Texture3D | Color and DXT1/3/5, all declared levels, normalized to Rgba8 |
| `TextureCubeReader` | TextureCube | Color and DXT1/3/5, six faces and all declared levels, normalized to Rgba8 |
| `CurveReader` | Curve | all loop/key/tangent/continuity fields |
| `SongReader` | Song | path/duration metadata plus contained external media dependency/XREF |
| `VideoReader` | Video | FNA String/Int32/Single object-reference graph plus contained external media dependency/XREF |

`ModelReader` is explicitly unsupported. Its real graph uses shared VertexBuffer, IndexBuffer and
BasicEffect resources and performs GPU construction; the current native Model schema cannot prove
lossless preservation of arbitrary effect/tag graphs. Every other custom/unknown root is also
rejected with its normalized reader identity. Shared-resource graphs and generic external object
references are not claimed merely because supported roots use fixed nested built-in readers.

None and LZX compression and XNB versions 4/5 are supported through CNA's existing container code.
LZ4 is recognized but CNA has no decoder, so it fails clearly. The existing 16 platform header
identifiers remain valid, but Xbox-swizzled texture/sample payloads are not transcoded without a
proven byte-order path. Frozen CNB schema 1 cannot preserve BGRA or NormalizedByte2/4 texture format
identity, so those formats are rejected rather than silently changed. XMA2 and unknown audio codecs
are likewise rejected.

For Song and Video, the external media stays external: it is a source-file dependency for
fingerprinting and a CNB XREF for deployment. Absolute paths, traversal and symlink escapes fail
through the ordinary source-root containment policy. The media bytes are not embedded or copied.

There is no `EmbeddedXnb`, `XNB0` chunk, opaque payload, reader/CLR name in output, second scheduler,
or alternate manifest. Unsupported XNB means a diagnostic and no published artifact.

## Configuration, profiles, parallelism, and CMake

The initial command still works without a project file. The optional strict per-asset JSON format
described above supplies only proven selection/parameter/logical-name needs; it is not a
`.cnaproj`, `.contentproj`, profile system, or second build graph.

There is no platform ID or target profile in CNB v1 processing. Current schemas use their existing
portable representations. A future profile can be added only when a demonstrated policy needs it;
it must participate in fingerprints.

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

`SOURCE_DIR` and optional `CONFIG_FILE` are resolved relative to the caller's source directory;
`OUTPUT_DIR` is relative to its binary directory. `CONFIG_FILE` must exist at configure time and is
passed to the CLI, whose canonical source-root containment check remains authoritative. `WORKERS`
is optional, strictly accepts 1 through 64, and defaults to the CLI-compatible serial value 1.

The custom target intentionally invokes `cna-content` whenever the target is requested; the
pipeline's byte-hashed manifest performs the correct per-asset no-op decisions. CMake therefore
does not duplicate configuration parsing, source discovery, dependency hashing, graph/cache logic,
or publication. `QUIET` forwards quiet output.

Projects that need the optional settings can add them without changing the convention-only flow:

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

The final extended-pipeline checkpoint built the complete HEADLESS Debug configuration and reran
the compatibility boundary on the finished implementation:

- 273 pipeline, configuration, manifest, graph, scheduler, custom-tool, source-route, producer,
  CNJ, golden-vector, and containment tests passed normally and again under combined ASan+UBSan;
- 107 concurrency-relevant tests passed under ThreadSanitizer with no report;
- the opt-in sparse 2 GiB+1-byte streaming-hash test passed without storing a giant fixture;
- the generated C-API coverage, compatibility, header, export, route, release, and ABI gates pass,
  and no Content Pipeline C ABI is exported;
- dynamic dependency and symbol inspection confirms `cna-content` does not initialize or depend on
  a renderer, window, graphics device, SDL/audio device, FFmpeg, or runtime ContentManager load.

LeakSanitizer cannot run the subprocess-heavy selection in the current `ptrace` environment, so
the successful ASan+UBSan run used `detect_leaks=0` and is not leak evidence. Native Windows/MSVC
execution is still required to close the last platform-verification gap. The graph's cycle
preflight uses recursive DFS; an extraordinarily deep acyclic graph may consume the process stack
even though build execution itself is iterative and worker-bounded.

Multi-file publication is recoverable, not a portable filesystem transaction. Prepared cold
outputs can temporarily consume disk space comparable to the compiled output set, and an abrupt
termination may leave an owner-only staging directory. The next build repairs digest/manifest
mismatches, but automatic staging cleanup and stale-output garbage collection are not implemented.
Song and Video CNBs retain streaming XREFs; deployment must place the media at those referenced
paths because the compiler does not copy raw media support files.

## Stability summary

**Stable/frozen:**

- CNB container 1.0, built-in schema-1 bytes, asset IDs, chunk IDs, CRC behavior, and existing
  typed encoders/decoders;
- the rule that built-in writers reuse those encoders;
- byte equivalence with legacy producers for matching implemented semantics;
- build/runtime separation, deterministic selection, categorized dependencies versus XREFs,
  content-hashed skips, logical path preservation, bounded output ownership, and per-artifact
  atomic publication.

**Experimental:**

- the public C++ importer/processor/writer interfaces and stable in-memory type strings;
- the C++ multi-output writer result and custom output generation surface;
- custom registration and the user-built `CNA::ContentCompiler` embedding surface;
- component names/versions as user configuration identifiers;
- content-build edges, recursive dependency builds, and bounded parallel scheduling.

**Future:**

- optional target profiles, if a concrete portable-output policy requires them;
- lossless Model XNB transcoding only after a canonical shared-resource/effect graph can be mapped
  to native Model without GPU construction or silent field loss.

**Not provided:**

- a stable dynamic plugin ABI or automatic shared-library discovery;
- binary compatibility for custom C++ compiler components across CNA/toolchain changes.

**Internal/versioned implementation detail:**

- `ContentValue` type-erasure mechanics and process-local RTTI guard;
- the exact manifest JSON layout and cache implementation;
- glTF's temporary canonical CNJ staging representation;
- temporary-file naming used by atomic publication.

The engineering decisions, rejected alternatives, current risks, and CP task ledger are maintained
separately in [`plans/plan_content_pipeline.md`](../plans/plan_content_pipeline.md).
