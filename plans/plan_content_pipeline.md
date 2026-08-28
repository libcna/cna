# plan_content_pipeline.md — CNA Content Pipeline

> **Status (2026-08-28):** `CP-001` and `CP-002` are complete. `CP-003` is current. The
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
| Intermediate object model | Lets several source formats converge before processing | Retain where source semantics differ from `Cnb*Data`; first real types are `ImportedImage` and `ImportedAudio`. Do not invent one class per XNA name. |
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
| glTF orchestration | `tools/gltf_to_cnj/gltf_to_cnj.cpp::ConvertGltfToCnj` | yes | still partly trapped in a tool translation unit and writes CNJ staging sidecars |
| CNJ canonical reading | `CnjCanonicalRead`, `CnjEnvelope`, `ResolveCnjSourceFileSafely` | yes | reusable parsing/validation/containment pieces |
| CNJ compilation | `CompileCnjToCnb` | mixed | headless for most assets; Curve/AnimationClip currently construct a null-device `ContentManager`; future pipeline work must remove that runtime shortcut |
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
* The source tool has no build manifest and knows only the primary source. glTF dependencies are
  read by the shared import core but not returned as a complete build result by the CLI.
* Directory traversal exists in the glTF staging tool and is explicitly sorted before compilation;
  no general content-root traversal exists yet.

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

## 5. Chosen C++23 architecture (`CP-003`, current)

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
* explicit stable input/output type identity strings such as `cna.imported.image.v1` and
  `cna.cnb.texture2d.v1`;
* an internal `ContentValue` carrying `std::any`, the stable type identity and an ephemeral
  `std::type_index` guard;
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

### 5.4 Focused contexts

`ContentImporterContext` contains only:

* canonical source root, primary source path and logical asset name;
* safe contained dependency resolution;
* source-dependency registration;
* read-only build options;
* scoped logger access.

`ContentProcessorContext` contains only:

* logical asset name;
* validated processor parameter view and build options;
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

Global `ContentBuildOptions` initially contains only proven cross-cutting values. Target/profile is
deferred; adding an unused enum would imply platform-specific output that CNB v1 does not have.

### 5.6 Deterministic registry and selection

All lookup candidates are stored/diagnosed in stable name order. Registration order never selects
a winner.

* importer: lowercase source extension -> candidates; explicit importer name may disambiguate;
* processor: imported stable type + optional requested processed type -> candidates;
* writer: processed stable type -> candidates;
* duplicate component identity is rejected at registration;
* a default route with more than one candidate is an ambiguity error naming every candidate;
* unknown routes report source extension/type and the stage;
* there is no "last registered wins" behavior.

Extension matching is only routing. The selected importer must validate bytes/content.

### 5.7 Error and logging model

Use ordinary typed exceptions internally while preserving their original `what()` text. The build
coordinator catches only at the outer stage boundary and throws/returns a `ContentBuildError` that
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

Paths are canonical native paths internally; manifest paths are normalized relative to the declared
root. Runtime references remain logical forward-slash names. `ContentBuildResult` records source,
logical name, output, component identities, parameters, categorized dependencies/references,
warnings, byte count, built/skipped state and fingerprint information once available.

### 5.9 Writer and runtime mapping

Built-in writers are adapters only:

```text
Texture2DContentWriter(CnbTextureData) -> EncodeTexture2DToCnb()
SoundEffectContentWriter(CnbSoundEffectData) -> EncodeSoundEffectToCnb()
ModelContentWriter(CnbModelData) -> EncodeModelToCnb()
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

### 6.2 SoundEffect (`CP-005`)

```text
WAV
 -> WavImporter (the existing bounded RIFF parser)
 -> ImportedAudio
 -> SoundEffectProcessor
 -> CnbSoundEffectData
 -> SoundEffectContentWriter
 -> existing EncodeSoundEffectToCnb
```

The split must move/reuse the parser, not copy it. Existing WAV parser tests remain the malformed
input oracle. New tests compare old and pipeline bytes.

### 6.3 Model/glTF (`CP-009`)

Do not rewrite glTF while introducing the pipeline. First wrap the current same-interpretation
staging path and pin direct glTF -> CNB == glTF -> CNJ -> CNB. Only then extract the remaining tool
orchestration into an imported in-memory model if that extraction preserves every oracle.

---

## 7. CLI and publication design

`cna-content build <source-or-directory> -o <output>` is the intended user surface. Initial source
extensions are exactly those the registered built-ins advertise; unsupported files produce useful
diagnostics rather than being silently copied or skipped. Directory discovery is sorted by normalized
relative generic path and produces `<relative stem>.cnb` while retaining the logical relative name.

Final publication remains all-or-nothing through the single implementation in
`tools/common/CnaToolAtomicWrite.hpp`. The first CLI can own orchestration at the tool boundary and
reuse that header directly. If library-level publication becomes necessary, the implementation will
move once to a shared module and the old header will forward to it; it will not be copied.

Directory creation and manifest update must not make a partial `.cnb` visible. A failed rebuild of
an existing artifact must leave its old bytes untouched and remove its sibling temporary.

### Windows pathname strategy

The new CLI will use `wmain(int, wchar_t**)` on Windows and construct `std::filesystem::path` from
wide arguments. POSIX uses `main(int, char**)`, treating argv as the locale-independent byte spelling
accepted by `std::filesystem`. Logical content names are UTF-8 with `/` separators and are converted
explicitly at the native/logical boundary; native paths are never serialized as logical names.
Narrow `main` on Windows is rejected for the new CLI because it cannot represent every filesystem
path. Old tools are not refactored as part of this decision.

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

## 9. Incremental manifest/cache design (`CP-008`, future)

The first manifest is a deterministic, versioned, inspectable file under the output root. It records
one entry per logical asset, sorted by logical name. A SHA-256 (or another explicitly selected stable
cryptographic digest already available without a heavy dependency) fingerprint covers canonical
length-prefixed fields:

* primary source bytes;
* every recorded source-dependency path relative to root and its bytes, sorted by category/path;
* content/build dependency identities and their effective fingerprints;
* importer, processor and writer stable names and versions;
* canonical processor parameters;
* relevant build options;
* logical content identity where it affects CMET/output bytes;
* CNB container identity plus selected asset schema/codec identity;
* manifest format version.

No timestamp alone, RTTI name, absolute temporary path, process ID or native path separator enters
the fingerprint. Missing/corrupt/incompatible manifest data causes a safe rebuild. The output's own
bytes/hash are recorded so tampering does not yield a false skip. Manifest publication is atomic.

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
| `CP-003` | **current** | Implement experimental component identities, type-erased values, focused contexts, dependency collector, logger, component contracts and deterministic explicit registry with selection/error tests. |
| `CP-004` | future | Complete Texture2D Importer -> Processor -> Writer -> existing encoder vertical slice; prove old/new bytes, deterministic bytes, decode/runtime compatibility and headless operation. |
| `CP-005` | future | Complete WAV/SoundEffect vertical slice by splitting/reusing the existing parser; prove byte equivalence and no audio initialization. |
| `CP-006` | future | Add `cna-content build` single/directory CLI, sorted traversal, logical relative names, atomic publication and failure preservation tests. |
| `CP-007` | future | Make categorized dependency collection and the build result complete/observable for built-in flows. |
| `CP-008` | future | Add deterministic inspectable manifest and content fingerprints; prove no-op and precise invalidation behavior. |
| `CP-009` | future | Integrate glTF/Model without a second interpretation; retain direct-vs-CNJ byte oracle and report glTF dependencies. |
| `CP-010` | future | Integrate CNJ as a front-end that converges on shared processors/writers; remove build-time ContentManager shortcut and preserve sidecar safety/equivalence. |
| `CP-011` | future | Add realistic custom importer/processor/writer plus custom runtime loader end-to-end example/test; review experimental API. |
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
* glTF's last orchestration is still tool-owned and file-staged. An eager in-memory rewrite could
  break the strongest existing equivalence oracle.
* `CompileCnjToCnb` uses ContentManager for two pure data types. Leaving it indefinitely would blur
  the new build/runtime boundary.
* A string type ID and C++ type can disagree in a custom extension. Checked boxing/unboxing and
  diagnostics are mandatory; the string is persistent identity, the RTTI guard is only defensive.
* Dependency correctness precedes incremental correctness. An incomplete dependency set must force
  rebuilds rather than permit a wrong skip.

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
* Which digest implementation best fits CNA without adding a heavyweight build dependency.
* Whether a future build dependency may intentionally live outside source root, and what explicit
  capability grants that access.
* Whether one source producing several logical assets (glTF skins/clips) should be one graph node
  with several outputs or deterministic child nodes. Current glTF behavior makes this a real design
  question; it will be settled before recursive build APIs.
* How custom writer schema/codec version identities should compose with custom CNB asset schema
  versions in fingerprints. Built-ins can use their frozen asset schema IDs directly.
* Whether manifest JSON should reuse `CNA::Internal::Json` or use a smaller pipeline-owned canonical
  representation; whichever is chosen must have deterministic field/key ordering.

