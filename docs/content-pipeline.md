# CNA Content Pipeline

> Status: implemented and extended pipeline, updated 2026-08-30. The command-line build/clean
> workflows and built-in byte-compatibility guarantees described here are supported behavior. The
> custom C++ component API is explicitly experimental. CNB container 1.0 and the existing built-in
> CNB schemas remain frozen.

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

After a valid manifest has established ownership, all unchanged pipeline-owned compiled and
deployment files can be removed without scanning the output tree:

```bash
cna-content clean Content
```

Clean preserves manual files, source files, changed former outputs, directories, and anything not
proven by the manifest. The persistent `.cna-content.lock` coordination file remains so later build
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
WAV           -> ImportedSound -> CnbSoundEffectData
glTF          -> ImportedModelDocument -> ProcessedModelBundle
```

`ImportedImage` contains validated RGBA8 pixels and dimensions before texture policy. `ImportedSound`
preserves whether accepted PCM samples were unsigned 8-bit or signed little-endian 16-bit until the
processor performs the exact Pcm16 conversion. `ImportedModelDocument` is an owned lifetime seam
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
builds. `TextureProcessor` accepts the string parameter `colorKey` in `R,G,B` decimal form.
`SongProcessor` accepts `streamReference` and `name` strings plus a `durationMs` u64. `ModelProcessor`
accepts the opt-in boolean `generateChildAssets` for glTF only. Texture2D CNJ
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
| `.mp3`, `.ogg`, `.oga`, `.qoa`, `.flac`, `.opus`, `.aac`, `.wma` | `CNA.SongImporter/2` | `ImportedSongSource` | `CNA.SongProcessor/2` | `CNA.SongContentWriter/1` |
| `.mp4`, `.ogv`, `.webm`, `.mkv`, `.avi`, `.mov` | `CNA.VideoImporter/2` | `ImportedVideoSource` | `CNA.VideoProcessor/2` | `CNA.VideoContentWriter/1` |
| `.gltf`, `.glb` | `CNA.GltfImporter/2` | `ImportedModelDocument` | `CNA.ModelProcessor/3` | `CNA.ModelContentWriter/3` |
| `.cnj` Texture2D | `CNA.CnjImporter/1` | `ImportedImage` | same texture processor | same Texture2D writer |
| `.cnj` SoundEffect | `CNA.CnjImporter/1` | `ImportedSound` | same sound processor | same SoundEffect writer |
| `.cnj` Model | `CNA.CnjImporter/1` | `ImportedModelDocument` | same Model processor | same Model writer |
| `.cnj` SpriteFont | `CNA.CnjImporter/1` | `ImportedSpriteFont` | `CNA.SpriteFontProcessor/1` | `CNA.SpriteFontContentWriter/1` |
| `.cnj` Texture3D | `CNA.CnjImporter/1` | `ImportedTexture3D` | `CNA.Texture3DProcessor/1` | `CNA.Texture3DContentWriter/1` |
| `.cnj` TextureCube | `CNA.CnjImporter/1` | `ImportedTextureCube` | `CNA.TextureCubeProcessor/1` | `CNA.TextureCubeContentWriter/1` |
| `.cnj` Curve | `CNA.CnjImporter/1` | `ImportedCurve` | `CNA.CurveProcessor/1` | `CNA.CurveContentWriter/1` |
| `.cnj` AnimationClip | `CNA.CnjImporter/1` | `ImportedAnimationClip` | `CNA.AnimationClipProcessor/1` | `CNA.AnimationClipContentWriter/1` |
| `.xnb` supported built-in root | `CNA.XnbImporter/3` | existing imported type selected by validated root reader | existing type processor (plus metadata/deployment `CNA.XnbVideoProcessor/2`) | existing native built-in writer/codec; Model selects schema 1 or 2 exactly |

DDS is currently a contained TextureCube CNJ sidecar, not a direct default route. `.wav` remains
the unambiguous SoundEffect route; it is not also registered as Song. `.ogg` remains the
unambiguous Song route even though FNA's legacy Video reader can probe one; ordinary Video formats
use the non-colliding route above. Effect remains intentionally outside this project until CNA's
shader/FX architecture is settled.

### glTF scenes and generated children

The shared glTF interpretation imports exactly the declared default scene, or the first scene when
no default is declared; a document with no scenes imports its root nodes. Other scenes are not
silently turned into Models. Within the selected scene the canonical converter groups mesh
placements by skin, with one additional static group where applicable. One group is one Model.

The default pipeline policy remains one primary Model. A single-group animated glTF succeeds and
keeps every clip embedded in Model schema 1; the standalone authoring clip documents are not
published. This path is byte-identical to the established direct glTF producer. A multi-group file
is refused unless its asset config explicitly sets:

```json
{
  "parameters": {
    "generateChildAssets": { "type": "bool", "value": true }
  }
}
```

With that option, the lexicographically first canonical Model document becomes the primary logical
asset and the remaining groups become named Model children. Generated standalone clips become
AnimationClip children. Extracted external, buffer-view, and data-URI PNG/JPEG images are decoded
headlessly through the shared image front end, processed into native Texture2D data, and written as
Texture2D children. Their Model XREFs are remapped from temporary source-stem names to children
under the configured logical Model name. All child values still use the existing schema-1 typed
encoders; neither Model nor any other frozen schema changes.

Generated names must retain the converter's source-stem prefix and pass ordinary logical-name
containment. Colliding skin or animation names after filename sanitization are rejected before a
file can overwrite another. Cross-node/primary collisions are rejected by the existing output
reservation pass. The whole bundle remains one graph node: its children share one fingerprint,
staging transaction, manifest owner, rebuild decision, and garbage-collection lifetime. This mode
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

`SongImporter` and `VideoImporter` never decode or buffer the media payload. They validate that the
primary source is non-empty, retain its canonical source path and normalized root-relative stream
reference, and rely on the normal primary-source fingerprint for byte dependency tracking. Their
processors produce only `CnbSongData`/`CnbVideoData`, record the media path as a runtime reference
with unconstrained asset type, and register the same source/path pair as a deployment-support file.
Their writers still delegate only to the existing media encoders. This keeps build dependencies,
runtime XREFs, compiled CNB outputs and deployment artifacts distinct while preserving bounded
compiler memory and HEADLESS operation.

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

Build-time dependencies, runtime content references, and deployment outputs are different records:

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
ID. A source-file dependency records either the implicit primary root or an explicit stable root
alias plus root-relative identity; the machine-local physical mapping is not semantic identity.
Deployment files pair one byte-hashed source with one contained output-relative path. All three
collections are separately sorted in `ContentBuildResult` and in the manifest.

Reading a source file does not automatically create an XREF. Registering an XREF does not claim
that the referenced runtime asset is enough to reproduce the build. For a custom type,
`AddRuntimeReference()` makes the reference observable to build tooling; the custom processed data
and authoritative codec must also encode the corresponding XREF. Built-in Model handling does both
through the existing canonical Model DTO and encoder. `AddDeploymentFile()` is an explicit copy
request, not an inference from an XREF. An external source may be deployed only after that exact
file was recorded through an explicit aliased dependency resolution. Publication still targets
only the output root; clean, orphan GC, and staging scavenging never treat a source identity as a
deletion target.

## Build result, logging, and errors

`ContentPipeline::Build()` builds to memory and returns `ContentBuildResult` containing:

- canonical source and logical name;
- selected importer, processor, and writer identities;
- effective parameters;
- sorted categorized build dependencies;
- sorted runtime references;
- sorted non-CNB deployment files, bounded to 256 files;
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
- every selected writer asset ID, canonical type name, schema version, and codec name/version;
- typed processor parameters;
- CNB container version;
- every owned compiled-output logical identity/type and deployment source/destination identity.

Each compiled output's path, type ID, type name, schema version, and SHA-256 are stored separately.
Each deployment-support file stores its source-root alias, root-relative source, output path, and
SHA-256. A missing or tampered primary, child `.cnb`, or deployment file; corrupt/incompatible
manifest; changed output set; changed
component version; changed parameter; or changed dependency forces the owning node to rebuild.
Identical effective inputs and intact outputs produce `SKIP`. Runtime XREF records are outputs
rather than independent inputs; the source/dependency/component inputs that produced them are
fingerprinted.

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
Version 8 gives each entry a stable build-node ID, ordered compiled and deployment ownership lists,
explicit writer schema/codec declarations, a direct fingerprint, an effective graph fingerprint,
and a bounded `fingerprintState` decomposition. Writer declarations are exact
`(assetTypeId, assetTypeName, assetSchemaVersion)` tuples, so one writer can truthfully advertise
separately versioned schemas for the same runtime asset; each compiled output records the exact
schema it emitted. The decomposition stores canonical SHA-256 domains
for primary bytes; source-dependency identities and bytes; content-dependency identities and
effective fingerprints; typed parameters; writer schema/codec declarations; compiled-output and
runtime-XREF definitions; and deployment definitions. It stores no prose, timestamp, temporary
path, RTTI name, or absolute host path. Aliased source/deployment identities persist as separate
`sourceRoot` and root-relative path fields; physical directory mappings never enter the manifest
or CNB. Version 7 introduced the aliased source identities and current reason-state decomposition,
but cannot represent multiple schemas for one asset identity under its declaration rule. Versions
1 through 7 are therefore rejected as incompatible and cause a safe rebuild; there is no ambiguous
in-place migration. An incompatible/corrupt manifest grants no deletion authority, so its existing
outputs remain unless the new build replaces the same paths. A corrupt or future incompatible
manifest is handled the same way.

`build ... --explain` compares the current structured decision inputs with this persisted v8
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
as a fabricated source change. Manifest v7 and earlier produce one broad incompatible-format
reason on their first rebuild; subsequent v8 builds have the precise persisted domains.

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
from memory. Thus retained cold-build memory is bounded by the active worker count rather than the
number of discovered assets. Before final publication, staged size and SHA-256 are checked against
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

Every build has an explicit source root. Primary sources, sidecars, generated dependencies, glTF
external buffers/images, and CNJ references are containment checked. Absolute authored references,
`..` escapes, and canonical symlink escapes are rejected. There is no implicit outside-root opt-in.
The only outside-root read capability is a named `sourceRoots` mapping plus an authored
`@alias/path` dependency resolution. Canonical source/external/output roots cannot overlap, and an
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
reply outputs. This is MinGW compile/link plus Wine execution, not a native Windows or MSVC test;
those verification gaps remain explicit. Existing direct glTF producer and pinned Model-byte
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
| `Texture3DReader` | Texture3D | Color and DXT1/3/5, all declared levels, normalized to Rgba8 |
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

The final post-XNB checkpoint built the complete HEADLESS Debug configuration and reran the broad
compatibility boundary on the finished implementation:

- the passing boundary ran 1,535 tests: 1,527 passed and eight opt-in/external-fixture cases
  skipped. An unfiltered run separately reproduced exactly the three known HEADLESS runtime
  TextureCube/Texture3D storage failures; no pipeline regression was hidden in the exclusion;
- independent selections passed CNB 371/371, CNJ 137 with two fixture skips, XNB pipeline/runtime
  readers 221 with four HEADLESS skips, glTF/Model 608 with three external-fixture skips, and all 11
  frozen CNB golden vectors;
- a rebuilt combined ASan+UBSan configuration passed 1,537 tests with eight skips and no report;
  91 concurrency-relevant tests passed under TSan with only the large-file case skipped and no
  report;
- the opt-in sparse 2 GiB+1-byte streaming-hash test passed in 30.0 seconds without storing a
  repository fixture;
- both CMake integration fixtures and all nine generated C-API consistency gates pass. The
  inventory remains 549 headers and 9,332 symbols, with 501 experimental pipeline declarations
  planned under open `CBIND-117` and no Content Pipeline C ABI export;
- focused symbol inspection confirms the XNB audio decoder object references only SDL's in-memory
  WAVE load/conversion functions, not SDL initialization, device-open, graphics-device, window, or
  runtime ContentManager load paths. HEADLESS is an execution/ownership invariant, not a claim that
  every transitive shared library is absent from a monolithic link.

LeakSanitizer cannot run the subprocess-heavy selection in the current `ptrace` environment, so
the successful ASan+UBSan run used `detect_leaks=0` and is not leak evidence. Native Windows/MSVC
execution is still required to close the last platform-verification gap. Cycle preflight now uses
an explicit visit stack: a 4,096-node acyclic chain builds with four workers and the same graph
closed into a 4,096-node cycle retains deterministic diagnostics without replacing the prior
manifest.

Multi-file publication is recoverable, not a portable filesystem transaction. Prepared cold
outputs can temporarily consume disk space comparable to the compiled output set, and an abrupt
termination may leave an owner-only staging directory. The next build repairs digest/manifest
mismatches; CP-042 also scavenges valid abandoned version-1 staging trees conservatively. Legacy
or malformed/incompletely initialized trees are left for manual inspection. CP-043 collects only
obsolete files proven by a valid prior manifest and matching digest; it never performs an output
tree or extension scan.
Song and Video CNBs retain streaming XREFs, while the compiler now deploys the referenced media as
separately fingerprinted/owned support files without embedding it in CNB.

The representative scheduler benchmark again proved complete-tree equality between workers 1 and
4 over 128 mixed assets and a 97-node shared graph. A separate 1,024-output cleanup benchmark
preflighted and removed about 480 MiB of manifest-owned compiled data in 7.05 seconds while leaving
the persistent lease marker. These are host-specific measurements, not compatibility thresholds.
MinGW links both Unicode-entry-point compiler executables, and Wine 10 exercises stock/custom build
plus clean through a non-ASCII path. Native Windows and MSVC remain untested and unclaimed.

## Stability summary

**Stable/frozen:**

- CNB container 1.0, built-in schema-1 bytes, asset IDs, chunk IDs, CRC behavior, and existing
  typed encoders/decoders;
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
- the manifest-v8 persisted fingerprint-domain decomposition and stable aliased source identities
  used by incremental decisions;
- named, explicit, read-only external source-root capabilities;
- structured incremental decisions and the human-readable `build --explain` rendering;

**Future:**

- optional target profiles, if a concrete portable-output policy requires them;
- reconsider independently scheduled glTF children only if a materially larger measured workload
  justifies a generated-source graph contract and supplies stable per-child dependency partitions;
- stable machine-readable build-decision output, if an IDE/build integration contract justifies a
  separately versioned format;

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
binary cannot honor the lease.

The engineering decisions, rejected alternatives, current risks, and CP task ledger are maintained
separately in [`plans/plan_content_pipeline.md`](../plans/plan_content_pipeline.md).
