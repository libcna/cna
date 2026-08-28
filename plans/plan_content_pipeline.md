# plan_content_pipeline.md — CNA Content Pipeline

> **Status (2026-08-28):** `CP-001` through `CP-010` are complete. `CP-011` is current. The
> project starts from the existing `content-pipeline` branch at `0e6899f17017c03c0e23d575d25cd70c678e2781`.
> That commit contains the completed CNB baseline through `CNBF-123`. Local `next` was actually
> `4ab1859dc8a540af1bd326df0fa816579adf7027`, two unrelated platform/binding commits ahead; the
> verified merge base was the starting commit. The existing branch was preserved: no merge,
> reset, rewrite or push was performed.
>
> **Boundary:** this plan owns the build-time CNA Content Pipeline. `plans/plan_cnb.md` remains the
> engineering record for the frozen CNB compiled format. The pipeline consumes the existing CNB
> codec APIs; it does not reopen CNB design.
>
> **Stability labels:** CLI behavior and byte equivalence become stable only when their tasks say
> so. The custom C++ component API is **experimental** until the custom end-to-end task and at
> least two materially different built-in flows validate it. Internal type erasure, orchestration
> and manifest representation are internal implementation details.

---

## 1. Purpose and terminology

The CNA Content Pipeline is the build-time system above CNB:

```text
source asset
    |
    v
Content Importer
    |
    v
imported/source-oriented object
    |
    v
Content Processor
    |
    v
processed/runtime-oriented Cnb*Data
    |
    v
Content Type Writer
    |
    v
existing Encode*ToCnb()
    |
    v
CNB bytes -- atomic publication --> .cnb
    |
    v
typed CNB decoder + CnbLoaderRegistry + ContentManager
    |
    v
runtime asset
```

The terms are deliberately not interchangeable:

* **CNA Content Pipeline** — source import, processing, dependency collection, build orchestration,
  deterministic selection, incremental decisions and publication.
* **CNB** — CNA's native compiled runtime content format. Container 1.0 and the existing built-in
  schema-1 bytes remain frozen.
* **ContentManager** — the runtime loading boundary.
* **XNB** — XNA/MonoGame/FNA compatibility, not an implementation substrate for this pipeline.

The project is inspired by XNA's useful Importer -> Processor -> Writer separation. It is not an
XNA Content Pipeline binary-compatible implementation and will not reproduce XNB's CLR machinery.

---

## 2. Verified starting state (`CP-001`)

Repository checks run before the first edit:

```text
starting branch              content-pipeline
starting HEAD                0e6899f17017c03c0e23d575d25cd70c678e2781
local next                   4ab1859dc8a540af1bd326df0fa816579adf7027
merge-base branch..next      0e6899f17017c03c0e23d575d25cd70c678e2781
content-pipeline...next      0 left, 2 right
working tree                 clean
content-pipeline existed     yes (also origin/content-pipeline)
CNBF-123                     64f772d82, ancestor of starting HEAD
```

The two local-`next` commits beyond the branch were `PLAT-1` generated-plan maintenance and
`CBIND-115` release-gate version reporting. They are unrelated to CNB or this build layer. The task
forbids merging and asks not to disturb an existing content-pipeline branch unnecessarily, so the
branch stays at its verified CNB-complete existing base.

`plans/plan_cnb.md` section 16.2 confirms ten implemented/frozen asset schemas with runtime loaders
and producers: Texture2D, Texture3D, TextureCube, SpriteFont, Model, AnimationClip, Curve,
SoundEffect, Song and Video. Effect remains intentionally absent. `docs/cnb-format.md` is the
implemented byte specification; `misc/cnb.md` is historical rationale, not the specification.

No frozen definition or existing CNB byte is expected to change in this project.

---

## 3. XNA/MonoGame Content Pipeline audit (`CP-001`)

The local FNA tree supplies CNA's runtime/XNA behavioral reference but intentionally contains the
runtime reader side, not a build-time pipeline. The build-side concepts were therefore inspected in
the local MonoGame tree at `/rv/data/library/github.com/MonoGame/MonoGame`, whose public pipeline
surface follows XNA's model.

### 3.1 Concepts retained

| XNA concept | Problem it solves | CNA decision |
|---|---|---|
| `ContentImporter<T>` | Isolates parsing/source semantics from runtime transformation | Retain as an experimental C++ component contract with explicit stable identity, version, supported extensions and imported-type identity. |
| `ContentImporterContext` | Supplies scoped logging and dependency recording | Retain only source/logical identity, safe path resolution, source-dependency recording, build options and logging. No output/intermediate directories until a component genuinely needs them. |
| Intermediate object model | Lets several source formats converge before processing | Retain where source semantics differ from `Cnb*Data`; proven types now include image, sound, Model document, font, volume, cube, Curve and AnimationClip source semantics. Do not invent one class per XNA name. |
| `ContentProcessor<TInput,TOutput>` | Separates source parsing from content-affecting transformations and runtime preparation | Retain. A thin adapter is valid when current behavior has no richer transformation yet. |
| `ContentProcessorContext` | Supplies validated parameters, dependency/XREF reporting and scoped logging | Retain as a smaller, distinct context. Build recursion is omitted initially. |
| Processor parameters | Makes transformations configurable and fingerprintable | Retain as a bounded ordered value map, validated by the chosen processor before processing. No reflection over C++ properties. |
| `ContentTypeWriter<T>` | Separates processed data from the compiled representation | Retain conceptually, but every built-in writer delegates to an existing `Encode*ToCnb()` function. |
| `ContentTypeReader<T>` | Maps compiled data into a runtime type | Already satisfied by typed `Decode*FromCnb()`, `CnbLoaderRegistry` and `ContentManager`; do not add another runtime hierarchy. |
| `ContentIdentity` | Keeps diagnostics tied to a source and source fragment | Retain source filesystem identity and logical content identity as separate values. Add fragment identity only when a real multi-object importer needs it. |
| `ExternalReference<T>` | Distinguishes referenced runtime content from embedded content | Retain the distinction, using CNB XREF for runtime references and a separate dependency collector for source/build inputs. |
| `AddDependency` | Feeds incremental invalidation | Retain from the first vertical slice, with explicit categories rather than one undifferentiated filename list. |
| component version | Invalidates cached output when code semantics change | Retain as an explicit stable string controlled by CNA/component authors, never derived from an ABI or assembly version. |
| build event/cache | Records producer choices, parameters, dependencies and outputs | Retain conceptually as a deterministic, inspectable CNA manifest after dependency collection is proven. Hash bytes, not mtimes. |

### 3.2 Concepts explicitly rejected or deferred

* Assembly scanning, importer/writer attributes and reflection-driven discovery.
* Assembly-qualified type/reader names, CLR generic type strings and XNB reader tables.
* `ContentTypeReaderManager`-style reflection and per-file reader negotiation.
* XNB target-platform identifier bytes, XNB header/compression policy and shared-resource fixups.
* MSBuild `.contentproj` plumbing and XNA's XML project format.
* Reflection over writable processor properties to synthesize configuration.
* Automatic assembly-version component identities. CNA identities are deliberate and stable.
* `BuildAsset`, `BuildAndLoadAsset` and recursive content builds until graph identity, cycle handling,
  ownership, caching and diagnostics are specified. Runtime XREF registration does not require
  recursive compilation.
* XNA target platform/profile enums. A minimal future profile string remains possible but v1 CNB
  uses portable representations and needs no platform ID.
* Intermediate/output directory services in every context merely because XNA exposes them.

XNA's separation is retained because it prevents a parser, transformation policy and binary format
from becoming one inseparable operation. Its .NET machinery is rejected because CNB already has
stable asset IDs, schema versions, canonical custom names, XREF and typed codecs.

---

## 4. Current CNA producer architecture (`CP-002`)

### 4.1 Reusable library boundaries already present

| Concern | Actual implementation | Headless? | Pipeline role |
|---|---|---:|---|
| PNG/JPEG/etc. decode | `CNA::Internal::Graphics::ImageLoader::Load` | yes | shared source decoder used by `ImageImporter`; never duplicate it |
| image validation/keying -> texture DTO | `CnbSourceImport.cpp::ImportImageAsCnbTexture2D` | yes | currently collapses Importer+Processor; extract stages around the same decoder and preserve wrapper behavior |
| WAV parse/PCM normalization | `DecodeWavAsCnbSoundEffect` / `ImportWavAsCnbSoundEffect` | yes | currently collapses Importer+Processor; split data ownership without a second RIFF parser |
| DDS cube decode | `CNA::Internal::Graphics::DecodeDdsCube` | yes | shared decoder used by runtime and producer; future cube importer |
| glTF semantic core | `CNA::Internal::GltfImport::GltfImportCore` | yes | proven reusable parser/builder core |
| glTF orchestration | `tools/gltf_to_cnj/gltf_to_cnj.cpp::ConvertGltfToCnj` | yes | one implementation is now linked into `cna_content` and shared by both front ends; it remains physically tool-owned and writes CNJ staging sidecars |
| CNJ canonical reading | `CnjCanonicalRead`, `CnjEnvelope`, `ResolveCnjSourceFileSafely` | yes | reusable parsing/validation/containment pieces |
| CNJ compilation | `CompileCnjToCnb` | yes | all eight implemented CNJ asset types now use pure canonical readers/processors; Curve/AnimationClip no longer construct a build-time `ContentManager` |
| Model CNJ -> canonical DTO | `BuildCnbModelFromCnj` | yes | reusable model-processor input path, with explicit XREF result |
| typed wire encoding | ten `Encode*ToCnb()` functions | yes | authoritative writer backends; one source of schema bytes |
| typed wire decoding | ten `Decode*FromCnb()` functions | yes | runtime reader half and test oracle |
| runtime dispatch | `CnbLoaderRegistry` + `ContentManager::LoadCnbAsset` | runtime | conceptual XNA `ContentTypeReader`; stays unchanged |
| final publication | `tools/common/CnaToolAtomicWrite.hpp` | yes | one audited atomic helper used by all three producers; reuse, do not copy |

### 4.2 Actual per-source flows

```text
image
  ImageLoader::Load
  -> validate/key pixels + MakeRgba8Texture2DData (CnbSourceImport)
  -> EncodeTexture2DToCnb
  -> source_to_cnb main
  -> WriteFileAtomically

WAV
  CnbSourceImport's bounded RIFF parser
  -> CnbSoundEffectData (PCM16)
  -> EncodeSoundEffectToCnb
  -> source_to_cnb main
  -> WriteFileAtomically

DDS cube
  shared DecodeDdsCube
  -> CnbTextureData
  -> EncodeTextureCubeToCnb
  -> source_to_cnb main
  -> WriteFileAtomically

CNJ
  ParseCnjEnvelope + type-specific canonical readers/safe sidecar resolution
  -> runtime reader (Curve/AnimationClip), direct headless adapters (textures/font/audio),
     or BuildCnbModelFromCnj (Model)
  -> existing typed encoder
  -> cnj_to_cnb main
  -> WriteFileAtomically

glTF
  GltfImportCore + gltf_to_cnj orchestration
  -> temporary CNJ and sidecars
  -> CompileCnjToCnb
  -> gltf_to_cnb main
  -> WriteFileAtomically
```

The direct glTF tool intentionally stages through CNJ so it is byte-identical to the two-tool path;
this is an oracle to preserve while extracting an in-memory imported model later.

### 4.3 Dependencies and references today

* `CnjToCnbResult::absorbedFiles` records authored sidecar names but not canonical resolved source
  paths, and is the nearest existing source-dependency report.
* `CnjToCnbResult::externalReferences` and model/media codec data feed CNB XREF. These are runtime
  content references, not an incremental dependency database.
* glTF URI containment is centralized in `GltfImportCore`; CNJ sidecars use
  `ResolveCnjSourceFileSafely`; CNB logical XREF names use `CnbLogicalNameProblem`.
* `CollectExternalUriDependenciesEXT` now returns the same safely resolved buffer/image paths that
  the glTF containment sweep validates. `GltfImporter` records them as source dependencies;
  `ModelProcessor` reports the Model DTO's external references separately as runtime XREFs.
* The legacy source tools still have no build manifest. `cna-content` now provides sorted general
  content-root traversal and content-hashed dependency invalidation.

### 4.4 Duplication and runtime boundaries

The important duplication is orchestration in tool `main()` functions: extension mapping, option
validation, component choice, reporting and publication are not reusable. Source decoders and CNB
encoders are already reusable and must not be replaced.

Runtime-only code begins after typed CNB decode: texture upload needs `GraphicsDevice`, SoundEffect
construction needs the audio implementation, Song/Video resolve streaming paths, and Model builds
runtime buffers/effects. None belongs in the build pipeline. `CompileCnjToCnb`'s current
Curve/AnimationClip use of `ContentManager` is a known migration seam, not a precedent for new
pipeline components.

---

## 5. Chosen C++23 architecture (`CP-003`, implemented)

### 5.1 Namespace and physical ownership

The build layer lives under `CNA::Content::Pipeline` in the existing content physical module:

```text
modules/content/include/CNA/Content/Pipeline/
modules/content/src/Pipeline/
modules/content/tests/CNA/Content/Pipeline/
```

It is not XNA public API and therefore does not enter `Microsoft::Xna`. Keeping it in the content
module avoids manufacturing a physical-module dependency cycle while the layer is experimental.
The API can later become a separate CMake target if measured consumers need that link boundary.

### 5.2 Component and value model

The initial hybrid deliberately favors debugger clarity over template machinery:

* virtual `ContentImporter`, `ContentProcessor` and `ContentTypeWriter` component contracts;
* explicit `ContentComponentIdentity { name, version }` with stable UTF-8 names;
* explicit stable input/output type identity strings; implemented routes include imported image,
  sound and canonical glTF Model-document values plus their corresponding `Cnb*Data` outputs;
* an internal `ContentValue` carrying a shared immutable erased value, the stable type identity
  and an ephemeral `std::type_index` guard;
* small templated helpers only for checked boxing/unboxing at component implementation boundaries;
* no persistent RTTI names or `std::type_index` values.

RTTI/type erasure exists only in memory during one build to let a heterogeneous registry invoke
custom components. Stable strings select and fingerprint components. A value whose declared stable
identity matches but whose C++ type does not is rejected at the component boundary with a stage and
component diagnostic.

### 5.3 Ownership and lifetime

* A `ContentPipelineRegistry` owns components through `std::shared_ptr<const ...>` and is explicitly
  passed to orchestration. No hidden global and no static registration.
* Registration is mutable during setup. Builds use const registry access; later parallel scheduling
  can share an immutable configured registry.
* The coordinator owns per-build contexts and collectors on the stack. Component calls may not
  retain them or references returned from them.
* Components should be stateless/reentrant. Stateful custom components must synchronize their own
  state and must not retain a transient context.
* Imported/processed values are moved between stages and destroyed after the build result/bytes no
  longer require them.
* `ImportedModelDocument` owns an opaque shared staging lifetime. Its generated canonical CNJ and
  absorbed sidecars remain valid through `ModelProcessor`, then the temporary tree is removed;
  neither component may retain its paths after that invocation.

### 5.4 Focused contexts

`ContentImporterContext` contains only:

* canonical source root, primary source path and logical asset name;
* safe contained dependency resolution;
* source-dependency registration;
* scoped logger access.

`ContentProcessorContext` contains only:

* logical asset name;
* validated processor parameter view;
* source/content-build/generated dependency registration when relevant;
* runtime XREF registration as a separate operation;
* scoped logger access.

Both contexts are non-copyable stack objects valid only for one component call. Neither exposes a
renderer, GraphicsDevice, window, audio device or ContentManager.

### 5.5 Parameters and build options

Processor parameters use an ordered `std::map<std::string, variant<...>>` with a deliberately small
value set: boolean, signed/unsigned integer, finite double and UTF-8 string. Each processor rejects
unknown names, wrong types and invalid values before transformation. Canonical key order and typed
canonical value encoding participate in fingerprints. There is no property reflection.

No global `ContentBuildOptions` type exists yet because there is no proven cross-cutting option.
Target/profile is deferred; adding an unused enum would imply platform-specific output that CNB v1
does not have.

### 5.6 Deterministic registry and selection

All lookup candidates are stored/diagnosed in stable name order. Registration order never selects
a winner.

* importer: lowercase source extension -> candidates; explicit importer name may disambiguate;
* processor: imported stable type -> candidates; an explicit stable component name can override
  default selection;
* writer: processed stable type -> candidates;
* duplicate component identity is rejected at registration;
* a default route with more than one candidate is an ambiguity error naming every candidate;
* unknown routes report source extension/type and the stage;
* there is no "last registered wins" behavior.

Extension matching is only routing. The selected importer must validate bytes/content.

### 5.7 Error and logging model

Use ordinary typed exceptions internally while preserving their original `what()` text. The build
coordinator catches only at the outer stage boundary and throws a `ContentPipelineError` that
adds source, logical asset, stage and component without discarding the nested cause. A small scoped
logger interface carries the same context and supports info/warning/error; tests may supply a
collector. No process-global logger is introduced.

### 5.8 Dependency categories and build result

Dependencies are explicit records, not inferred from XREF:

* primary source;
* source file read;
* content/build dependency;
* generated dependency;
* runtime content reference (XREF).

Paths are canonical native paths internally; manifest paths will be normalized relative to the
declared root. Runtime references remain logical forward-slash names. `ContentBuildResult` now
records source, logical name, output, component identities, parameters, categorized
dependencies/references, the ordered info/warning messages emitted by successful stages, and a
built/skipped state. `CP-008` maps successful results into the separate persistent manifest record
that owns fingerprint and output-digest fields.

The coordinator places the primary source in the dependency set before importing. Context methods
are the only component-facing way to add contained source, content-build, generated, and runtime
reference records. A per-build recording logger forwards each message to the caller's optional sink
while retaining the same contextual message in the successful result. Image/WAV importers and
Texture/SoundEffect processors now emit concrete stage messages; tests assert both the stage and
stable component identities. This makes built-in flows observable without a global logger and keeps
runtime XREFs absent from the build-dependency list.

### 5.9 Writer and runtime mapping

Built-in writers are adapters only:

```text
Texture2DContentWriter(CnbTextureData) -> EncodeTexture2DToCnb()
SoundEffectContentWriter(CnbSoundEffectData) -> EncodeSoundEffectToCnb()
ModelContentWriter(CnbModelData) -> EncodeModelToCnb()
Texture3DContentWriter(CnbTextureData) -> EncodeTexture3DToCnb()
TextureCubeContentWriter(CnbTextureData) -> EncodeTextureCubeToCnb()
SpriteFontContentWriter(CnbSpriteFontData) -> EncodeSpriteFontToCnb()
CurveContentWriter(ProcessedCurve) -> EncodeCurveToCnb()
AnimationClipContentWriter(ProcessedAnimationClip) -> EncodeAnimationClipToCnb()
```

They do not parse source files, construct chunks or reproduce schema field order. CNB's typed encoder
remains the only wire implementation. Writers produce bytes; orchestration owns final publication.

The runtime mapping is already complete:

```text
XNA ContentTypeReader concept
    ~= CNA Decode*FromCnb + CnbLoaderRegistry + ContentManager runtime construction
```

No new runtime reader hierarchy is planned.

---

## 6. First vertical slices

### 6.1 Texture2D (`CP-004`)

```text
PNG/JPEG/etc.
 -> ImageImporter
 -> ImportedImage { width, height, Rgba8 pixels }
 -> TextureProcessor { optional colorKey }
 -> CnbTextureData
 -> Texture2DContentWriter
 -> existing EncodeTexture2DToCnb
 -> bytes
 -> existing WriteFileAtomically
 -> .cnb
```

The existing source tool is the byte oracle. Its implementation is not changed until the new path
is proven equivalent for default and color-key options. Tests compare bytes, decode the output, and
exercise the existing ContentManager runtime path where a headless-compatible test fixture permits.

Implemented identities are `CNA.ImageImporter/1`, `CNA.TextureProcessor/1` and
`CNA.Texture2DContentWriter/1`. `ImageImporter` calls the existing shared `ImageLoader` and emits
only source semantics. `TextureProcessor` owns the optional, strictly validated string parameter
`colorKey=R,G,B` and constructs `CnbTextureData`. The writer contains no schema logic: it calls
`EncodeTexture2DToCnb()` and reports the frozen Texture2D asset ID/name.

Verification on the HEADLESS Debug build passed five vertical-slice tests plus the ten core tests.
Both default and color-key output matched the unchanged `cna_tool_source_to_cnb` subprocess and its
library path byte-for-byte; two repeated pipeline builds were identical; typed decode preserved
every pixel; and `ContentManager::Load<Texture2D>("Textures/wall")` loaded the resulting bytes.
The relevant original source-tool, producer, texture-codec and all CNB golden-vector tests also
passed (41 focused regression cases total in that run). The old producer implementation remains
untouched as an oracle; sharing the new components from old front ends is a later safe migration.

### 6.2 SoundEffect (`CP-005`)

```text
WAV
 -> WavImporter (the existing bounded RIFF parser)
 -> ImportedSound
 -> SoundEffectProcessor
 -> CnbSoundEffectData
 -> SoundEffectContentWriter
 -> existing EncodeSoundEffectToCnb
```

The split must move/reuse the parser, not copy it. Existing WAV parser tests remain the malformed
input oracle. New tests compare old and pipeline bytes.

Implemented identities are `CNA.WavImporter/1`, `CNA.SoundEffectProcessor/1` and
`CNA.SoundEffectContentWriter/1`. The existing bounded RIFF parser now returns experimental
source-oriented `ImportedSound`, preserving accepted unsigned 8-bit versus signed 16-bit PCM.
`SoundEffectProcessor` owns exact unsigned-8-to-signed-16 widening and constructs
`CnbSoundEffectData`; the compatibility `DecodeWavAsCnbSoundEffect()` wrapper composes the same
parser and processing helper. The writer calls only `EncodeSoundEffectToCnb()`.

This extraction was pinned against the pre-refactor `cna_tool_source_to_cnb` binary using the real
`tone.wav` example: both full files matched, with SHA-256
`bd5b30f756e661b9f1f7dddb623922c15213fb0492f233c5b1096b23c71da8ff`. Six new tests prove the
stage split, exact 8-bit widening, malformed imported-data validation, component/error context,
repeated determinism, typed decode and byte equality with the producer library/executable paths.
The complete original WAV producer, source tool, SoundEffect codec/runtime-load and CNB golden
regression selection passed (66 focused cases). The build path opens no audio device; runtime
loading remains the existing typed decoder/loader responsibility.

### 6.3 Model/glTF (`CP-009`)

```text
.gltf/.glb
 -> GltfImporter (shared GltfImportCore + existing glTF-to-CNJ orchestration)
 -> ImportedModelDocument { canonical CNJ, absorbed sidecars, owned staging lifetime }
 -> ModelProcessor
 -> existing BuildCnbModelFromCnj
 -> CnbModelData
 -> ModelContentWriter
 -> existing EncodeModelToCnb
```

Implemented identities are `CNA.GltfImporter/1`, `CNA.ModelProcessor/1` and
`CNA.ModelContentWriter/1`. The equivalence-hardened converter is compiled once into
`cna_content`; `cna-content` and `cna_tool_gltf_to_cnb` link that same implementation. The
standalone CNJ tool retains its CLI/oracle entry point. No new cgltf interpretation, CNJ reader or
Model serializer was introduced.

The importer returns a source-oriented canonical Model document rather than pretending that the
wire-oriented `CnbModelData` is an import result. The processor is the only stage that invokes
`BuildCnbModelFromCnj`, registers the returned runtime references, and produces `CnbModelData`.
External glTF buffers/images are collected through the exact same URI resolver that enforces
directory and symlink containment, then recorded as source dependencies through
`ContentImporterContext`. Generated staging paths never enter output bytes or fingerprints.

The output for `skin-four-weighted.gltf` was pinned before refactoring and remained byte-identical
afterward: SHA-256
`9f432dff5a02ee2092ffc4c04e72e91505d1365ebec6274b15cf6dbba7d0276b`. Tests also prove pipeline
bytes equal the legacy direct producer, direct glTF -> CNB still equals glTF -> CNJ -> CNB, repeated
builds are deterministic, typed Model decode succeeds, and the resulting CNB loads through the
existing `ContentManager` Model path in the HEADLESS configuration. The final focused run passed
73 pipeline, direct-tool, containment and CNB golden-vector cases.

Two multi-output issues are deliberately not hidden. A glTF producing several Model documents is
rejected with an explicit graph-scheduling diagnostic by the one-output `ContentPipeline::Build`
API. Textured glTF currently preserves the legacy generated texture XREF names; producing those
textures as child `.cnb` artifacts needs the content-build graph decision recorded in section 14.
The dependency/XREF distinction is nevertheless real and tested now, rather than inferred later.

### 6.4 CNJ convergence (`CP-010`)

`.cnj` is one importer route whose validated envelope selects one of a bounded, declared set of
stable intermediate identities. This required replacing an importer's former singular
`OutputType()` declaration with `OutputTypes()`: the registry rejects empty or duplicate sets, and
the coordinator verifies that the actual imported value belongs to the declaration. The set is
persistent CNA identity, not C++ RTTI; `std::type_index` remains only the process-local checked-cast
guard inside `ContentValue`.

All eight CNJ types supported by the established compiler now converge on real processor/writer
stages:

```text
Texture2D       -> ImportedImage         -> TextureProcessor       -> existing encoder
SoundEffect     -> ImportedSound         -> SoundEffectProcessor   -> existing encoder
Model           -> ImportedModelDocument -> ModelProcessor         -> existing encoder
SpriteFont      -> ImportedSpriteFont    -> SpriteFontProcessor    -> existing encoder
Texture3D       -> ImportedTexture3D      -> Texture3DProcessor     -> existing encoder
TextureCube     -> ImportedTextureCube    -> TextureCubeProcessor   -> existing encoder
Curve           -> ImportedCurve         -> CurveProcessor         -> existing encoder
AnimationClip   -> ImportedAnimationClip -> AnimationClipProcessor -> existing encoder
```

Image, WAV and DDS data use their existing shared decoders. Model uses the existing canonical CNJ
builder. Curve and AnimationClip parsing, including `.clip.bin`, was extracted once into
`CnjCanonicalRead`; both runtime `ContentManager` readers, `CompileCnjToCnb`, and the pipeline call
that same implementation. Consequently the old compiler no longer initializes `ContentManager`
for build-time compilation. CNJ sidecars are resolved through the existing containment helper and
the focused importer context, and become explicit source dependencies; Model runtime XREFs remain
separate.

Nine CNJ pipeline tests prove exact bytes against `CompileCnjToCnb` for every supported type,
including both inline and sidecar AnimationClip forms, typed decoding, selected component
identities and dependency collection. Runtime Curve/AnimationClip regressions, the compiler suite,
the 11 frozen golden vectors and the CLI/incremental suite also pass. The final broad CNJ/pipeline/
compiler/golden selection passed 241 of 243 cases; the two environment-gated large glTF fixtures
were skipped. A real CLI
directory test compiles a SpriteFont CNJ while treating its atlas as a dependency rather than a
separate unsupported source artifact.

---

## 7. CLI and publication design

`cna-content build <source-or-directory> -o <output>` is the intended user surface. Initial source
extensions are exactly those the registered built-ins advertise. An explicitly requested unsupported
file produces a useful diagnostic; convention-based directory discovery ignores unregistered support
files such as glTF `.bin` buffers. Directory discovery is sorted by normalized relative generic path
and produces `<relative stem>.cnb` while retaining the logical relative name.

Final publication remains all-or-nothing through the single implementation in
`tools/common/CnaToolAtomicWrite.hpp`. The first CLI can own orchestration at the tool boundary and
reuse that header directly. If library-level publication becomes necessary, the implementation will
move once to a shared module and the old header will forward to it; it will not be copied.

Directory creation and manifest update must not make a partial `.cnb` visible. A failed rebuild of
an existing artifact must leave its old bytes untouched and remove its sibling temporary.

Implemented as the `cna_content_tool` CMake target with output name `cna-content`:

```text
cna-content build <source-file> -o <artifact.cnb>
cna-content build <source-directory> -o <output-directory>
```

The configured registry contains the completed image/Texture2D, WAV/SoundEffect, glTF/Model and
all eight supported CNJ envelope routes. Single-file builds use the source stem as the logical name. Directory builds
enumerate registered source files, derive logical names and output paths from extensionless relative
paths, and sort by the UTF-8 generic logical name before building. The output root is
forbidden inside the source root so generated artifacts cannot become new source discoveries; a
single build cannot overwrite its source. Every artifact is fully imported/processed/encoded before
its parent directory is created and its bytes are handed to the one existing atomic helper.

Twelve real-process CLI tests now cover pipeline-byte equality, nested output creation, sorted
multi-asset directory compilation, logical path preservation, support-file filtering, glTF image
dependency invalidation, repeated no-op skips, unknown explicit extensions, destructive-layout
rejection, corrupt/tampered cache recovery, and old-output preservation/no temporary debris after a
failed rebuild.

### Windows pathname strategy

The new CLI will use `wmain(int, wchar_t**)` on Windows and construct `std::filesystem::path` from
wide arguments. POSIX uses `main(int, char**)`, treating argv as the locale-independent byte spelling
accepted by `std::filesystem`. Logical content names are UTF-8 with `/` separators and are converted
explicitly at the native/logical boundary; native paths are never serialized as logical names.
Narrow `main` on Windows is rejected for the new CLI because it cannot represent every filesystem
path. Old tools are not refactored as part of this decision.

The CLI entry point and logical-name conversion now implement that decision. End-to-end Windows
non-ASCII source paths remain `CP-012`: the current shared image/WAV importer APIs still accept
`std::string` paths, so the build cannot claim complete Windows Unicode support until that lower
boundary accepts native `std::filesystem::path` (or an explicitly audited UTF-8 conversion).

---

## 8. Path and security model

Every build has an explicit source root. Primary sources and dependencies are weakly canonicalized
and must remain within that root component-by-component. Absolute authored references and `..`
escapes are rejected. Symlink resolution is part of containment, so a symlink inside the root to a
file outside is an escape and is rejected. An outside-root dependency needs a future explicit opt-in;
none is silently allowed in v1.

Importers resolve dependency references through `ContentImporterContext`, or through an already
audited shared resolver (`ResolveCnjSourceFileSafely`, glTF URI resolver) whose result is checked and
recorded. Runtime XREFs separately pass CNB's logical-name validation. Temporary/output paths never
enter a content fingerprint.

---

## 9. Incremental manifest/cache (`CP-008`, implemented)

The first manifest is the deterministic, versioned, inspectable
`.cna-content-manifest.json` under the output root. It records one entry per logical asset, sorted by
logical name. CNA reuses the existing SharpRuntime SHA-256 implementation rather than adding a
second digest implementation. The effective fingerprint covers canonical length-prefixed fields:

* primary source bytes;
* every recorded source-dependency path relative to root and its bytes, sorted by category/path;
* content/build dependency identities and their supplied effective fingerprints;
* importer, processor and writer stable names and versions;
* canonical processor parameters;
* logical content identity where it affects CMET/output bytes;
* CNB container major/minor identity, asset type ID, and selected writer identity/version (the
  built-in writer build version owns schema/codec behavior);
* manifest format version.

No timestamp, RTTI name, absolute path, temporary path, process ID or native path separator enters
the fingerprint. Source and generated paths are root-relative generic paths. Processor values are
encoded with explicit `bool`/`i64`/`u64`/`f64`/`string` tags, so neither JSON number precision nor a
C++ variant index becomes persistent identity. Missing/corrupt/incompatible manifest data causes a
safe rebuild. The output's own SHA-256 is checked so deletion or tampering never yields a false
skip. CNB and manifest files are both published through the same audited atomic helper; an unchanged
manifest is not rewritten.

The CLI resolves the current importer -> processor -> writer route without invoking it, compares
stable identities and parameters, recomputes file/dependency hashes from the previous record, and
only then emits `[SKIP]`. A directory run builds a fresh next-manifest in deterministic discovery
order and publishes it only if every asset succeeded. If an earlier artifact was updated before a
later failure, the old manifest remains; its output hash then safely forces a rebuild on the next
run.

Content-build dependency fingerprints are represented and unit-tested, including invalidation when
the referenced effective fingerprint changes. The current serial CLI deliberately refuses to cache
or publish a component that declares such a dependency until graph scheduling/cycle handling is
implemented; it never substitutes a stale fingerprint. File dependency invalidation is fully live.

Verification covers the known SHA-256 `abc` vector, deterministic parse/serialize round trips, all
five parameter types, source/dependency byte changes, mtime-only changes, component version,
parameter, logical-name, asset-type and referenced-build invalidation, traversal/symlink rejection,
identical no-op builds, one-of-two independent rebuilds, output deletion/tampering, corrupt-manifest
recovery and failed-build preservation. The combined pipeline/CLI/golden selection passed all 47
cases after a successful fresh HEADLESS build.

Scheduling remains serial initially. Registries become read-only during builds and components are
expected to be reentrant so later parallel scheduling does not require an API rewrite.

---

## 10. Configuration, profile and CMake decisions

* No `.cnaproj` or `.contentproj` in the initial implementation.
* Directory conventions and command-line defaults come first.
* Per-asset configuration is deferred until at least one real parameter beyond Texture2D color key
  requires it; then the smallest format for importer/processor override, parameters and logical name
  will be designed from implemented semantics.
* No platform-specific CNB variants or platform IDs. A future profile remains a stable string only
  after a processor has demonstrated the need.
* CMake integration is deferred until the CLI and incremental dependency behavior are stable. It
  must call the same CLI/library rather than duplicate pipeline logic in CMake script.

---

## 11. Test and verification matrix

Required before the corresponding task closes:

* registry selection, explicit override, duplicate identity, ambiguous route and missing route;
* bad value/type crossing a type-erased boundary;
* invalid/unknown processor parameter;
* primary/source/build/generated dependency categories distinct from runtime XREF;
* source-root, `..`, absolute and symlink escape rejection;
* PNG and WAV old-vs-new byte equivalence, repeated determinism and typed decode;
* final artifact survives decode/process/write/publication failures unchanged;
* directory order and logical path preservation;
* custom importer, processor, writer and runtime loader end to end before the extension API can
  leave experimental status;
* incremental first build/no-op/independent source/shared dependency/parameter/component-version
  invalidation;
* glTF direct-vs-CNJ equivalence before and after integration;
* all existing CNB golden vectors unchanged;
* build and relevant tests under the normal debug configuration;
* ASan and UBSan over the affected pipeline/tool tests. TSan only when shared mutable registries or
  parallel scheduling actually exist.

---

## 12. Task ledger

| ID | State | Scope / exit criterion |
|---|---|---|
| `CP-001` | **completed** | Verify branch/CNB baseline; audit XNA/MonoGame importer, processor, writer/reader, contexts, identities, dependencies, external references and build cache concepts; record retained/rejected concepts. |
| `CP-002` | **completed** | Trace real CNA image/WAV/DDS/glTF/CNJ imports, canonical DTO construction, encoders, runtime loaders, path rules, XREFs, tool publication and duplication; record the build/runtime map. |
| `CP-003` | **completed** | Implemented experimental component identities, checked type-erased values, focused importer/processor contexts, categorized dependency and separate runtime-reference collectors, scoped logging, component contracts, a serial build-to-bytes coordinator, and an explicit deterministic registry. Ten focused tests prove duplicate/ambiguous/missing route diagnostics, explicit selection, parameter errors, persistent-type/RTTI separation, dependency/XREF reporting, traversal and symlink containment, and the complete abstract stage flow. `cna_content` and `CnaTests` built in a fresh HEADLESS Debug configuration; all 10 `ContentPipelineCoreTest` cases passed. |
| `CP-004` | **completed** | Added `ImportedImage`, `ImageImporter`, `TextureProcessor`, and `Texture2DContentWriter`; the writer calls the existing encoder. Five slice tests prove strict parameter validation, stage identities/dependencies, repeated determinism, typed decode, runtime loading, and default/color-key byte equality against both the unchanged producer library path and real `cna_tool_source_to_cnb` subprocess. The focused original producer/tool/codec and all golden-vector regressions passed. |
| `CP-005` | **completed** | Split the existing bounded RIFF parser to source-oriented `ImportedSound`, shared its one exact PCM processing helper with the compatibility API, and added `WavImporter`, `SoundEffectProcessor`, and a writer that calls the existing encoder. Six new tests and 66 focused old/new regressions pass; pre/post-refactor real-tool bytes also match exactly. No build component initializes audio. |
| `CP-006` | **completed** | Added the `cna-content` executable with single/directory builds, deterministic discovery, relative logical/output preservation, explicit built-in selection diagnostics, wide Windows entry point, safe output layouts and the shared audited atomic publisher. Six subprocess tests prove the CLI contract and failed-rebuild preservation. |
| `CP-007` | **completed** | Completed the observable build result: categorized build dependencies and separate runtime XREFs are returned with ordered contextual messages. The coordinator records while forwarding to an optional scoped logger; image/WAV importers and Texture/SoundEffect processors expose their actual stages. The fresh HEADLESS Debug targets built and the 38-case combined pipeline/CLI/golden selection passed. |
| `CP-008` | **completed** | Added the version-1 inspectable JSON manifest, canonical SHA-256 effective-input fingerprints, output-integrity hashes, safe corruption fallback and atomic manifest publication. Nine real CLI tests and six manifest tests prove no-op skip, independent rebuilds, byte-not-mtime invalidation, component/parameter/dependency identities, output repair and path containment. Content-build dependency fingerprints are modeled but serial graph scheduling remains intentionally disabled until cycle rules exist. |
| `CP-009` | **completed** | Linked the one existing glTF-to-CNJ implementation behind both front ends; added `ImportedModelDocument`, `GltfImporter`, `ModelProcessor` over `BuildCnbModelFromCnj`, and a writer over `EncodeModelToCnb`. External buffers/images are explicit source dependencies, Model XREFs remain separate, the manifest invalidates on dependency bytes, runtime loading succeeds, direct/two-step/pipeline bytes remain equal, and all focused/golden tests pass. Multi-Model and generated-texture child outputs remain explicit build-graph work rather than silent partial behavior. |
| `CP-010` | **completed** | Added a bounded multi-output `CnjImporter` and integrated all eight compiler-supported CNJ types with explicit intermediate values, processors and writer adapters over the existing codecs. Image/WAV/DDS/Model implementations are reused; canonical Curve/AnimationClip readers are now shared by runtime and both build paths, eliminating the build-time `ContentManager` shortcut. Sidecars are contained source dependencies, XREF stays separate, every type is byte-equivalent to the old compiler, and the final broad selection passed 241/243 tests (two environment-gated large glTF fixtures skipped). |
| `CP-011` | **current** | Add realistic custom importer/processor/writer plus custom runtime loader end-to-end example/test; review experimental API. |
| `CP-012` | future | Implement/audit Windows wide argv and non-ASCII pathname tests; document logical/native conversion. |
| `CP-013` | future | Add `docs/content-pipeline.md`, stable/experimental/internal labels, compatibility/migration guidance and architectural review. |
| `CP-014` | future | Evaluate and, only if justified, implement CNA-convention CMake orchestration over the same CLI/library. |
| `CP-015` | future | Final sanitizer, golden-vector, compatibility, architecture and risk review; reconcile plan status with the tree. |

Tasks are intentionally vertical/coherent. The ledger is revised when implementation evidence makes
the ordering wrong; it is not a promise to build speculative abstractions.

---

## 13. Risks and rejected alternatives

### Current risks

* `cna_content` currently contains both build-friendly codecs and runtime `ContentManager`; physical
  co-location can obscure the boundary even when the dependency direction is correct. Tests and
  target linkage must prove the new CLI does not initialize runtime services.
* glTF's last orchestration is physically tool-owned and file-staged even though it is now one
  linked library implementation. An eager in-memory rewrite could break the strongest existing
  equivalence oracle; the staging seam should move only with pinned outputs.
* The current one-output build API cannot publish glTF multi-skin Model groups, standalone clips or
  generated texture child assets. It rejects multi-Model input rather than choosing silently;
  graph identity/output ownership must be settled before those cases are advertised as complete.
* A string type ID and C++ type can disagree in a custom extension. Checked boxing/unboxing and
  diagnostics are mandatory; the string is persistent identity, the RTTI guard is only defensive.
* Dependency correctness precedes incremental correctness. An incomplete dependency set must force
  rebuilds rather than permit a wrong skip.
* SharpRuntime's current SHA-256 API accepts one `intcs`-bounded in-memory buffer. CP-008 therefore
  rejects any individual fingerprinted file above 2 GiB instead of truncating it. A future streaming
  hash API should remove that implementation limit before very large model/video dependencies are
  advertised as supported by the cache.
* Content-to-content dependency records have deterministic fingerprint semantics, but the serial
  CLI intentionally forces/refuses that route until graph ordering and cycles are implemented.

### Rejected alternatives

* **Use `Cnb*Data` as every importer output.** Rejected because it erases the import/process split;
  an image importer would already have made texture policy decisions.
* **One giant `CompileSourceToCnb` switch.** This is the current tool shape CNA is replacing; it is
  not extensible and hides component choice/dependencies.
* **Pure template/concept pipeline.** Rejected for registry diagnostics/custom discovery and build
  tooling; it would move selection to compile time and encourage opaque metaprogramming.
* **`std::variant` of all built-in types.** Rejected because every game extension would require
  editing CNA's closed variant.
* **Persist `std::type_index`/`typeid(T).name()`.** Rejected as ABI/compiler dependent.
* **Global self-registering components.** Rejected for static initialization order, test isolation,
  registration-order selection and future concurrency.
* **Writer rebuilds CNB chunks.** Rejected categorically; typed CNB encoders are authoritative.
* **Add a new `ContentTypeReader` hierarchy.** Rejected because the runtime capability already
  exists and works.
* **Infer source dependencies from XREF.** Rejected because source reads and runtime loads have
  different semantics.
* **Timestamp-only incremental build.** Rejected because it cannot answer whether effective inputs
  changed.
* **Invent a project/config format first.** Rejected until implemented processor needs demonstrate
  its minimum useful semantics.
* **Parallel scheduler in v1.** Rejected until registry, dependency and publication correctness are
  stable.

---

## 14. Unresolved questions

* Whether the experimental layer eventually deserves its own physical/CMake module after its link
  closure and consumer set are measured.
* Whether a future build dependency may intentionally live outside source root, and what explicit
  capability grants that access.
* Whether one source producing several logical assets (glTF skins/clips) should be one graph node
  with several outputs or deterministic child nodes. Current glTF behavior makes this a real design
  question; it will be settled before recursive build APIs.
* How custom writer schema/codec version identities should compose with custom CNB asset schema
  versions in fingerprints. Built-ins can use their frozen asset schema IDs directly.
