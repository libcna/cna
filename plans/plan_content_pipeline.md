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
* The bounded multi-output API can publish generated CNB children, but existing glTF conversion
  still rejects multi-Model input and does not yet extract standalone clips or texture child
  assets. Those routes need explicit canonical processor results and CP-024 graph edges rather than
  being enabled implicitly by the generic writer facility.
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
  compiled output size until the run completes. Cleanup is best effort after abrupt process or
  machine termination; abandoned private staging directories are not yet scavenged automatically.
* Song/Video compilation records and encodes the streaming media XREF but does not copy raw media
  into the output tree. CP-023 intentionally models compiled CNB outputs, not arbitrary deployment
  files; deployment must still place media at the referenced path until an explicit support-file
  policy exists.
* Multi-file publication is recoverable but not transactional across paths. A failed later output
  can leave earlier complete replacements beside the old manifest; the next build repairs the
  owning node. Outputs no longer claimed by any manifest entry are not garbage-collected.
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
* Whether a future build dependency may intentionally live outside source root, and what explicit
  capability grants that access.
* Whether one source producing several logical assets (glTF skins/clips) should be one graph node
  with several outputs or deterministic child nodes. Current glTF behavior makes this a real design
  question; it will be settled before recursive build APIs.
* How custom writer schema/codec version identities should compose with custom CNB asset schema
  versions in fingerprints. Built-ins can use their frozen asset schema IDs directly.

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

`XnbImporter` parses with the existing `XnbHeader`, `XnbDecompression`/`LzxDecoder`,
`XnbTypeReaderTable`, `ContentReader` primitives and `XnbReadLimits`. Its canonical table mode does
not instantiate runtime readers through the mutable process-global `ContentTypeReaderManager`.
Root and nested references are 1-based validated table indices; reader version zero is required.
None and LZX compression are accepted. LZ4 is recognized but rejected because CNA's existing XNB
stack has no LZ4 decoder. Versions 4/5 and the same 16 platform bytes as runtime are container-valid;
individual routes can impose stricter semantic rules (notably Xbox texture/audio payloads).

Shared resources are parsed and bounded but are rejected for transcoding before root construction.
Generic external XNB object references are not interpreted as build edges. The only external forms
admitted are Song/Video streaming-media path fields; they pass through source-root containment,
become source-file dependencies, and remain distinct CNB XREFs. Absolute paths, `..`, canonical
symlink escapes, missing/empty media and unconsumed graph bytes fail.

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
| `ModelReader` | yes | shared resources, GPU vertex/index buffers, effects, ownership graph | no lossless native adapter | **unsupported**; real fixture identifies ModelReader and 3 shared resources |
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
tool entry point. Linking the executable reaches the pre-existing `wmain` configuration defect: the
target omits `-municode`, so MinGW's CRT asks for `WinMain`. Native Windows/MSVC execution is not
available and is not claimed. All nine build-free C-API consistency gates pass. The four canonical
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

The other reviewed risks remain open because they require separate policy, not merely a safer
implementation of existing semantics: scavenging abandoned staging directories needs an age and
live-owner protocol; orphan-output collection needs an explicit ownership/retention policy; raw
Song/Video support-file copying needs deployment semantics; and glTF child/multi-Model output or
custom schema-version fingerprints change component contracts. None was guessed as a side effect
of the XNB phase.
