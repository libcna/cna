# plan_content_pipeline.md — CNA Content Pipeline

> **Status (2026-08-29):** `CP-001` through `CP-015` remain complete. The implemented initial CNA
> Content Pipeline v1 scope is integration-stabilized against `next` at
> `909e5adab95d38b5514dd99e89e316592fe53362`; this synchronization did not reopen a CP task or add
> a pipeline feature. The
> project starts from the existing `content-pipeline` branch at `0e6899f17017c03c0e23d575d25cd70c678e2781`.
> That commit contains the completed CNB baseline through `CNBF-123`. Local `next` was actually
> `4ab1859dc8a540af1bd326df0fa816579adf7027`, two unrelated platform/binding commits ahead; the
> verified original merge base was the starting commit. The historical branch audit and final v1
> verification below record the pre-integration state; the completed branch was subsequently
> preserved through a normal merge of current `next`, without reset, rebase, squash, history
> rewrite or push.
>
> **Continuation (2026-08-29):** development resumes from the clean merged baseline
> `5cb92628d4ab85cde23e6b85231e3f5819d8e4c6`, where local `next`, `origin/next` and the completed
> `content-pipeline` branch agree. New work is isolated on `content-pipeline-next`; it will not be
> merged into `next` or pushed by this plan. `CP-016` records the continuation audit, and
> `CP-017` onward consume the remaining backlog without reopening `CP-001` through `CP-015`.
> `CP-016` through `CP-030` are now complete on that continuation branch. `CP-031` through
> `CP-038` are the following XNB-to-native-CNB compatibility phase.
>
> **XNB continuation (2026-08-29):** work started from clean `next` and
> `content-pipeline-next` commit `f230b2f6b2f01632287e3fb507545401388db0c7` on the isolated
> `content-pipeline-xnb` branch. The phase keeps XNB strictly as a compatibility source format:
> supported built-in roots are decoded to canonical CPU data, passed through the existing
> processors/writers, and emitted as ordinary native CNB. No XNB payload, reader identity, new
> chunk, or frozen-schema change is present in the output.
> `CP-039` is the focused post-XNB follow-on: iterative validation removes the remaining process
> stack dependency from very deep content-build graphs.
>
> **Post-XNB continuation (2026-08-29):** local `next` had advanced independently to
> `ffd32388b220ddd47669bbd90a794100afa6fd1a` and did not contain the completed XNB branch.
> Existing `content-pipeline-model-next` already preserved the XNB/CP-039 head
> `d86a402d14aa376ace990d63fc9a60b1bbbc53df`; a normal, non-rewriting merge produced the safe
> combined baseline `8029488d02d561d35bad8509941aa679e2ac6ab1`. `next` remains untouched.
> `CP-040` through `CP-050` are complete only on that dedicated branch. During final verification,
> local `next` advanced independently by two unrelated `SAMPLE-001` spike commits; they were not
> merged, rebased, rewritten, or otherwise folded into this work.
>
> **Final continuation (2026-08-30):** local `next` was verified at
> `45515bb0a122d582e87d9b4cb48b170cd9b6249a` and still did not contain CP-040 through CP-050.
> The clean completed feature head `dd19589f7af0f63f328cfab4c73d81f995894065` was preserved, a
> new `content-pipeline-final` branch was created from it, and current `next` was merged normally.
> The resulting combined baseline is `5671ebb54`; neither history was rebased, squashed or reset,
> and `next` remains untouched. `CP-051` resumes the remaining evidence-backed backlog from that
> combined history. `CP-051` through `CP-063` are complete: the manifest/explain and named-source-
> root work is implemented, the bounded Model schema-2 codec/runtime proof is complete, and the XNB
> route now selects it only for exactly representable semantics that do not fit frozen schema 1.
> Measured generated-child scheduling and target-profile audits retained the current same-node and
> portable-output policies rather than adding unproven graph or configuration abstractions. A
> focused native-MSVC CI gate now exists. During the later integration session, native run
> `33309632114` passed on feature commit `83807bef64990541e7d41274c11b9562e49112cc`
> with the `windows-2025-vs2026` image, Visual Studio environment 18.9.1 and MSVC tools
> 14.51.36231: both targets built, 177 tests passed with the opt-in sparse-file test skipped, and
> the complete Unicode CLI/explain/workers/clean/deterministic-rebuild probe passed.
> During CP-063 verification, `next` independently advanced from the session-start
> `45515bb0a122d582e87d9b4cb48b170cd9b6249a` through eight renderer-remediation commits to
> `905be872ee5f098f90cfcee7f484dca8136cd33e`. Those commits were not merged or rewritten; the
> established combined baseline remains the topology under review here.
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
  sound, Model document, font, volume, cube, Curve and AnimationClip semantics plus their processed
  outputs;
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
ModelContentWriter(ProcessedModelBundle) -> EncodeModelToCnb()
                                         -> optional EncodeTexture2DToCnb()
                                         -> optional EncodeAnimationClipToCnb()
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

### 5.10 Custom extension proof (`CP-011`)

The extension seam is proven end to end by a realistic `ExampleGame.WorldLevel` test component set:

```text
arena.level
 -> ExampleGame.WorldLevelImporter/3
 -> ImportedWorldLevel + contained arena.collision source dependency
 -> ExampleGame.WorldLevelProcessor/5 { solidBorder: bool }
 -> CompiledWorldLevel + Textures/dungeon runtime reference
 -> ExampleGame.WorldLevelWriter/2
 -> standalone EncodeWorldLevelToCnb() custom codec over CnbWriter
 -> custom CNB
 -> ContentManager::RegisterCnbLoaderEXT<WorldLevel>()
 -> WorldLevel
```

The writer calls the custom asset's one codec function rather than embedding its schema a second
time. The test proves deterministic bytes, stable component selection, strict typed parameter
validation, contained sidecar collection, the build-dependency/runtime-XREF distinction, custom
CMET identity, custom XREF serialization and runtime `ContentManager` loading.

`ContentPipelineExtensionApiIsExperimental` deliberately remains true. One complete custom type
validates that the virtual/type-erased model is usable, but it is not enough evidence to promise
long-term C++ source or ABI stability. In particular, multi-output graph components and dynamic
plugin loading have not yet exercised the API. Persistent component/type identity strings and CNB
custom-type rules are stable behavior; the C++ registration surface is experimental.

### 5.11 User-built custom compiler (`CP-022`)

The stock executable and user-owned compilers now link one `cna_content_compiler` implementation.
`RegisterBuiltInContentPipeline()` performs explicit deterministic built-in registration, while
`RunContentCompiler()` accepts the completed shared registry and owns the same parsing, discovery,
configuration, manifest, cache, diagnostic and atomic-publication path as `cna-content`. The stock
front end is only native argv conversion plus those two calls.

`modules/content/examples/custom-content-compiler.cpp` is a real separately linked executable. It
adds a `.greeting` importer, configurable processor and writer over one custom codec while retaining
all CNA routes. Its subprocess test builds a mixed `.greeting`/PNG source tree, validates custom and
built-in CNB documents plus manifest identities and typed parameters, and proves deterministic
incremental skips.

The supported contract is C++ **source/toolchain** integration through `CNA::ContentCompiler`; it is
not binary plugin loading. A custom compiler is rebuilt against the CNA revision and compatible C++
toolchain it uses. Arbitrary shared-library discovery was rejected because the experimental virtual
interfaces, STL values, exceptions and ownership cross a compiler-specific ABI with no current
version handshake capable of making that stable or safe.

### 5.12 Bounded multi-output build nodes (`CP-023`)

One primary source remains one stable build node, identified by its primary logical asset name.
`ContentWriteResult` preserves its original primary fields and now permits explicitly named
`ContentAdditionalWriteOutput` CNB images, with at most 256 total outputs. This extends existing
writers without changing them; built-in codecs and their bytes remain the sole schema authority.

Manifest version 2 replaces the singular output fields with a sorted ownership list containing
logical name, root-relative path, asset type ID and digest. The effective fingerprint includes the
complete output identity/type set. Version-1 manifests are rejected as incompatible cache state and
cause a safe rebuild rather than being guessed into the new semantics. The primary keeps an
explicit single-file output path; children map deterministically from logical names below the
output root.

The CLI pre-reserves all discovered primary identities/paths and reserves every generated child
before publishing its node. Logical or physical collisions, traversal, empty images, invalid type
IDs and unbounded output sets fail with stage/node context. Each artifact and the manifest use the
one existing atomic-write helper. Publication is deliberately a recoverable per-artifact protocol,
not a fictitious multi-file transaction: if a later child fails, earlier replacements may be
complete, but the old manifest remains and the next run detects the digest mismatch and rebuilds
the node. A removed child can remain as an unclaimed stale file; deletion/garbage collection is not
part of CP-023.

The user-built `.greeting` compiler is the end-to-end proof. Its writer emits a primary greeting
and generated reply using one custom codec. Tests cover stable manifest ordering, child tamper
repair, primary/child collision rejection, failure after primary publication, retained old
manifest/child state, recovery, and subsequent no-op.

### 5.13 Content-to-content build graph (`CP-024`)

The CLI now treats `ContentDependencyKind::ContentBuild` as a directed edge between discovered
primary build-node IDs. A deterministic serial depth-first coordinator owns Unvisited, Visiting,
Done and Failed state per node. It executes a shared dependency once, publishes it before any
dependent, prevents a failed dependency from publishing parents, and leaves runtime XREFs entirely
outside the graph.

Manifest version 3 separates a direct fingerprint from the effective graph fingerprint. The direct
hash covers source/file/generated bytes, stable components, parameters, output identities/types and
dependency edge identities, but not dependency results. When it matches, the previous edge set is
safe to schedule without rerunning components. The effective hash combines it with each completed
dependency's effective fingerprint. Thus a changed shared dependency rebuilds every dependent,
while a complete no-op graph performs no import/process/write work. A changed direct input runs the
node to rediscover its current edges before requiring them, so a removed stale edge cannot block
the rebuild. Manifest versions 1 and 2 safely rebuild.

The `.greeting` example's optional `dependsOn` parameter supplies end-to-end graph evidence.
Subprocess tests prove dependency-first ordering despite lexical source order, single execution of a
shared node, deterministic graph no-op, transitive rebuild with unchanged dependent CNB bytes,
direct-versus-effective fingerprint behavior, missing-primary diagnostics, failure propagation,
no dependent publication, and recovery. A Visiting guard already prevents recursive overflow;
the exact cycle-chain diagnostics are hardened separately in CP-025 below.

### 5.14 Deterministic cycle diagnostics (`CP-025`)

The graph coordinator retains an active logical-node stack. Encountering a Visiting node slices
that stack at its first occurrence and reports the complete closed chain, including the repeated
start node. Dependency lists and root traversal are sorted, so self, two-node and longer cycles
have one stable spelling independent of component registration order.

A dedicated cycle error crosses graph parents without being repeatedly wrapped. Every affected
node still transitions to Failed for correct summary/failure propagation, but the identical cycle
diagnostic is emitted only once. Subprocess tests prove `A -> A`, `A -> B -> A`, and
`A -> B -> C -> A`, zero publication/manifest replacement, exact failed-node counts, and
byte-identical diagnostics on a repeated long-cycle invocation.

### 5.15 Frozen registry and scheduler readiness (`CP-026`)

The registry audit found that a `shared_ptr<const ContentPipelineRegistry>` did not prevent its
owner from retaining a mutable alias. The registry now protects configuration/lookups with a
shared mutex and has a permanent, idempotent freeze bit. `ContentPipeline` freezes at construction;
`RunContentCompiler()` freezes before discovery. Every later `Register*()` call fails under the
same lock, so no retained alias can mutate component tables once workers are possible. A regression
proves all three late registration families fail and sixteen concurrent direct builds through one
frozen registry return their own correct results.

The component audit found no mutable state in CNA's built-ins. Component objects are shared `const`;
contexts, values, dependency collectors, canonical DTOs, cgltf options/data, CNJ readers, and codec
inputs are invocation-local. Vendored stb uses thread-local failure/configuration state. Model/glTF
staging directories use atomic `create_directory`; the sole publisher uses exclusive-create sibling
temporaries, and graph ownership already forbids two nodes from sharing a final path. A reused
custom logger may receive concurrent calls and must synchronize; the stock CLI currently uses no
downstream logger.

The coordinator audit identifies the state CP-027 must not expose to workers: manifest maps,
logical/path ownership, graph states, counters, and stdout/stderr. Parallel work will return
node-local outcomes. One coordinator integrates outcomes in stable logical-node order, freezes the
resolved edge set before dependency scheduling/publication, runs deterministic cycle validation,
and alone commits manifest/ownership/diagnostics. Changed nodes whose processors reveal edges need
a no-publication preparation step; retained bytes must be bounded or staged rather than allowing
unbounded cold-build RAM. Registry mutation, streaming worker diagnostics, and direct worker
manifest writes are rejected designs.

### 5.16 Bounded deterministic graph scheduler (`CP-027`)

`cna-content` and user-linked custom compilers accept `--workers 1..64`; omission and `1` use the
same synchronous serial path. The scheduler selects sorted ready nodes only after their
content-build dependencies have succeeded and executes at most the requested count. A shared node
is represented by one resolution record and cannot be dispatched twice. Failed nodes propagate a
stable Graph-stage failure to dependents without publishing them.

Nodes with changed direct inputs run a parallel preparation pass before graph validation so their
writer outputs and complete edge sets are frozen. Each finished result is atomically staged below
one private owner-only temporary directory and its in-memory bytes are released, bounding retained
memory by the active batch rather than the whole cold build. The coordinator reserves every final
logical/path owner and validates missing edges and deterministic cycles before any staged artifact
is committed. Publication rechecks staged size and SHA-256, then uses the existing sole atomic
publisher for each final artifact.

Workers receive only immutable graph plans and effective dependency fingerprints. Manifest maps,
ownership, graph resolution, counters, event ordering, stdout and stderr stay coordinator-owned.
Outcomes are joined and integrated in logical-name order, so scheduling changes neither artifact
bytes nor manifest/diagnostic identity. Subprocess tests compare worker counts 1, 2 and 4 for mixed
built-in/custom cold builds, no-op builds and shared-dependency invalidation; the output tree,
manifest and logs are byte-identical. Strict CLI tests reject missing, duplicate, signed,
non-numeric and out-of-range counts. The long-cycle diagnostic is also identical between serial and
four-worker runs.

### 5.17 Representative scheduler benchmark (`CP-028`)

`tools/content/benchmark_content_pipeline.py` builds temporary repository-derived fixtures and
compares `--workers 1` with a chosen parallel count. It times only compiler subprocesses, alternates
worker order, records every sample/median/p95 in optional JSON, and rejects any worker-dependent
SHA-256 of the complete artifact/manifest tree. It covers a 128-node equal mix of Texture2D,
SoundEffect, valid skinned Model and Curve CNJ sources plus a 97-node custom graph in which 96
parents share one dependency.

On revision `191b56de7`, a HEADLESS GCC 14.2 Debug build on an 8-core/16-thread Ryzen 7 PRO 7840U
measured seven-sample median speedups for four workers of 2.662x cold, 2.243x no-op, 2.177x for one
changed image, and 1.241x for one changed shared dependency. Every serial/parallel output-tree hash
matched. The tiny custom nodes show the scheduler overhead boundary; no automatic worker default or
CI threshold is inferred. `docs/content-pipeline-benchmark.md` records full methodology, medians,
p95 values and limitations.

### 5.18 CMake configuration/scheduler follow-through (`CP-029`)

The existing `cna_add_content()` remains a custom-target command wrapper over the one stock or
user-linked compiler executable. Optional `CONFIG_FILE` resolves relative to the caller's source
directory, must identify a file at configure time, and is forwarded as `--config`; the compiler
retains the one canonical containment/parser/fingerprint implementation. Optional `WORKERS` is
validated against the exact CLI range 1..64 and always forwards an explicit count, defaulting to
the conservative serial value 1. Neither setting creates CMake-side source enumeration, graph,
cache, dependency fingerprints, or publication.

The generated stock integration target uses a non-default configuration filename to select explicit
stable Curve components and override `Nested/curve` to `Configured/curve`, plus two workers. The
actual generated command contains both options, and the test decodes the configured artifact and
inspects its manifest source/logical/output identity. A second generated target selects the
user-built `.greeting` compiler via `CONTENT_EXECUTABLE`, forwards its own non-default config and two
workers, and verifies custom primary/child artifacts, component identities and typed parameter in
the manifest. Native target dependency and cross-build explicit-host-tool rules are unchanged.

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

The CP-009 identities were `CNA.GltfImporter/1`, `CNA.ModelProcessor/1` and
`CNA.ModelContentWriter/1`; CP-046 deliberately advances all three to version 2 for the processed
bundle/output-set contract. The equivalence-hardened converter is compiled once into
`cna_content`; `cna-content` and `cna_tool_gltf_to_cnb` link that same implementation. The
standalone CNJ tool retains its CLI/oracle entry point. No new cgltf interpretation, CNJ reader or
Model serializer was introduced.

The importer returns source-oriented canonical Model documents rather than pretending that the
wire-oriented `CnbModelData` is an import result. The processor is the only stage that invokes
`BuildCnbModelFromCnj`, registers the returned runtime references, and produces the canonical
primary/child bundle.
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

This CP-009 snapshot predates the bounded multi-output graph. `CP-046` now supplies the optional
generated-child policy while preserving this default single-Model byte oracle; see section 22.

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

Thirteen real-process CLI tests now cover pipeline-byte equality, nested output creation, sorted
multi-asset directory compilation, logical path preservation, support-file filtering, glTF image
dependency invalidation, repeated no-op skips, unknown explicit extensions, destructive-layout
rejection, corrupt/tampered cache recovery, old-output preservation/no temporary debris after a
failed rebuild, and non-ASCII source/output paths with an unchanged incremental skip.

### Windows pathname strategy

The new CLI uses `wmain(int, wchar_t**)` on Windows and constructs `std::filesystem::path` from
wide arguments. POSIX uses `main(int, char**)`, treating argv as the locale-independent byte spelling
accepted by `std::filesystem`. Logical content names are UTF-8 with `/` separators and are converted
explicitly at the native/logical boundary; native paths are never serialized as logical names.
Narrow `main` on Windows is rejected for the new CLI because it cannot represent every filesystem
path. Old tools are not refactored as part of this decision.

`CNA::Internal::ContentPathToUtf8` and `ContentPathFromUtf8` are the one explicit conversion seam.
The pipeline keeps source/output paths native, diagnostics and dependency identities use generic
UTF-8, and manifest reads reconstruct native paths from UTF-8 rather than feeding bytes to the
Windows active code page. Shared image, WAV and DDS imports now accept native filesystem paths;
their legacy narrow-string overloads retain their existing behavior. Image import opens
the file natively and feeds bytes to the same `ImageLoader::LoadFromMemory`, so this did not create
a decoder. CNJ image/WAV/DDS/raw/clip sidecars pass authored JSON UTF-8 through the same conversion
and then through the context's existing containment checks.

Cross-platform unit tests exercise native non-ASCII image and WAV paths, authored CNJ sidecars, and
native <-> persistent UTF-8 round trips. A real POSIX `cna-content` process test builds and then
skips `Textury/žluťoučký_壁.png` beneath non-ASCII source/output roots, verifying the CNB logical name
and manifest paths. The Windows entry point and lower native-path APIs compile in the normal target,
but this Linux run is not reported as a Windows execution result.

At the CP-012 checkpoint one audited limitation remained explicit: the shared glTF orchestration
ultimately passed `path.string()` to cgltf, and `BuildCnbModelFromCnj` retained a legacy narrow-path
compiler seam. CP-021 later closed that seam with CNA-owned cgltf callbacks that translate explicit
UTF-8 to native filesystem paths and a native-path Model CNJ compiler overload. No locale conversion
or second glTF parser was introduced, and native Windows runtime execution remains separately
unverified rather than inferred from the portable regression test.

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
* CNB container major/minor identity, selected writer identity/version, and every declared output
  asset ID, canonical type name, asset schema version and explicit codec name/version;
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
* `cna_add_content(TARGET ... SOURCE_DIR ... OUTPUT_DIR ...)` now adds one explicit custom target
  that invokes the same `cna-content` executable. The target intentionally runs whenever requested;
  the content-hashed manifest, not a second CMake dependency model, decides per-asset skips.
* Relative source/output roots follow the caller's source/binary directories. `QUIET` is optional.
  A cross build must supply a host `CONTENT_EXECUTABLE`; attempting to execute CNA's target binary
  on the host is rejected at configure time.
* A generated Curve fixture proves the helper creates the logical nested CNB and versioned manifest
  through the actual CLI. No `cna_add_game()` convenience wrapper is added before game-helper UX is
  understood.

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

### 11.1 Architectural review evidence (`CP-013`)

The pre-finish checklist was checked against code and tests, not answered from the design alone:

* The image path is `ImportedImage -> TextureProcessor -> CnbTextureData`, WAV preserves source PCM
  encoding in `ImportedSound` until `SoundEffectProcessor`, and glTF uses an owned
  `ImportedModelDocument` before `ModelProcessor`; import and processing are separate in real
  vertical slices rather than interface names only.
* A source search of `modules/content/src/Pipeline` finds exactly eight built-in writer calls, each
  to the corresponding existing `Encode*ToCnb()` function, and no `CnbWriter`, `AddChunk`, schema
  field ordering or CRC implementation. Custom tests define one custom codec and adapt the custom
  writer to it; built-in schema sources remain singular.
* Pipeline/tool sources contain no `GraphicsDevice`, `ContentManager`, renderer, audio-device or
  SDL initialization. In the tested HEADLESS build, `ldd cna-content` lists only zstd and standard
  C/C++ runtime libraries; no SDL, graphics driver, FFmpeg or audio library is required to execute
  the compiler.
* `ContentPipelineCliTest.FailedRebuildPreservesTheOldValidOutputAndLeavesNoTemporary` exercises the
  real process and shared `CnaToolAtomicWrite.hpp`: malformed replacement input leaves old output
  and manifest bytes intact and no publisher temporary remains. No writer owns filesystem
  publication.
* Model and custom-level tests independently show source dependencies and runtime XREFs in separate
  collections; the custom CNB document also proves the intended XREF was actually encoded. Source
  dependencies are explicit in successful build results and normalized manifests.
* Core registry tests cover deterministic default and explicit resolution, duplicate component
  identities, ambiguous importer/processor/writer routes, missing routes and checked type-erasure
  mismatch. Ordered maps/sets, stable author-controlled strings and component versions drive
  selection/fingerprints; RTTI remains a process-local cast guard only.
* The realistic `.level` test registers a game importer, typed configurable processor, custom
  writer/codec and runtime loader end to end. `ContentPipelineExtensionApiIsExperimental` is asserted
  by that test and documented; one example does not freeze source/ABI compatibility or solve
  dynamic loading into the stock CLI.
* Texture2D and SoundEffect tests compare both library and real legacy-tool outputs. Model tests pin
  pipeline = direct glTF producer = glTF -> CNJ -> CNB where supported. CNJ tests compare all eight
  integrated types against `CompileCnjToCnb`. The broad CP-010 run retained all 11 frozen golden
  vectors. No second image, WAV, DDS, glTF or built-in CNB serializer was added.
* The glTF tool and `cna-content` already share the one conversion implementation. The source and
  CNJ producer tools remain intentionally unchanged compatibility/oracle front ends; their safe
  migration can use the same registered components later, but CP-013 does not remove the oracle
  while relying on it for byte proof.
* Fingerprints hash effective file bytes, component identities/versions, typed processor options,
  logical identity, container identity and asset type. They contain no mtime or RTTI spelling.
  Directory discovery is UTF-8-logical-name sorted. Tests cover no-op, independent/dependency/
  parameter/version invalidation and output tampering. Content-build graph scheduling is refused
  until cycle and multi-output rules exist, preventing a wrong skip.
* Importer and processor contexts are non-copyable, call-scoped, and expose different focused
  services. Components are registry-owned shared const objects and expected to retain no context,
  providing a comprehensible ownership model and a future reentrancy path without adding threads
  now.
* `ContentPipelineError` carries native source, logical name, stage, stable component and the
  underlying exception text. Path diagnostics and persisted path identities use explicit UTF-8.
  Primary/dependency containment, absolute traversal and symlink escape tests pass. CP-012 also
  records the exact remaining Windows glTF/Model narrowing boundary instead of claiming it solved.
* No project format or platform-specific CNB profile was introduced. Effect can later add an
  importer/processor/writer route after shader architecture stabilizes without changing this
  registry model or any frozen asset schema.

`docs/content-pipeline.md` now presents this implemented boundary, built-in route matrix, CLI,
incremental and security behavior, XNA mapping, CNJ/CNB/XNB relationship, custom extension flow,
known limitations and stable/experimental/internal labels. `docs/README.md` links it from the main
documentation index.

### 11.2 Final verification (`CP-015`)

The final review used the repository's existing HEADLESS Debug configuration and a separate fresh
sanitizer configuration rather than inferring results from earlier task-local runs:

* `cmake --build cmake-build-debug -j2` completed the whole configured tree successfully. Existing
  warnings in renderer example sources did not fail the build.
* The final normal selection ran 259 tests from 27 suites covering the pipeline core and CLI,
  CMake integration, the custom extension, manifest/cache behavior, every vertical slice, legacy
  producer oracles, typed codecs, runtime CNB dispatch and all 11 frozen golden vectors. All 259
  passed.
* Three unrelated runtime texture-upload tests were excluded from that selection because the
  configured HEADLESS renderer explicitly has no complete TextureCube/Texture3D storage. Their
  isolated failures report that renderer capability boundary; the corresponding CNB codecs,
  producers and deterministic byte tests remain in the passing selection.
* The broad run initially caught that the CNBF-123 source-level producer guard still expected only
  the three legacy tools. Its expected set now includes `tools/content/content.cpp`; the guard also
  proves all four front ends include and invoke the one `CnaToolAtomicWrite.hpp` implementation.
* A fresh `/tmp/cna-content-pipeline-asan-ubsan` Debug build used
  `-DCNA_GRAPHICS_RENDERER=HEADLESS`, `-DCNA_ENABLE_VIDEO=OFF`,
  `-DCNA_SANITIZE=address,undefined`, and built `CnaTests`, `cna-content`, and all three legacy
  producer executables. The same 259 tests passed with ASan and UBSan active and both configured to
  halt on the first finding.
* Leak detection alone was disabled for the successful sanitizer run because LeakSanitizer refuses
  to operate under this runner's `ptrace` supervision. A preliminary `detect_leaks=1` run proved
  that limitation directly: 218 in-process tests passed while sanitised child tools exited only on
  LeakSanitizer's explicit `does not work under ptrace` guard. This is not reported as LSan
  coverage. TSan was not run because v1 has no parallel scheduler or concurrently mutable
  registry; the registry is explicitly configured before serial builds.
* The final diff modifies no frozen CNB codec definition, asset ID, schema declaration, format
  specification or existing golden-vector asset. All 11 byte-for-byte golden tests pass in both
  normal and ASan+UBSan configurations. Therefore no frozen CNB definition or existing frozen CNB
  byte changed.
* Local `next` advanced independently during this work from the verified starting value
  `4ab1859dc8a540af1bd326df0fa816579adf7027` to
  `d5d66d8735e2aac8246246056d3ef4219c97623d`. No commit from it was merged or rebased; the final
  merge base remains the intended CNB-complete starting commit
  `0e6899f17017c03c0e23d575d25cd70c678e2781`.

There is no active CP task at this checkpoint. Further work is deliberately the bounded future work
listed under current risks and unresolved questions: multi-output build graphs, custom-component
loading for the stock CLI, remaining Windows Model/glTF Unicode seams, optional configuration and
profiles, and only then parallel scheduling. Song/Video source import routes can be added when
their authoring semantics are specified; Effect remains outside this project.

### 11.3 Integration stabilization against current `next`

The completed feature branch was synchronized without reopening `CP-001` through `CP-015`:

* Starting `content-pipeline` was `7f7739d81ff4c042566e19bf9d7493ceff8f3386`; current `next` was
  `909e5adab95d38b5514dd99e89e316592fe53362`, with original merge base
  `0e6899f17017c03c0e23d575d25cd70c678e2781`. The normal merge was textually conflict-free. The
  one semantic CMake seam was the newer focused-test split: tool-path definitions and the generated
  `cna_add_content` fixture now attach to the content test object target, so both `CnaContentTests`
  and aggregate `CnaTests` receive them.
* The newer complete-public-header C-ABI inventory correctly discovers the experimental pipeline
  surface. Its generated snapshot is 544 headers / 9,173 symbols: 8,352 implemented, 15 approved
  partial, 342 planned under future `CBIND-117`, and 464 not applicable. No C route, ABI version or
  export was changed; all nine build-free C-API inventory/compatibility/release-gate tests pass.
* A fresh HEADLESS Debug Ninja tree configured and built completely. The required integration gate
  ran 563 tests from 60 content/CNB/CNJ/pipeline suites: 561 passed and two optional large-fixture
  tests skipped. It includes old/new Texture2D (including color key), SoundEffect, Model/direct
  glTF, direct glTF versus glTF-to-CNJ, all eight CNJ compiler routes, dependency/XREF separation,
  incremental invalidation, atomic artifact/manifest publication, custom extension, CMake helper,
  runtime decode and 11/11 frozen golden vectors.
* The broader focused content runner ran 1,446 tests from 172 suites: 1,431 passed, seven skipped
  and eight failed. Three are the already recorded HEADLESS Texture3D/TextureCube upload boundary.
  Five are pre-existing broad glTF corpus/renderer-source policy failures; neither their tests nor
  the inspected renderer sources changed in this merge. They are reported rather than folded into
  the green pipeline gate.
* A fresh ASan+UBSan HEADLESS Debug tree built `CnaContentTests`, `cna-content` and all legacy CNB
  producers. With `halt_on_error=1`, the same 563-test gate passed (561 passed, two environment-
  gated skips). LeakSanitizer again refused the subprocess fixture under this runner's `ptrace`, so
  the successful run used `detect_leaks=0` and is not claimed as LSan evidence. TSan remains not
  applicable because v1 adds no parallel scheduler or concurrently mutable registry.
* `cna-content` now links the specific `cna_content` target instead of the aggregate `CNA` facade.
  The legacy static-module closure still lists platform archives as possible link inputs, but the
  resulting executable has no renderer, SDL/audio, `GraphicsDevice`, audio-device or
  `ContentManager::Load` runtime dependency/symbol and initializes none of them. Its sources contain
  no runtime compiler shortcut. Writers still call only the existing typed `Encode*ToCnb()` APIs,
  and all four producer front ends still publish through the single `CnaToolAtomicWrite.hpp` helper.
* No frozen CNB codec, schema, asset ID, format document or golden asset changed. No future CP item,
  project format, target profile, parallel scheduler, Song/Video importer, Effect or package work
  was added.

### 11.4 Extended-pipeline final verification (`CP-030`)

The continuation was closed with fresh evidence over the final implementation rather than only
carrying forward task-local results:

* The complete configured HEADLESS Debug tree built successfully. A 273-test selection spanning
  the extended pipeline, CLI, configuration, manifests, custom compiler, graph/scheduler,
  Song/Video/Model routes, legacy producers, CNJ equivalence, all 11 frozen golden vectors, and path
  containment passed 273/273 normally and again with combined ASan+UBSan and `halt_on_error=1`. The
  initial O1 sanitizer build encountered a GCC 14 `std::regex` `-Wmaybe-uninitialized` false positive
  in the unchanged SharpRuntime dependency, so the successful sanitizer build used O0.
  LeakSanitizer explicitly refused the subprocess runner under `ptrace`; the green run used
  `detect_leaks=0` and is not LSan evidence.
* A freshly reconfigured GCC ThreadSanitizer HEADLESS tree passed 107/107 pipeline/configuration/
  manifest/custom/CMake tests with `halt_on_error=1` and no TSan report. The opt-in large-file case
  was excluded because its purpose is I/O-size coverage rather than concurrency; it passed
  separately in the normal tree.
* The sparse 2 GiB+1-byte SHA-256 regression ran with `CNA_RUN_LARGE_FILE_TESTS=1` and passed in
  48.7 seconds without a repository fixture. File hashing remains the original SHA-256 semantics in
  bounded 1 MiB updates.
* A fresh MinGW-w64 Windows-target configuration compiled and linked the complete `cna_content`
  static target, covering native-path pipeline and scheduler sources. This is cross-compile
  evidence only: neither native MSVC nor Windows execution was available, so the Unicode runtime
  seam remains explicitly unverified on Windows.
* The normal 256-test compatibility/security selection produced 254 passes, the expected
  default-disabled large-file skip, and the known pre-existing HEADLESS TextureCube runtime upload
  failure. Repeating that one case confirmed the HEADLESS renderer does not retain the complete
  requested cube-face region; it is outside the build pipeline. An unfiltered content runner also
  reached an existing PulseAudio fixture and could not wake its mainloop in this restricted
  environment, so it was interrupted rather than reported as a pipeline regression.
* The generated C API coverage/compatibility/header/limitations/export/route/bool/release/ABI gates
  all pass. The inventory sees the experimental C++ declarations but exposes no pipeline C ABI;
  `CBIND-117` remains the separate planning boundary.
* The final `cna-content` executable's dynamic dependencies are zstd and the standard C/C++ runtime
  only. Symbol and dynamic-section inspection found no `GraphicsDevice`, renderer, SDL/audio,
  FFmpeg, audio-device or `ContentManager::Load` dependency. No private staging directories were
  left under the tested temporary root.
* `next...content-pipeline-next` changes no frozen codec, container/schema definition, asset/chunk
  identifier, CRC implementation, golden vector, `plans/plan_cnb.md`, or atomic publication helper.
  Built-in writers still delegate to the existing ten typed encoders, and every final producer
  still reaches the one shared atomic publication implementation.

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
| `CP-011` | **completed** | Added a realistic custom `.level` importer, parameterized processor, custom codec/writer and `ContentManager` loader end-to-end test. It proves deterministic output, source dependency versus runtime XREF behavior, stable component identities, custom CMET/XREF data, configuration diagnostics and runtime loading. The C++ API remains explicitly experimental because one custom schema does not settle source/ABI stability or multi-output/plugin requirements. |
| `CP-012` | **completed** | Kept wide Windows argv and native filesystem paths through the CLI/core, added explicit generic-UTF-8 manifest/diagnostic conversion, added native image/WAV/DDS import overloads and Unicode CNJ sidecar resolution, and passed 43 focused tests including a real non-ASCII directory build/no-op. Windows execution was not available; glTF/Model's audited legacy narrow seam is recorded rather than hidden. |
| `CP-013` | **completed** | Added the implementation-derived `docs/content-pipeline.md` and index entry, documenting the build/runtime boundary, XNA mapping, exact component/context/data APIs, built-in routes, dependencies versus XREF, CLI/cache/atomic/path behavior, migration limits and stable/experimental/internal status. The evidence-based architecture review above found no duplicate built-in codec/parser or runtime-device dependency; the remaining Windows Model and build-graph limits stay explicit. |
| `CP-014` | **completed** | Added the minimal `cna_add_content(TARGET ... SOURCE_DIR ... OUTPUT_DIR ... [QUIET] [CONTENT_EXECUTABLE ...])` helper. It always delegates to the real CLI, leaving dependency/cache/publication semantics singular; native builds depend on the tool target and cross builds require an explicit host compiler. A generated nested Curve integration fixture and test prove CNB logical-name/manifest output through the helper. |
| `CP-015` | **completed** | Completed the whole HEADLESS Debug build and a 259-test/27-suite compatibility selection, including all 11 frozen golden vectors. Fixed the CNBF-123 producer guard to recognize `cna-content` as the fourth atomic publisher. A fresh ASan+UBSan build of the tests, new CLI and legacy producers passed the same 259 tests with `halt_on_error`; LSan was explicitly unavailable under the runner's `ptrace`, and TSan is not applicable before concurrent scheduling. The final architecture review found no frozen CNB or byte change. |
| `CP-016` | **completed** | Audited the integrated v1 baseline, requested plans/docs, component sources, CLI, CMake helper, tests and producer/runtime media seams. The concrete dependency order is: preserve SHA-256 semantics while removing the 2 GiB one-shot limit; add the smallest optional per-asset configuration needed to supply non-inferable media metadata; complete Song and Video routes; close the independent Windows Model path seam; expose custom components through a user-linked compiler executable; then evolve one-output manifests into a graph with cycles proved before parallelism. |
| `CP-017` | **completed** | Primary-source, file-dependency, generated-dependency and output verification now stream through the existing SharpRuntime SHA-256 implementation in 1 MiB chunks. The known vector and a 3 MiB cross-chunk file prove digest compatibility; missing/unreadable inputs fail cleanly; the opt-in sparse 2 GiB+1-byte gate passed against an independently pinned digest in 34.9 seconds. Memory is bounded, manifest/fingerprint version 1 remains valid, and 21/22 focused manifest/CLI tests passed with only the default-disabled large gate skipped. |
| `CP-018` | **completed** | Added strict optional `.cna-content.json` plus contained `--config`: normalized root-relative asset keys, logical-name/output override, stable importer/processor/writer selection and explicitly tagged bool/i64/u64/f64/string parameters. Unknown/duplicate fields, missing/unsupported assets, unsafe paths, unknown components/options and wrong types fail contextually. Existing convention builds remain unchanged; effective identities/parameters already fingerprint each record, so a config edit rebuilt only the affected asset while its independent neighbor skipped. Five parser and 17 CLI tests passed. |
| `CP-019` | **completed** | Added `SongImporter`, `SongProcessor`, and `SongContentWriter` over the unchanged `EncodeSongToCnb()`. The importer retains a non-empty media file as a streamed, root-relative external source without decoding/embedding it; configured/default metadata produces one separately recorded XREF. `.wav` remains unambiguously SoundEffect. Six component/runtime tests plus a single/directory CLI test prove stable selection, Unicode paths, validation, determinism, primary-source invalidation, manifest XREF separation, runtime metadata loading, and exact bytes against both the library encoder and real legacy producer. The one-output builder intentionally does not copy media before CP-023 defines safe multi-output publication. |
| `CP-020` | **completed** | Added `VideoImporter`, `VideoProcessor`, and `VideoContentWriter` over the unchanged `EncodeVideoToCnb()`. The non-decoding importer covers unambiguous runtime video extensions; required configured width/height/fps prevents invented metadata or an FFmpeg dependency, while duration/soundtrack retain schema defaults. Six component/runtime tests and two CLI tests prove strict types/ranges/missing metadata, Unicode, single/directory builds, deterministic incremental invalidation, manifest XREF separation, HEADLESS runtime metadata compatibility, and exact bytes against the library encoder and legacy producer. `.ogg` remains Song-only for deterministic convention routing, and media copying remains CP-023 work. |
| `CP-021` | **completed** | Kept Model/glTF paths native through pipeline discovery, intermediate Model CNJ compilation, sidecar opens and generated output publication. The one shared glTF implementation now gives cgltf generic UTF-8 names plus CNA file callbacks that reconstruct native filesystem paths; authored URI and serialized/generated-name boundaries use the existing explicit UTF-8 helpers. A repeated POSIX build with non-ASCII source root, nested directories, `.gltf`, external `.bin`, texture and generated XREF passes and preserves the pinned Model/direct-producer bytes; all four affected conversion sources also pass MinGW Windows-target syntax compilation. No native MSVC/Windows runtime was available, so Windows execution is still an explicit verification gap rather than a claimed result. |
| `CP-022` | **completed** | Extracted the stock CLI coordinator into the linkable `CNA::ContentCompiler` target and added explicit built-in registration plus a configured-registry runner. Stock and custom executables now share discovery, configuration, incremental manifests, diagnostics and the sole atomic publisher. A real `.greeting` compiler example and subprocess test prove mixed custom/built-in directory output, typed configuration fingerprints, custom CMET/chunk bytes, manifest identities, determinism and no-op skips. The 150-test pipeline/legacy-producer/CNJ/golden gate passed (149 pass, one expected large-file skip), all 23 C-header compatibility cells passed, and the two new C++ declarations remain honestly planned under `CBIND-117` with no C ABI/export change. The contract is C++ source/toolchain compatibility; no dynamic plugin ABI or library search is claimed. |
| `CP-023` | **completed** | Added a backward-extending writer result with at most 256 explicitly named CNB outputs, stable primary-node/output identities, global logical/path ownership checks and manifest v2 output lists. Version 1 is rejected into a safe rebuild. Every artifact and the manifest still use the sole atomic publisher; a later-output failure retains the old manifest so digest mismatch deterministically repairs the whole node. The custom `.greeting` compiler proves generated child publication, stable ordering/no-op, child-tamper repair, primary collision rejection and recovery after partial multi-file publication. The 141-test pipeline/producer/CNJ/golden selection passed 140 with only the expected large-file gate skipped; all 23 C-header compatibility cells and generated inventory gates pass. Frozen built-in encoders and bytes are unchanged. |
| `CP-024` | **completed** | Added deterministic serial graph scheduling for `ContentBuild` edges between discovered primary node IDs, distinct from file/generated inputs and runtime XREFs. Manifest v3 stores direct/topology and effective dependency fingerprints; versions 1/2 safely rebuild. Shared dependencies execute once before parents, no-op graphs reuse prior edges without running components, dependency changes rebuild every dependent, and missing/failed targets prevent parent publication with Graph-stage context. The `.greeting` subprocess suite proves ordering, shared coordination, cache propagation, failure/recovery and direct/effective hash behavior. The 144-test pipeline/producer/CNJ/golden selection passed 143 with only the expected large-file skip, and all 23 C-header compatibility cells pass with no C ABI/export change. |
| `CP-025` | **completed** | Added an active-stack cycle detector that reports the complete logical chain with its repeated start node. Sorted root/edge traversal makes selection deterministic; a dedicated error avoids nested duplicate chains while every affected node still fails. Self, two-node and three-node subprocess tests prove exact chains, one diagnostic, no publication/manifest replacement, correct failure counts and byte-identical repeated output. |
| `CP-026` | **completed** | Added a permanent mutex-protected registry freeze, invoked before CLI discovery and by direct coordinators, with deterministic late-registration refusal through retained aliases. Public component/logger contracts now state their concurrency obligations. The audit found built-ins invocation-local, cgltf per-call, stb thread-local, and staging/publication names exclusively claimed; it confines manifest, ownership, graph states, counters and terminal diagnostics to a deterministic coordinator. Tests prove all late registration families fail and sixteen direct builds share a frozen registry safely. The 149-test pipeline/producer/CNJ/golden selection passed 148 with only the expected large-file skip, and all seven generated C-API consistency gates pass; the two new experimental declarations are planned under `CBIND-117` with no C export. CP-027 owns the bounded node-local scheduler implementation. |
| `CP-027` | **completed** | Added strict `--workers 1..64` with a true synchronous fallback, bounded parallel preparation/staging, dependency-ready execution and coordinator-only deterministic integration. Shared nodes dispatch once, failures propagate without dependent publication, and private staged outputs are size/digest verified before the sole atomic publisher commits them. Worker counts 1, 2 and 4 produce byte-identical mixed cold, no-op and shared-dependency rebuild trees, manifests and logs; long-cycle diagnostics are identical between serial and four-worker runs. The 174-test normal pipeline/producer/CNJ/golden selection passed 173 with only the expected large-file skip. A fresh GCC ThreadSanitizer HEADLESS build passed 106/107 pipeline/config/manifest/custom/media/model tests, with only that same opt-in >2 GiB test skipped and no TSan report. |
| `CP-028` | **completed** | Added a reproducible fail-fast benchmark harness for 128 mixed PNG/WAV/glTF/CNJ nodes and a 97-node shared-dependency custom graph. It alternates serial/parallel order, excludes fixture/seed/verification work, emits machine-readable samples, and proves complete tree/manifest equality. Seven-sample HEADLESS Debug medians for workers 1 versus 4 measured 2.662x cold, 2.243x no-op, 2.177x one-change and 1.241x shared-change speedups on the recorded 8-core host. The conservative default remains one worker and results are documented as host-specific evidence, not a CI threshold. |
| `CP-029` | **completed** | Extended the thin `cna_add_content()` wrapper with configure-time-checked `CONFIG_FILE` and strict `WORKERS 1..64`, defaulting to serial and forwarding both to the selected stock/custom compiler. The real CLI still solely owns containment, JSON parsing, fingerprints, graph/cache and atomic publication. Generated stock and user-linked custom targets prove `--config ... --workers 2`: one changes a Curve logical output through explicit stable components; the other is accepted only by the `.greeting` compiler and verifies its custom primary/child outputs, component identities and typed parameter. Native/cross host-tool separation is unchanged. The 175-test normal compatibility selection passed 174 with only the expected >2 GiB skip. |
| `CP-030` | **completed** | Closed the extended pipeline with a complete HEADLESS Debug build, the same 273/273 compatibility/security selection normally and under ASan+UBSan, a 107/107 TSan pipeline/concurrency selection, and the real opt-in sparse 2 GiB+1-byte hash test. MinGW-w64 compiled and linked the complete `cna_content` target; native Windows/MSVC execution remains unclaimed. All nine C-API gates pass with no pipeline export. `cna-content` has no renderer/SDL/audio/FFmpeg/runtime-loader dependency, no staging residue remained, and the final diff changes no frozen CNB definition, byte oracle or atomic publisher. The known HEADLESS TextureCube runtime failure, ptrace-disabled LSan, and restricted PulseAudio full-run block are recorded separately from pipeline results. |
| `CP-031` | **completed** | Audited header/version/platform validation, None/LZX/LZ4 dispatch, LZX framing, limits, normalized type tables, root dispatch, shared resources, external references, and every candidate built-in reader. The matrix in section 15 records exact runtime seams and native representability. The build parser reuses those container/type/limit components without the process-global runtime factory registry. |
| `CP-032` | **completed** | Added reentrant `CNA.XnbImporter/1`. It validates the container and normalized root reader identity, accepts only the explicit built-in graph matrix, emits existing imported types, and reports the source plus unsupported/custom reader identity. It has no `ContentManager`, graphics/audio/video service, mutable registration, filename guessing, or XNB-in-CNB fallback. |
| `CP-033` | **completed** | Extracted shared canonical Texture2D bytes and retained the runtime GPU adapter. Color and DXT1/3/5, level-zero/full mip chains, legacy v4 DXT mappings, bounds and truncation are tested through native Texture2D CNB; DXT normalizes to the frozen schema's Rgba8 representation. BGRA/normalized-vector and Xbox-swizzled transcoding are explicit rejections because schema 1 cannot prove equivalent observable format/bytes. |
| `CP-034` | **completed** | Extracted the SpriteFont reader graph into a shared headless decoder for the nested Texture2D atlas, rectangle/character/Vector3 lists and all scalar/default-character fields. Uncompressed DXT-atlas and multi-block LZX fixtures are field/atlas equivalent through native SpriteFont CNB and the old runtime path remains covered. |
| `CP-035` | **completed** | Extracted WAVEFORMATEX/sample parsing and a device-free audio decode seam shared by the runtime adapter and pipeline. PCM8/16, float32, MS-ADPCM and IMA-ADPCM fixtures produce deterministic native PCM CNB with loop/rate/channel/frame validation; no audio device is opened. XMA2, unknown codecs and unproven Xbox sample byte order fail explicitly. |
| `CP-036` | **completed** | Added headless Texture3D, TextureCube, Curve, Song and FNA-layout Video routes through their existing native processors/writers. Song/Video retain external media as contained byte dependencies plus XREFs. Model remains intentionally unsupported: its real fixture uses three shared resources and runtime VertexBuffer/IndexBuffer/BasicEffect construction, while current native Model cannot prove arbitrary effect/tag graph equivalence without silent loss. |
| `CP-037` | **completed** | Added a permanent fixture matrix covering container variants, all supported roots, negative/truncated/custom/shared graphs and deterministic bytes. Runtime-XNB versus transcoded-CNB oracles compare Texture2D pixels/format/mips, every SpriteFont field and atlas, SoundEffect duration/name plus canonical PCM for all codecs, Curve keys, and Song/Video metadata/path semantics. Texture3D/Cube use canonical CPU level/face byte comparison because HEADLESS deliberately has no safe runtime GPU storage/readback. Existing runtime XNB regressions remain gates. |
| `CP-038` | **completed** | Registered `.xnb` in the stock frozen registry and ordinary single/directory discovery. CLI tests prove single-file build, directory layouts, worker 1/2/4 byte-identical trees/manifests, no-op skip, source change, output-tamper repair, configuration fingerprint invalidation and unsupported-reader non-publication. XNB nodes use the existing manifest, graph, scheduler, staging and atomic publisher without a separate build path. |
| `CP-039` | **completed** | Post-XNB review selected the recursive cycle preflight as the highest-value unambiguous risk. Replaced recursive DFS with an explicit frame/active-position stack while preserving sorted traversal, exact cycle chains and one shared diagnostic per cycle. A 4,096-node acyclic graph builds under workers 4, the same graph closed into a 4,096-node cycle fails deterministically without replacing its prior manifest, all three legacy cycle oracles remain exact, and the deep case passes ASan+UBSan. |
| `CP-040` | **completed** | Audited the complete FNA/CNA XNB Model graph against frozen Model schema 1. Section 17 records the field-level representability proof, the deliberately narrow useful subset, the two demonstrated blockers in the existing Blender cube fixture plus its remaining bounds gate, and the separately reviewable schema-2 requirements. No Model codec, schema, runtime path or byte changed. |
| `CP-041` | **completed** | Added one field-order Model graph walker and shared CPU decoders for declarations, vertex/index buffers and BasicEffect state. The runtime readers remain adapters over those decoders, while `XnbImporter` resolves the shared graph into canonical data and accepts only the CP-040 schema-1 subset through the existing Model processor/writer. Synthetic positive and real/synthetic negative fixtures cover complete geometry/material/hierarchy semantics, texture XREF containment, tags, declarations, effect type/power, ranges, bounds, sharing and truncation; runtime XNB versus CNB Model equivalence and all pre-existing Model/effect reader regressions pass. No renderer/device is used by compilation and no Model wire byte changed. The one new experimental C++ carrier field is inventoried as planned under existing `CBIND-117` (449 total); no C route/export/ABI version changed. |
| `CP-042` | **completed** | Moved compiler staging under a private versioned per-user temporary parent and added an exact session name/owner marker plus an OS-held lease. Startup inspects at most 4,096 direct entries/256 candidates, requires same-user ownership, a non-symlink directory, matching bounded metadata and age >=24h, then deletes only after exclusively claiming the lease. PID is diagnostic identity only, so reuse cannot authorize deletion; old live builds retain their lock. Malformed, recent, future-dated, symlinked, legacy and indeterminate candidates remain untouched with sorted diagnostics. Tests cover an old same-PID abandoned tree, old active lease, recent/malformed/symlink cases, authored source survival, scan bounds and normal cleanup. |
| `CP-043` | **completed** | Added sorted previous-owned minus next-owned collection only after every node succeeds and before manifest replacement. A valid prior manifest plus unchanged recorded SHA-256 proves each regular non-symlink candidate; all parents must be real directories inside the canonical output root. There is no tree/extension scan or recursive deletion. Corrupt manifests authorize nothing, failed builds collect nothing, and changed/symlinked/indeterminate candidates fail conservatively with the old manifest retained. Tests cover removal, logical rename, multi-output contraction, manual files, corruption, failed builds, digest replacement, symlink escape, recovery ordering and workers 1/2/4 deterministic trees. |
| `CP-044` | **completed** | Added an explicit bounded deployment-file result separate from CNB writer outputs and runtime XREFs. Song/Video and XNB Song/Video register their canonical media source and final XREF path; manifest v4 stores contained source/path/SHA-256 ownership. Preparation and publication stream through the existing atomic helper with a 1 MiB buffer, skip checks verify support digests, reservations reject compiled/cross-node collisions, and CP-043 GC handles renamed/removed support paths. In-place single-file media is never copied or owned; every other destination inside the source root is rejected. Tests cover direct and XNB routes, overrides, tamper repair, source change, removal/rename GC, manual-file survival, collision/escape guards, failed-publication recovery and workers 1/2/4 deterministic trees. Importer/processor identities moved to version 2; frozen Song/Video CNB bytes remain identical. The generated inventory now records 9,298 symbols with all 467 experimental pipeline rows planned under `CBIND-117`; no C route, export or ABI version changed. |
| `CP-045` | **completed** | Added sorted `ContentWriterSchemaIdentity` declarations containing asset ID/name, native schema version and explicit codec name/version. The core rejects incomplete/duplicate/undeclared identities; manifest v5 persists and fingerprints declarations plus per-output schema/name, and versions 1–4 rebuild safely. The skip path compares the current declaration without executing the writer. All ten built-ins declare their frozen schema-1 encoders, and the real custom compiler proves independently stale asset ID, type name, schema and codec records all force rebuild while unchanged identities skip. The generated inventory records 9,319 symbols/488 experimental pipeline rows under `CBIND-117`; no RTTI, C API route/export/version, CNB schema or encoder byte changed. |
| `CP-046` | **completed** | Audited scenes, skin/static Model groups, embedded/standalone clips, external/data-URI textures and the existing output graph. A single animated Model now builds normally with identical primary bytes. Optional bool `generateChildAssets` publishes canonical additional Models, standalone AnimationClips and remapped native Texture2D children through the existing typed encoders; multi-Model input requires this explicit mode. Default-scene selection remains unchanged, the lexicographically first generated Model is primary, sanitized group/clip name collisions fail before overwrite, and ordinary reservations/atomic staging/manifest ownership/CP-043 contraction GC govern the bundle. Real fixtures prove embedded/child clip equivalence, external/embedded texture decode, multi-Model semantics, default producer bytes, no-op/incremental contraction and workers 1/2/4 identical trees. The inventory now records 9,332 symbols/501 pipeline rows under `CBIND-117`; no C route/export/version, frozen schema or default glTF byte changed. |
| `CP-047` | **completed** | Confirmed from MonoGame `ContentWriter`/`Lz4DecoderStream` that flag `0x40` carries one raw LZ4 block after the ordinary decompressed-size field, never an LZ4 frame. Added one bounds-checked shared decoder used by runtime and canonical compiler paths with no runtime dependency. A fixture whose real MonoGame body was compressed by upstream liblz4 proves exact bytes; runtime and headless XNB-to-CNB pixels agree. Negative tests cover every token/length/offset/input/output boundary, size limits and 1,500 deterministic whole-container mutations. Both compression bits remain invalid. |
| `CP-048` | **completed** | Added MinGW's Unicode-console startup option only to the stock and custom content compiler executables that define `wmain`. Both link as x86-64 PE console programs; Wine 10 runs each, the stock tool builds through a native non-ASCII path, and the custom tool publishes its two-output fixture. A Windows-target Curve CNB is byte-identical to the Linux build. Linux entry points still build/run normally. Native Windows and MSVC remain untested and unclaimed. |
| `CP-049` | **completed** | Added `clean <output-directory> [--quiet]` as an empty-next-manifest call through CP-043's exact sorted digest/containment preflight; it removes the valid manifest last and never scans or prunes the tree. Builds and cleans now hold one persistent per-output-root OS lease, so an active operation, unsafe lease, corrupt/symlinked manifest, changed output, or symlinked path fails before destructive work. Compiled, generated and deployment outputs are covered; manual/source files survive. The 53-test CLI suite and focused ASan+UBSan/TSan selections pass, and the MinGW build plus Wine build/clean route succeeds. |
| `CP-050` | **completed** | Closed the post-XNB continuation with a complete HEADLESS Debug build and a 1,535-case passing boundary (1,527 pass/eight opt-in or fixture skips) after separately reproducing the three known renderer-only HEADLESS TextureCube/Texture3D failures. Independent gates pass for CNB 371/371, CNJ 137 plus two skips, XNB/runtime readers 221 plus four skips, glTF/Model 608 plus three skips, and all 11 frozen golden vectors. Combined ASan+UBSan passes 1,537 plus eight skips (`detect_leaks=0`; no LSan claim), TSan passes 91 plus the large-file skip, the opt-in >2 GiB hash passes, both CMake fixtures and all nine C-API gates pass. Representative workers 1/4 trees remain identical; a 1,024-output ownership-clean benchmark removes about 480 MiB in 7.05 seconds and preserves only the lease. No frozen CNB definition, byte, default glTF output, C ABI, merge, or push changed. |
| `CP-051` | **completed** | Audited the combined post-CP-050 implementation and converted the remaining documented questions into evidence-gated tasks. Manifest v5 persists route/configuration/topology/output records but collapses authored bytes and effective content dependencies into final hashes; artifact checks and manifest loading also return booleans/empty state that cannot support trustworthy reason identity. Named external source roots require an explicit configuration and containment design. Model schema 1 remains frozen and any schema 2 is gated behind a field/runtime audit. Same-node glTF children, target profiles and MSVC/Windows remain audits rather than assumed features. |
| `CP-052` | **completed** | Evolved the manifest to v6 with nine bounded canonical SHA-256 domains: primary bytes; source-dependency set/bytes; content-dependency set/effective fingerprints; parameters; writer schemas/codecs; compiled-output/XREF definitions; and deployment definitions. Direct/effective aggregate hashes now derive from the same domains. Versions 1-5 rebuild as incompatible without granting orphan-deletion authority; deterministic round-trip/domain-isolation/migration tests and the complete 66-case manifest/configuration/CLI/custom/CMake boundary pass. No CNB definition or byte changed. |
| `CP-053` | **completed** | Added private structured build decisions and `build ... --explain`. Reasons compare inspectable route/schema/codec fields plus the persisted v6 domains, effective graph inputs and per-artifact digests; they distinguish manifest state, source/dependency/configuration/component/output/deployment changes and missing versus tampered artifacts without guessing from aggregate hashes. Root-relative sorted explanations are byte-identical under workers 1/2/4; `--quiet` suppresses successful explanations and `clean` rejects the option. The complete 71-case manifest/CLI ASan+UBSan boundary and an 11-case TSan selection pass. On the 128-node no-op fixture the median was 0.291 s with explanations versus 0.276 s normally (5.6%, about 15 ms). |
| `CP-054` | **completed** | Audited configuration, context resolution, manifest hashing, deployment and destructive paths and specified the bounded capability model in section 30. Strict configuration gains at most 32 lowercase aliases under `sourceRoots`; authored references use explicit `@alias/root-relative-path` syntax. The native mapping is request-local and never persisted. Manifest v7 stores alias and relative path as separate fields, hashes both, and resolves without root search. Existing source-relative resolution remains the default. Canonical source/external/output roots may not equal or nest; deployment from an external root is accepted only after the same explicitly aliased source dependency was recorded, while publication/clean/GC remain output-root-only. |
| `CP-055` | **completed** | Implemented bounded named external source roots end to end. Strict config maps aliases under `sourceRoots`; unqualified dependencies retain source-root containment while `@alias/path` resolves directly through a canonical request-local read capability. Manifest v7 persists alias plus relative identity, hashes both, and never stores physical roots. Same-byte physical remapping skips; alias/identity/set/byte changes invalidate. External deployment requires the exact aliased dependency first and still publishes only below the output root. Normal 101-test, ASan+UBSan 101-test and focused TSan 10-test gates prove workers 1/4 identity, migration, traversal/absolute/backslash/symlink/unknown/duplicate/missing/file/overlap rejection, and external sentinel survival through deployment contraction, GC and clean. |
| `CP-056` | **completed** | Audited FNA's complete Model/vertex/index/stock-effect readers against CNA's canonical XNB graph, frozen schema-1 carrier, runtime Model/buffer/effect APIs, CNB adapter and renderer declaration boundary. Section 32 records the field matrix and a demonstrated real use case: MonoGame's Blender cube needs an explicit Position+Normal declaration, serialized sphere and non-default BasicEffect SpecularPower, all of which CNA already constructs and exposes. A separate resource-table schema is coherent without CLR object graphs; null tags remain the only supported tag policy, custom effects remain rejected, and declaration-limited renderers retain their existing explicit fidelity rejection. No CNB definition or byte changed in this audit. |
| `CP-057` | **completed** | Specified the byte-exact candidate in section 33: eleven schema chunk types plus typed container XREFs; fixed header/bone/mesh/part/declaration/resource/effect rows; complete stable vertex/effect ID tables; resource identity, null-tag and canonical ordering rules; overflow/count/index/window validation; schema selection; runtime construction; and an independent conformance vector. CP-058 subsequently proved and froze that candidate without changing schema 1. |
| `CP-058` | **completed** | Added a separate CPU carrier/codec for Model asset type 5/schema 2 and runtime dispatch by schema version. Exact declarations, shared vertex/index/effect resources, part windows, authored spheres, explicit root identity/transforms, typed XREFs, null-only tags, and all five stock effects round-trip and construct exactly. The decoder validates mandatory topology/alignment, every count/table/product/range/reserved value, graph/resource identity, typed references, and inactive fields before GPU construction. A manually specified Python vector pins all 1,468 bytes, fixed offsets, and SHA-256 `6a9dc3f5363ae82a93ba8e01fee1059802ac1325d5fd76565ccddb09d928ad78`; production encode and CPU/runtime decode match it. Schema 1 and every prior golden remain byte-identical. |
| `CP-059` | **completed** | Broadened lossless XNB Model transcoding onto schema 2 without changing the schema-1 route or bytes. The headless canonical graph now preserves all five stock effects and shared resources; the converter maps every XNA declaration format/usage, exact buffers/windows/bounds/root hierarchy, stock material fields and typed texture references. Selection attempts the proved schema-1 converter first and uses schema 2 only after complete independent validation. Null tags remain the only policy; custom effects and malformed graphs fail. The writer now declares both Model schema tuples and every output reports its actual schema, requiring manifest v8; v7 rebuilds safely without deletion authority. Synthetic and real MonoGame tests compare runtime XNB against runtime schema-2 CNB field-by-field and prove schema-1-compatible XNB bytes remain exactly equal to the unchanged encoder. |
| `CP-060` | **completed** | Measured generated glTF child rebuild behavior and retained same-node scheduling. An external image pixel-only edit changed the Texture2D child while leaving Model bytes identical, but a representative full glTF node took about 1.47 ms warm and both full-node and texture-only CLI rebuilds rounded to 0.03 s. Animation names/values remain embedded in schema-1 Model; renaming one clip changed the Model and child set while another child stayed byte-identical. Every proposed split still requires the shared glTF parse/scene/material conversion, and embedded/data-URI images have no independent authored input. That bounded cache opportunity does not justify a generated-source graph, ownership, or partial-publication contract. |
| `CP-061` | **completed** | Audited every built-in importer/processor/writer and found no target-dependent output policy. Images and font/volume atlases use portable Rgba8; WAV uses Pcm16; authored DDS/XNB representations are preserved or losslessly normalized; Model schemas encode source semantics; Curve/Animation are canonical pass-through; Song/Video copy their exact media and emit portable metadata; and no Effect/shader compiler exists. Container compression is an explicit codec concern, not a platform profile, and renderer selection belongs to runtime. No profile ID, API, CLI/config key, fingerprint field, CMake forwarding, CNB field, or C ABI was added. Existing per-asset typed parameters already cover real policies such as color key and deployment metadata. |
| `CP-062` | **completed** | Confirmed this host has MinGW and Wine but no `cl`, `clang-cl`, PowerShell, native Windows host, or way to execute GitHub Actions locally. Added a focused `windows-latest` workflow matching the repository's native-MSVC convention: pinned sharp-runtime, HEADLESS platform/renderer, SDL3's in-memory audio conversion path, two focused targets, bounded parallelism, CPU manifest/codec/pipeline/XNB tests, and a real Unicode CLI build/no-op/explain/workers/clean/deterministic-rebuild probe. A fresh equivalent Linux configuration passed 177 of 178 selected tests with the opt-in sparse-file gate skipped. Subsequent authorized native run `33309632114` passed the same 177/1 selection and complete Unicode CLI probe on `windows-2025-vs2026`, Visual Studio 18.9.1 and MSVC tools 14.51.36231. Documentation also states that neither a manifest generation nor modern diagnostic can force pre-CP-049 binaries to honor `.cna-content.lock`; mixed-generation overlap remains unsupported. |
| `CP-063` | **completed** | Closed the final branch with a clean complete HEADLESS build and an attributable 1,438-case content/CNB/CNJ/XNB/glTF boundary: 1,430 passed and eight opt-in/external-fixture cases skipped after excluding exactly the three previously documented HEADLESS TextureCube/Texture3D storage-adapter failures. All 14 schema-1/schema-2 golden tests and the real >2 GiB streaming hash pass. A fresh ASan+UBSan selection passes 187 plus the large-file skip with leak detection disabled only for the runner's known `ptrace` limitation; a rebuilt TSan boundary passes 108 plus that skip with no report. Both real CMake fixtures and all nine C-API gates pass; the inventory remains 551 headers/9,485 declarations with 654 experimental content declarations planned under `CBIND-117` and no C ABI change. Final review found no new security, determinism, atomicity, schema-1-byte, or publisher regression. Native MSVC execution, stable machine-readable decision output, arbitrary XNB object graphs/custom effects, and evidence-gated future profile/child scheduling remain honestly outside the completed scope. |

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
* Optional glTF generated children remain one bounded graph node. This deliberately does not offer
  independent cache invalidation for extracted clips/textures; Model schema 1 embeds clips and its
  material references still make the Model dependent on the selected generated texture identities.
* A string type ID and C++ type can disagree in a custom extension. Checked boxing/unboxing and
  diagnostics are mandatory; the string is persistent identity, the RTTI guard is only defensive.
* Dependency correctness precedes incremental correctness. An incomplete dependency set must force
  rebuilds rather than permit a wrong skip.
* The public SharpRuntime SHA-256 convenience call remains `intcs`-bounded, but CP-017's internal
  streaming adapter feeds bounded chunks through the same implementation. Content files are no
  longer capped at 2 GiB and no second SHA-256 algorithm was introduced.
* Parallel work is opt-in and built-ins are audited reentrant. A custom component or logger with
  unsynchronized mutable per-instance state is safe with the default `--workers 1` but violates the
  documented contract when the user explicitly enables multiple workers.
* Prepared cold-build outputs use bounded RAM but may occupy temporary disk space up to the total
  compiled output size until the run completes. CP-042 scavenges only version-1 directories with
  valid identity metadata that are at least 24 hours old and whose lease can be claimed. Legacy or
  malformed/incompletely initialized trees remain deliberately untouched rather than guessed safe.
* Song/Video compilation now copies raw streaming media as explicit deployment-support files rather
  than CNB writer outputs. The manifest and extension API shape are experimental; files are copied
  byte-for-byte with no transcoding, and an in-place single-file layout remains user-owned rather
  than becoming garbage-collection ownership.
* Multi-file publication is recoverable but not transactional across paths. A failed later output
  can leave earlier complete replacements beside the old manifest; the next build repairs the
  owning node. CP-043 likewise removes obsolete manifest-owned files before manifest replacement,
  so a crash can leave already-removed old outputs named by the previous manifest; the next build
  treats those missing stale files as already clean and retries publication.
* Output-root serialization is cooperative among compiler binaries that implement
  `.cna-content.lock`. A concurrently running older compiler that predates CP-049 does not honor
  the marker; mixed-version concurrent build/clean operation against one root is unsupported.
* Windows Unicode paths stay native through CLI discovery, manifests, image/WAV/DDS/CNJ and
  Model/glTF flows. cgltf and authored/generated JSON names cross one explicit generic-UTF-8
  boundary backed by a native file callback. Portable tests cover the complete non-ASCII Model
  source/sidecar layout and existing model bytes remain pinned, but no native MSVC/Windows runtime
  execution was available for CP-021.
* Custom compilers are source/toolchain-linked executables. `CNA::ContentCompiler` intentionally
  exposes the same experimental C++ types as component registration, so it is not a stable binary
  boundary and separately distributed dynamic plugins are unsupported.

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
* **Load custom components as arbitrary shared libraries.** Rejected for the current experimental
  API: virtual interfaces, STL ownership and exceptions cross the C++ ABI, while CNA has no stable
  plugin handshake. A user-built compiler linked from source provides the real extension use case
  without implying binary compatibility or searching untrusted directories.

---

## 14. Unresolved questions

* Whether the experimental layer eventually deserves its own physical/CMake module after its link
  closure and consumer set are measured.
* Whether additional built-in authoring formats beyond the context-resolved CNJ/XNB/custom seams
  need explicit external-root reference syntax. In particular, glTF's shared converter validates
  and opens URIs before the pipeline context observes them, so it must not claim capability support
  without a separately reviewed resolver callback.

---

## 15. XNB compatibility sources to native CNB (`CP-031`–`CP-038`)

The implemented feature transcodes supported XNA/FNA/MonoGame XNB assets into ordinary native
CNB assets. XNB becomes another compatibility source format beside PNG, WAV, glTF and CNJ:

```text
known built-in .xnb
    -> XnbImporter
    -> existing imported/canonical CNA data
    -> existing processor
    -> existing ContentTypeWriter
    -> existing Encode*ToCnb()
    -> native .cnb
```

`XnbImporter` parses with the existing `XnbHeader`, shared `XnbDecompression` decoders,
`XnbTypeReaderTable`, `ContentReader` primitives and `XnbReadLimits`. Its canonical table mode does
not instantiate runtime readers through the mutable process-global `ContentTypeReaderManager`.
Root and nested references are 1-based validated table indices; reader version zero is required.
None, LZX, and MonoGame's single raw-LZ4-block representation are accepted. Versions 4/5 and the
same 16 platform bytes as runtime are container-valid;
individual routes can impose stricter semantic rules (notably Xbox texture/audio payloads).

Shared resources are parsed and bounded. Generic shared-resource graphs remain rejected; the Model
route admits only its CP-041 VertexBuffer/IndexBuffer/BasicEffect subset and then proves one-owner
identity before conversion. Generic external XNB object references are not interpreted as build
edges. Song/Video media paths become source dependencies plus CNB XREFs, while the admitted Model
BasicEffect texture path becomes a contained logical XREF. Absolute paths, escaping `..`, canonical
symlink escapes for source dependencies, missing/empty media and unconsumed graph bytes fail.

### 15.1 Reader audit and support matrix

| XNB root reader | Existing runtime reader | Runtime construction seam | Headless canonical decode | Native CNB result / status |
|---|---:|---|---:|---|
| `Texture2DReader` | yes | `GraphicsDevice`, Texture2D allocation/upload | yes, shared | Texture2D **supported** for Color/DXT1/3/5; DXT becomes Rgba8 |
| `SpriteFontReader` | yes | nested GPU Texture2D plus runtime SpriteFont | yes, shared built-in nested graph | SpriteFont **supported**; atlas follows Texture2D limits |
| `SoundEffectReader` | yes | SoundEffect construction and former WAV runtime decode | yes, shared; SDL decode calls do not open a device | SoundEffect **supported** for PCM8/16, float32, MS/IMA ADPCM |
| `Texture3DReader` | yes | GraphicsDevice and Texture3D upload | yes, shared | Texture3D **supported** for Color/DXT1/3/5 to Rgba8 |
| `TextureCubeReader` | yes | GraphicsDevice and six-face upload | yes, shared | TextureCube **supported** for Color/DXT1/3/5 to Rgba8 |
| `CurveReader` | yes | none; CPU value already | yes, shared | Curve **supported** through the existing Curve schema |
| `SongReader` | yes | ContentManager path resolution and runtime Song | yes, shared metadata | Song **supported** with contained external media dependency/XREF |
| `VideoReader` | yes | ContentManager path resolution, GraphicsDevice stored by Video | yes, shared metadata | Video **supported** for FNA's String/Int32/Single object-reference graph |
| `ModelReader` | yes | shared resources, GPU vertex/index buffers, effects, ownership graph | yes, shared field-order graph plus CPU resource decoders | Model **supported subset** per section 17; incompatible declarations/effects/tags/windows/bounds/sharing fail exactly |
| any other/custom root | maybe | unknown application semantics | no | **unsupported** with exact normalized reader identity |

Texture transcode deliberately targets the only representation frozen CNB schema 1 can currently
emit: Rgba8. Color bytes remain Rgba8; DXT1/3/5 are decoded with the same shared DXT implementation
used by historical runtime fallback. `ColorBgraEXT`, `NormalizedByte2`, `NormalizedByte4`, unknown
formats and Xbox-swizzled texture payloads are rejected rather than changing the schema or lying
about observable format. Likewise XMA2/unknown audio and unproven Xbox sample ordering are rejected.
On a runtime renderer that preserves DXT natively, the old XNB object's optimization-level
`SurfaceFormat` can remain DXT while native CNB is Rgba8; HEADLESS and other fallback renderers are
format/pixel equivalent. This is the precise compatibility boundary, not a claim of arbitrary
bit-preserving XNB texture conversion.

SpriteFont accepts exactly the built-in nested graph it needs: Texture2D,
`List<Rectangle>`, `List<Char>` and `List<Vector3>`, with matching table identities and counts.
Video accepts FNA's real `ReadObject<String/Int32/Single>` references. The runtime Video adapter also
keeps CNA's older direct-field fixture seam for backward compatibility; that non-FNA test layout is
not advertised as a pipeline source format.

### 15.2 Shared headless decoder and equivalence evidence

```text
validated XNB bytes
    -> Decode*XnbData() canonical CPU value
       -> runtime adapter (existing runtime object behavior)
       -> pipeline adapter -> existing processor -> existing writer -> existing Encode*ToCnb()
```

The extracted parsers own no renderer, device, window, playback service or global registry state.
Texture/runtime adapters alone allocate GPU objects. Sound conversion may use SDL's in-memory WAVE
decoder when CNA is built with the SDL3 audio backend, but it does not initialize SDL subsystems,
open an audio device, or construct a mixer/playback object. Other audio backends retain PCM8/16
support and reject formats for which they provide no decoder.

| Supported root | Permanent equivalence evidence |
|---|---|
| Texture2D | real uncompressed Color and LZX DXT fixtures; canonical levels; runtime width/height/format/mips and per-level pixels versus CNB |
| SpriteFont | real uncompressed DXT atlas and multi-block LZX fixtures; all glyph/crop/character/kerning/scalar/default fields and runtime atlas pixels |
| SoundEffect | six real PCM8/16, float32, MS-ADPCM and IMA-ADPCM fixtures; exact canonical processed samples/rate/channels/frames/loops; PCM16 runtime duration/name versus CNB |
| Texture3D | hand-built FNA-order full mip fixture; every canonical dimension and level byte versus decoded native CNB (runtime GPU oracle unavailable in HEADLESS) |
| TextureCube | real DXT1 six-face/full-mip fixture; all 42 canonical face/level images versus decoded native CNB (runtime GPU oracle unavailable in HEADLESS) |
| Curve | hand-built FNA-order fields; canonical/native keys and runtime-XNB versus runtime-CNB equality |
| Song | real MonoGame fixture and companion Ogg; duration/name/media filename, dependency and XREF equality |
| Video | hand-built FNA object-reference graph; all metadata/media filename, dependency and XREF equality |

The CLI matrix also proves identical output trees and manifest bytes with workers 1, 2 and 4,
normal incremental skip, source-byte invalidation, configuration-parameter invalidation and output
digest repair. The frozen registry and invocation-local canonical readers make parallel decoding
reentrant; there is no mutable reader registration during a build.

### 15.3 Executed verification and environment boundaries

The final HEADLESS Debug gate built the complete configuration. Focused XNB conversion tests are
18/18; the existing XNB/container/reader selection is 197 passed with five expected HEADLESS skips;
the complete pipeline selection is 126 passed with only the opt-in greater-than-2-GiB fixture
skipped. CNB is 370/370 after excluding the three known HEADLESS Texture3D/TextureCube runtime
storage tests, CNJ is 108/108, and the glTF selection is 589 passed with three opt-in large-fixture
skips. The platform SDL ownership ratchet remains at zero files/references.

ASan+UBSan passes 200/200 XNB/runtime/pipeline tests with leak detection disabled because LSan cannot
operate under this environment's ptrace wrapper; SDL's dummy audio backend avoids contacting the
restricted host PulseAudio service while retaining runtime SoundEffect construction. TSan passes
17/17 conversion and XNB CLI tests, including the workers 1/2/4 directory route. The decoder object
itself references only `SDL_LoadWAV_IO` and `SDL_ConvertAudioSamples`, not SDL initialization or
device-open calls.

MinGW-w64 compiles every modified source, including `cna_content`, `cna_audio`, the compiler and the
tool entry point. CP-048 subsequently fixed the locally owned `wmain` link configuration and added
Wine execution coverage; native Windows/MSVC execution is still unavailable and is not claimed.
All nine build-free C-API consistency gates pass. The four canonical
reader helpers remain private implementation details behind a source-private friend shim; the 19
new/changed experimental pipeline declarations are inventoried as planned under open `CBIND-117`,
without adding a C export.

The primary compatibility oracle compares both runtime results:

```text
source.xnb -> existing XNB loader -> runtime value A

source.xnb -> XnbImporter -> native CNB -> existing CNB loader -> runtime value B

relevant fields/bytes of A == B
```

An opaque `XNB0` chunk or `EmbeddedXnb` asset that stores the original XNB bytes inside CNB is
deliberately **not implemented**. It would retain the full XNB runtime layer and stack CNB validation,
compression and dispatch over XNB's own container machinery. Reconsider it only for a concrete
deployment/interchange requirement that cannot be served more directly by a future package format;
bit-preserving wrapping is not a fallback for an unsupported native conversion.

---

## 16. Focused post-XNB review (`CP-039`)

The review selected iterative cycle validation because the risk was already demonstrated by the
architecture: graph execution is iterative, but its preflight still consumed one C++ call frame per
dependency level. The replacement uses explicit visit frames, the same sorted dependency lists and
an active-node position map. It preserves the established self/two/three-node diagnostic bytes and
removes process-stack depth from graph correctness. The permanent integration oracle builds a
4,096-node acyclic chain with four workers, then closes its last edge into a 4,096-node cycle and
proves one complete deterministic chain, 4,096 failed nodes, no publication, and preservation of
the last valid manifest. The same case passes combined ASan+UBSan.

The review also identified independent policy work. CP-042 through CP-046 subsequently supplied
staging recovery, manifest-proven obsolete ownership collection, Song/Video deployment,
writer/schema/codec cache identities, and the optional glTF child/multi-Model policy without
replacing the graph or publisher.

---

## 17. XNB Model representability (`CP-040`)

This audit compares FNA's complete `ModelReader`, `VertexBufferReader`, `IndexBufferReader`,
`VertexDeclarationReader` and stock-effect readers with CNA's runtime readers, `CnbModelData`,
Model schema 1 codec, CNJ/glTF processor, and CNB runtime adapter. The conclusion is deliberately
not "Model fits": a useful subset fits only when every value omitted by schema 1 is either a
proved runtime default or exactly recoverable from retained data.

| XNB field or semantic | Model schema-1 equivalent | Classification | Required rule |
|---|---|---|---|
| bone name | `CnbModelBone::name` | **EXACT** | preserve UTF-8 name |
| bone transform | `CnbModelBone::transform` | **SUPPORTED-SUBSET** | every non-root transform is exact; bone 0 must be identity because the current CNB runtime adapter intentionally ignores its stored transform |
| parent and child lists | `CnbModelBone::parent` | **NORMALIZABLE** | source lists must agree, have one root, and already be parent-before-child; CNB reconstructs children from parents |
| root bone | implicit bone 0 | **SUPPORTED-SUBSET** | serialized root must be bone 0 |
| mesh name / parent bone | `CnbModelMesh::name` / `parentBone` | **EXACT** | parent must be valid |
| mesh bounding sphere | recomputed by CNB runtime from retained positions | **SUPPORTED-SUBSET** | serialized sphere must equal the deterministic recomputation; otherwise the public property would change |
| mesh, part and Model `Tag` | none | **UNSUPPORTED** | all three tag levels must be null; arbitrary CLR graphs are never persisted |
| part primitive count | `CnbModelPart::primitiveCount` | **EXACT** | XNB Model topology is triangle-list and count must match the retained indices |
| `VertexOffset`, `StartIndex`, `NumVertices` | none; CNB creates one whole buffer per part at offset zero | **SUPPORTED-SUBSET** | both offsets must be zero and `NumVertices` must cover the complete referenced vertex buffer |
| vertex bytes / count / stride | `vertexBytes`, `vertexCount`, `vertexStride` | **SUPPORTED-SUBSET** | byte sizes must be exact and bounded |
| vertex declaration elements | not serialized; runtime infers one fixed layout from stride | **SUPPORTED-SUBSET** | usage, usage index, format and offset must exactly equal CNA's canonical table for that stride; no element is dropped or repacked |
| canonical declarations | inferred stride table | **EXACT within subset** | admitted strides are 16, 20, 24, 32, 48, 52, 56, 60, 68, 76 and 80 only when every declared element exactly matches that stride's table row |
| 16/32-bit index bytes | `indexElementSize`, `indexBytes`, `indexCount` | **EXACT** | payload length must be a multiple of 2/4 and every index must be in range |
| shared vertex/index buffers | no sharing identity; one buffer is built per CNB part | **SUPPORTED-SUBSET** | each shared buffer may be referenced by exactly one part; otherwise public pointer identity and mutation behavior change |
| `BasicEffect` type | `CnbEffectKind::BasicEffect` | **EXACT within subset** | no other effect reader in the initial slice |
| diffuse/emissive/specular colour and alpha | existing material factors | **NORMALIZABLE** | the CNB runtime adapter must apply these existing schema-1 fields to BasicEffect; no wire change is needed |
| `BasicEffect.SpecularPower` | none | **SUPPORTED-SUBSET** | must equal the constructed XNA/CNA default 16 exactly |
| BasicEffect texture and `TextureEnabled` | base-colour XREF; enabled iff non-null | **NORMALIZABLE** | relative logical reference must remain contained and resolve to the same content identity |
| `VertexColorEnabled` | part flag | **EXACT** | preserve the serialized flag exactly; both runtime paths use the same admitted declaration |
| World/View/Projection, lighting rig, fog, per-pixel preference | not serialized by `BasicEffectReader` | **RUNTIME-ONLY** | both paths retain constructor defaults; callers set draw-time state after load |
| shared BasicEffect identity | no sharing identity; one effect is built per CNB part | **SUPPORTED-SUBSET** | each effect resource may be referenced by exactly one part |
| AlphaTest/DualTexture/EnvironmentMap/Skinned/custom `Effect` graphs | partial or no schema concepts | **UNSUPPORTED** | reject by exact reader identity until every field and sharing behavior is proved |
| external texture object loading | CNB XREF and `ContentManager` cache | **NORMALIZABLE** | store the resolved root-relative logical asset name, never source bytes or an embedded XNB |
| GPU object construction | none in canonical data | **RUNTIME-ONLY** | compiler parses declarations/buffers/effects as CPU data only |

The existing real `BlenderDefaultCube.xnb` is a valuable negative fixture, not a positive oracle.
It cannot pass schema 1 losslessly because (1) stride 24 means Position+Normal in that file while
CNA's schema-1 runtime infers Position+Color+TextureCoordinate for stride 24, (2) its
`SpecularPower` is `9.607843399047852` rather than the only representable value 16. Independently,
its serialized mesh sphere is not itself carried by schema 1 and would also have to pass the
recomputation equality gate before conversion. No one of those conditions may be silently ignored.

### 17.1 Initial useful subset

`CP-041` supports uncompressed or already-supported LZX XNB v4/v5 Models containing a valid
parent-before-child hierarchy rooted at bone 0, identity bone-0 transform, null tags, triangle-list
parts that consume unique whole vertex/index buffers, an exact canonical CNA vertex declaration,
16- or 32-bit indices, and one uniquely referenced `BasicEffect` per part with default
`SpecularPower`. Texture references, colours, alpha and vertex-colour enablement are retained
through fields schema 1 already has. Every failed condition names the mesh/part/resource and the
first incompatible semantic. This is narrow but useful for conventional
`VertexPositionNormalTexture` and other already-native CNA layouts; it is not described as general
XNA Model compatibility.

### 17.2 Implemented canonical decode architecture (`CP-041`)

`ReadXnbModelGraph()` is the single field-order parser for bones, hierarchy, meshes, parts, tags,
shared-resource references and root identity. A runtime sink retains the existing `ModelReader`
ownership/fixup behavior; a canonical sink retains scalar references and rejects non-null tags
before constructing runtime objects. `DecodeVertexDeclarationXnbData()`,
`DecodeVertexBufferXnbData()`, `DecodeIndexBufferXnbData()` and
`DecodeBasicEffectXnbData()` are likewise shared by the runtime readers and compiler. Only after
all shared CPU resources are decoded does `ConvertXnbModelToCnb()` prove every subset condition.

The successful route is therefore:

```text
validated XNB + shared field-order graph/CPU resource decoders
    -> XnbModelData
    -> subset proof / CnbModelData
    -> ImportedModelDocument
    -> existing ModelProcessor
    -> existing ModelContentWriter
    -> existing EncodeModelToCnb()
```

The compiler never creates `GraphicsDevice`, `VertexBuffer`, `IndexBuffer`, `Effect`, renderer or
window state. Texture references become contained root-relative logical XREFs. Every shared
vertex buffer, index buffer and effect must have exactly one use: duplicating immutable bytes
would preserve drawing in many cases, but shared runtime effect/buffer mutation and pointer
identity are observable, so schema 1 cannot claim the general case. Existing schema-1 material
factors are now applied to XNA stock effects by the CNB runtime adapter; this realizes already
documented fields and changes no wire layout or encoded bytes.

### 17.3 Possible Model schema 2 requirements (not authorized here)

A future schema able to claim general XNB Model fidelity would need, at minimum: the complete
vertex declaration (usage, usage index, format, offset and stride); part vertex/index windows;
serialized mesh bounding spheres; explicit root-bone identity plus preservation of root transform;
a stable policy for supported tag values; a resource table capable of distinguishing shared from
distinct buffers/effects; complete discriminated stock-effect records including SpecularPower,
SkinnedEffect weights, alpha-test and environment-map fields; and a deliberate policy for embedded
custom Effect bytecode versus external Effect assets. Those requirements are documented for a
separate architectural review. `CP-040` neither changes nor reinterprets frozen Model schema 1.

---

## 18. Abandoned staging recovery (`CP-042`)

Every compiler run now owns one child of the private, owner-only
`<system-temp>/cna_content_staging_v1` parent. A child name contains an exact versioned prefix,
decimal PID, 16-digit session token and decimal collision attempt. Its bounded owner marker repeats
the directory name, PID/token and creation time. A separate lease file is held with `flock()` on
POSIX or an exclusive no-share handle on Windows for the entire build. PID is never treated as
proof that a process is alive: a reused PID cannot hold the abandoned session's lease.

Before claiming its own child, a run scans at most 4,096 direct parent entries and retains at most
256 matching candidates. Candidate diagnostics are sorted. Deletion requires all of the following:

1. an exact current-version name directly below the staging parent;
2. a real non-symlink directory owned by the current POSIX user (the Windows temp parent is already
   per-user/ACL protected);
3. a regular, non-symlink, <=1 KiB marker whose identity exactly matches the directory;
4. a non-future creation time at least 24 hours old; and
5. successful exclusive claim of the regular, non-symlink lease.

The claim remains held through recursive removal on POSIX. Windows closes its exclusive claim only
immediately before removal because the OS refuses deletion of an open no-share file; session names
are unique and no build can adopt an old directory. `remove_all()` is applied only to the validated
direct child, and filesystem symlinks encountered below it are removed as links rather than
traversed. User source/output roots are never inputs to scavenging.

Normal success and handled failures still remove the current tree through RAII. A crash releases
the OS lease, so a later run can remove the valid tree after the safety threshold. Old live builds,
recent/future timestamps, malformed or missing metadata/leases, symlink candidates, owner mismatch,
and the pre-CP-042 legacy naming scheme remain untouched. That intentionally leaves a tiny
incomplete-initialization/legacy residue class rather than guessing that an unproved directory is
pipeline-owned. Hitting either scan bound leaves the remainder for a future invocation and emits a
deterministic diagnostic; scavenging never prevents an otherwise valid build merely because one
candidate cannot be classified or removed.

---

## 19. Manifest-proven obsolete-output collection (`CP-043`)

The prior successfully parsed manifest is the sole ownership authority. After the whole requested
graph succeeds, the coordinator computes the sorted set difference between every old output path
and every newly owned output path. This catches source deletion, logical/configuration renames,
route replacement and additional-output contraction without scanning `Content/` or inferring
ownership from `.cnb`. A manually placed file at any unowned path is invisible to collection.

Before the first removal, every obsolete candidate is preflighted. Its manifest path is already a
validated safe relative UTF-8 path; resolution starts at the canonical output root, each existing
parent must be a real non-symlink directory, and the target must be a real regular non-symlink file
whose streaming SHA-256 exactly equals the old ownership record. Missing paths need no action.
Directories are never recursively removed. A changed file, symlink target/parent, root escape,
conflicting old ownership record or filesystem error aborts collection before planned removals,
preserves the suspect path and leaves the previous manifest in place. A corrupt/incompatible
manifest sets no ownership authority and therefore permits a safe rebuild/manifest replacement but
no deletion of prior output bytes.

The recoverable ordering is:

```text
publish every successful current artifact atomically
    -> require every requested graph node to have succeeded
    -> preflight old-owned minus new-owned
    -> remove only validated obsolete regular files
    -> atomically publish the new manifest
```

A node failure performs no collection. A crash or I/O failure during removal, or failure to publish
the new manifest afterward, can leave some complete new artifacts and some already absent stale
artifacts beside the old manifest. The next run uses that retained record: current digest mismatch
forces rebuilding while an already missing obsolete path is accepted, so cleanup/publication can be
retried without a second journal or false multi-file transaction claim.

CLI regressions prove source deletion and configuration rename, an unrelated hand-authored `.cnb`,
corrupt-manifest non-deletion, failed-build preservation, exact-digest refusal after user
replacement, refusal of an intermediate symlink escape, and contraction of the custom writer's two
outputs to a built-in single output. The source-removal case produces identical output trees and
manifests with workers 1, 2 and 4.

---

## 20. Song/Video deployment-support files (`CP-044`)

Compiled artifacts and deployment artifacts are now different typed concepts. A
`ContentTypeWriter` still returns only complete CNB images. A processor may explicitly call
`AddDeploymentFile(source, outputPath)`, producing a bounded `ContentDeploymentFile` in the build
result. The call contains/canonicalizes the source, validates the generic output path, adds a
non-primary source to byte-hashed dependencies, deduplicates identical mappings, and rejects a
second source claiming the same destination. Runtime XREF registration remains independent.

Song/Video importer carriers retain the canonical native media source without reading its payload.
Their version-2 processors register that source at the final (possibly configured) stream XREF and
their unchanged version-1 writers encode exactly the prior metadata CNB. XNB Song and Video reuse
the same path: version-2 `XnbImporter` retains the already resolved external-media source and
`SongProcessor/2` or `XnbVideoProcessor/2` registers it. This is still:

```text
media/XNB source -> ordinary importer -> ordinary processor -> ordinary CNB writer
                                      \-> explicit deployment-support file
```

It is not a raw-media writer output, inferred XREF copy, embedded CNB chunk, alternate scheduler or
CMake-side copy.

CP-044's manifest version 4 added a sorted `deploymentFiles` list containing source-root-relative
source, output-root-relative path and SHA-256; CP-045's version 5 retains it unchanged.
Source/destination identity participates in the direct
fingerprint; source bytes already participate through the primary/source-file dependency. Support
paths share the coordinator's physical reservations with compiled outputs and other nodes. Skip
checks require both the CNB and support digests. CP-043 ownership inventories include support paths,
so configured-path changes, source removal and output-set contraction collect only the old
manifest-proven bytes.

Large media never enters a `std::vector`. The existing atomic helper now has a streaming copy entry
point over the exact same exclusive sibling temporary, checked close and POSIX/Windows replacement
primitive as CNB publication. Preparation copies each source to the private per-run staging tree in
1 MiB chunks and verifies the staged SHA-256; final publication repeats the bounded copy from that
immutable staged image. Repair of an output with otherwise-current graph topology verifies source
and final digests around direct atomic publication. Import/decode remains HEADLESS.

A normal directory build produces, for example, both `Music/theme.cnb` and
`Music/theme.ogg`. A configured XREF such as `Streaming/theme.ogg` changes both CNB metadata and the
support destination. If a single-file output root already contains the exact authored source at
that path, the compiler neither copies nor owns it; this preserves user source. Any different
deployment destination resolving inside the source root is rejected. Compiled/support path
collision, two-node support collision, traversal, symlink escape, missing source and tampered old
ownership all fail before unsafe publication/deletion.

Artifact publication still is not a portable multi-file transaction. Support files publish after a
node's CNBs and the manifest publishes only after every node plus CP-043 collection succeeds. A
support-copy failure retains the previous manifest and old support file; already published complete
CNBs are repaired/verified on the next run. Tests force that failure through a locked deployment
directory and prove recovery/no-op. Direct Song/Video and XNB media routes, configured rename,
tamper repair, removal GC, manual `.cnb` survival, API containment/conflict checks and atomic-copy
cleanup are covered. The source-removal worker 1/2/4 oracle includes a deployed Song and requires
byte-identical final trees/manifests.

A representative 64 MiB Song support-file run in the HEADLESS Debug build measured 4.55 seconds
for a cold hash/stage/publish build and 2.43 seconds for a digest-verifying no-op, with peak RSS of
34 MiB in both cases. This is a bounded-memory implementation check on the current host, not a CI
performance threshold; the existing 1 MiB buffer keeps memory independent of media size.

---

## 21. Writer/schema/codec fingerprint contract (`CP-045`)

The audit confirmed a real skip-path gap. `ContentComponentIdentity` was explicit and stable, but
one writer identity stood in for three independent facts: the writer adapter, native asset schema,
and codec implementation. A custom writer that changed its schema or same-schema encoding without
bumping the broad component version left the previous manifest internally self-consistent and
eligible for `SKIP`. The output's type ID was recorded only after `Write()`, so it could not describe
the current implementation on a path deliberately avoiding `Write()`.

Each `ContentTypeWriter` now returns a nonempty, strictly sorted set of
`ContentWriterSchemaIdentity` values before writing:

| Field | Meaning | When it changes |
|---|---|---|
| writer `ContentComponentIdentity` | adapter selection/orchestration and output-set behavior | any content-affecting writer behavior |
| `assetTypeId` + `assetTypeName` | native runtime dispatch identity; the name disambiguates custom-ID collisions | the canonical asset type changes |
| `assetSchemaVersion` | native asset wire schema | the decoder needs a new schema interpretation |
| codec name/version | authoritative encoder semantics within that schema | bytes or decoded meaning can change without a schema change |

The core refuses zero/empty fields, duplicate or unordered asset ID/name pairs, more than 256
declarations, and primary or additional writer outputs whose returned ID/name was not declared.
This is an explicit trusted extension contract, not reflection: neither `typeid`, compiler RTTI
names nor an invocation of `Write()` participates in route preflight. The custom writer remains
responsible for declaring what its authoritative codec actually writes. The custom integration
oracle parses the resulting CNB and pins schema/type metadata; the built-in codec round trips pin
the same relationship for CNA writers.

Manifest version 5 stores the complete sorted declaration and each owned output's actual selected
ID/name/schema. All fields enter the length-prefixed direct fingerprint. Route preflight resolves
the current writer and compares its declaration with the prior manifest, so independently changing
the asset ID, canonical name, schema version or codec version forces `BUILD` even if the writer's
own component version remains unchanged. An unchanged declaration retains `SKIP`. Manifest
versions 1–4 rebuild without migration because none could prove the full current writer contract.

All ten built-in writers declare their existing schema-1 `Encode*ToCnb()` route with codec version
1. These are new cache identities only: no frozen container/schema constant, chunk, CRC, encoder,
golden vector or output byte was modified.

---

## 22. glTF Model/generated-child policy (`CP-046`)

The audit began with real products from the shared `ConvertGltfToCnj()` implementation rather than
an imagined multi-output design. It found that `GltfImporter` treated every generated CNJ document
as if it were a Model: a normal animated file therefore failed merely because its standalone clip
documents made `documents.size() > 1`. A default scene containing static geometry plus a skin, or
two skins, genuinely produces several Model documents and failed through the same imprecise check.
Extracted images survived only as temporary loose PNG/JPEG files while the Model retained an XREF
to their temporary source-stem name.

| glTF semantic/product | Existing native representation | CP-046 policy |
|---|---|---|
| declared default scene | one scene graph in Model schema 1 | unchanged: import it only |
| no declared default | first scene | unchanged and deterministic |
| no scenes array | root-node fallback | unchanged |
| other scenes | no place in one Model's selected scene | not emitted or silently selected |
| one static/skin group | primary `CnbModelData` | default exact path |
| several skin/static groups | several canonical Model CNJs | explicit generated-child mode; lexicographically first Model is primary |
| animation clips in a Model | embedded schema-1 animations | always retained in the primary/group Model |
| standalone clip documents | schema-1 `AnimationClip` | optional generated children; semantic fields compared with embedded clips |
| external image URI | source dependency plus extracted image | optional native Texture2D child |
| data URI / buffer-view image | extracted image with no authored image file | optional native Texture2D child |
| vertex/index/skeleton/morph sidecars | absorbed Model bytes | remain internal; never child assets |
| material/effect state | Model schema-1 part/material fields | remains in Model; no extraction |
| generated output ownership | bounded additional outputs | same node, publisher, manifest and CP-043 GC |

`ModelProcessor` accepts one new boolean, `generateChildAssets`, only for glTF imports. It is off by
default. A single-group animated source now builds by ignoring only the redundant standalone clip
documents; the clips already embedded in its Model are preserved, and its primary CNB remains
byte-identical to `cna_tool_gltf_to_cnb`. A true multi-Model source still fails unless the option is
true, so no arbitrary group silently wins.

When enabled, the processor builds every Model through `BuildCnbModelFromCnj`, parses generated
clips through shared `ReadCnjAnimationClip`, and decodes each referenced generated image through
`DecodeImportedImage` plus the same parameter-free `BuildCnbTexture2DData` core used by
`TextureProcessor`. `ModelContentWriter` declares the three frozen schema/codec identities and
delegates to `EncodeModelToCnb`, `EncodeAnimationClipToCnb`, or `EncodeTexture2DToCnb`. This is one
canonical glTF parser and the existing processors/codecs, not a second importer or wire writer.

Child logical names replace the canonical source-stem prefix with the configured primary logical
name. Texture XREFs are remapped to those names only in the opt-in mode, making the resulting tree
deployable and avoiding a temporary name leaking into runtime resolution. Default mode does not
remap and therefore preserves its established Model bytes. Every generated file must be a regular
non-symlink child of the owned intermediate root. Unsafe names fail the normal CNB logical-name
check; generated children cannot shadow discovered primaries because the existing physical
reservation pass sees the complete result before publication.

The shared converter now also rejects two group names or two animation names that sanitize to the
same filename. Previously the later conversion overwrote the earlier file and document de-duplication
could make the loss look like a legitimate single output. Rejection occurs during import, before a
Model can be selected or published.

Independent graph nodes were rejected for this slice. Model schema 1 embeds clips, so extracting a
clip does not remove it from the Model fingerprint or rebuild. Texture pixels are external at
runtime, but the canonical glTF conversion still decides texture identity/material references as
part of the Model. A generated-source scheduling API would add a second ownership lifetime without
demonstrated rebuild savings. The bounded children instead share one direct/effective fingerprint,
atomic staging set, manifest owner, and failure outcome. A successful option contraction removes
only old manifest-owned children through CP-043; a failed rebuild retains the old manifest/content.

Real corpus tests cover one animated Model with two clips, a static-plus-skin two-Model source,
external and data-URI textures, collision rejection, semantic child decoding, default producer
byte equality, cold/no-op builds, successful child-set contraction, and byte-identical output trees
and manifests under workers 1, 2, and 4. No container, asset schema, chunk, CRC, golden vector, or
default direct-glTF byte changed.

---

## 23. MonoGame raw-LZ4 XNB decoding (`CP-047`)

The format audit used MonoGame's own `ContentWriter.WriteCompressedStream`, runtime
`ContentManager.GetContentReaderFromXnb`, and `Lz4DecoderStream`. Flag `0x40` has the same outer
layout as compressed XNB generally: the 10-byte header is followed by a little-endian declared
decompressed size. Every remaining byte is one raw LZ4 block. There is no frame magic, block-size
table, checksum, dictionary ID, concatenation, or LZX-style 32 KiB framing.

The implementation therefore adds one narrow `DecompressXnbLz4Payload` beside the existing LZX
function. It decodes raw sequences directly into the exact declared output allocation and rejects:

* non-positive/over-limit compressed sizes and negative/over-limit decompressed sizes;
* truncated extended literal or match lengths, literal input overruns, and output overruns;
* truncated, zero, or pre-history 16-bit little-endian match offsets;
* match output overruns, trailing undecoded input, and final-size mismatches.

The shared `XnbReadLimits` cap is checked before allocation, so output work and memory remain
bounded by the same 256 MiB default used for LZX. Match copying is bytewise to preserve LZ4's
required overlapping-copy semantics. Runtime `ContentManager` and canonical headless decoding call
this same function; neither initializes a graphics/audio device merely to decompress. Both bits
set still map to `XnbCompression::Unknown` and fail before either decoder.

No system or fetched runtime dependency was added. The system here has upstream liblz4 1.10.0 at
runtime but no development headers, and relying on it would make support host-dependent; vendoring
the full optimizing encoder/decoder was disproportionate for the six raw decode operations CNA
needs. Independence is retained in the oracle: the exact body of MonoGame's externally-produced
`white-1.xnb` was compressed by upstream `LZ4_compress_HC` and wrapped according to MonoGame's
writer. CNA's decoder reproduces the original body byte for byte.

The fixture loads through the ordinary runtime Texture2D reader and through `XnbImporter` to native
Texture2D CNB; the runtime XNB and transcoded-CNB pixels match. Unit tests cover each malformed
token/length/offset/extent class and decompression limits, while the whole-container fuzzer adds
1,500 deterministic mutations of the LZ4 container. This changes no CNB container, schema, chunk,
CRC, encoder, golden vector, or output byte.

A 125-test normal XNB/LZX/LZ4/runtime/pipeline selection passes. The focused nine-test
runtime/compiler/decoder/fuzz selection also passes under combined ASan+UBSan and under TSan with
`halt_on_error=1`; no sanitizer report occurred. The sanitizer build had to set
`ASAN_OPTIONS=detect_leaks=0` for build-time content generation because this runner's `ptrace`
environment makes LeakSanitizer abort, so no LSan coverage is claimed.

---

## 24. Windows content-tool entry points (`CP-048`)

The Windows CLI design was already correct at the source boundary: both the stock and custom
compiler front ends define `wmain(int, wchar_t**)` and construct `std::filesystem::path` directly
from each wide argument. The defect was target-local. MinGW's default console startup object seeks
`main`/`WinMain`; unlike MSVC, it requires `-municode` to select the CRT path that invokes `wmain`.
`ToolContentPipeline.cmake` now adds that private link option to exactly those two executables when
`MINGW` is true. It does not affect the Linux `main` branch, libraries, unrelated tools, or MSVC.

The x86_64-w64-mingw32 configuration compiles and links `cna-content.exe` and
`cna_custom_content_compiler_example.exe`; both PE files export the expected `wmain` symbol and
their Ninja link edges contain `-municode`. Wine 10.0 then exercises more than process startup:

* the stock tool builds a real CNJ Curve through importer, processor, writer, staging and atomic
  publication using `Z:/.../Zażółć/曲線/curve.cnb` as its native output path;
* that 308-byte Windows-target CNB has SHA-256
  `49107687976e9087be79fae6790fbf33822d739d0b9f429b16541ae26f773145` and is byte-identical to
  the Linux tool's output for the same source;
* the custom compiler builds its configured greeting source and publishes both its primary and
  generated reply outputs under Wine.

The ordinary Linux stock/custom compiler targets and corresponding fixture builds also pass after
the CMake change. This evidence is specifically MinGW compile/link plus Wine execution. It is not a
native Windows run and provides no MSVC result; those two platform claims remain open.

---

## 25. Manifest-owned clean and output-root serialization (`CP-049`)

The post-backlog review selected a clean command because CP-043 now provides a strict ownership
proof and users otherwise have to delete an output tree indiscriminately. The implementation adds
only this narrow syntax:

```text
cna-content clean <output-directory> [--quiet]
```

Clean parses one current valid regular, non-symlink manifest, then calls the existing
`CollectObsoleteOwnedOutputs` with an empty next manifest. That same function enumerates only the
sorted manifest ownership map, preflights every candidate before deleting any, requires real
non-symlink parents and regular files under the canonical root, and compares each SHA-256. It
therefore covers primary/additional CNBs and Song/Video deployment files without an extension or
directory scan. Manual files, authored source, modified former outputs, unknown files and
directories are never inferred as owned. A missing root or manifest is a successful no-op; a
corrupt/incompatible/symlinked manifest authorizes nothing.

The manifest is removed only after every eligible artifact deletion succeeds and after its text is
rechecked. A crash or removal failure before that point leaves the manifest as recovery evidence;
missing previously owned paths are accepted on the next clean. Clean deliberately does not remove
empty directories or the persistent coordination marker.

The audit also found that a clean command could not safely coexist with an independent active
publisher under the prior process-local scheduler. Both build and clean now claim
`.cna-content.lock` below the canonical output root for their complete lifetime, using the already
portable staging lease primitive (`flock` on POSIX, exclusive no-share `CreateFileW` on Windows).
The regular marker persists across clean exit and crashes; a later operation claims its released
OS lock. An active owner fails a second operation before manifest inspection/publication, while a
symlinked/non-regular/indeterminate marker is rejected rather than replaced.

Tests cover narrow syntax and missing-root no-op, compiled plus deployment cleanup, custom generated
child cleanup, manual/source survival, corrupt manifest, modified bytes, symlinked root/output/lock,
an actively held build/clean lease, quiet behavior, retry and the pre-existing CP-043 orphan cases.
All 53 CLI tests pass. The seven most relevant subprocess cases pass under combined ASan+UBSan
(`detect_leaks=0`, so no LSan claim) and TSan with `halt_on_error=1`. Both MinGW executables compile
and link after the change; Wine performs a real Windows-target build followed by clean while a
manual file and the persistent lease survive. The generated C-API inventory remains 9,332 symbols
with 501 experimental pipeline rows planned under `CBIND-117`, and all nine C-API consistency gates
pass; no C route/export/version or CNB definition/byte changed.

---

## 26. Post-XNB hardening and final verification (`CP-050`)

The final continuation checkpoint rebuilt the complete configured HEADLESS Debug tree, then ran
the unfiltered `CnaContentTests` binary. It executed 1,538 cases: 1,527 passed, eight were expected
opt-in/external-fixture skips, and exactly three failed at the already documented HEADLESS runtime
storage boundary (`TextureCube` face upload twice and `Texture3D` volume upload once). Repeating the
same boundary with only those three non-pipeline cases excluded passed 1,527 and skipped eight.
Independent category gates make the result attributable: CNB passed 371/371, CNJ passed 137 with
two fixture skips, the XNB pipeline/runtime-reader selection passed 221 with four HEADLESS skips,
and glTF/Model passed 608 with three external-fixture skips. All 11 frozen CNB golden vectors pass.

A rebuilt O0 HEADLESS configuration with combined AddressSanitizer and UndefinedBehaviorSanitizer
ran its broader 1,545-case boundary: 1,537 passed and eight skipped with halt-on-first-error and no
report. LeakSanitizer itself again aborted compiler subprocesses under the runner's `ptrace`, so the
successful build and run used `detect_leaks=0` and provide no LSan evidence. A rebuilt O1
ThreadSanitizer tree ran the complete configuration/manifest/CMake/CLI/core/custom concurrency
boundary: 91 passed and only the default-disabled >2 GiB I/O case skipped, with no TSan report. That
sparse-file case passed separately in 30.0 seconds in the normal tree. Both CMake integration
fixtures and all nine C-API inventory/compatibility/release gates pass; the inventory remains 549
headers and 9,332 symbols (8,352 implemented, 15 partial, 501 planned, 464 not applicable).

The repository benchmark regenerated 128 mixed PNG/WAV/glTF/CNJ nodes and a 97-node shared graph,
alternated workers 1 and 4 for two samples each, and rejected any complete-tree difference. Median
speedups on this run were 2.545x cold, 1.938x no-op, 1.917x after one image change and 1.215x after a
shared dependency change. A separate 1,024-Texture2D output tree contained about 480 MiB of compiled
data; four-worker build took 14.42 seconds and manifest/digest-proven clean took 7.05 seconds with a
41 MiB maximum resident set, leaving only `.cna-content.lock`. These are host-specific engineering
measurements, not performance thresholds. No private staging session remained after verification.

CP-048/CP-049 already rebuilt both MinGW executables and exercised stock/custom compilation plus
clean under Wine 10, including a non-ASCII native path and a Curve CNB byte-identical to Linux.
That is cross-compile and Wine execution evidence, not native Windows or MSVC evidence. The final
review also evaluated the proposed `--explain` UX: current manifests can reliably classify broad
direct/effective/output-integrity changes, but not every field-level direct cause without running
components or persisting a new stable reason breakdown. The ordinary log already reports each
`BUILD`/`SKIP`; an explain mode remains future work rather than shipping a heuristic reason.

No changed path is a frozen CNB codec, container/schema specification, asset/chunk identifier, CRC
implementation, or existing golden asset. Default glTF mode remains byte-compatible; optional
generated children remain explicitly configured. No pipeline C route/export/version was added.
Local `next` advanced from the verified starting `ffd32388b220ddd47669bbd90a794100afa6fd1a`
to `91be3f7a8f5cb3fe343f78adb9034aad7b0cb6a7` through two unrelated `SAMPLE-001` commits during
the session; this branch deliberately did not absorb them and nothing was merged into `next` or
pushed.

---

## 27. Remaining post-CP-050 gap audit (`CP-051`)

The continuation started by inspecting branch topology rather than trusting reported commits.
`next` was `45515bb0a122d582e87d9b4cb48b170cd9b6249a`; the CP-050 feature head was
`dd19589f7af0f63f328cfab4c73d81f995894065`; their merge base remained
`ffd32388b220ddd47669bbd90a794100afa6fd1a`. A new `content-pipeline-final` branch preserved the
CP history and merged `next` normally at `5671ebb54`. No commit was rewritten and nothing was
merged back or pushed.

The highest-value gap is precise incremental reasoning. Manifest v5 already persists component
identities, processor parameters, dependency identities, writer schema/codec declarations,
logical outputs, deployment definitions and final artifact digests. It does not persist the byte
contributions of the primary source and file dependencies as separate domains, nor the effective
fingerprints received from content-build dependencies. Those values are collapsed into
`directFingerprint` and `fingerprint`. The CLI then reduces route freshness, effective freshness
and artifact integrity to booleans, while an incompatible/corrupt manifest becomes the same empty
manifest in memory. A detailed explanation layered over those booleans would be a heuristic.
CP-052 therefore owns a real v6 reason-state decomposition; CP-053 consumes it through structured
decision values before formatting human output.

The other gaps remain subordinate and evidence-gated:

* Strict configuration has no external-root concept, and every dependency is normalized beneath
  `sourceRoot`. CP-054/CP-055 must introduce named read-only roots, stable alias-relative identity
  and explicit non-overlap/containment rules together; an absolute-path exception is not viable.
* Frozen Model schema 1 cannot preserve the broader XNB graph listed in section 17. CP-056 first
  audits real runtime construction/rendering capability, effect carriers, sharing and tags.
  Schema 2 implementation is conditional rather than presumed.
* CP-046's generated glTF children intentionally share a node because clips remain embedded and
  texture identity affects Model XREFs. CP-060 requires measurements before changing ownership or
  scheduling.
* Current processors expose no generic platform switch. CP-061 looks for a concrete output policy
  and rejects a profile API if none exists.
* This Linux environment preserves the existing MinGW/Wine evidence but does not by itself prove
  native MSVC execution. CP-062 may improve an existing CI gate only when repository conventions
  and an executable test boundary justify it.

The audit found no reason to change CNB, add a stable C route, duplicate the scheduler/publisher,
or weaken the cooperative-lock statement. The frozen schema-1 definitions and eleven golden
vectors are untouched.

---

## 28. Persisted rebuild-reason state (`CP-052`)

Manifest version 6 adds one `fingerprintState` object per build node. It contains nine lowercase
SHA-256 values, each computed from canonical length-prefixed fields with an explicit domain tag
and manifest-version identity:

| Domain | Exact semantic input |
|---|---|
| `primarySourceBytes` | bytes of the one contained primary source |
| `sourceDependencySet` | sorted non-primary source/generated dependency kind and root-relative identity |
| `sourceDependencyBytes` | the same sorted identities plus each streamed file digest |
| `contentDependencySet` | sorted content-build node IDs |
| `contentDependencyFingerprints` | those node IDs plus their resolved effective fingerprints |
| `processorParameters` | sorted name, stable value type and canonical value text |
| `writerSchemas` | sorted asset ID/schema/type plus codec name/version declarations |
| `outputDefinitions` | sorted logical/path/type/schema output definitions plus runtime XREF definitions |
| `deploymentDefinitions` | sorted contained source and output-root-relative deployment paths |

The component identities and logical node/source identities remain directly inspectable fields and
are compared as such; duplicating them as additional hashes would add no information. Published
compiled/deployment byte digests also remain their existing per-artifact records so a later
decision can name the affected root-relative path. Human prose, timestamps, native absolute paths,
RTTI identities and temporary paths are absent.

`directFingerprint` now combines the direct domains with logical identity, stable importer/
processor/writer identities and the CNB container version. `fingerprint` combines that direct hash
with `contentDependencyFingerprints`. The preparation and scheduler paths refresh the persisted
domains at the same point they compute their aggregate hashes; there is no second explanation-only
hashing pass and no second cache contract.

The parser accepts exactly version 6. Versions 1 through 5, malformed documents and future versions
return no trusted ownership to the build coordinator. A first v6 build therefore rebuilds safely
and replaces the manifest atomically, but an old-only output whose source disappeared is retained:
an incompatible manifest cannot authorize CP-043 collection. A dedicated transition test rewrites
a valid two-node manifest to version 5, removes one source, and proves the old output survives while
the remaining node rebuilds into a valid v6 manifest.

The focused manifest/migration selection passed 14 tests plus the expected disabled large-file
case. The complete configuration, CMake integration, CLI and custom-component boundary then passed
66/66, including workers 1/2/4 tree/manifest identity, graph/cycle behavior, atomic recovery,
deployment ownership, clean and GC. The implementation changes no CNB encoder/decoder, schema,
container definition, asset/chunk ID or existing golden file. The canonical C API inventory now
records 9,346 symbols; all 515 experimental pipeline rows remain planned under the existing
`CBIND-117`, with no C route, export or ABI-version change. The 15-case affected ASan+UBSan
selection passed 14 plus the disabled large-file test with halt-on-error and `detect_leaks=0`
(the runner's existing ptrace limitation prevents an LSan claim). The same focused concurrency
boundary passed under TSan. An accidentally broad TSan selection reached the known PulseAudio
runtime boundary and reported a libpulse race; it is outside the pipeline and the corrected
manifest/scheduler selection remained green.

---

## 29. Trustworthy incremental explanations (`CP-053`)

The compiler now represents an incremental outcome before formatting it as
`ContentBuildDecision` containing sorted `ContentBuildReason` values. These types remain private
to the tool coordinator: the human log is not the source of truth, but this task also does not
prematurely establish a stable JSON protocol or add experimental C++ declarations. The reason
codes cover manifest unavailable/incompatible/corrupt, new asset, logical identity, each component
identity, writer schema and codec identities, primary bytes, source-dependency set and bytes,
processor parameters, content-dependency set and effective fingerprints, compiled/XREF and
deployment definition sets, missing/tampered/unsafe compiled or deployment artifacts, defensive
unknown direct/effective fingerprint changes, and the intact unchanged state.

Manifest loading preserves its missing/current/incompatible/corrupt state instead of collapsing
every untrusted file into one empty manifest. A current entry is compared through its inspectable
component fields and CP-052 domains. Effective dependency inputs and published artifact digests
are assessed after the graph becomes ready. The ordinary valid skip remains fast: it does not rerun
an importer or processor to rediscover history, and enabling explanations adds only structured
comparison/formatting to work the skip path already performs. Source and published-path detail is
root-relative; temporary and absolute host paths are not exposed.

`cna-content build ... --explain` renders each reason below the existing `BUILD` or `SKIP` event.
Normal output remains byte-for-byte unchanged when the flag is absent. `--quiet` wins over
`--explain` for successful events and the summary but does not hide errors; `clean --explain` is an
unknown-option failure. Sorted reason codes/details and the coordinator's existing ordered event
flush make complete cold, no-op and shared-dependency-change logs identical for workers 1, 2 and 4.

Tests prove new/unchanged assets, primary source changes, source-dependency set/bytes, content-edge
set/effective changes, typed parameters, importer/processor/writer versions, writer schema and
codec versions, logical rename, compiled and deployment missing/tamper cases, corruption, v5-to-v6
transition, and multi-output contraction. The normal 59-case CLI suite passes. The complete
12-manifest plus 59-CLI boundary passed under combined ASan+UBSan (70 pass, the expected opt-in
large-file case skipped, `detect_leaks=0` so no LSan claim). Eleven focused explain, artifact,
schema/codec, worker and multi-output cases passed under TSan with no report.

On the retained repository benchmark's 128 mixed-asset no-op tree, 20 alternating four-worker
runs measured a 0.275540-second median without the flag and 0.291001 seconds with it: 0.015460
seconds or 5.61% on this host. This is engineering evidence rather than a threshold. The task
changes no CNB definition, encoder, output byte, atomic publisher, public C++ inventory, C route,
export or ABI version.

---

## 30. Named external source capability design (`CP-054`)

The existing path flow has three distinct representations. A component receives a canonical native
path from its context; `ContentBuildResult` currently carries that path until the manifest builder
normalizes it; the manifest then hashes one source-root-relative UTF-8 identity. Deployment records
likewise carry a native read source but persist an output-relative ownership path. Clean, orphan GC
and staging scavenging consume only output ownership or private temporary-session metadata. The
capability design extends the read-side representation rather than granting any additional output
root.

The strict version-1 configuration syntax is:

```json
{
  "format": "CNA.ContentPipeline.Config",
  "version": 1,
  "sourceRoots": {
    "shared-textures": "../SharedTextures",
    "shared-audio": "/build-machine/content/CommonAudio"
  },
  "assets": {}
}
```

`sourceRoots` is optional and bounded to 32 entries. An alias is 1-64 ASCII characters matching
`[a-z][a-z0-9-]*`; JSON duplicates, case variants and noncanonical spellings are therefore rejected
rather than normalized. A physical path must be a non-empty native directory path. Relative values
are resolved against the primary source root; absolute values are permitted only here because this
object is the explicit machine-local capability mapping. The configuration file itself remains a
regular contained source-root file.

Before any component runs, the CLI and the embedding API canonicalize every physical root and
require it to exist as a directory. No pair among the primary source root and external roots may be
equal or nested in either direction; external roots may not equal/nest each other. The output root
may not equal or nest an external root in either direction. Duplicate canonical physical roots,
missing roots and roots naming files fail the build. These conservative overlap rules make root
selection unique and prevent authored reads from ever sharing a publication/clean namespace.

An authored dependency explicitly selects a capability with
`@<alias>/<normalized-root-relative-path>`. The `@` form is reserved; unknown aliases, empty paths,
backslashes, absolute/rooted remainders, `.`/`..`, non-normal spelling and canonical symlink escape
fail. The existing unqualified form remains relative to the primary asset's directory and retains
the current source-root-only policy. Resolution chooses the named map entry directly; it never
searches all configured roots. The same resolver is available in importer and processor contexts,
so CNJ/custom paths that already use those services can opt in without weakening unrelated
formats. glTF is explicitly outside this initial claim because its shared converter validates and
opens external URIs before returning dependencies to the context.

`ContentBuildRequest` carries the native alias map for one build invocation. A transient
`ContentDependency` pairs its canonical native path with the selected alias. Manifest version 7
persists source-file dependencies as separate `sourceRoot` alias plus `identity` relative path;
primary/generated/content-build dependencies require an empty alias. Deployment records use the
same pair. All source-dependency set/byte and deployment-definition fingerprints include the alias
and relative identity. Fingerprint file opens resolve exactly that pair through the current
request-local map. Absolute physical mappings, configuration paths, temporary paths and native
separator spellings are never serialized or included in CNB bytes.

Consequently, moving `shared-textures` to another checkout path with identical relative bytes
retains the semantic identity and can skip. Changing the alias, selected alias-relative path,
dependency set or bytes invalidates the node. A missing alias/root fails instead of treating the
old fingerprint as current. Version 6 has no structured root selector and is rejected as
incompatible rather than being reinterpreted.

An external file can become a deployment-support source, but only if an importer/processor first
recorded that exact canonical file through the explicit alias-qualified dependency resolver.
`AddDeploymentFile` does not search capabilities or infer permission from containment. It carries
the recorded alias/relative identity into the manifest, while its destination remains below the
ordinary output root and uses the sole atomic publisher. Clean and orphan GC still inspect only
manifest-proven output paths and digests; the manifest source identity is never a deletion target.
Staging scavenging remains confined to its versioned private temporary parent.

CP-055 must prove configuration parsing and round-trip identity, same-byte physical remapping,
alias/byte invalidation, explicit deployment, workers 1/4 determinism and the full negative matrix:
default rejection, unknown/duplicate/colliding aliases and roots, traversal/absolute/backslash/
symlink escape, missing/file roots, every source/external/output overlap direction, and external
sentinel survival through build failure, orphan collection, clean and scavenging. No C binding or
CNB schema change follows from this experimental build-time capability.

---

## 31. Named external source capabilities (`CP-055`)

CP-055 implements section 30's design without adding a second resolver, hasher, publisher, or
cleaner. The configuration parser accepts only the bounded alias map described above. The central
capability resolver canonicalizes all roots before discovery and is shared by the embedding
`Build()` path and the CLI. Importer and processor contexts select aliased roots explicitly;
deployment checks the exact already-recorded physical dependency rather than searching the
capabilities. Manifest v7 normalizes the transient native source into `sourceRoot` plus relative
`source`, and its source-dependency set/bytes and deployment-definition domains hash those stable
fields. A v6 manifest is incompatible cache state and cannot authorize deletion; a rebuilt v7
manifest retains the CP-052 reason domains and CP-053 explanations.

The implementation tests configuration parsing, deterministic manifest round trips with no
physical-path leak, same-byte remapping to a second checkout, external byte invalidation and
root-qualified CNJ source references. Negative coverage includes the default outside-root refusal,
unknown and duplicate aliases, duplicate or nested physical roots, missing/file roots, traversal,
absolute paths, repeated separators, backslashes, canonical symlink escape, and every source/
external/output equality or nesting direction. The custom compiler proves explicitly resolved
external deployment, workers 1/4 byte-identical trees/manifests, same-byte root remapping, source
invalidation, deployment contraction through manifest-owned GC, and clean. Sentinel files in each
external root survive every build, contraction and clean; the only destructive paths remain the
existing output publisher, manifest-proven orphan collector and private staging scavenger.

The normal configuration/manifest/core/CLI selection ran 101 cases: 100 passed and the explicit
>2 GiB hashing case remained skipped without its opt-in. The same 101-case boundary passed under
combined ASan+UBSan with `detect_leaks=0` (the runner still prevents an LSan claim); ten focused
capability, security, deployment and worker cases passed TSan. A 40-run alternating one-asset no-op
benchmark measured ordinary contained hashing at 0.028613 seconds median and named external hashing
at 0.028311 seconds on this host, a -0.303 ms difference within noise rather than measurable
overhead. The broad dummy-audio HEADLESS content binary ran 1,554 tests: 1,543 passed, eight
opt-in/external-fixture cases skipped, and exactly the three CP-050-documented renderer-only
TextureCube/Texture3D cases failed; the default host PulseAudio backend itself is unavailable in
this sandbox. MinGW-w64 also compiles and links the updated stock and custom Unicode-entry-point
compiler executables. The generated C-API inventory moves from 9,346 to 9,360 declarations and assigns all 14
new experimental C++ rows to existing future `CBIND-117`; no C route, export, ABI version, CNB
definition, schema, encoder, or frozen byte changes.

The durable invariants are:

* a missing `sourceRoots` object preserves the original source-root-only policy;
* every outside read explicitly selects exactly one bounded alias;
* the request-local canonical directory grants reads only, while alias plus relative path is the
  persisted and fingerprinted semantic identity;
* all source, external, and output roots are pairwise non-overlapping;
* an external deployment source must first be the exact explicitly resolved dependency; and
* publication, atomicity, ownership, orphan collection, clean and staging recovery remain confined
  to the output/private-staging namespaces they already owned.

glTF remains intentionally outside this claim because its shared converter opens authored URIs
before the pipeline context observes them. Extending it would require a separately reviewed
resolver callback, not a containment exception.

---

## 32. Model schema-2 feasibility audit (`CP-056`)

This audit returns to the authoritative FNA readers rather than extrapolating from schema 1.
`ModelReader` serializes bone names/transforms plus explicit parent and child references; mesh
name, parent bone, exact `BoundingSphere` and tag; each part's `VertexOffset`, `NumVertices`,
`StartIndex`, `PrimitiveCount`, tag and three shared-resource references; an explicit root bone;
and the Model tag. `VertexBufferReader` retains a complete `VertexDeclaration` (stride plus every
element's offset, format, usage and usage index) and complete bytes. `IndexBufferReader` retains
the 16/32-bit choice and bytes. The five stock effect readers retain the following source state:

| Reader | Serialized state relevant to a native record |
|---|---|
| `BasicEffectReader` | Texture2D, diffuse/emissive/specular RGB, `SpecularPower`, alpha, vertex-colour enable |
| `SkinnedEffectReader` | Texture2D, weights per vertex, diffuse/emissive/specular RGB, `SpecularPower`, alpha |
| `DualTextureEffectReader` | two Texture2D references, diffuse RGB, alpha, vertex-colour enable |
| `AlphaTestEffectReader` | Texture2D, compare function, reference alpha, diffuse RGB, alpha, vertex-colour enable |
| `EnvironmentMapEffectReader` | Texture2D, TextureCube, amount, specular RGB, Fresnel, diffuse/emissive RGB, alpha |

These are content properties; World/View/Projection, fog, lighting enablement and palettes remain
constructor/draw-time state exactly as they do after direct XNB loading.

### 32.1 Runtime and carrier evidence

| Semantic | CNA evidence | Schema-2 conclusion |
|---|---|---|
| arbitrary XNA vertex declaration | `VertexDeclaration(stride, elements)`, `VertexBuffer`'s declaration constructor and raw upload already carry all twelve `VertexElementFormat` and thirteen `VertexElementUsage` values | preserve stable CNB-owned IDs, exact order and stride; validate nonempty, in-stride and non-overlapping elements |
| declaration rendering | declaration-translating renderers consume the exact elements; seven stride-table renderers run the shared fidelity guard and explicitly refuse an unrepresentable declaration before submission | schema may preserve declarations; it must not promise every renderer can draw every XNA layout, and must retain the loud runtime capability failure |
| shared whole buffers | runtime XNB fixups already give multiple parts the same `VertexBuffer*`/`IndexBuffer*`; `ModelResources` can own one object per table row | use document-local resource indices and one payload chunk per buffer, never pointer or XNB fixup identity |
| part windows | `ModelMeshPart` stores all four fields and `ModelMesh::Draw` passes them to `DrawIndexedPrimitives`; the graphics boundary validates the declared vertex and index windows | store all four signed-XNA values as bounded nonnegative `u32` after validation; XNB Model topology remains TriangleList |
| mesh bounds | public setter and getter already preserve the serialized sphere | store center/radius floats and reject non-finite/negative values; never recompute in schema 2 |
| explicit root and root transform | the five-argument CNAEXT `Model` constructor takes `rootBoneIndex`; every `ModelBone` transform is settable | store root index explicitly and apply every transform, including the root's |
| Basic/Skinned/Dual/AlphaTest/EnvironmentMap effects | CNA implements all five classes and production XNB readers already construct and assign every serialized field | use discriminated complete stock records and a shared effect table; no generic `Effect` record |
| texture references | CNB's container `XREF` is a validated logical ContentManager identity; Texture2D and TextureCube have frozen asset IDs | stock records address typed XREF rows; do not embed textures or physical paths |
| resource sharing | buffer/effect identity and mutation are observable, and direct XNB preserves them | table indices preserve same-versus-distinct identity within one Model document |
| tags | XNA accepts arbitrary CLR objects; CNA runtime readers can retain some registered `System::Object` values, but there is no bounded native wire contract common to those graphs | schema 2 initially admits null Model/Mesh/MeshPart tags only; every non-null tag remains an explicit transcode failure |
| custom effects | direct XNB may resolve registered Effect readers, but no stable CNA-native custom effect graph or frozen `Effect.cnb` schema exists | reject every reader outside the five complete stock readers; do not serialize bytecode, XNB reader names or CLR graphs |

The declaration boundary is a capability fact, not a reason to discard the format. The direct XNB
runtime already constructs the exact Position+Normal stride-24 declaration in MonoGame's
`BlenderDefaultCube.xnb`; renderers capable of declaration translation can consume it, while an
inference-only renderer refuses rather than silently reading Normal bytes as Color/UV. A schema-2
runtime adapter can use the same public declaration/buffer path. The compiler itself remains
headless and does not initialize a device or renderer.

### 32.2 Demonstrated source/runtime use case

The repository's independently described MonoGame fixture is outside schema 1 for three
observable reasons and inside the bounded design above:

* stride 24 contains `Position Vector3 @ 0` plus `Normal Vector3 @ 12`, rather than schema 1's
  inferred Position/Color/UV layout;
* `BasicEffect.SpecularPower` is `9.607843399047852`, rather than schema 1's reconstructed 16; and
* its authored mesh sphere must be retained as serialized rather than accepted only when equal to
  a schema-1 recomputation.

The direct XNB runtime test already proves bone/root/mesh/part geometry, the exact declaration and
material values. It therefore supplies an external producer fixture for the required future
runtime-XNB versus native-CNB semantic comparison, not merely a production-code-generated vector.
Synthetic fixtures can separately prove sharing, nonzero part windows, other roots and each stock
effect without weakening that independent case.

### 32.3 Compatibility and implementation boundary

The coherent design is a separate `CnbModelDataV2` CPU carrier and separate codec. It uses the
existing Model asset type ID with schema version 2 and does not add optional fields to or reinterpret
the schema-1 carrier. The schema-1 encoder/decoder and its chunk meanings stay source-compatible and
byte-identical. Runtime dispatch selects a decoder from the header schema version; it does not make
the existing schema-1 decoder accept version 2.

Pipeline routing must also remain explicit. A schema-1-compatible XNB Model continues through the
existing `ImportedModelDocument -> ModelProcessor -> ModelContentWriter` route and emits schema 1.
The imported/processed carrier is a schema-tagged C++ variant: the writer declares both exact Model
schema/codec tuples and every emitted output states its selected schema. This keeps one component
route without labeling schema-1 bytes as schema 2. PNG/WAV/CNJ/glTF and generated Model children
therefore never select schema 2 merely because it exists. Catching a schema-1 subset error and
blindly upgrading is insufficient: the schema-2 converter must revalidate the original canonical
graph and reject malformed windows, inconsistent bone links, non-null tags, unsupported effects
and unsafe XREFs on their own merits.

The wire format must bound and cross-check every header count, fixed-stride table size, string/XREF
index, declaration element range, buffer byte product, mesh-part slot, resource index, root/parent
index and draw window before allocation or GPU construction. It must validate selected indices
against the part's declared vertex window, preserve unused shared resources if the XNB graph
contains them, and allow distinct resource rows with equal bytes. All identities are document-local
ordinals; no pointer, RTTI name, XNB shared-resource number, assembly name or host path is semantic
output.

This proves a bounded schema 2 is architecturally justified. It does not authorize implementation
from prose alone: CP-057 owns the byte-exact table/chunk specification and malformed-input rules;
CP-058 requires an independent Python golden vector plus frozen-schema-1 byte regression before a
producer or runtime reader lands; CP-059 then broadens XNB support only to the proven matrix.

---

## 33. Model schema-2 specification (`CP-057`, frozen by `CP-058`)

This section was the implementation contract for CP-058 and is now the engineering record for the
frozen public wire definition specified normatively in `docs/cnb-format.md` §11.2–11.5.
The file remains CNB container 1.0, built-in asset type `Model` (`5`), asset schema version `2`.
Every integer and IEEE-754 float uses the container's little-endian primitive encoding. Every
schema chunk below is mandatory, singleton except repeated `MVTX`/`MIDX`, and emitted in the exact
order shown. Table chunks have alignment 4; raw buffer chunks have alignment 16.

```text
container CMET, optional XREF (container-owned canonical positions)
M2HD  header and counts
M2ST  string table
M2BN  bone table
M2MS  mesh table
M2PT  part table
M2VD  vertex-declaration and vertex-element tables
M2VR  vertex-buffer resource table
MVTX  one raw chunk per M2VR row, in row order
M2IR  index-buffer resource table
MIDX  one raw chunk per M2IR row, in row order
M2FX  stock-effect resource table
```

Schema-1 chunk IDs and meanings are not changed. Reusing `MVTX`/`MIDX` under schema 2 is safe
because the asset schema version selects the interpretation before any schema chunk is opened;
all descriptor-table IDs are new and cannot be mistaken for schema-1 rows.

### 33.1 Header and graph tables

`M2HD` is exactly 64 bytes, sixteen `u32` values:

| Byte | Field | Rule |
|---:|---|---|
| 0 | flags | zero; no feature bit is defined |
| 4 | boneCount | at least one, within `maxArrayElementCount` |
| 8 | meshCount | bounded count; zero is allowed |
| 12 | partCount | bounded count; may be zero only when every mesh is empty |
| 16 | declarationCount | bounded; zero iff `vertexBufferCount` is zero |
| 20 | elementCount | bounded total across declarations |
| 24 | vertexBufferCount | bounded, exact number of `MVTX` chunks |
| 28 | indexBufferCount | bounded, exact number of `MIDX` chunks |
| 32 | effectCount | bounded stock-effect resource count |
| 36 | rootBoneIndex | less than `boneCount`, and that bone has parent `-1` |
| 40..63 | reserved[6] | all zero |

The reader rejects a part when any of the three resource-table counts is zero, so an empty Model
may have zero buffers/effects while a renderable Model cannot.

`M2ST` is `u32 stringCount`, followed by that many container strings (`u32 byteLength` then exact
UTF-8 bytes, no terminator). Names are valid UTF-8 and may be empty because XNA permits unnamed
bones/meshes. The producer interns names by first occurrence while traversing bones then meshes;
duplicate string bytes have one row. No XREF name is duplicated into this table.

`M2BN` contains `boneCount` 72-byte rows:

```text
u32 nameStringIndex
i32 parentBoneIndex                 // -1 or an earlier row
f32 transform[16]                   // M11..M44
```

Every string index is in range and every matrix component is finite. Parent-before-child is
required because XNA/FNA and CNA both calculate absolute transforms in one ascending pass. More
than one parentless bone is representable; `rootBoneIndex` states which one is `Model.Root`.
Children are reconstructed in ascending row order from parents. The XNB converter accepts only a
source whose explicit child arrays equal that reconstruction, so no accepted public relationship
changes.

`M2MS` contains `meshCount` 32-byte rows:

```text
u32 nameStringIndex
i32 parentBoneIndex
f32 boundingSphereCenter[3]
f32 boundingSphereRadius
u32 firstPart
u32 partCount
```

The parent is in range. Sphere values are finite and radius is nonnegative. Mesh part ranges form
one canonical contiguous partition of `M2PT`: row 0 starts at part 0, each later row starts where
the preceding row ended, and the final end equals the header's `partCount`. This matches the XNA
object shape—one `ModelMeshPart` has one owning `ModelMesh`—without a redundant slot table.

`M2PT` contains `partCount` 32-byte rows:

```text
u32 vertexOffset
u32 numVertices
u32 startIndex
u32 primitiveCount
u32 vertexBufferResource
u32 indexBufferResource
u32 effectResource
u32 reserved                         // zero
```

All three resource indices are in range. `numVertices` and `primitiveCount` are nonzero. Checked
addition requires `vertexOffset + numVertices <= vertexBuffer.vertexCount`; checked multiplication
and addition require `startIndex + primitiveCount * 3 <= indexBuffer.indexCount`. ModelReader
parts are always TriangleList. Every 16/32-bit value in that selected index window must be less
than `numVertices`; `vertexOffset + index` is therefore inside the declared vertex buffer. Bytes
and resources outside a part's selected windows remain intact and may be selected by another part.

Model, mesh and part tags have no row or flag. Their only schema-2 value is null. An XNB source
with a non-null tag fails before encoding; an unknown chunk cannot be used to smuggle tag data
because the container already rejects unknown mandatory chunks and the schema ignores optional
unknown chunks rather than attaching them to runtime objects.

### 33.2 Vertex declarations and buffer resources

`M2VD` consists first of `declarationCount` 16-byte declaration rows, then exactly `elementCount`
20-byte element rows:

```text
// declaration row
u32 vertexStride                    // 1..4096
u32 firstElement
u32 elementCount                    // at least one
u32 reserved                        // zero

// element row
u32 offset
u32 formatId
u32 usageId
u32 usageIndex                      // 0..31
u32 reserved                        // zero
```

Declaration element ranges form a contiguous partition of the element rows. Element order is
preserved. `offset + FormatSize(formatId)` is checked and no greater than `vertexStride`; byte
ranges in one declaration may not overlap; and `(usageId, usageIndex)` pairs may not repeat.
Equal declarations may be interned by first vertex-buffer occurrence because declarations are
value objects, not shared mutable resources.

The following IDs are CNB-owned wire values, even though their initial numbers deliberately match
XNA for reviewability:

| `formatId` | Meaning | Bytes |
|---:|---|---:|
| 0 | Single | 4 |
| 1 | Vector2 | 8 |
| 2 | Vector3 | 12 |
| 3 | Vector4 | 16 |
| 4 | Color (normalized packed BGRA bytes) | 4 |
| 5 | Byte4 | 4 |
| 6 | Short2 | 4 |
| 7 | Short4 | 8 |
| 8 | NormalizedShort2 | 4 |
| 9 | NormalizedShort4 | 8 |
| 10 | HalfVector2 | 4 |
| 11 | HalfVector4 | 8 |

| `usageId` | Meaning | `usageId` | Meaning |
|---:|---|---:|---|
| 0 | Position | 7 | BlendWeight |
| 1 | Color | 8 | Depth |
| 2 | TextureCoordinate | 9 | Fog |
| 3 | Normal | 10 | PointSize |
| 4 | Binormal | 11 | Sample |
| 5 | Tangent | 12 | TessellateFactor |
| 6 | BlendIndices |  |  |

`M2VR` contains `vertexBufferCount` 16-byte rows:

```text
u32 declarationIndex
u32 vertexCount
u32 payloadOrdinal                  // must equal this row index
u32 reserved                        // zero
```

The declaration index is in range and `vertexCount` is nonzero. The corresponding `MVTX` logical
size must equal checked `vertexStride * vertexCount`; the codec neither repacks nor infers fields.

`M2IR` contains `indexBufferCount` 16-byte rows:

```text
u32 indexElementSize                // exactly 2 or 4
u32 indexCount
u32 payloadOrdinal                  // must equal this row index
u32 reserved                        // zero
```

`indexCount` is nonzero and the corresponding `MIDX` logical size is checked
`indexElementSize * indexCount`. Indices are little-endian unsigned values. Separate table rows
remain separate GPU resources even if their bytes match; multiple parts naming one row receive
the same runtime pointer. Unreferenced resources remain rows and are constructed, preserving the
native document's resource table rather than treating byte equality as identity.

### 33.3 Stock-effect resource table

`M2FX` contains `effectCount` fixed 96-byte rows. A fixed discriminated row makes every inactive
field provably canonical and prevents an implementation from retaining hidden per-reader data:

```text
u32 kind
u32 flags
u32 primaryTextureXref
u32 secondaryTextureXref
u32 cubeTextureXref
u32 integer0
u32 integer1
u32 reserved0
f32 vector0[3]
f32 vector1[3]
f32 vector2[3]
f32 scalar0
f32 scalar1
f32 scalar2
f32 scalar3
u32 reserved1[3]
```

An absent XREF is `0xFFFFFFFF`. Primary/secondary texture rows require XREF expected asset type
`Texture2D`; cube rows require `TextureCube`. Every referenced XREF index is in range. All floats
are finite, every reserved/inactive integer is zero, every inactive XREF is absent, every inactive
vector/scalar is positive zero, and flags contain only bits the selected kind defines. Active
interpretation is:

| kind | Record meaning |
|---:|---|
| 0 BasicEffect | flag bit 0 `VertexColorEnabled`; primary Texture2D; vector0 diffuse, vector1 emissive, vector2 specular; scalar0 `SpecularPower`, scalar1 alpha |
| 1 SkinnedEffect | no flags; primary Texture2D; integer0 weights per vertex (1, 2 or 4); vectors as Basic; scalar0 `SpecularPower`, scalar1 alpha |
| 2 DualTextureEffect | flag bit 0; primary and secondary Texture2D; vector0 diffuse; scalar0 alpha |
| 3 AlphaTestEffect | flag bit 0; primary Texture2D; integer0 compare ID 0..7 (`Always`, `Never`, `Less`, `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`); integer1 exact serialized `u32` reference-alpha bits; vector0 diffuse; scalar0 alpha |
| 4 EnvironmentMapEffect | no flags; primary Texture2D and cube TextureCube; vector0 diffuse, vector1 emissive, vector2 environment-map specular; scalar0 amount, scalar1 Fresnel factor, scalar2 alpha |

Effect table identity is observable: parts sharing a row get the same `Effect*`, distinct equal
rows stay distinct. The runtime constructs all effect rows once, loads each typed XREF through the
existing `ContentManager` cache, applies exactly the fields above, then attaches pointers to parts.
It creates no generic Effect, shader graph or reader table. Any reader identity outside these five
stock types is unsupported.

### 33.4 Determinism, dispatch and validation order

The encoder preserves source bone, mesh, part, resource and declaration-element order. It interns
strings and typed XREFs by first semantic occurrence; it never sorts graph objects or deduplicates
buffer/effect resources by bytes. The same CPU carrier and logical content name therefore produce
identical bytes. No native pointer, RTTI spelling, original XNB fixup number, physical path,
temporary path or timestamp enters the carrier.

The decoder first requires Model asset type 5 and schema version 2, rejects every unknown mandatory
chunk, proves singleton/repeated chunk cardinalities and `M2HD`, then validates table byte sizes
from checked count/stride products before resizing a vector. It validates strings, tables,
resource byte products, graph/resource indices and draw windows in that order. It does not create
GPU resources until the entire CPU document is valid. Aggregate counts and byte sizes remain under
the caller's `CnbReadLimits`; all additions/multiplications are checked in `u64` before conversion
to native sizes. Encoder and decoder apply the same semantic validator.

Runtime Model construction is separate from the schema-1 adapter: it creates all exact
`VertexDeclaration` values, then one vertex/index/effect object per resource row, then parts,
meshes and bones; applies every bone transform including the root; sets exact spheres; derives
children from parents; and calls the explicit-root Model constructor. A declaration-inference-only
renderer may subsequently reject a declaration through its existing capability guard. The loader
must not rewrite, infer or silently substitute a declaration to make that renderer accept it.

`DecodeModelFromCnb()` remains the exact schema-1 function. A new schema-2 decoder and CPU carrier
are selected by ContentManager from `document.AssetSchemaVersion()`; neither function accepts the
other version. The schema-1 writer constant remains 1. The Model writer declares distinct asset
type 5/schema 1 and asset type 5/schema 2 codec identities, and reports the exact selected schema
with every output, so the manifest never labels schema-1 bytes as schema 2.

The XNB route validates and attempts the existing schema-1 conversion first. Success retains the
existing schema-1 carrier, codec and output bytes. Only a canonical graph that fails a schema-1
representability condition but passes every schema-2 condition enters the schema-2 variant. A
malformed graph, unsafe texture name, non-null tag or unsupported effect fails rather than
upgrades. CNJ/glTF/default generated Models never select the new carrier. `CP-059` advances the
XNB importer and shared Model processor/writer identities to version 3 because their accepted
semantics and declared output set changed; that intentionally causes one safe cache rebuild while
leaving the selected schema-1 CNB bytes unchanged.

### 33.5 Independent conformance result

CP-058 added one manually specified Python Model-v2 vector without calling the production C++
encoder. It contains two bones with an explicitly selected, non-implicit root and non-identity root
transform; one exact authored sphere; a Position+Normal declaration; one vertex and one index
buffer; two parts sharing both buffers and one BasicEffect while selecting different index windows;
a nonzero vertex offset; and a typed texture XREF plus non-default `SpecularPower`. Python asserts
the complete byte image SHA-256 and every fixed row/chunk offset. C++ decodes those bytes to the
expected carrier/runtime semantics and its encoder reproduces them exactly. The 1,468-byte vector
has SHA-256 `6a9dc3f5363ae82a93ba8e01fee1059802ac1325d5fd76565ccddb09d928ad78`.

Separate malformed tests mutate every count/table size, reserved word, enum, string/XREF/resource
index, declaration range/overlap, payload cardinality, byte product, graph parent/root/mesh range,
part partition/window/index value and effect discriminant/inactive field. Schema-1's independent
golden bytes retain their prior values. All gates pass; schema 2 is frozen independently of schema
1, and existing source routes do not select it merely because it exists.

### 33.6 Implementation and verification (`CP-058`)

The implementation is deliberately separate from schema 1:

* `CnbModelV2Data` is a CPU-only carrier and `CnbModelV2Codec` is the only schema-2 encoder/
  decoder. `CnbModelData`, `EncodeModelToCnb()`, and `DecodeModelFromCnb()` are unchanged.
* `ContentManager` dispatches Model by `assetSchemaVersion`, validates the complete CPU graph
  before GPU construction, and creates declarations and resource rows once. Tests observe exact
  root identity/transform, bounds, windows, vertex bytes, buffer/effect pointer sharing, null tags,
  and every field on all five stock-effect runtime classes.
* Rebuilt-checksum negative documents cover schema-chunk presence/cardinality/mandatory flags/
  alignment; every header count; table sizes and partitions; reserved words; strings, enums and
  typed XREFs; resource ordinals/products; graph parents/root; bounds; declaration overlap;
  part windows/selected indices; and inactive effect fields.

The normal Model/CNB/CNJ/XNB/glTF gate passes **105/105 tests across 13 suites**. The same selection
passes **105/105** in the combined ASan+UBSan O0 build with both sanitizers set to halt on the first
finding. LeakSanitizer is not claimed: its subprocess fixture terminates on the runner's explicit
`does not work under ptrace` guard, so the successful sanitizer run used `detect_leaks=0`. No
concurrent scheduler or shared-state code changed, so CP-058 does not add a new TSan obligation.
The MinGW-w64 HEADLESS build compiles and archives the complete affected `cna_content` target;
native MSVC/Windows execution remains CP-062 rather than being inferred from cross-compilation.

A non-CI host measurement repeated each independent golden encode and parse/decode case 20,000
times in one process. Schema 1 versus schema 2 took 4.16 s versus 4.66 s to encode (about +12%) and
3.51 s versus 3.91 s to parse/decode (about +11%). The schema-2 vector is larger and semantically
richer (1,468 versus 1,094 bytes), so these values are a sanity measurement, not a performance
threshold.

The public C++ carrier/codec adds 122 inventory rows. They are assigned to open `CBIND-117`, taking
the generated inventory to 551 headers / 9,482 symbols with 651 planned rows. No C header, export,
ABI version, or C route changed; wire-format stability does not silently authorize a C ABI design.

---

## 34. Broader lossless XNB Model transcoding (`CP-059`)

CP-059 extends the shared headless XNB graph rather than adding a second Model parser. The same
canonical readers used by runtime XNB loading now expose all five stock-effect records and retain
vertex-buffer, index-buffer, and effect shared-resource identity. The runtime stock-effect readers
delegate their field decoding to those CPU routines after resolving their ordinary external
references. The compiler neither creates a `GraphicsDevice` nor constructs a runtime Effect.

`ConvertXnbModelToCnbV2()` independently validates and maps the canonical graph. It uses explicit
switches for all twelve `VertexElementFormat` and thirteen `VertexElementUsage` values rather than
persisting ABI enum ordinals. It interns exact declarations, keeps each shared vertex/index/effect
resource as one document-local row (including supported unused resources), and preserves exact
buffer bytes, part windows, spheres, bone order/parents/transforms, explicit root, material fields,
and typed Texture2D/TextureCube XREFs. Schema-2's authoritative encoder applies the complete frozen
wire validator before any artifact is published.

### 34.1 XNB Model support matrix

| Source semantic | Native result |
|---|---|
| canonical schema-1 declaration, unique whole buffers, root 0/identity, reproducible sphere, unique BasicEffect with default `SpecularPower` | schema 1, through the unchanged converter/encoder, with exactly the pre-CP-059 bytes |
| all XNA declaration formats/usages, exact offsets/stride/usage index | schema 2, exact stable declaration IDs |
| shared or unused supported vertex/index/effect resources | schema 2, document-local identity retained; equal bytes are not deduplicated |
| `VertexOffset`, `NumVertices`, `StartIndex`, `PrimitiveCount` | schema 2, exact validated windows |
| serialized mesh sphere | schema 2, exact finite center and nonnegative radius |
| nonzero explicit root, multiple parentless bones, every bone transform | schema 2, exact hierarchy/root semantics |
| `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect` | schema 2, every field admitted by §11.4 plus shared effect identity |
| contained stock-effect texture references | schema 2, normalized logical XREF with exact Texture2D/TextureCube type |
| null Model/Mesh/MeshPart tags | supported in both schemas |
| non-null tag | rejected; no CLR object serialization is invented |
| custom Effect reader/material graph or another shared-resource reader | rejected; no approximation or generic Effect encoding |
| invalid fixup/resource reference, parent/child graph, range/count/window/index, compare function, or skin weight count | rejected before publication |
| absolute, traversal, empty, or otherwise unsafe logical texture reference | rejected through the existing content-path policy |

Selection is deterministic: `XnbImporter` first runs the existing schema-1 representability
converter. If it succeeds, that carrier goes to `EncodeModelToCnb()` unchanged. If and only if it
reports a fidelity failure, the schema-2 converter must validate the original graph completely;
its success selects `EncodeModelV2ToCnb()`, and its failure is fatal. The compiler logs which
schema was selected and, for schema 2, the specific schema-1 fidelity boundary. CNJ and glTF
continue to produce schema 1, including every existing default and generated-child byte.

### 34.2 Writer and manifest contract

One Model writer can legitimately emit two schemas for the same asset type/name. Writer schema
declarations are therefore unique and sorted by `(assetTypeId, assetTypeName, assetSchemaVersion)`,
and `ContentWriteResult` plus each additional output reports the exact emitted schema. The core
rejects an emitted tuple the writer did not declare. This is structured cache identity; the writer
does not parse its own CNB bytes and the manifest does not infer a schema from RTTI.

Manifest version 8 records the resulting exact output schema and permits multiple declared schemas
for one asset identity. Version 7 cannot express that declaration contract unambiguously and is
therefore incompatible rather than being reinterpreted. Its outputs are not deletion authority:
the first v8 build safely rebuilds/replaces requested paths, preserves old-only paths, and publishes
a fresh manifest atomically. The persisted CP-052 reason domains and CP-053 structured explanations
remain unchanged. Importer/processor/writer identities advance to version 3 because accepted
semantics and the declared output set changed; the selected schema-1 CNB bytes themselves do not.

### 34.3 Semantic equivalence boundary

Synthetic fixtures compare runtime XNB A with XNB-to-schema-2-CNB runtime B for bone graph/root/
transforms, mesh names/parents/bounds, exact declarations and vertex/index bytes, part windows,
shared object identity, all five stock-effect properties, typed texture references, and null tags.
The repository's real MonoGame Blender cube independently proves a Position+Normal declaration,
authored sphere, and non-default BasicEffect `SpecularPower` through the same comparison. A
schema-1-compatible fixture additionally compares compiler output byte-for-byte with a direct call
to the unchanged schema-1 encoder. Counts alone are not treated as equivalence.

The normal affected boundary passes 143/143 cases across seven suites (142 pass plus the expected
disabled >2 GiB hash gate), including the 20,000-node deep graph/cycle case. The same 143-case
selection passes combined ASan+UBSan with both sanitizers halting on first error and
`detect_leaks=0`; LeakSanitizer is not claimed because this runner still reports its explicit
ptrace incompatibility. No scheduler/shared mutable state changed, so CP-059 adds no new TSan
obligation beyond the already passing frozen-registry and worker determinism cases.

MinGW-w64 compiles and links both PE32+ Unicode-console compiler executables. Wine 10 runs the new
stock executable against the real MonoGame Blender cube: it publishes a 1,896-byte Model type 5/
schema-2 document on the first invocation, then reports the manifest-v8 fingerprint/output-digest
no-op on the second. Native inspection confirms all thirteen schema-2 chunks. This is Wine evidence,
not native MSVC/Windows evidence.

The generated C-API inventory records the two emitted-schema fields and `CanonicalModelValue`
alias under open `CBIND-117`: 551 headers / 9,485 symbols, 654 planned rows. No C header, export,
ABI version, or C route changed.

---

## 35. Generated glTF child scheduling audit (`CP-060`)

CP-060 audits the existing optional generated-child bundle rather than assuming that the general
content-build graph should expose its children as nodes. `GltfImporter` performs one complete
`ConvertGltfToCnj()` pass: it parses the document and every referenced buffer, validates the shared
scene, derives mesh/skin groups and material references, and extracts images. `ModelProcessor`
then encodes the primary and additional Models, embedded and standalone clips, and Texture2D
children from those shared results. There is no per-child importer input or dependency partition
that the scheduler can currently discover before doing that common work.

The audit found one real but bounded cache-isolation case. In the representative external-image
fixture, replacing a valid 16-by-16 PNG changed the generated Texture2D child while the Model CNB
remained byte-identical. A 1,000-iteration in-process run of the complete external/data-URI image
case averaged about 1.47 ms per glTF build. A real one-process compiler rebuild took 0.03 seconds;
the comparable direct texture-only rebuild also took 0.03 seconds at the host timer's 10 ms
resolution. Startup, discovery, hashing and publication dominate this small input, so the saved
Model encode does not establish a material end-to-end benefit.

Animation isolation is weaker. Renaming `Walk` to `Run` in the two-clip fixture changed the primary
Model bytes and generated child set, because schema-1 Model embeds clip names and values. The
unrelated `Clip1` child remained byte-identical, but identifying that fact still required the full
glTF parse and animation conversion; edits to its values necessarily also change Model. Buffer-view,
data-URI and GLB images are bytes inside the primary source rather than independent authored files.
Multi-group Models likewise share one source, buffer set and constructed scene graph.

Independent scheduling is therefore not implemented. It would require a new generated-source
identity and discovery phase, stable per-child dependency fingerprints, cross-node XREF ownership,
and a partial publication/GC contract while retaining the same common conversion. That complexity
is not supported by the measured saving, and it would weaken the existing all-or-nothing staging
guarantee without changing Model rebuild behavior for animations. The compatible bounded same-node
policy remains authoritative. A future revisit requires a materially larger real workload and a
proven dependency partition; the current graph capability alone is not sufficient evidence.

---

## 36. Target-profile policy audit (`CP-061`)

CP-061 traced every built-in route from imported carrier through processor to the native writer.
None consults a target platform, graphics renderer, host OS or architecture, and none has a latent
choice that becomes meaningful merely by naming a profile:

| Route | Current output policy | Target-dependent alternative available now |
|---|---|---|
| PNG/JPEG/ordinary Texture2D | canonical Rgba8 plus authored mip levels; optional color key | none; no texture compressor/transcoder |
| Texture3D and SpriteFont | canonical Rgba8 volume/atlas | none |
| DDS TextureCube and supported XNB textures | preserve or losslessly normalize validated source representations | none; source semantics, not target selection |
| WAV SoundEffect | exact supported input decoded to portable Pcm16 | none; no target audio encoder |
| Model schema 1/2 | select only by representability of source semantics | none; renderer capability does not rewrite assets |
| Curve and AnimationClip | validated canonical semantic data | none |
| Song and Video | portable metadata plus byte-exact deployment-support copy | none; no media re-encode policy |
| Effect/shader | no compiler route | no policy exists to select |

CNB can carry multiple texture representations and supports opt-in per-chunk compression at the
codec layer, but the pipeline has no target-driven generator for either. Choosing a subset of an
authored texture would reduce portability rather than establish a platform contract. Container
compression changes storage/runtime compatibility independently of desktop/mobile/OS labels and
would be better expressed as a deliberate codec policy if it is ever exposed. Compile-time renderer
selection remains a runtime/build configuration and must not make the host `cna-content` executable
emit machine-specific bytes accidentally.

No target-profile abstraction is implemented. There is no CLI flag, top-level configuration key,
global build option, manifest/fingerprint domain, CMake forwarding argument, CNB platform field,
processor API, or C ABI change. Existing strictly typed per-asset processor parameters already
participate in the canonical parameter fingerprint and are the smallest correct home for a future
single-route policy. A shared profile identity becomes justified only when an implemented policy
must coordinate multiple processors and has defined output consequences; names such as desktop,
mobile, Windows, Linux or Xbox without such consequences are explicitly insufficient.

---

## 37. Native Windows/MSVC gate and lock boundary (`CP-062`)

The working host is Linux and exposes `x86_64-w64-mingw32-g++` plus Wine 10, but no `cl`,
`clang-cl`, PowerShell, native Windows environment, or local GitHub Actions runner. CP-048/CP-059's
MinGW compile/link and Wine Unicode/schema-2 executions remain valid cross-toolchain evidence; they
cannot be relabeled as MSVC or native Windows verification.

The repository already uses bounded `windows-latest` workflows for D3D, GDI and the Windows
leg of focused cross-platform renderer tests. CP-062 follows that convention with
`.github/workflows/content-pipeline-windows-ci.yml`: one job available through
`workflow_dispatch` plus a scoped `content-pipeline-final` push trigger for pre-integration proof,
a pinned sharp-runtime sibling, MSVC x64 plus Ninja, `HEADLESS` platform/renderer, SDL3 audio for
the compiler's existing device-free in-memory XNB conversion, video/net/Draco disabled, two
explicit build targets, two-way parallelism and a 45-minute timeout. It does not build unrelated
renderer/example targets beyond dependencies of the focused tests.

The native job builds `cna_content_tool` and `CnaContentTests`. Its focused unit selection covers
strict configuration, manifest v8/migration/reason domains, component/registry contracts, built-in
processors, CNB codecs/golden vectors, and CPU XNB transcoding while excluding runtime device tests.
Windows intentionally omits the POSIX-only `ContentPipelineCliTests.cpp`, so a separate shell probe
runs the real `cna-content.exe` through a non-ASCII source/output path. It proves workers 4 build,
persisted explanation output, workers 1 no-op, manifest-owned clean with the lease retained, a
workers 1 rebuild, byte-identical SHA-256 and manifest version 8. Failure logs are uploaded without
requiring credentials beyond read-only checkout access.

The workflow first exposed three native-Windows integration defects rather than being weakened to
hide them: vendored SDL's multi-config build/install configuration disagreed, the pinned
sharp-runtime date parser raised MSVC C4456 under its own `/WX`, and six focused tests assumed
POSIX null-device/path spelling. The fixes install the exact SDL configuration that was built,
suppress C4456 only for the five pinned-runtime translation units that consume that parser, use
Windows `NUL` in the three subprocess oracles, and compare canonical deployment source paths.

Native run `33309632114` (`https://github.com/openeggbert/cna/actions/runs/33309632114`) then passed
on commit `83807bef64990541e7d41274c11b9562e49112cc`. GitHub runner 2.336.0 used image
`windows-2025-vs2026` version 20260824.214.3, Visual Studio environment 18.9.1, MSVC tools
14.51.36231, Windows SDK 10.0.26100.0 and x64 Ninja Debug. Both focused targets built; 177 of 178
tests passed in 18 suites with only the opt-in sparse >2 GiB hash gate skipped; the non-ASCII
build/explain/workers-4/no-op/workers-1/clean/rebuild probe passed with manifest v8 and identical
SHA-256 output. This is real native Windows/MSVC evidence, independently of the retained
MinGW/Wine evidence.

The output-root lease boundary remains honest. Modern build and clean processes take the same
`.cna-content.lock` OS lock for their complete lifetimes. A pre-CP-049 executable has no code that
opens or observes that file. No newer manifest version, minimum-generation field, warning, process
scan or diagnostic can retroactively force that concurrently running legacy process to cooperate.
Mixed-generation overlap on one output root is unsupported; serialize those invocations externally
or use different output roots. No false backward-compatible locking mechanism is added.

---

## 38. Final compatibility, security and performance review (`CP-063`)

The final review rebuilt the complete configured HEADLESS Debug tree, including both real
`cna_add_content()` fixtures. A broad CPU/content boundary then exercised configuration, manifest,
registry, custom components, the CLI, every built-in pipeline route, CNB codecs, CNJ, XNB/LZX/LZ4,
Model schemas 1 and 2, runtime-reader equivalence, and the glTF conformance families. The first run
executed 1,441 tests: 1,430 passed, eight opt-in/external-fixture cases skipped, and exactly the
three CP-050-documented HEADLESS storage-adapter tests failed (two TextureCube loads and one
Texture3D load). The identical selection excluding only those three renderer-only cases passed all
1,430 remaining tests with the same eight skips. This is a reproduced pre-existing limitation, not
a Content Pipeline regression or a changed oracle.

Independent final gates add the following evidence:

| Boundary | Result |
|---|---|
| workflow-equivalent HEADLESS/SDL3 configuration | both targets build; 177 pass, one opt-in sparse-file skip; Unicode CLI lifecycle passes |
| frozen/schema-2 golden vectors | 14/14 pass: 11 schema-1 tests plus three independent Model-v2 conformance tests |
| streaming hash above 2 GiB | 1/1 opt-in sparse-file test passes in 33.3 seconds |
| ASan+UBSan affected boundary | 187 pass, one opt-in skip; both sanitizers halt on first finding; no report |
| TSan scheduler/manifest/CLI/custom boundary | 108 pass, one opt-in skip; no report |
| CMake integration | stock and custom generated content fixtures build in normal and sanitizer trees |
| C API | all nine inventory/compatibility/header/release/ABI gates pass |

LeakSanitizer remains an environment limitation rather than a hidden success: with its default
setting the build-time compiler subprocess aborts because LSan does not work under this runner's
`ptrace`; the successful ASan+UBSan run therefore used `detect_leaks=0`. The C-API inventory remains
551 headers and 9,485 declarations: 8,352 implemented, 15 partial, 654 planned and 464 not
applicable. The 654 planned content declarations remain under the existing `CBIND-117`; no pipeline
symbol was exported and no ABI version changed.

The architecture audit rechecked the destructive and compatibility boundaries rather than merely
counting tests. Explanation reasons derive from the persisted nine-domain state plus inspectable
component/schema/artifact records; workers 1/2/4 ordering remains coordinator-sorted. Manifest
versions 1 through 7 cannot grant deletion authority to v8. Named roots are selected by alias,
canonicalized without search, kept request-local physically, and grant no publication/clean/GC/
scavenging authority. Deployment from one requires the exact recorded external dependency. Clean
and orphan collection still use the one manifest-proven digest preflight, staging still uses its
single private publisher, and no staging residue was found. Model schema 1 and all existing golden
bytes remain unchanged; schema 2 uses the same asset ID with separate version dispatch and refuses
unknown effects, non-null tags, malformed sharing/windows/ranges and arbitrary object graphs.

The retained performance evidence remains adequate and deliberately non-normative: explain added
about 15 ms/5.6% to the 128-node no-op median; an aliased external dependency was within 0.3 ms of
an ordinary contained dependency; Model-v2's richer 1,468-byte vector encoded/decoded about
12%/11% slower than the 1,094-byte schema-1 vector; and a representative complete glTF build cost
about 1.47 ms in-process, with no measurable CLI advantage for a separately rebuilt texture child.
No CI threshold is inferred from those host-specific measurements.

The remaining backlog is bounded rather than another pipeline rewrite:

* consider a separately versioned machine-readable decision stream only when an IDE/build consumer
  exists, potentially supporting validation or dry-run without making English output a contract;
* revisit manifest scaling only with a measured large-project bottleneck;
* extend XNB Model beyond the schema-2 support matrix only through an independently reviewed native
  representation—never generic CLR serialization;
* revisit target profiles or independent glTF child nodes only when a real output policy or
  materially larger measured cache-isolation benefit satisfies CP-060/CP-061's gates.

Current and legacy compiler processes must still be serialized externally when they share an
output root. The new binary cannot make an old one acquire a lock it never implemented. No task in
this continuation merged into `next` or pushed a branch.
