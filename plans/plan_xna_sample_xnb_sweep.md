# plan_xna_sample_xnb_sweep.md — the real Microsoft XNA 4.0 sample assets, rebuilt and compared

> **Why this plan exists.** [`plan_xnapipeline_parity.md`](plan_xnapipeline_parity.md) closed the
> *API* question: 128/128 public types, 705/705 members, 10/10 importers, 18/18 source extensions,
> 12/12 processors, 47/47 processor properties, with a 112-case differential corpus this repository
> wrote for itself. Its own final audit found the weak spot in that sentence — *representative
> content projects build end-to-end* was standing on projects CNA authored — and building **one**
> real sample project (Platformer) immediately produced four framework defects and then two more.
> One project cannot be the acceptance layer for a compatibility claim. This plan makes the
> acceptance layer the corpus: every `.xnb` that the genuine Microsoft XNA 4.0 Content Pipeline
> produced from a public XNA sample and that this machine holds, rebuilt from the original source
> asset through CNA's product pipeline, and compared byte for byte.
>
> **The denominator is fixed and is not CNA's to choose.** It is the set of genuine reference
> `.xnb` files already present under `/rv/tmp/samples`. No reference is generated for this campaign;
> no asset is added because it would be easy; no asset is dropped because it is hard.
>
> **Boundaries.** Everything this campaign writes lives under
> `/rv/data/development/github.com/openeggbert/cnatmp` on branch `xnapipeline`.
> `/rv/tmp/samples` and `/rv/tmp/XNAGameStudio/Samples` are read-only and are proved unchanged at
> the end (§9). Sample-specific compatibility code never enters CNA core; a framework deficiency
> the corpus exposes is fixed generally, in the component that owns it, with its own regression
> test.
>
> **Task IDs.** `XNASWEEP-###`, three digits, allocated in the phase ranges of §10. Never reuse an
> ID; never renumber; append new tasks at the tail of their phase.

---

## 1. What counts as a genuine reference

`/rv/tmp/samples` holds 157 per-sample artefact roots, and only some of what is in them came out of
Microsoft's pipeline. The rule is the artefact root's own directory convention, which every sample
follows and its `MANIFEST.md` documents:

| Directory | Verdict | What it is |
|---|---|---|
| `xna4-build/` | genuine | `BuildContent` run against the sample's own assets, under Wine with the real XNA 4.0 assemblies |
| `xna4-diagnostic/`, `xna4-diag/` | genuine | the same pipeline, run for a diagnostic variant |
| `win7-export/`, `win7-build/`, `win7-work/` | genuine | produced on the offline Windows 7 virtual machine |
| `SAMPLES-DEC-007-Win7-SongProcessor/export/` | genuine | the Windows 7 `SongProcessor` run, with its `.wma` beside each `.xnb` |
| `xnb-staging/` | genuine | staged output of the same runs |
| `xna4-original/`, `xna-original/`, `original/`, `xbox-refs/` | genuine | `.xnb` Microsoft shipped inside the sample itself |
| `cna-*/` | **not** a reference | CNA's own output |
| `fna-*/` | **not** a reference | FNA's output, kept as a third opinion |
| `evidence/`, `fixtures/`, `mgcb/`, `diagnostic*/` | **not** a reference | captures, hand-made fixtures, another tool |

One further exclusion is by file rather than by directory, and it is the reason this section exists
at all. `SAMPLE-013-Platformer_4_0/xna4-build/Content/Sounds/Music.xnb` and its copy under `bin/`
were **not written by `BuildContent`**: the sample's own `scripts/build-original.sh` compiles
`scripts/BuildSongXnb.cs`, a hand-written `BinaryWriter`, and runs it to produce them, because the
Wine prefix has no Windows Media to run `SongProcessor` through. It writes one type reader where
XNA writes two — every one of the eight genuine songs in this corpus carries `SongReader` **and**
the `Int32Reader` its duration goes through — so `plan_xnapipeline_parity.md` `XNAPP-332`'s
"one is the song, which differs only in writing its duration through `Int32Reader`" was CNA being
marked wrong by a file XNA never wrote. `corpus_inventory.py` excludes it by name, with that
reason.

---

## 2. Corpus (`XNASWEEP-010`)

Frozen by `tools/xna-sample-sweep/corpus_inventory.py` into
`build/xna-sample-sweep/manifest/sample-xnb-corpus.json` and committed as
`tests/reference/xna40/sample-xnb-corpus.json`. Counts are mechanical; §11 carries the current
values and nothing in this file is hand-counted.

---

## 3. Mapping (`XNASWEEP-020`)

`tools/xna-sample-sweep/map_sources.py`. An asset's logical name is a suffix of its output path —
XNA writes to `<output root>/<the item's directory>/<the item's Name>.xnb` — so an item's name found
at the tail of a reference path identifies both the item and the output root the build used. A
sample's projects are then scored against each root and the one that explains the most of it owns
it; that pair is the **build unit** the sweep rebuilds.

---

## 4. Rebuild (`XNASWEEP-040`)

Through the product path only: `cna-content build <project.contentproj> -o <dir> --format xnb
--only-configured-assets --xna-compatible`, with the target read off the reference header rather
than assumed. Never through an internal serializer entry point, never by copying a reference.

---

## 5. Comparison and classification

Raw bytes first (`sha256`, first differing offset). Where they differ, the normalized semantic diff
of `tools/xna-pipeline-oracle/differential/compare.py`, which reads both sides with the independent
parser `tools/xnb/xnb_conformance.py`. Every non-identical pair gets exactly one status:
`BYTE_IDENTICAL`, `SEMANTICALLY_IDENTICAL`, `ACCEPTED_DIFFERENCE` (with its reason, in
`decisions.json` form), `CNA_BUG`, `PROJECT_METADATA_BUG`, `IMPORTER_BUG`, `PROCESSOR_BUG`,
`XNB_WRITER_BUG`, `CUSTOM_PIPELINE_GAP`, `EXTERNAL_BLOCKED`, `SOURCE_MAPPING_ERROR`. The gate fails
while `UNEXPLAINED > 0`.

---

## 6. Environment this campaign may use

| Component | Where | Used for |
|---|---|---|
| genuine XNA 4.0 assemblies | `/rv/tmp/samples/_tools/xna-game-studio-4-refresh/` | black-box measurement only |
| genuine XNA 4.0 runtime | `~/.wine-cna-xna40` | loading both sides in a real `ContentManager` |
| `fxc.exe`, June 2010 DirectX SDK | `/rv/tmp/samples/_tools/directx-sdk-june-2010/` | the `.fx` route, under Wine |
| XNA redistributable TTFs | `SAMPLE-140-RedistributableTTFs_ARCHIVE_3_1/original/` | the fonts the samples' `.spritefont` files name |

---

## 7. Provenance

Unchanged from `plan_xnapipeline_parity.md` §2, with one addition this campaign needs: **the public
C# source of a sample's own importer, processor or writer may be read**, because it is the sample's
published source and is exactly what a game shipping that component would have. Microsoft's own
pipeline implementation is still black-box only.

---

## 8. Sample-defined pipeline components

A sample that ships its own importer/processor/writer is `CUSTOM_PIPELINE_GAP` until an equivalent
exists. The equivalent is written under `tests/sample-pipeline/`, against the public XNA façade, and
never inside a core processor. If porting one shows that a *public* Content Pipeline behaviour is
missing, that behaviour is implemented generally and the port stops being the fix.

---

## 9. Read-only integrity

**One violation, found by this audit and fixed.** `BuildContent` wrote its own build
configuration into the *source root* and deleted it again, because the coordinator required a
configuration to live inside the root it was reading. Nothing was added, removed or altered in the
read-only tree -- but the modification time of ~130 project directories under
`/rv/tmp/samples/*/xna4-original/` changed, which is a write the mission's boundary does not
allow and which would make any read-only content tree unbuildable. It is `XNASWEEP-110`: the
configuration goes to the task's own intermediate directory now and the coordinator is told about
it rather than discovering it, and a test builds a source tree with the write bit removed.

The remaining ~6,800 differences between the two baselines of `/rv/tmp/samples` are another
session's CNA build trees under `SAMPLE-070/cna-web-webgl2/` and
`SAMPLE-070/cna-native-opengles3-release/`, which this campaign never touched.
`/rv/tmp/XNAGameStudio/Samples` is byte for byte and timestamp for timestamp unchanged.


`build/xna-sample-sweep/audit/` holds a `find -printf '%y %s %T@ %p'` baseline of both read-only
roots taken before any work, and the final audit re-takes it and diffs.

---

## 10. Task log

Phase ranges: 001–009 audit and workspace, 010–019 corpus freeze, 020–039 source mapping, 040–059
sweep tooling, 060–099 per-family sweeps, 100–199 defects, 200–209 runtime verification, 210–219
final qualification.

### Phase 0 — audit, workspace, boundary

| ID | Task | State |
|---|---|---|
| `XNASWEEP-001` | Audit the live tree: branch, HEAD, upstream, untracked files preserved; read the parity plan, its final report, the differential tooling and the `.contentproj` route before changing anything. | [x] |
| `XNASWEEP-002` | Take the read-only baseline of both roots and put every campaign artefact under the gitignored `build/xna-sample-sweep/`. | [x] |

### Phase 1 — freeze the reference corpus

| ID | Task | State |
|---|---|---|
| `XNASWEEP-011` | `map_sources.py`: every genuine `.xnb` to the item that built it. | [x] The reference's own path tail is the item's name, and a sample's projects are scored against each output root. Cited by `tools/xna-sample-sweep/map_sources.py`. |
| `XNASWEEP-021` | `runner_provenance.py`: what actually produced each sample's reference. | [x] The sample's own `BuildContent` runner, not its `.contentproj`: 89 of them hand-list their assets and pass no `ProcessorParameters` at all, so the reference carries the processor's defaults. Extended twice since -- `XNASWEEP-120` for parameters set inside a helper, `XNASWEEP-133` for runners the name list could not find and shapes the classifier read wrongly. |
| `XNASWEEP-010` | `corpus_inventory.py`: every genuine `.xnb`, with size, SHA-256, container header, compression, root reader and complete type-reader table; non-genuine output excluded by directory and the synthesized song excluded by name. | [x] |

### Phase 2 — map every reference to its original source

| ID | Task | State |
|---|---|---|
| `XNASWEEP-020` | `contentproj.py` + `map_sources.py`: a second reader of the `.contentproj`, the asset-name-as-path-suffix match, and the build units it yields. | [x] |

### Phase 3 — the sweep

| ID | Task | State |
|---|---|---|
| `XNASWEEP-041` | `report.py`: what the sweep found, grouped so it can be acted on. | [x] Cited by `tools/xna-sample-sweep/report.py`. |
| `XNASWEEP-042` | `classify.py`: explain every pair the sweep found differing. | [x] Byte-identical, payload-identical, semantically identical, float-tolerance, or the first difference itself. Cited by `tools/xna-sample-sweep/classify.py`. |
| `XNASWEEP-050` | `taxonomy.py`: one class, with a reason, for every reference in the corpus. | [x] The campaign's gate: `UNEXPLAINED` is the class everything that is not explained falls into, and its target is zero. Cited by `tools/xna-sample-sweep/taxonomy.py`. |
| `XNASWEEP-040` | `sweep.py`: build every unit through `cna-content`, compare every mapped reference, classify, emit JSON and a table. | [x] |

### Phase 5 — defects the corpus exposed

| ID | Task | State |
|---|---|---|
| `XNASWEEP-100` | `<FontName>` is a font *family* name and CNA matched a file name, so a `.spritefont` could only be built where a font file happened to be called after its family; and there was no way at all to tell a build where a game's fonts are. | [x] |
| `XNASWEEP-101` | The build-tool services a command line selects -- effect compiler, XMA encoder, font directories -- reached nothing on a `.contentproj` build. | [x] |
| `XNASWEEP-102` | `--xnb-platform`, `--xnb-profile` and `--xnb-compress` were accepted on a `.contentproj` build and ignored. | [x] |
| `XNASWEEP-103` | One asset with no component refused the whole project, three times over. | [x] |
| `XNASWEEP-104` | A sprite-font atlas's height is the graphics profile's rule, and CNA used Reach's for both. | [x] |
| `XNASWEEP-105` | A font family was matched against the typographic family, not the one Windows matches. | [x] |
| `XNASWEEP-106` | A `.x` material's specular power of zero is not a value; the genuine importer writes none. | [x] |
| `XNASWEEP-107` | A `.x` mesh with no `MeshNormals` got a constant normal instead of a generated one. | [x] |
| `XNASWEEP-108` | Every effect was compiled optimized, because nothing carried the build configuration. | [x] |
| `XNASWEEP-109` | A PNG's own `gAMA` chunk was ignored; GDI+, and therefore XNA, applies it. | [x] |
| `XNASWEEP-110` | A content build needed write access to the content it was reading. | [x] |
| `XNASWEEP-111` | An FBX camera is not a node, and what makes a scene's root is what the scene names. | [x] |
| `XNASWEEP-112` | An FBX vertex is a control point *and* its channel values. | [x] |
| `XNASWEEP-113` | `PreRotation` and the scene's own unit, both of which decide where a model stands. | [x] |
| `XNASWEEP-114` | A `.x` names its materials as often as it nests them, and its texture paths are Windows paths. | [x] |
| `XNASWEEP-115` | A path a source file names is matched the way Windows matches one; the *name* stays as authored. | [x] |
| `XNASWEEP-116` | An external reference is written relative to the asset that carries it, with Windows separators. | [x] |
| `XNASWEEP-117` | Two models that name the same texture are refused; XNA builds it once and lets both reference it. | [x] Half fixed. `ReserveOutputs` in `tools/content/content.cpp` gives each output logical name exactly one owning node, so Spacewar's `p1_bfg` and `p1_dual`, which both name `textures/p1_back.tga`, collide on `textures/p1_back_0` and neither builds. XNA's own build produces that texture once and both models reference it; the coordinator needs nested nodes shared across parents by their `(source, processor, parameters)` key rather than owned by whichever parent reached them first. 85 of Spacewar's 154 references were missing for this reason alone. Two nodes may now own one output *when the two outputs are equal* -- same path, same reader, same SHA-256 -- which is what a nested build keyed by its source and its processing produces; two nodes whose outputs differ under one name are still refused, which is the case the check exists for. What is left is the same collision *within* one node, which still refuses five of Spacewar's models. |
| `XNASWEEP-118` | Every processor property XNA lets a game override was `final` in all but name. | [x] The pipeline is extended by derivation, and a *property* is overridden as often as a method: the Normal Mapping sample writes `public override bool GenerateTangentFrames { get { return true; } }` so that no project setting can switch off what its effect requires. Read from the CLR metadata already in `tests/reference/xna40/content-pipeline-api.json`, XNA marks 45 processor properties virtual; CNA declared all 45 non-virtual, across `ModelProcessor` (14), `MaterialProcessor` (7), `TextureProcessor` (6), `ModelTextureProcessor` and `SpriteTextureProcessor` (5 each, inherited), `FontTextureProcessor` (3), `EffectProcessor` (2) and the two content attributes (1 each) -- 78 declarations. A non-virtual accessor fails silently: the derived class compiles, its override is never called, and the asset is built with the base's answer. The declarations are now virtual *and* each processor's own work reads through its getter rather than its field (31 call sites), which is the half that makes an override take effect. 26 build units in the corpus derive from a built-in processor. |
| `XNASWEEP-119` | A dictionary asset was refused for its value type, and written in the wrong order. | [x] Two defects, one asset. Movipa's `App.config.xml` (SAMPLE-133) declares `Generic:Dictionary[string,string]` and the genuine build wrote `App.config.xnb`, 692 bytes, rooted at `DictionaryReader`2[[System.String],[System.String]]`. CNA refused it -- *`importer returned undeclared output type`* -- because only `Dictionary<string,int>` was a known built-in; a dictionary of primitives is pipeline infrastructure rather than a type the game owns, so the missing instantiation is registered on both sides, writer and runtime reader, exactly as the `string,int` one already was. With it registered the file came out the right size and the wrong order: XNA's `DictionaryWriter` writes what enumerating the dictionary gives it, and a .NET `Dictionary<K,V>` that has only ever been added to enumerates in **insertion order**, so a built asset stands in its document's order. CNA held dictionaries in `std::map` and the XNB writer sorted on top of that, so both the intermediate and the `.xnb` came out in key order. The pipeline's dictionary is now `System::Collections::Generic::OrderedDictionary`, and the writer writes a container's own order (an unordered one is still sorted, so the same input still gives the same file). `App.config.xnb` is byte-identical. The committed `xml_dictionary.xml` fixture could not have caught this -- its keys were listed in alphabetical order, so document order and key order were the same file -- so `xml_dictionary_order.xml` lists `zulu, mike, alpha, tango`, and the genuine XNA build of it, taken through the differential oracle, carries exactly that. |
| `XNASWEEP-120` | Three runners set processor parameters the reconstruction could not see, so CNA was blamed for them. | [x] `runner_provenance.py` attributed a `SetMetadata("ProcessorParameters_X", "V")` call to *the nearest file-like string literal before it*, which is right only when the call sits beside the literal that names the asset. Three runners put it in a helper instead, where there is no literal to find: Audio3D's `Asset(file, name, importer, processor, bool generateMipmaps)` sets `GenerateMipmaps` only when its flag argument is `true`; RimLighting's six-argument overload takes the parameter's name and value as arguments; ShadowMapping's takes `params string[]` and walks it two at a time. All three came out with no parameters at all, so the sweep built Audio3D's three textures with one mip level against a nine-level reference and recorded the difference as CNA's -- 10 rows accused of a defect that belonged to the reconstruction. The extractor now finds the method enclosing each metadata call, and where that method has no literal of its own it reads the call sites instead, binding arguments to parameters by position and honouring an `if (parameter)` guard. `head.fbx` gets `DefaultEffect=EnvironmentMapEffect`, `grid.fbx` and `dude.fbx` get `CustomEffect` and `Scale`, and Audio3D's three textures get `GenerateMipmaps=True`. No other sample's provenance changed. |
| `XNASWEEP-121` | Every model standing on an exporter's quarter turn was rotated in `float`. | [x] `FbxImporter::LocalTransform` composed scaling, `PreRotation`, `Lcl Rotation` and translation through `Matrix::CreateRotation*`, which are `float` and call the `float` `std::cos`. XNA's FBX SDK composes in double and narrows once at the end, and a quarter turn is exactly where the two part company: `cos` of a `float` pi/2 is `-4.371e-08`, of a `double` pi/2 it is `6.123e-17`. SAMPLE-003's `Cube.fbx` carries `PreRotation -90` under `UnitScaleFactor 2.54`, and XNA's own `Cube.xnb` holds `2.54 * 6.123233995736766e-17 = 1.555301383669155e-16` on the two matrix entries the turn zeroes; CNA held `-1.11e-07` there, seven orders of magnitude away, on every model a Z-up exporter wrote. Composed in double and narrowed once, `Cube.xnb` goes from a whole-matrix disagreement to three bounding-sphere centre components differing in the seventh digit -- float accumulation order in `CreateFromPoints`, which is its own question. The genuine importer's own answer for `Cube.fbx`, read through `run-model-oracle.sh` with `CNA_MODEL_FIXTURES` pointed at the sample, confirmed CNA's importer was right about the transform in every other respect. Note that `Content-built/Cube.xnb` in that sample is a *different* genuine build from the other three copies (2,137 bytes against 2,044, root bone identity against the scaled one), so a sample can hold two references for one source that do not agree with each other. |
| `XNASWEEP-122` | An unnamed bone and a bone with an empty name were the same thing, and XNA writes them differently. | [x] XNA's `ContentItem.Name` is a nullable string, and the two empty values reach a built `.xnb` as different bytes: a null object against a zero-length string. The `.x` importer produces both -- a file with no enclosing frame gets a synthesized root that carries *no* name (measured: the genuine importer answers `/<null>` for `bare_mesh.x`), while a `Frame {` declares one that happens to be *empty* (measured over SAMPLE-028's `Car.x`, whose unnamed frames answer `""`). CNA held the name in a `std::string`, so the two collapsed, and the writer guessed: it wrote null whenever the name was empty. That is right for `bare_mesh.x` and wrong for the 17 corpus references whose `.x` files declare unnamed frames. `ContentItem`, `ModelBoneContent`, the canonical bone record and the XNB writer now carry the distinction; `Car.xnb`'s eight leading bone names match the reference exactly and `model_x_bare_mesh` still writes its null. The one place it is deliberately not carried is the *file* format of CNB Model schema 2, which is frozen and stores a name string: the flag rides beside it in memory -- which is what the `.xnb` route needs, since every built model passes through the canonical model -- and a model that has been through a `.cnb` file comes back with an empty name rather than none. |
| `XNASWEEP-123` | The mip residual is a dither in XNA's own filter, and now says so. | [x] `ACCEPTED_DIFFERENCE`, measured rather than assumed. `plans/plan_xnapipeline_parity.md` `XNAPP-254` had already found XNA's mip kernel -- a separable four-tap `{1/16, 7/16, 7/16, 1/16}` with clamped edges, and an exact area average on an odd dimension -- and recorded a residual of 61 bytes in 2,944 differing by one. On a real photograph the same residual is 9% of the bytes, so it was re-measured here over SAMPLE-059's 256x256 `CatTexture.tga`: level 0 is byte-identical, every generated level differs, every differing byte differs by exactly one, and **the sign is decided by the accumulator's own fraction**. Sorted by distance from the rounding tie the disagreement rate rises monotonically -- 0.5% at 112/256 from the tie, 19% at 80, 33% at 48, 45% at 16 -- and never crosses over: below the tie XNA is always the higher value, above it always the lower. That is a dither of about half a least-significant bit applied before quantisation. Nothing else reaches it: the kernel was re-fitted over every symmetric six-tap with integer weights and `{1,7,7,1}` is the optimum (a six-tap with any non-zero outer tap is worse by 2,500 bytes), and float32, float64, normalised and pairwise-summed arithmetic, and truncation, half-up, half-even and half-down rounding, move the count by at most eight bytes in 65,536. |
| `XNASWEEP-124` | A `.wav`'s loop was one frame short, because RIFF's loop end is the last frame played. | [x] `CnbSourceImport` read a `smpl` chunk's first loop as `loopLength = End - Start`. RIFF's `End` names the last frame *played*, so the region is inclusive and one frame longer. Spacewar's `Menu_Loop.wav` declares 67693-859611 and XNA's own `Menu_Loop.xnb` carries 791919; CNA wrote 791918. With the fix that file is byte-identical, all 3,438,557 of it. No committed audio-oracle case could have caught it: every one of them has no `smpl` chunk at all, so every recorded `loopStart` is 0 and every `loopLength` is the whole buffer. The two CNA fixtures that do declare a loop asserted the arithmetic rather than the format, and now assert the measured rule. |
| `XNASWEEP-125` | The `Song` rows, classified: XNA re-encodes to WMA and CNA cannot. | [x] `ACCEPTED_DIFFERENCE`, and two distinct shapes inside it. Where the source is not already WMA -- SAMPLE-060's `Music.mp3`, SAMPLE-065's `NinjAcademy_Music.wav` -- XNA's `SongProcessor` **re-encodes it**, and its `.xnb` names `Music.wma` where CNA's names the source it passed through; the duration follows the encode. There is no free WMA encoder, and `plans/plan_xnapipeline_parity.md` `XNAPP-202` already records that Wine carries no Windows Media Format runtime to measure one against. Where the source *is* WMA -- SAMPLE-062's `One Step Beyond.wma` -- the path matches and only the duration differs, by 19 ms in 366 seconds: the file's ASF File Properties Object says `PlayDuration` 367,645 ms and `Preroll` 1,579 ms, whose difference is exactly CNA's 366,066, while XNA answers 366,085, which is `PlayDuration` less 1,560 -- a number that appears nowhere in the header, so XNA is reporting what the codec decoded rather than what the container declared. Same reason, same conclusion. |
| `XNASWEEP-126` | A model's meshes were listed parent-first and each carried its own buffers; XNA does neither. | [x] SAMPLE-030's `tank.fbx` settles both halves. Its twelve meshes hang three deep under `tank_geo`, and XNA's own `tank.xnb` lists them `r_back_wheel, r_front_wheel, r_steer, r_engine, l_back_wheel, ..., canon, hatch, turret, tank` -- **every child before its parent** -- while its bone table is the plain depth-first order, parent first, which CNA already matched exactly. So the two lists are not the same walk, and CNA emitted meshes in the bone's order. The same file has **four** shared resources: one vertex buffer, one index buffer, and the two distinct effects. CNA merged batches only within one mesh, so it wrote a buffer pair per mesh and twenty-six shared resources; XNA merges every batch whose vertex declaration agrees into one pair for the whole model and lets each part address it with a vertex offset and a start index, which is what those fields are for. Walking the children before building the node's own mesh gives both at once -- the descendants' geometry reaches the shared buffers first and their meshes are listed first. `tank.xnb` goes from 158 recorded differences to 57, its shared-resource table from 26 entries to XNA's 4, and what is left is float noise on bounding-sphere centres and two bone translations. |
| `XNASWEEP-127` | An FBX material's colours ignored their factors, and its texture was never read at all. | [x] Three things, all on SAMPLE-030's `tank.fbx` and all confirmed against the genuine importer. **A colour is its `<Name>Color` times its `<Name>Factor`**: that file's `DiffuseColor` is (1,1,1) with a `DiffuseFactor` of 0.8, its own `Diffuse` compatibility property is (0.8,0.8,0.8), and XNA answers 0.8 where CNA answered 1. **A texture reaches the material through the model**, not through the material: the connections read `Texture::steamroller_tank61_file3 -> Model::r_engine_geo`, the `Texture` object carries the `RelativeFilename`, and it counts only where the geometry declares a `LayerElementTexture` -- the same fixture built without one answers a material with no texture. CNA parsed no `Texture` object at all, so every textured FBX model lost its texture and the nested build that produces it. **And a material is converted once**: `MaterialProcessor` replaces a texture reference with the built one *in place*, so converting `engine_phong` a second time handed it `engine_diff_tex_0.xnb` to build, which is outside the content root and refused the whole model. `tank.xnb` goes from 158 differences to 49 -- 45 bounding-sphere centres and two bone translations at 1e-7, and the two buffer digests that follow from them -- with its two effects carrying XNA's colours and XNA's texture names. A fourth thing fell out of the new fixture: a mesh that declares no normals gets generated ones **after** the channels it did declare, where CNA put them first. |
| `XNASWEEP-128` | An FBX carries one of *two* material property tables, and a texture belongs to one batch. | [x] SAMPLE-033's `Ship.fbx` is an FBX **6.0** whose materials are `Version: 101`: they name a bare `Diffuse`, `Specular` and `Emissive`, and their `Shininess` really is the specular power -- the genuine importer answers 29.54, 20 and 6.4 for its three materials. SAMPLE-030's `tank.fbx` is a 6.1 whose materials are `Version: 102`: `DiffuseColor` times `DiffuseFactor`, and `ShininessExponent`. Which table a file uses is what decides, not which properties are present -- the committed `fbx_two_materials.fbx` is a newer material that *also* carries `Shininess: 2`, and XNA answers the newer table's default of 20 for it. Reading only the newer table gave every 6.0 material a black diffuse and a specular power of 20. And a texture belongs to **its own batch**: `Ship.fbx` has three materials and one texture, and XNA puts that texture on the first batch and leaves the other two without one, where CNA had fallen back to the first texture for every batch. `Ship.xnb` goes from 27 differences to 5 -- three bounding-sphere components at 1e-7 and the two buffer digests that follow. A second fixture, `fbx_material_legacy.fbx`, carries the older table so both are measured. |
| `XNASWEEP-117` | *(second half)* Two batches of **one** model that name the same texture. | [x] Closed by the same rule the first half used, one level down. A twelve-mesh tank whose meshes share a material shares that material's texture with them, so one node asks for the same nested output nine times; the writer refused the repeat outright. It is now allowed when the two outputs really are the same -- same name, same asset type and schema, same reader, same bytes -- and still refused when they are not, which is the case the check exists for. Ten build units that had started failing the moment FBX textures were read build again. |
| `XNASWEEP-129` | What is left of the model family, re-framed: not the order inside a batch but the order *of the batches' triangles*. | [x] **Closed as a question; its answer is `XNASWEEP-139`.** The row's title claimed the vertex order inside a batch, and the reading behind it -- *675 texture coordinates differ and the normals follow them* -- was reading a *symptom*. `Ship.fbx`'s UV layer is not read wrongly at all: what differs is the order the triangles are emitted in, which renumbers the vertices and so permutes every channel at once. Two later measurements settle that. `XNASWEEP-143` found the real channel defect on that family of files -- a mesh has as many UV sets as its `Layer` blocks name -- and `XNASWEEP-139` found what actually permutes them: `MeshHelper.OptimizeForCache` is a cache-aware greedy reorder and CNA's stand-in reverses the list. Every reference this row owned is now owned by one of those two. |
| `XNASWEEP-130` | The sprite-font packer chooses a *wide* sheet where CNA chooses a tall one, 74 times out of 74. | [x] Evidence toward a rule the differential's one measurement could not settle. `decisions.json`'s `fonttexture_sheet_edge_touch` records the atlas width as the single thing about XNA's packer that two measurements disagree on -- two 5x7 glyphs go into an 8-wide sheet and three into a 16-wide one. The corpus is a much larger sample: over the 74 fonts whose atlas dimensions differ from CNA's, **CNA's sheet is taller than it is wide every single time**, and XNA's is wider than it is tall in 49 of them, 35 of which are the same area transposed -- `hudFont` is 128x64 against CNA's 64x128, `BigFont` 512x256 against 256x512. So the two packers do not disagree about area; they disagree about which way round to grow. It stays an `ACCEPTED_DIFFERENCE` because the glyph boxes differ first -- GDI+ against FreeType -- so matching the sheet would not make one of these files identical. Recorded because the next person to work on the packer should start from 74 measurements rather than two. |
| `XNASWEEP-131` | `FontTextureProcessor` gave the runtime a cell where XNA gives it the ink. | [x] Mostly fixed. SAMPLE-062's `NetRumbleFont.png` is a 256x256 sheet of 95 glyphs. XNA's `NetRumbleFont.xnb` carries a **128x156** atlas; CNA's carries the source sheet unchanged at 256x256. Glyph 0 shows both halves of why: XNA's bounds are `[124, 1, 1, 1]` with a cropping rectangle of `[7, 26, 8, 27]` -- a single ink pixel, its offset inside the original 8x27 cell recorded in the crop -- where CNA's are `[34, 117, 8, 27]` and `[0, 0, 8, 27]`, the whole cell with no trim. So XNA **trims each glyph to its non-transparent bounds, records the trim in the cropping rectangle, and repacks the trimmed glyphs into a new sheet**; CNA finds the glyph columns and stops there. The five `fonttexture/*` cases in the differential corpus all pass, because their glyphs fill their cells and their sheets are already as small as a repack would make them. The trim is now done, and the rule it needed was not the one the cell scan uses: a cell is bounded by the *separator* colour, and the ink inside it is bounded by **alpha** -- NetRumbleFont's cells are separated by magenta and padded with transparent *white*, so trimming on the separator alone finds no border at all. An empty cell -- the space -- answers a 1x1 box at the bottom-right corner, which is what a scan whose initial minimum is the last texel and whose extent is clamped to one produces. All 95 cropping rectangles now match, the line spacing matches (21, the tallest **trimmed** glyph, not the cell height), the atlas width matches, and every glyph's size matches; `NetRumbleFont.xnb` goes from 572 differences to 167. What is left is the packer: 163 placements and the atlas height. Three more rules fell out of it and the family is **closed**: `NetRumbleFont.xnb` is byte-identical, all 79,872 texels of it. **The packer is not a shelf.** CNA placed each glyph on a row as tall as its tallest member; XNA places each at the lowest row it fits in and the leftmost column of that row. Ordered tallest-then-widest -- which CNA already had -- that reproduces **every** placement of all five measured sheets: 95 of 95, 10 of 10, 3, 3 and 2. **The height is the rows used rounded up to a multiple of four**, and then to a power of two while it is still no more than 32. Thirteen real fonts in the corpus say the first half exactly -- `DebugFont` 64x112 from 111 rows, `NetRumbleFont` 128x156 from 154, `LargeGameFont` 512x348 from 346, and only one of the thirteen is a power of two at all -- and the four small differential sheets say the second: 16 rows answer 16, and 18 and 30 both answer 32. Where the threshold really is, this campaign cannot say: "no more than 32 rows" and "narrower than 64 texels" separate the same seventeen files. **And the atlas is premultiplied**, which is this processor's own `PremultiplyAlpha` defaulting to True: CNA validated that parameter and never applied it, so the sheet's transparent *white* padding reached the atlas as (255,255,255,0) where XNA writes (0,0,0,0). The product truncates -- `c * a / 255` -- which is the last byte between the two files. |
| `XNASWEEP-134` | A mesh's bounding sphere was grown by a rule that is algebraically XNA's and not XNA's in floating point. | [x] 123 of the corpus's model references carried a mesh bounding-sphere centre a few parts in ten million away from XNA's, and nothing about the model was wrong: `tile.fbx`'s vertex buffer is byte-identical to the reference's and its sphere is not. Handing the genuine `BoundingSphere.CreateFromPoints` the same points settled where the difference lives -- **the point set is the mesh's control points in the file's own order, duplicates and all** (24 of them for that mesh, not the 8 distinct corners), and XNA's answer for exactly those 24 is not the answer CNA's arithmetic gives for them. Three things were then measured, on 431 committed point sets run through the genuine assembly (`tools/xna-pipeline-oracle/framework/run-bounding-sphere-oracle.sh`, `tests/reference/xna40/framework/bounding-sphere-oracle.txt`). **`Vector3.Length`, `Distance` and `DistanceSquared` accumulate wider than `float`**: XNA is a 32-bit assembly and that arithmetic runs on the x87 unit, so `x*x + y*y + z*z` is summed at extended precision and narrowed once. Over 200 random pairs a `float` accumulation reproduces XNA's `Distance` on **none** of them and a `double` one on **all** of them. **The seed radius is half the distance between the widest pair**, not the distance from their midpoint to either end; the two differ by an ulp often enough to change which points the growth pass then touches. **The growth is `r' = (r + d) / 2` with the centre slid by `(1 - r'/d)` of the difference**, where CNA moved by half the overshoot along the unit direction and then took the radius as the distance to the point -- the same sphere in exact arithmetic, different bits in this one. Fitted over 226 single-growth point sets and confirmed on all 431: CNA now answers XNA's exact bits on every one, and the negative control -- the former growth, replayed against the same corpus -- disagrees on 288 of them, so the corpus separates the two rules rather than accepting both. A mesh's control-point list repeats its corners, which is what made this visible at all: the growth pass keeps meeting points that lie exactly on the sphere, and whether each one grows it is decided in the last bit. Minjie's six meshes go from six differing centres to six exact ones, and `tile.xnb` differs from its reference in nothing at all. |
| `XNASWEEP-132` | Eight of the corpus's 7,734 references were MonoGame's, not Microsoft's. | [x] `SAMPLE-003`'s `xna4-original/Content-built/` sits under a directory §1 calls genuine and holds output of the **MonoGame** content builder: beside it, `Content-obj/` holds seven `.mgcontent` documents -- MonoGame's own `PipelineBuildEvent` XML -- each naming a file in `Content-built/` as its `<DestFile>` and a **`.gltf`** under `gltf-intermediate/` as its `SourceFile`. So those seven were not built from the sample's `.fbx` at all, and the eighth, `TexturesAndColors.xnb`, embeds the `TexturesAndColors.fxb` beside it verbatim at offset 184, which is a wrapper rather than `EffectProcessor`'s output. The sample's genuine XNA build is the one its own `evidence/xna-content-pipeline.log` records, into `Content-xna-pipeline-all/`, and the copy under `TexturesAndColors/bin/` is byte for byte that. Comparing against the MonoGame eight cost five `UNEXPLAINED` model rows -- an identity root bone where XNA's build has the `.fbx`'s own 2.54 unit scale, and 147 vertices against 168 -- and three more that were accepted for reasons that were not the real ones. `corpus_inventory.py` now finds MonoGame's destination directories by reading every `.mgcontent` under the root rather than by guessing at a directory name, and the corpus is **7,726**. |
| `XNASWEEP-133` | Four samples' runners were invisible to the provenance reader, and 21 more were read as the wrong shape. | [x] `XNASWEEP-021` established that what produced a reference is the sample's own `BuildContent` runner rather than its `.contentproj`, and looked for that runner by **name**, in `scripts/`. Neither holds: `SAMPLE-133`, `SAMPLE-141` and `SAMPLE-142` call theirs `Xna4ContentDiagnostic.cs`, `Xna4AssetProbe.cs` and `RobotGameXna4StockProbe.cs`, and `SAMPLE-003` was pruned and keeps its only copy beside the output it wrote. The classifier was wrong too: it called a runner *project-faithful* only if the literal `.contentproj` appeared in it, so the 21 runners that take the project path as an argument -- `SAMPLE-073`'s among them -- were read as hand-listed and their assets built with the processor's defaults. That is why SAMPLE-073's `Base.png` came out with one mip level against a reference with seven: the project asks for mipmaps and the reconstruction dropped the request. A runner is now recognised by what it does -- constructing `BuildContent` -- anywhere under the sample, and read as project-faithful when it walks the project's `Compile` items and copies each item's child elements on to the task item **under their own names**. That last clause is `SAMPLE-146`'s: its runner strips the `ProcessorParameters_` prefix before calling `SetMetadata`, and `BuildContent` reads only that prefix, so none of that project's parameters ever reached the build and its four textures really are `Color` with one level where the project asks for `DxtCompressed` with mipmaps. 118 samples have a runner now, 39 of them project-faithful. |
| `XNASWEEP-135` | A `.dds` brings its own mip chain and its own blocks, and CNA kept neither. | [x] Measured over a four-level `.dds` whose every level carries the colour key at (0,0) and a half-transparent texel beside it, plus a DXT3 one with no mips, in eight processor configurations each (`texture/dds_mipchain_*`, `texture/dds_dxt3_*` in the differential corpus). **The chain survives**, and every step reaches every level: XNA's four output levels are each keyed and premultiplied, where CNA converted level zero and threw the rest away -- SAMPLE-073's `Stripe2.dds` came out with one level against a reference with ten. **`GenerateMipmaps=True` does not regenerate a chain that already exists**: the same file built with it is byte for byte the file built without it. **`TextureFormat=NoChange` is the type the texture arrived as**, so a DXT3 source stays Dxt3 -- decoded, keyed, premultiplied and *re-encoded*, which is why it is not the file's own bytes. **`PassThroughProcessor` is**: it writes the source's blocks verbatim, unkeyed and unpremultiplied, and CNA had been routing it through the texture processor's defaults instead. And **`ResizeToPowerOfTwo` resizes every level**, which on a 6x6 with levels 3x3 and 1x1 produces 8x8, 4x4 and 1x1 and stops the build with XNA's own *Invalid texture. Face 0 mip 2 is sized 1x1, but should be 2x2.* -- a refusal CNA did not have. |
| `XNASWEEP-136` | The mip residual `XNASWEEP-123` could not name is an ordered dither, and here is its matrix. | [x] `XNASWEEP-123` measured the residual exactly -- every differing byte differs by one, the sign decided by the accumulator's own fraction, the disagreement rate rising monotonically to 45% at the rounding tie -- and called it *a dither of about half a least-significant bit* without being able to say which one. The matrix came from somewhere else entirely: sweeping all 32 five-bit and all 64 six-bit endpoint values of a block-compressed `.dds` through the genuine pipeline shows the *same* dither in D3DX's endpoint expansion, and there it is readable directly. A flat block of endpoint 1 comes out 9 in four of its sixteen texels and 8 in the other twelve; endpoint 2 comes out 17 in seven and 16 in nine; endpoint 9 comes out 75 in one and 74 in fifteen -- the fractions of 255/31, 510/31 and 2295/31 to within a sixteenth. Fitted per texel the thresholds are exactly `{{0,8,2,10},{6,14,4,12},{3,11,1,9},{5,13,7,15}}`, and the rule is `up if 32*remainder >= denominator*(2*threshold+1)`. Applied to the mip filter's accumulator it takes SAMPLE-059's `CatTexture` from 5,904 differing bytes in its first level to 9, and SAMPLE-059's `checker` and SAMPLE-130's `Grid` to **none at all on every level** -- `checker.xnb` is byte-identical. Applied to the block decoder -- where a colour is the exact rational `255*n/d` and only the store narrows it -- all six DXT3 measurements come out exact, 8,704 texels with none differing. The renderers keep the hardware rule, which is what the hardware beside them answers; only a build takes D3DX's. |
| `XNASWEEP-137` | The sprite-font atlas width is not a search, and its height is the profile's. | [x] `XNASWEEP-130` counted 74 fonts whose atlas differs from CNA's and could say only that XNA grows wide where CNA grew tall; `XNASWEEP-131` replaced the shelf packer with XNA's placement rule and left the width. The width is a formula: **the integer square root of the glyphs' own total area, rounded up to a power of two** -- no margin, no gap, and the truncation matters. Measured over every one of the **195 distinct sprite fonts in the corpus**, `.spritefont` and font sheet alike: the rule answers the reference's own width on all 195, and with it the placement rule reproduces **every glyph box of all 195**. The height is the profile's, on both routes: HiDef rounds the rows used up to a multiple of four, Reach up to a power of two -- 195 of 195, where `XNASWEEP-131`'s *multiple of four, then a power of two while no more than 32* was right for 30 of the first 60 and could not distinguish its own two candidate thresholds because both separate the same files. SAMPLE-070's `DamageFont` and `HudDetailFont` go from a 256x64 sheet against XNA's 128x132 to **byte-identical**, and every glyph rectangle, cropping rectangle and kerning triple already agreed. |
| `XNASWEEP-138` | A Lambert material has no specular power, and CNA gave it one. | [x] SAMPLE-035's and SAMPLE-036's five models each carry a `BasicEffect` whose specular power XNA writes as **16** and CNA wrote as 20. 16 is `BasicEffect`'s own default, which is what the writer emits when the material has no specular power at all -- and the genuine importer's answer for `SphereLowPoly.fbx` carries a diffuse colour, an emissive, an alpha and a specular *colour* and **no `SpecularPower`**. The file is a `Version: 102` material with `ShadingModel: "lambert"` and neither `ShininessExponent` nor `Shininess`. `XNASWEEP-128` had read the newer table's default of 20 off `fbx_two_materials.fbx` and the older table's `Shininess` off `Ship.fbx`, and both of those are Phong; every material measured that answers a specular power is. So the rule is the shading model: a Lambert leaves it unset, and 10 corpus references stop carrying 20 where XNA carries nothing. |
| `XNASWEEP-140` | A `Texture` names its file twice, and XNA takes the one that resolves. | [x] Spacewar's `bfg_proj.fbx`, `p1_rocket_proj.fbx` and `p2_rocket_proj.fbx` built nothing at all -- *primary source `texture/bfg_proj.tga` is not a regular file* -- and six references stood at `UNEXPLAINED` for it. The file names its texture twice and the two disagree: the `Texture` object's `FileName` is `../textures/bfg_proj.tga` and its `RelativeFilename` is `../texture/bfg_proj.tga`, one letter apart, and only the first is a directory that exists. XNA's own build writes `..\\textures\\bfg_proj_0`, so it took the `FileName`. Preferring `FileName` outright is wrong for the other direction, which the corpus also has: SAMPLE-005's `saucer.fbx` names `saucer_p1_diff_v1.tga` there and `saucer_texture.tga` in `RelativeFilename`, only the second is beside it, and XNA writes `saucer_texture_0`; and SAMPLE-138's `model.fbx` carries an authoring machine's `E:/dev/xna/user/mitchw/...` in `FileName` against a bare `tile1.png`. Of the 197 `.fbx` files in the corpus 101 name the same path twice and 11 do not. **The one that resolves is the one XNA records**, matched the way Windows matches a path (`XNASWEEP-115`), and the spelling kept is the one that resolved. Two fixtures separate the branches and the genuine importer answers `texturepath/fbx_texture_a.tga` for one and `fbx_texture_b.tga` for the other; the oracle prints a reference's directory now rather than only its file name, because the file name alone cannot tell the two apart -- which is why the importer's own replay test could not have caught this. |
| `XNASWEEP-141` | Which texture a batch gets is its polygons' own `TextureId`, not its ordinal. | [x] `XNASWEEP-128` read *a texture belongs to its own batch* off SAMPLE-033's `Ship.fbx` -- three materials, one texture, and XNA puts it on the first batch -- and implemented it as *texture i belongs to batch i*. The mesh says so directly and says something else: `Ship.fbx`'s `LayerElementTexture` is `ByPolygon` and its `TextureId` array carries **0** on material 0's 5,942 polygons and **-1** on the other two materials' 2,228, so the ordinal rule was right there by coincidence. Spacewar has both directions: `p2_pencil.fbx`'s *second* material's polygons are the textured ones and its first material's are all -1, so XNA leaves the first batch without a texture and CNA gave it one; `p2_saucer.fbx` and `p2_wedge.fbx` have the texture on two of three batches and not the middle one. With the polygons' own ids read, every one of Spacewar's twenty models carries exactly the texture references XNA's build carries. The batch's **first** polygon decides rather than its first textured one: `fbx_texture_second_batch.fbx` is one batch whose two polygons carry -1 and 0, and the genuine importer answers a material with no texture at all. |
| `XNASWEEP-142` | The colour key matches on four channels, not three. | [x] `plan_xnapipeline_parity.md` `XNAPP-251` left this open in as many words -- *nothing measured here says whether that alpha takes part in the match: every case the corpus has is an opaque key against an opaque texel* -- and matched three channels rather than guess. The corpus has the case now. SAMPLE-070's `Potion3h.png` carries sixteen texels that are exactly the key colour (255,0,255) at an alpha of **254**, and XNA's own build keeps every one of them: they arrive in the reference as (254,0,254,254), which is (255,0,255,254) premultiplied. CNA cleared them to transparent black, and that one texture was the last `TextureProcessor` reference in the unexplained set. A fixture pins both directions -- the key colour at alpha 255 and at alpha 254 in one 4x2 image -- and the genuine build keys the first and keeps the second. |
| `XNASWEEP-143` | A mesh has as many UV sets as its `Layer` blocks name, and the block's number is the channel's. | [x] Three things, one rule. **A `LayerElementReflectionUV` is a UV set**: SAMPLE-131's `p1_piece.fbx` declares only that spelling -- an older Maya exporter's -- and XNA's build carries a `TextureCoordinate0` for it where CNA carried none, so its vertex is 32 bytes against CNA's 24 and its buffer holds 237 vertices against 194, because the missing channel merged corners XNA keeps apart. **A mesh can declare more than one**: its sibling `p1_piece_tile.fbx` declares a `LayerElementUV` *and* a `LayerElementReflectionUV`, and XNA answers both. **And the index is the `Layer` block's own number**, not the set's ordinal: SAMPLE-035's `SphereHighPoly.fbx` declares its single UV set in `Layer: 1` rather than `Layer: 0`, and XNA's vertex declaration carries usage index **1** -- four references differed in nothing else. Where two sets share a block the second takes the next free index, which is what `p1_piece_tile` needs. Two fixtures measure the two halves and the genuine importer answers `TextureCoordinate1` alone for one and both indices for the other. Minjie's `tile.xnb` and `tile_highlight.xnb` are byte-identical now. |
| `XNASWEEP-144` | A `.x`'s `MeshNormals` carries its own face list, and CNA read it as one normal per position. | [x] SAMPLE-032's `Cube.x` has **8** positions and **6** normals -- one per cube face -- and twelve `3;n,n,n;` rows saying which corner gets which. Read as though a normal belonged to a position, that mesh comes out with 8 vertices where XNA's own build has **24**: a vertex is a position *and* its channel values, which `XNASWEEP-112` established on the FBX route and the `.x` route did not have. SAMPLE-057's `cylinder.x` is the same shape, 97 vertices against 135. The vertex key is now the pair, and `normal_per_face.x` -- a quad whose two triangles name one normal each -- measures it: the genuine importer answers six vertices for four positions, with `positionIndices 0,1,2,0,2,3`. |
| `XNASWEEP-139` | `MeshHelper.OptimizeForCache` reorders triangles; CNA's stand-in reverses the list. | [x] **Closed as a question; it is `XNASWEEP-149` now, which carries ten more probes and the restart rule.** The reading below is what the first ten probes said and is kept because `XNASWEEP-149` builds on it; the row is not separately actionable. -- and the last thing standing between the model family and zero.** `XNASWEEP-129` read *the vertex order inside a batch* off SAMPLE-033's `Ship.fbx` and could not say what set it. It is `MeshHelper.OptimizeForCache`, which `ModelProcessor` runs, and CNA implements it as *take the triangles in reverse and renumber the vertices in the order the reversed list first reaches them* -- fitted to two committed measurements, a quad and a 3x3 grid, that could not distinguish it from anything else. **They could not because their vertices are not shared**: those fixtures give one vertex per corner, where a cache optimiser has nothing to gain and answers the reverse. Ten new probes -- strips, grids and UV spheres, each built twice, one vertex per corner and one per position -- separate the two: every unshared mesh comes out reversed, and every *shared* grid and sphere comes out **reordered**. SAMPLE-035's `SphereLowPoly.fbx` is the corpus's own case: its 120 triangles come out of the genuine build in the order 119, 108, 73, 94, 95, 92, 93, 118, 117, 116, ..., which is not the file's order and not its reverse. What it is has not been settled here. Forsyth's linear-speed optimiser reproduces the strips and none of the grids or spheres, at every cache size and both tie-breaks; Tipsify likewise; a greedy that maximises the cached vertices misses 31 of 488 measured decisions at its best cache size. A linear program over the 488 one-step decisions -- *given the cache state XNA was in, was its pick the argmax?* -- is feasible for a general `score(cachePosition, remainingValence)` table and **infeasible for any additive** `cacheScore(position) + boost(valence)`, which rules out the whole Forsyth family. The fitted table does not reproduce the *sequences* when simulated, though, which says the one-step constraints are not the whole rule. **What the output actually looks like is a triangle strip walk**, and that is the lead the next attempt should start from: of grid_2's eight triangles, seven consecutive pairs share an edge, and of sphere_2x4's sixteen, thirteen do; the shared edge is the *emitted* triangle's first two vertices every time, and the two breaks are where no unused triangle shares one. The strips have a length: grid_6 breaks after 7, 11, 12, 12, 12, 12, 5 and 1 triangles and sphere_5x12 after 12, 11, 11, 11, 13, 9, 9, 9, which is what a cache-size cutoff looks like rather than a topological dead end -- at 465 of the 478 steps an unused edge-adjacent triangle existed, and at 30 of them XNA took something else anyway. What remains unmeasured is the seed, the choice when two or three triangles share the edge (the pick is the lowest-numbered in 104 of those and the highest in 98, so it is neither), and when the walk leaves a strip that could still be continued. 141 references still differ only in the buffers this permutes, and the ten probe meshes and their measurements are committed with the graphics oracle. |
| `XNASWEEP-145` | A scene with five meshes loses one of them, and lists the other four backwards. | [x] **Fixed.** SAMPLE-047's `table.FBX` connects five `Mesh` models to `Model::Scene` -- `TableTop`, `BackRightLeg`, `BackLeftLeg`, `FrontLeftLeg`, `FrontRightLeg` -- and XNA's build answers a synthesized `RootNode` with exactly those five under it, in the file's connection order. CNA answered `RootNode` with **four**, `TableTop` gone and the four legs backwards. Two separate faults, both in how the object table is keyed and walked. **(1)** An FBX 6 file names an object by the whole declared string, prefix included, and the importer keyed on the bare name: `Material::TableTop` overwrote `Model::TableTop`, so `Connect: "OO", "Model::TableTop", "Model::Scene"` put the *material* in the scene and left the model attached to nothing -- neither in the scene nor a child, so it fell out entirely. The prefix is now part of the key, with the bare name kept only as a fallback for a file that connects without one, first declaration winning. **(2)** The objects of a pre-7000 file are keyed by a descending synthetic identity, so walking the map to collect the roots walked the file backwards; the roots are now collected in declaration order. Measured on `fbx_scene_order.fbx`, three meshes and a material sharing the second mesh's name: XNA answers `/RootNode/{Alpha,Beta,Gamma}`. Regression: it joins `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`; with either rule removed that test fails on this fixture (verified both ways). Four references. |
| `XNASWEEP-146` | The geometry's own offset from its node, and the flag that decides whether `PreRotation` counts. | [x] **Fixed, two rules, both measured.** SAMPLE-047's `Sphere.fbx` reaches XNA's own `Sphere.xnb` with a root bone scaled 0.508 and translated `(0, 0.254, 1.555e-17)`, where CNA answered a plain 2.54 and no translation at all; the file's `Lcl Scaling` is 1 and its `Lcl Translation` is 0, so neither number is in the local transform. They are in FBX's **geometric transform**: that model carries `GeometricScaling 0.2` and `GeometricTranslation (0,0,0.1)`, and `0.2 * 2.54 = 0.508` while `(0,0,0.1)` turned by the model's own `PreRotation -90` and scaled by 2.54 is exactly `(0, 0.254, 1.555e-17)`. The same sample's `Cylinder.fbx` settles it without any rotation or unit scale in the way: `GeometricScaling 0.25`, `GeometricTranslation (0,0.25,0)`, and XNA's bone is `diag(0.25)` translated `(0,0.25,0)`. The offset belongs to the *geometry*, not to the node, so a child must not inherit it, and `fbx_geometric_offset.fbx` shows XNA doing exactly that: the offset node's child, whose own local transform is the identity, comes back carrying the **inverse** of its parent's geometric transform, so that its absolute transform is the parent's local one alone. CNA now composes `S(GeometricScaling) * R(GeometricRotation) * T(GeometricTranslation) * local`, and hands every child the inverse to undo it. **The second rule** fell out of the same fixture: it carries `PreRotation -90` and XNA does *not* apply it, where `fbx_prerotation_units.fbx` carries the same `PreRotation -90` and XNA does. The difference is `RotationActive`, which the second file sets and the first does not -- FBX's own rule, and CNA applied `PreRotation` unconditionally. Regression: `fbx_geometric_offset.fbx` joins `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`, and removing either rule fails it (verified separately for each). |
| `XNASWEEP-147` | The model oracle measured the directory, not the file. | [x] **Fixed; and it invalidated nothing, which had to be checked rather than assumed.** Adding `fbx_geometric_offset.fbx` to `tests/assets/xna40/model` changed XNA's answer for a file that does not name it: `fbx_texture_second_batch.fbx` came back with a material carrying `fbx_material_texture.tga` where the committed reference had a material with no texture. Deterministic in both directions -- three runs with the new file, three without, no other change -- so it is not flakiness but state the FBX SDK carries from one import to the next inside one process, which `new FbxImporter()` per file does not clear. `ModelImportOracle` now takes a third argument: `--list` prints the case names in order and measures nothing, and a case's name measures only that one. `run-model-oracle.sh` lists the cases and runs **one process per case**, merging the results in list order. The re-measured reference is byte for byte the committed one plus the new fixture's line, which says the contaminated reading was the transient and `XNASWEEP-141`'s rule -- the batch's first polygon decides -- was measured on the clean one. |
| `XNASWEEP-148` | A `.x` file's normals are values, and two entries holding the same three numbers are one normal. | [x] **Fixed.** SAMPLE-057's `cylinder.x` has 97 positions, 114 faces and a `MeshNormals` list of **418** entries over only **21 distinct values**. XNA's build answers 135 vertices, which is exactly the count of distinct *(position, normal value)* pairs; CNA answered 418, the count of distinct *(position, normal index)* pairs -- one vertex per normal entry, none of the duplicates collapsed, and a vertex buffer three times the size (10,032 bytes against 3,240). `XNASWEEP-144` had the shape of the rule right -- a vertex is a position and its channel values -- and keyed on the index, which is not the value. The importer now folds every normal entry onto the first entry holding the same three floats and keys on that. Measured on `normal_duplicate_values.x`, which writes one normal six times under six indices across two triangles: XNA answers four vertices. Regression: it joins `XnaXImporter.EveryFileAnswersTheGraphXnaAnswers`, and keying on the index again fails it. |
| `XNASWEEP-149` | `OptimizeForCache`, round three: 233 probes, measured on the genuine method. | [ ] **Open; ten facts it did not have before, and two questions left, each isolated to one decision.** `tools/xna-pipeline-oracle/optimize/` is a laboratory for this one method: a deterministic generator of small meshes, a driver that runs the genuine method over them under Wine, and the analysis that scores a candidate reconstruction. 233 probes are frozen under `tests/reference/xna40/optimize/`. **Settled:** the emitted triangle keeps its input corner order in all 233 answers, so the answer is a permutation of the face list; vertex coordinates are ignored (`grid_4x4_far_coords` scales by 1000 and answers identically); vertex numbering is ignored (`_vertexflip`, `_vertexshuffle`); winding is ignored (`strip_8_wind`); the input face order decides (`_perm3`, `_perm7`, `_shuffled` all differ); **a connected component's answer does not depend on what was emitted before it** (`hist_grid44_soup6_after`, `hist_grid42_twice`), which rules out the emitted count and any cache carried across components; components come out highest-face-index first, which is why a mesh with no shared vertices comes back exactly reversed; the answer is a sequence of runs, each a generalized triangle strip that from a face entered across `(older, newer)` with third vertex `r` crosses to the face carrying `(newer, r)` or the one carrying `(older, r)` -- and the second is taken in the corpus **even where the first is available**; an open strip or fan runs uncapped (`row_24` answers 48 faces in one run); a closed fan of nine or more stops at exactly seven where one of eight does not stop at all, and a two-ring cylinder does the same. **Ruled out by implementation and measurement:** a global greedy over a cache-position-and-valence score answers 44 of 120, a fanning-vertex walk 1 of 120; a search over every lexicographic rule of depth three over nineteen features reaches 1,954 of 2,316 decisions teacher-forced and stops there. **Left:** the seed's tie-break -- "the highest-index unused face containing a vertex of minimum remaining live count" reproduces 279 of 366 seeds and preferring a still-cached vertex 329, but `cfan_9` seeds at the *lowest* such face and `sphere_2x4_reversed` at a face with no minimum-live vertex at all -- and when a run ends: `grid_4x4`'s first run and its second reach, after eight faces, states identical in every feature the walk can read (run length, live counts on same-shaped candidates, cache ages, no preferred edge, the same available swap) and the first restarts while the second takes the swap. The production implementation is unchanged; a reorder fitted to part of a corpus is a guess dressed as a fix. | **Round four adds a rule that is exact, and one correction to how it was found.** An edge's faces are looked up in input order; the walk crosses to the first one that is not the current face, and where that first candidate is wound the *same* way as the current face -- which no strip walk can enter -- the run **ends**, rather than the second candidate being tried. `ins_f8_at07` puts the consistently wound face at index 7 and the other at 8 and the walk crosses; `ins_f8_at08` swaps the two and the walk restarts at the far end of the strip although the consistently wound face is still there one index on; `chain_j5_a15_b0` and `chain_j5_a0_b15` hold the neighbour's own index fixed and move only the two faces sharing its edge, and the answer follows those. The correction: the pendant triangle those probes glue to face `T` shares that face's first edge, which a strip already gives two faces, so 108 of the 112 probes hold an edge with **three** faces and the clean `j < T` threshold they show is a proxy for which of the two comes first -- not, as first read, the index deciding on its own. Two refinements follow, both scored on all 372 probes: the edge lookup itself, and taking the seed's pivot as the *last* minimum-live corner of its face rather than the first, worth 18 probes. Four readings of the lookup were scored -- 135 for a plain adjacency walk, 160, 178 and 179 -- and **the 178 one is kept**, because the three probes that discriminate all want it and the 179 one is wrong wherever the corpus can tell: `ins_f8_at08` restarts because the first *unused* face on its edge is wrongly wound, `strip_8_rot1` walks its whole strip forward, which needs the used face the edge lists first to be passed over, and `grid_4x4` leaves its first run after two faces under any reading that stops at a used face. The reconstruction stands at **178 of 372**, nineteen below a rule fitted to the aggregate. Production is unchanged.
| `XNASWEEP-150` | SAMPLE-142's skeletons: XNA answers an identity root where the file gives it a quarter turn. | [x] **Fixed; it is `XNASWEEP-160`'s rule, one case further on.** Every RobotGame mech connects exactly one object to `Model::Scene`, and it is the skeleton root -- `Duskmas.FBX`'s `Model::Root`, a `LimbNode`. `XNASWEEP-111` makes a single top-level object the scene's root node, and `XNASWEEP-160`'s promotion then re-expresses the bone's transform *against that root*, which is the bone itself: `M * M^-1`, the identity. `cs_bone_dusk` hands the genuine importer that node alone, with the file's own ten transform terms, and it answers the identity; `cs_null_dusk` is the same node as a `Null` and keeps its quarter turn, which is the control. The residues the sweep recorded -- `9.157e-17` is `1.49544 * 6.123e-17`, the double `cos` of a right angle -- are what `M * M^-1` leaves when the FBX SDK composes `M` in double, and CNA computes the same expression in `float`, so a mech's root is the identity to within them rather than bit for bit. Fifteen references. **SAMPLE-138's `photograph.fbx` is not this and is now `XNASWEEP-162`**: its root is a `Mesh`, the genuine *importer* answers it with the file's own `0.01` scaling (`cm_meshroot_photo`), and what divides that scaling out happens later, in the processor. |
| `XNASWEEP-151` | An FBX 7 mesh is two objects, and the second is not a node. | [x] **Fixed.** FBX 6 writes a mesh's `Vertices` inside its `Model`; FBX 7 splits them into a `Geometry` object connected to it. SAMPLE-061's `marble.FBX` is the second shape -- version 7100, binary -- and XNA's build answers **one** bone, `marble`, carrying the mesh. CNA answered **two**: `marble` with no mesh, and a nameless child that had it, because the importer read every `Geometry` as a node of its own. A `Geometry` object is now marked as the mesh data of the model it connects to: it never becomes a node, never becomes a root, and the model it hangs off builds its mesh from it. Measured on `fbx7_geometry.fbx`, this repository's first FBX 7 fixture, written in 7100 ASCII; XNA answers `/marble` as a `MeshContent` with the geometry's three positions. Authoring it turned up a rule of the format worth writing down: the SDK links the geometry to the model only where the model's `Properties70` carries `DefaultAttributeIndex`, and without it XNA answers the same node as a bare `NodeContent` with no mesh at all -- measured both ways. Regression: it joins `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`, and reading `Geometry` as a node again fails it. |
| `XNASWEEP-152` | A node's transform has ten terms, and CNA composed four. | [x] **Fixed.** FBX's own formula, in the order a row vector meets them, is `Sp^-1 . S . Sp . Soff . Rp^-1 . Rpost^-1 . R . Rpre . Rp . Roff . T`. CNA composed `S . Rpre . R . T`, which is four of the ten, and the missing six are not decoration: SAMPLE-138's `photograph.fbx` gives its only mesh a `ScalingPivot` of about `-(1.06, 6.89, 6.43)` against a `ScalingOffset` that nearly cancels it, and XNA's own build holds what is left over -- `(2.35e-06, 1.53e-05, -1.42e-05)` -- where CNA held a clean zero. Measured on two fixtures. `fbx_pivots.fbx` sets `RotationOffset (1,0,0)`, `RotationPivot (0,2,0)`, `ScalingOffset (0,0,3)` and `ScalingPivot (4,0,0)` beside a scaling of 2, a 30-degree turn about Z and a translation of `(5,6,7)`, and XNA answers the translation `(3.535898, 4.267949, 10)` -- which the formula reproduces exactly, term by term, and `S . R . T` cannot. `fbx_postrotation.fbx` sets `PostRotation (0,0,15)` beside `PreRotation (20,0,0)` and `Lcl Rotation (0,0,30)`, and its answer settles something no earlier fixture could: **`R` comes before `Rpre`**, because every fixture until now set only one of the two. CNA had them the other way round. `PostRotation` is gated by `RotationActive` alongside `PreRotation`. Regressions: both fixtures join `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`; dropping the six terms fails the first and only the first, and putting `Rpre` back before `R` fails the second and only the second. |
| `XNASWEEP-154` | The scene's order is the order it is *connected* in, not the order it is declared in. | [x] **Fixed; a correction to `XNASWEEP-145`, caught by the corpus and not by the fixture.** 145 replaced a reversed root list with the order the `Objects` block declares, and `table.FBX` -- whose declaration order and connection order are the same -- could not tell the two apart. Run 14 could: four SAMPLE-142 models got *worse*, `Hammer.FBX` by 279 differences, `MaomingT1.FBX` by 235, `France.FBX` by 102 and `AircraftCarrier.FBX` by 45. `France.FBX` says why: it declares `Sky` before `Object04` and connects `Object04` to `Model::Scene` first, and XNA's own build answers `RootNode` with `Object04, Sandbag20, Box_Big03, Truck06, Barricade_11` -- the connection order, with `Sky` (which nothing connects to the scene) absent. The roots are now collected in the order the `Connect` lines put them in the scene, with the declaration order kept only as the fallback for a file that connects nothing at all. `France.FBX`'s bone list and mesh list now match XNA's exactly; what is left of that model is a mesh-part difference, which is its own question. `fbx_scene_order.fbx` was re-authored so its three meshes are declared `Gamma, Alpha, Beta` and connected `Alpha, Beta, Gamma`: XNA answers the connection order, and the declaration order fails the fixture. |
| `XNASWEEP-153` | One number in `tank.fbx` comes back one float apart, and it is not the parser. | [ ] **Open, and wider than this row said.** The account here was one component of one translation; the built `.xnb` differs in **80** numbers, and the shape of most of them is not a translation at all. `tank.xnb`'s `bones[0]` is XNA `[?, ?, -5.09e-15, ?, ?, 0.9999999403953552, ...]` against CNA's `[..., 0.0, ..., 1.0, ...]`: the root basis XNA answers is `diag(1, 0.99999994, 0.99999994)` with `+-5e-15` off its diagonal, where CNA answers the identity. The file's root, `tank_geo`, carries `Lcl Scaling 0.00999999977648258` -- which is `float(0.01)` written back as a double -- under `UnitScaleFactor 100`, and `0.99999994` is one unit of last place below one. The original observation stands and is still unexplained: the two back wheels' `Lcl Translation` Z, written `-234.273040771484`, comes back as the adjacent float toward zero. Ruled out here in addition to the five parsing routes the row already names: eight more decimal routes (mantissa over a power of ten, digitwise in float and in double, repeated division, repeated multiplication by 0.1), a float and a double round trip through the parent's translation, and every single float multiplier near one that a `0.01`-and-`100` pair can make -- each rounds to exactly `1.0f` and changes nothing. **One expression does reproduce it exactly**, `float(float(v * 0.01f) * 100f)`, and it is *rejected*: over the file's sixty translation components it changes ten where XNA changes two, and two of the ten are the same wheels' Y, which XNA answers exactly. Ten references. |
| `XNASWEEP-155` | A mesh's batches come out in the order its polygons first name a material. | [x] **Fixed.** CNA walked a mesh's materials in the order they are connected, index 0 upward. XNA walks them in the order the *polygons* first name them. SAMPLE-142's `France.FBX` settles it beyond argument: `Object04` carries 25 connected materials and 3,354 polygons over 23 of them, and XNA's own build answers batches of 840, 28, 28, 252, 56, 81, 179, 863, 221, 200, ... triangles, which is the first-use order of the material indices `14, 16, 15, 17, 18, 8, 6, 3, 12, 0, ...` -- computed from the file's own `Materials` array and matching all twenty-three, in order. The connection order answers 200, 8, 863, 81, ... , which is what CNA held. The batches themselves were already right; only their order was not, which is why the difference reads as a permutation of the same vertex and primitive counts and a renumbering of every shared resource that follows. Measured on `fbx_material_order.fbx`: two triangles, `First` and `Second` connected in that order, the polygons naming them the other way, and XNA answers `Second`'s batch first. Regression: it joins `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`, and walking the connection order fails it. |
| `XNASWEEP-156` | A mesh with no normals got a constant `(0,0,1)`, which is right only in the XY plane. | [x] **Fixed.** CNA wrote `(0, 0, 1)` into the normal channel of every FBX mesh that declares none. Every fixture the campaign had was drawn in the XY plane, where that constant happens to *be* the answer, so nothing caught it -- it took a triangle out of the plane, `fbx_float_rounding.fbx`, whose XNB XNA fills with `(0.5773503, 0.5773503, 0.5773503)` and CNA filled with `(0, 0, 1)`. XNA computes them: each polygon's own unit normal, `(b-a) x (c-a)` normalized, summed onto every control point the polygon names and normalized once at the end -- a per-control-point average, not per corner, so the vertices do not split. Measured on `fbx_generated_normals.fbx`, two triangles sharing an edge with face normals `(0,0,1)` and `(0,-1,0)`: XNA answers `(0, -0.707107, 0.707107)` on the two shared positions and each face's own normal on the other two, and four vertices rather than six. `fbx_generated_normals_area.fbx` is the same fold with one face **four times** the other's area and XNA answers the *same* normal, which rules area weighting out -- an area-weighted average would be `(0, -0.970143, 0.242536)`. Regression: both join `XnaFbxImporter.EveryFileAnswersTheGraphXnaAnswers`, and the constant fails both and only those two. |
| `XNASWEEP-157` | One index buffer for the model, and a shared-resource table in first-reference order. | [x] **Fixed, two rules, and SAMPLE-142's `France.FBX` went from 52 differences to 3.** **(1)** `ModelProcessor` started a new *index* buffer whenever a batch's vertex declaration did not match the one it was merging into. XNA starts a new vertex buffer there and keeps the one index buffer: `France.FBX` carries a 36-byte mesh and three 32-byte ones, and XNA's own build answers **two** vertex buffers and **one** index buffer of 24,855 indices whose `startIndex` runs 0, 17,811, 18,591, 24,423 straight through the second vertex buffer's meshes. CNA answered two of each with the second mesh group's `startIndex` restarting at 0. **(2)** The schema-2 `Model` -> XNB conversion concatenated its three tables -- every vertex buffer, then every index buffer, then every effect -- where XNA's writer emits a shared resource the **first time the graph names it**. For `France.FBX` that is `vertex buffer (1), index buffer (2), the first mesh's twenty-three effects (3..25), the second vertex buffer (26), three more effects`, and CNA's grouping put its second vertex buffer at 2 and shifted every effect and index-buffer reference after it. The conversion now walks the meshes and their parts and emits each buffer and effect the first time it is referenced, with anything the graph never names appended after, so no table entry is lost. What is left of `France.FBX` is three buffer digests, which is `XNASWEEP-149`. |
| `XNASWEEP-158` | The two gap classes, audited rather than asserted. | [x] **Audited; both hold, and one of them names its own remedy.** `CUSTOM_PIPELINE_GAP` (1,370): a random twelve were opened and every one checks out -- `Maps/Chests/BronzeGear.xml` declares `<Asset Type="RolePlayingGameData.Chest">`, a type only SAMPLE-070's own assembly defines, and SAMPLE-048's `TrianglePickingProcessor` is a class in that sample's own `TrianglePickingPipeline/TrianglePickingProcessor.cs`. Neither can be built without loading a .NET assembly, which is the boundary, not a defect. `CORPUS_GAP` (931) splits three ways. **527** are a model's side outputs -- `cat_0.xnb`, `lizardeye_diff_0.xnb` -- under a project's output root that no project item names, and they exist only because the model that names them does; where that model is itself a custom-pipeline gap, so is its texture. **37** name a source file the tree has not got. The remaining **367 are the sweep's own tooling**, and they are concentrated: `SAMPLE-145-SoundLab` (172), `SAMPLE-141-Riemers` (112), `SAMPLE-140-RedistributableTTFs` (28), `SAMPLE-113-AvatarAnimPack` (21), `SAMPLE-120-ButtonImages` (15) and four smaller ones. **None of the nine has a `.contentproj` outside its `cna-*` trees** -- six were built by a hand-written `XnaPipelineRunner.cs` that enumerates a directory and assigns an importer and a processor per file (SAMPLE-120's picks `FontTextureProcessor` for the one asset named `xboxControllerSpriteFont` and `TextureProcessor` for the other fourteen), and three by something else again. `map_sources.py` maps projects, so it maps none of them. Reaching them means synthesizing a build unit from each runner's own enumeration, which is per-sample work rather than one rule -- named here so it is a job rather than a gap. |
| `XNASWEEP-159` | `XNASWEEP-142` changed one of the two colour-key routes, and the other suite noticed. | [x] **Fixed.** `CnaContentTests` runs `CnjContentPipelineTest.Texture2DConvergesOnTheExistingTextureProcessorAndWriter`, which compiles one `.cnj` -- `"colorKey":[7,26,45]` over a 4x3 PNG -- through the content pipeline and through `CompileCnjToCnb`, and compares the bytes. `XNASWEEP-142` made the pipeline's key match on four channels and left `CnbSourceImport`'s at three, so the two routes keyed out different pixels and the suite went from its five recorded environmental failures to six. `CnbSourceImport` matches on four now, a three-component document key standing for an alpha of 255, which is the same reading the pipeline uses. The suite is back to exactly the five `HEADLESS` failures §33.1 of `plan_xnapipeline_parity.md` records -- the ones where the headless renderer stores no `TextureCube` or `Texture3D` texels at all. Worth recording as its own row rather than folding into 142: a fix measured against the sample corpus broke a contract only the *other* suite asserts, and the campaign's own regression run is what found it. |
| `XNASWEEP-160` | One scene puts its children in an order none of the file's three lists gives. | [x] **Fixed, and the rule is the importer's own, in its own words.** Synthetic scenes hand the genuine importer one variable at a time: `co_hammer_names_plain` makes both objects `Null` and the connection order survives, so the leading space is not it; `co_root_then_null` and `co_null_then_root` swap the connection order of a `Root`-class model and a `Null` and both answer the null first, so the model's **class** is it. A `Root` and a `LimbNode` both come back as `BoneContent`, and **the first bone a depth-first walk reaches is moved to be the last child of the scene's root**, with its transform re-expressed against that root: `cd_deep_bone` puts a bone two levels down and it comes back at the root carrying the transform it had in the world; `cb_four_mixed` has two bones and only the first moves; `cb_two_limbs_rev` says "first" means first *connected*, not first declared. XNA says so itself -- a scene with two skeletons logs `Multiple skeletons were found in the file. The first skeleton, named "{0}" has been moved to be a child of the scene root. The other, "{1}", will be ignored.`, placeholders and all. `Hammer.FBX` is exactly that shape. Five fixtures and the warning are in the differential; the test harness now records warnings, which it discarded before. |
| `XNASWEEP-161` | Three FBX importer rules the corpus had never carried, measured rather than guessed. | [x] **Fixed.** (1) An FBX model of class `LimbNode` **or** `Root` answers a `BoneContent`; CNA made every node a plain `NodeContent`, so `MeshHelper::FindSkeleton` found nothing in any FBX at all. (2) A `Light` and a `Marker` are **not** nodes, which the comment on `IsSceneNode` had explicitly left unmeasured: `fbx_light_marker.fbx` connects one of each beside a `Null` and the genuine importer answers a single node. (3) A scene with more than one skeleton logs XNA's own warning, unformatted placeholders and all. All three are in the committed differential. |
| `XNASWEEP-162` | SAMPLE-138's `photograph.fbx`: a root whose scaling is divided out, and two mesh parts against three. | [ ] **Open, and narrowed to the processor.** Split from `XNASWEEP-150`, which turned out to be a different rule. The file's one object, `cube1`, is a `Mesh` with `Lcl Scaling 0.01`, a `ScalingPivot` of about `-(1.06, 6.89, 6.43)` and a `ScalingOffset` that nearly cancels it, and it is the scene's only top-level object. The genuine **importer** answers it with the file's own `0.01` scaling and the pivot-composed translation (`cm_meshroot_photo`, `cm_meshpair_photo`), so nothing in the import divides the scaling out. What the built `.xnb` differs by is three numbers -- `root/bones[0]/transform[12..14]`, XNA `(2.35153561e-06, 1.52507637e-05, -1.4228086e-05)` against CNA's zeroes -- which is exactly `M * S^-1`: XNA removes the root's scaling by right-multiplying by its inverse, which carries the translation with it, where CNA writes the identity outright. Two more differences in the same file are its own: XNA answers three mesh parts and five shared resources where CNA answers two and four. Ten references, one file. |
| `XNASWEEP-164` | A polygon with more than three corners is triangulated as a strip, not a fan. | [x] **Fixed, and found by asking `XNASWEEP-149`'s question of real content.** `validate_model.py` hands CNA's own pre-optimisation face order to `D3DXOptimizeFaces` and compares the answer with the reference; on `France.FBX` 21 of its 26 mesh parts matched exactly and the other five did not have the same *triangles* to order -- a difference the sweep had been counting as "the vertex or index buffer differs", which is where `XNASWEEP-149` was assumed to own it. It does not. **The genuine `.x` importer fans a polygon from corner 0 and the genuine FBX importer does not**: it answers a strip, and what it answers is a function of the corner count alone -- a concave quad and a convex one give the same triangles, and so does the same octagon walked from a different corner or walked backwards. Measured for 3 to 20 corners: `(2,1,0)`, `(0,3,2)`, then two pointers walking in from the ends. A fan agrees with it up to five corners and disagrees from six, and disagrees on a **quad's corner order** already -- `3,2,0` against `0,3,2`, the same triangle wound the same way and not the same six bytes. Two more rules came with it: the corners are numbered in the order the triangulation introduces them (a hexagon's are `0,1,2,3,5,4`, a twelve-gon's `0,1,2,3,11,4,10,5,9,6,8,7`), and a generated normal is the sum of the unit normals of the *triangles* naming a control point, not of the polygons -- `fbx_polygon_concave6.fbx` gives one control point two triangles wound against each other and XNA answers the zero vector, where CNA substituted `(0, 0, 1)`. Thirteen fixtures, their genuine answers frozen, and a second test that keeps the corner order the existing one normalises away. `France.FBX` goes from 21 of 21 to **26 of 26**. |
| `XNASWEEP-165` | A polygon whose material index names no connected material is not dropped. | [x] **Fixed; SAMPLE-138's `photograph.fbx` goes from two mesh parts to XNA's three.** `XNASWEEP-162` recorded that file as answering "three mesh parts and five shared resources where CNA answers two and four" without saying why. Its `LayerElementMaterial` names indices 0, 1 **and 2**, and only two materials are connected to `cube1`; CNA batched over the *connected* materials and silently dropped the 86 polygons that named 2, losing a whole mesh part and 154 vertices. XNA keeps them: three fixtures measured on the genuine importer settle the rule -- `fbx_material_gap.fbx` names 0, 1 and 2 with one material connected and answers two batches, `Only` and then **one** null-material batch holding both the 1 and the 2; `fbx_material_gap_negative.fbx` does the same for -1; and `fbx_material_gap_skip.fbx`, whose polygons name only out-of-range indices, answers a single null batch. So the batch key is the connected material a polygon names *or* "none", every "none" sharing one batch, still in first-use order (`XNASWEEP-155`). What is left of `photograph.fbx` is its root transform, which stays with `XNASWEEP-162`. |
| `XNASWEEP-163` | The 367 references the sweep has no route to, as a coverage task of its own. | [ ] **Open, and deliberately separate from every behaviour row above.** `XNASWEEP-158` audited the two gap classes and is closed on what it set out to check; what it *found* -- that nine samples had their references built by a hand-written runner rather than a `.contentproj`, so `map_sources.py` reaches none of them -- is a property of this campaign's harness, not of CNA, and it kept being read as unfinished behavioural work. It is this row now. The remedy the audit named is a synthesized build unit per runner: read the runner's own asset list and the processor parameters it sets, write a `.contentproj` that names them, and stage it the way a reconstructed project already is. Until that exists the 367 stay `CORPUS_GAP`, which is an honest label -- the reference is genuine and CNA has never been asked to match it -- and no conclusion in this file rests on them. |


---

## 11. Where this stands

Counts are read off the tools; nothing here is hand-counted. The commands that
produce them, in order:

```bash
python3 tools/xna-sample-sweep/corpus_inventory.py \
        --out build/xna-sample-sweep/manifest/sample-xnb-corpus.json
python3 tools/xna-sample-sweep/runner_provenance.py \
        --out build/xna-sample-sweep/manifest/runner-provenance.json
python3 tools/xna-sample-sweep/map_sources.py \
        --manifest build/xna-sample-sweep/manifest/sample-xnb-corpus.json \
        --provenance build/xna-sample-sweep/manifest/runner-provenance.json \
        --out build/xna-sample-sweep/manifest/sample-source-map.json
cp cmake-build-debug/cna-content build/xna-sample-sweep/bin/cna-content   # freeze it
python3 tools/xna-sample-sweep/sweep.py \
        --map build/xna-sample-sweep/manifest/sample-source-map.json \
        --out build/xna-sample-sweep/manifest/sweep-results.json \
        --tool build/xna-sample-sweep/bin/cna-content --jobs 2
python3 tools/xna-sample-sweep/report.py \
        --map build/xna-sample-sweep/manifest/sample-source-map.json \
        --results build/xna-sample-sweep/manifest/sweep-results.json \
        --json build/xna-sample-sweep/manifest/sweep-report.json
python3 tools/xna-sample-sweep/classify.py \
        --map build/xna-sample-sweep/manifest/sample-source-map.json \
        --results build/xna-sample-sweep/manifest/sweep-results.json \
        --out build/xna-sample-sweep/manifest/sweep-classified.json --jobs 2
```

**Never rebuild `cna-content` while a sweep is running**: the sweep runs the
frozen copy under `build/xna-sample-sweep/bin/` for exactly that reason, and the
first run of this campaign was invalidated by a mid-run relink.

### 11.1 The corpus

| | |
|---|---:|
| genuine reference `.xnb` | **7,734** |
| distinct contents | 5,285 |
| samples represented | 129 |
| build units (project x output root) | 328 |
| references mapped to a project item | 6,578 |
| references under a root but named by no item (model side outputs) | 746 |
| references in a sample with no `.contentproj` | 359 |
| references whose item names a source the tree has not got | 43 |

### 11.2 The sweep, run 17 (2026-09-08)

Run with everything through `XNASWEEP-161`. Run 3 is kept because it is what
`XNASWEEP-104`--`110` were measured against, and run 16 because it is the last
one before this day's importer work.

| | run 1 | run 3 | run 10 | run 13 | run 16 | run 17 |
|---|---:|---:|---:|---:|---:|---:|
| byte-identical | 922 | 3,619 | 3,738 | 3,771 | 3,771 | **3,771** |
| differing | 343 | 1,547 | 1,659 | 1,639 | 1,639 | 1,639 |
| missing (CNA produced nothing) | 6,102 | 2,201 | 1,970 | 1,949 | 1,949 | **1,949** |
| build units that finished | -- | 156 | 156 | 155 | 155 | 155 |

**Run 16's frozen binary was not built from the commit run 16 is attributed
to, and run 17 is the first sweep this campaign can prove was.** Sixty-two font
references and the five `photograph.xnb` ones change between the two runs, and
this session's only production change is `FbxImporter.cpp`, which no font and
no camera-only FBX ever reaches. It was checked rather than argued: building
`SAMPLE-071`'s `ScoreFont.spritefont` three times running gives one byte
sequence, building it once more with `FbxImporter.cpp` reverted to `a41fd22ca`
gives that same sequence, and `git log --name-only` over the session's commits
names two files, one of them a test. So a run-16-to-run-17 delta is not this
session's alone, and nothing below is attributed to a change on the strength of
the delta by itself.

What *is* attributable, because the same change is visible in the committed
model differential: every RobotGame mech's root bone. `Duskmas.xnb`'s
`bones[0]` was the file's quarter turn against XNA's identity and is now the
identity, and the file went from 103 differing numbers to 95; `Tiger` 28 to 23,
`Hammer` 352 to 346, `PhantomBoss` 318 to 314, `Yager` 378 to 373, `Kiev` 379 to
374, `cameleerT1` 253 to 250. What is left on those bones is float residue XNA
carries and CNA writes as zero -- `9.157e-17` and `-2.35e-41` against an
identity -- which §11.3.1 accounts for.

Over the whole corpus the field-level difference count fell from 149,777 to
148,222, 151 references improved and 15 got worse; of the fifteen, five are
`spaceship.xnb` going from *structurally absent* (1 bone where XNA has 2, no
mesh at all, no shared resources) to structurally right with eight numbers out,
one is `MaomingT1.xnb` going from 25 bones and 6 meshes to XNA's 26 and 7 --
both counted as "worse" only because there is finally something to compare --
and the rest are the font references the paragraph above disclaims.

### 11.3 Every reference, classified

`taxonomy.py` puts all 7,726 into one class each, with the reason beside it.

| class | count | what it means |
|---|---:|---|
| `IDENTICAL` | **3,771** | CNA's build is the reference, byte for byte. |
| `SEMANTICALLY_IDENTICAL` | 728 | Everything a runtime reads is equal; the LZX stream is not. |
| `ACCEPTED_DIFFERENCE` | 736 | Measured, understood, and not closable from here. |
| `CUSTOM_PIPELINE_GAP` | 1,370 | The asset needs a component the *sample* defines, in a .NET assembly C++ cannot load. |
| `ENVIRONMENT_GAP` | 15 | This machine's, not CNA's: 10 effects the June-2010 fxc refuses and XNA's own D3DX9 accepted, and 5 fonts Windows has and this machine has not. |
| `CORPUS_GAP` | 931 | The reference is in the corpus and the sweep has no way to build it (`XNASWEEP-158`). |
| `UNEXPLAINED` | **175** | Everything else. |

The accepted differences, by reason:

| count | reason |
|---:|---|
| 256 | Generated mip levels only, from the dither in XNA's own filter (`XNASWEEP-123`). |
| 249 | Two rasterizers disagree about a glyph's ink by a pixel: GDI+ against FreeType. |
| 91 | The effect blob carries its compiler's version string (`XNASWEEP-108`). |
| 88 | Two conformant JPEG decoders inside an IDCT's tolerance. |
| 48 | An Xbox 360 target: XMA has no publicly implementable encoder. |
| 4 | XNA re-encodes a song to WMA (`XNASWEEP-125`). |

### 11.3.1 What is still unexplained

All 175 are model assets, and there is no unexplained reference outside `.fbx`
and `.x` any more. By what differs:

| count | what differs |
|---:|---|
| 99 | The vertex or index buffer alone: `XNASWEEP-149`, and nothing else. |
| 40 | A buffer *and* a bone transform. |
| 21 | A buffer *and* the bounding sphere computed over it. |
| 9 | A bone transform alone: eight RobotGame mechs and `tank.fbx` (`XNASWEEP-153`). |
| 5 | `photograph.fbx`: a root transform, a buffer, and two mesh parts against three (`XNASWEEP-162`). |
| 1 | `AircraftCarrier.xnb`: mesh parts split at different vertices. |

160 of the 175 carry a buffer difference, which is one question:
`MeshHelper.OptimizeForCache`. The 21 bounding spheres are downstream of it --
the sphere is computed over the vertices in the order the optimiser leaves them
-- and so are most of the 40.

**The child-ordering family is gone.** `XNASWEEP-160`'s one reference was the
only case in the corpus whose difference was a bone's child list, and the
skeleton-root promotion closed it.

**What is left on the eight mechs is float residue, characterised.** XNA's
`Duskmas.FBX` root comes back as
`[1 0 0 0  0 1 -0 0  0 -2.347e-41 1 0  9.157e-17 5.607e-33 0 1]` where CNA
writes the exact identity; `9.157e-17` is `1.4954437 * 6.123234e-17`, the
double cosine of a right angle, and `5.607e-33` is the same times that cosine
again. It is **not** what `M * Invert(M)` leaves: XNA's own inverse is the
cofactor formula in `float`, that formula was reimplemented here in strict
`float32` over XNA's own `M`, and the product is the exact identity, zeros and
all. Whatever sequence leaves those residues also reaches the bone's children
-- `cs_bonekid_dusk`'s child answers a basis of `+-6.123234e-17` where its file
gives it none -- so it is not one multiplication either.

### 11.4 The read-only roots, re-audited 2026-09-08 (second pass)

`build/xna-sample-sweep/audit/` holds the `find . -printf '%y %s %T@ %p'`
baseline of both roots, taken before any work at 17:54 on 2026-09-07, and the
audit re-takes and diffs it. **Take it from inside each root**, as the baseline
was: an absolute-path re-take differs from it on every line and says nothing.

| check | result |
|---|---|
| `/rv/tmp/XNAGameStudio/Samples` against the baseline | **0 differing lines.** Every type, size, modification time and path identical. |
| every corpus reference against its frozen `sha256` | **7,726 of 7,726 identical, 0 missing.** The denominator is the one `corpus_inventory.py` froze. |
| `/rv/tmp/samples` against the baseline | Changed, and none of it by this session -- itemised below. |
| the sample tree's directory times across a whole sweep | **0 changes.** Snapshotted before sweep 16 and again after 170 of its 327 units; not one directory time moved. |

The third row needs its evidence rather than an assurance, because this machine
runs many sessions at once and that tree is not only this campaign's. Outside the
`cna-*` and `CNA_BUILD` build trees the diff holds three kinds of thing:

- **2,300 lines under `SAMPLE-070-RolePlayingGame_4_0_Win_Xbox`** and 38, 27 and
  18 under `SAMPLE-068`, `SAMPLE-067` and `SAMPLE-071`, arriving with an
  `evidence/space-key-probe/` directory, a `scripts/probe-space.sh` and a new
  `MANIFEST.md`: another session's browser probe.
- **121 directory times**, and nothing else -- no file added, removed, resized or
  rewritten inside any of them. They are the sample content directories the sweep
  reads, and every one of the new times falls between 19:23 and 20:15 on
  **2026-09-07**, before this session began. A directory's time moves when an
  entry is created or removed in it, so something wrote and then removed a file
  there that evening; whatever it was, it is not in the binary now, which the
  fourth row measures directly.
- nothing else.

Nothing this session does writes into either root: the sweep builds into
`build/xna-sample-sweep/out/units/` and stages reconstructed projects into
`build/xna-sample-sweep/staged/`, the model oracle into `build/xna-pipeline-oracle/model/`,
the differential oracle into `build/xna-pipeline-oracle/`, and `find` over both
roots returns no `cna-buildcontent.json` at all.

### 11.4.1 The final qualification, 2026-09-08 (second pass)

| run | result |
|---|---|
| `CnaContentPipelineTests` | **463 / 463**, no skips, with five new FBX fixtures and the importer warning they provoke. |
| `CnaContentTests` | 1,804 run, **1,790 passed**, 9 skipped, **5 failed** -- exactly the five `HEADLESS` failures §33.1 of `plan_xnapipeline_parity.md` records, and no others. |
| `CnaMathTests` | **845 / 845**. |
| `git diff --check` | clean. |
| the corpus | run 17, `sweep.py` -> `report.py` -> `classify.py` -> `taxonomy.py` over all 7,726. |
| the frozen references | **7,726 of 7,726 `sha256`-identical, 0 missing**, checked before the session's first build and again after its last. |
| the model oracle | regenerated over the extended fixture set: every pre-existing case byte for byte unchanged, five added. |

The one thing this pass adds to the discipline, because it cost a day's
attribution: **freeze the sweep binary from a commit, not from a working tree.**
Run 16's frozen `cna-content` answered a different sprite-font atlas and a
different `photograph.xnb` root than the committed tree does, which was found
only by rebuilding the same font four times -- three with this session's code
and once with its one production change reverted -- and getting one byte
sequence every time.

### 11.5 Next

In the order the measurements put them:

1. **`XNASWEEP-149`.** 160 of the 175 unexplained references turn on the order
   `MeshHelper.OptimizeForCache` puts a mesh's triangles in, or on a number
   that follows from it. 372 probes are now measured against the genuine method
   and frozen under `tests/reference/xna40/optimize/`; what they settle and what
   they do not is in the row and in
   `tools/xna-pipeline-oracle/optimize/README.md`. The sharpest open form: a
   strip with one pendant triangle glued to its face `T`, with that face moved
   to index `j`, continues into it for every `j < T` and restarts for every
   `j >= T`, and no live count, cache age, valence or vertex number this
   campaign can compute moves with the threshold.
2. **`XNASWEEP-162`.** SAMPLE-138's `photograph.fbx`, five references: the
   genuine *importer* answers the file's own `0.01` scaling, so what divides it
   out is in the processor. `tools/xna-pipeline-oracle/modelroot/` runs the
   genuine `FbxImporter` **and** `ModelProcessor` over a directory of FBX files
   and prints every bone at round-trip precision, which is the instrument that
   question needs.
3. **`XNASWEEP-153`.** `tank.fbx`, ten references. The row's account was
   narrower than the data: the built `.xnb` differs in 80 numbers, not one, and
   they are not all translations -- `bones[0]/transform[5]` is XNA's
   `0.9999999403953552` against CNA's `1.0`, on a file whose root carries
   `Lcl Scaling 0.00999999977648258` under `UnitScaleFactor 100`.
4. **`XNASWEEP-163`'s 367.** The corpus-coverage row, separated from the
   behaviour rows so that it stops being read as unfinished parity work.

The partial `OptimizeForCache` model is still deliberately **not** shipped.


---

## 12. What the campaign found

**The denominator.** 7,726 genuine `.xnb` files, produced by Microsoft's own XNA
4.0 Content Pipeline from public XNA samples, present on this machine. Frozen by
`corpus_inventory.py` before any work; not one was generated, added or dropped
for this campaign, and all 7,726 still match the digest it froze them with. The
number was 7,734 until `XNASWEEP-132` found eight of them were MonoGame's output
rather than XNA's and the inventory stopped counting them.

**The answer.** 3,771 of them are byte for byte what CNA's product pipeline
produces from the same source, up from 922 when the sweep first ran and 3,619 at
the run the earlier fixes were measured against. Another 728 differ only in ways
nothing a runtime does can see. 736 differ for a reason that is written down and
cannot be closed from here. 1,370 need a component the *sample* defines and .NET
loads from an assembly. 931 the sweep has no way to build at all, and 15 are this
machine's fault rather than CNA's. **175 are unexplained**, every one of them a
model, and §11.3.1 says exactly what differs in each.

### 12.1 What the corpus found that one project could not

Twenty-eight framework defects, each fixed in the component that owns it, each
with its own regression test. None of them is sample-specific and none of them
was visible from the API surface, the differential corpus, or Platformer:

| | |
|---|---|
| `XNASWEEP-104` | A sprite-font atlas's height followed Reach's rule on both profiles. |
| `XNASWEEP-105` | A font family was matched against the typographic family, not the one Windows matches. |
| `XNASWEEP-106/107` | A `.x` material's specular power of zero, and a `.x` mesh's generated normals. |
| `XNASWEEP-108` | Every effect was compiled optimized, whatever the build configuration said. |
| `XNASWEEP-109` | A PNG's own `gAMA` chunk, which GDI+ applies and CNA ignored. |
| `XNASWEEP-110` | A content build needed write access to the content it was reading. |
| `XNASWEEP-111/112/113` | What an FBX node is, what a vertex is, and where a model stands. |
| `XNASWEEP-114/115/116` | A `.x`'s materials, its texture paths, and how an external reference is written. |
| `XNASWEEP-117` | Two assets may name the same nested one, because it is the same asset. |
| `XNASWEEP-118` | Every processor property XNA lets a game override was `final` in all but name. |
| `XNASWEEP-119` | A `Dictionary<string,string>` asset, refused, and then written in the wrong order. |
| `XNASWEEP-121` | The quarter turn every Z-up exporter writes, composed in `float`. |
| `XNASWEEP-122` | A bone with no name and a bone whose name is empty are different bytes. |
| `XNASWEEP-124` | A `.wav`'s loop was one frame short of the one RIFF describes. |
| `XNASWEEP-131` | A font sheet's glyph is the ink in the cell, not the cell. |
| `XNASWEEP-145` | An FBX object's name includes its prefix; a material could answer for a model. |
| `XNASWEEP-146` | The geometry's own offset from its node, and the flag that decides whether `PreRotation` counts. |
| `XNASWEEP-148` | A `.x` file's normals are values; two entries holding the same numbers are one normal. |
| `XNASWEEP-151` | An FBX 7 mesh is two objects, and the second is not a node. |
| `XNASWEEP-152` | A node's transform has ten terms, and CNA composed four. |
| `XNASWEEP-154` | The scene's order is the order it is connected in, not declared in. |
| `XNASWEEP-155` | A mesh's batches come out in the order its polygons first name a material. |
| `XNASWEEP-156` | A mesh with no normals got a constant `(0,0,1)`, right only in the XY plane. |
| `XNASWEEP-157` | One index buffer for the model, and a shared-resource table in first-reference order. |
| `XNASWEEP-126` | A model's meshes were listed parent-first and each carried its own buffers. |
| `XNASWEEP-127/128` | An FBX material's colour factors, its two property tables, and the texture never read. |
| `XNASWEEP-131` | A font sheet's glyph is the ink in the cell, packed the way XNA packs it, premultiplied. |

Two of the sweep's own findings were about the *measurement* rather than the
thing measured, and both mattered: `XNASWEEP-021/041/042` established that 89 of
114 sample runners hand-list their assets and pass no processor parameters at
all, so the reference reflects the processor's defaults and not the project; and
`XNASWEEP-120` found three runners that set parameters inside a helper the
reconstruction could not see, which had the sweep blaming CNA for ten rows that
were its own.

### 12.2 What is accepted, and why

726 references differ for a reason that is measured and recorded rather than
assumed. The two largest are worth stating plainly because they bound what byte
parity can ever mean here:

* **250 sprite fonts.** XNA rasterizes through GDI+ and CNA through FreeType,
  and two rasterizers disagree about a glyph's ink by a pixel. Everything that
  decides where text lands -- every ABC width, the line spacing, the character
  map -- agrees exactly.
* **244 mipmapped textures.** XNA's own mip filter dithers before it quantizes.
  `XNASWEEP-123` measures it: every differing byte differs by exactly one, the
  sign is decided by the accumulator's fraction, and the disagreement rate rises
  monotonically to 45% at the rounding tie. No kernel, precision, summation order
  or rounding mode reaches it.

### 12.3 What a next pass should do

Everything that is still unexplained is in the model family, and almost all of it
is one question.

1. **`MeshHelper.OptimizeForCache` (`XNASWEEP-149`).** The order it puts a mesh's
   triangles in decides the vertex and index buffers, the digests that follow
   them and the bounding sphere computed over them. Twenty probes are committed
   with the graphics oracle -- strips, grids of five sizes, four UV spheres,
   disjoint quads, a closed fan, two disjoint grids, and four meshes whose faces
   or vertices are renumbered without changing the topology. What is known: the
   answer depends on the order the faces arrive in; a strip walks a *generalized*
   triangle strip with an ordered leading edge and one allowed swap; and when a
   strip ends the next face is the **highest-indexed unused face containing a
   vertex of minimum remaining valence**, which reproduces nine of the twenty
   exactly. What is not: what stops a strip that could still be continued, and
   why a closed surface's two symmetric poles are not symmetric to it.
2. **The root bone XNA answers as the identity (`XNASWEEP-150`).** Fifteen
   references, and `photograph.fbx` gives the relation a number: the translation
   is the local one divided by the scaling that went missing.
3. **One float in `tank.fbx` (`XNASWEEP-153`).** Ten references, one component
   each, one unit of last place, and five parsing routes ruled out.
4. **The 367 the sweep cannot reach (`XNASWEEP-158`).** Nine samples built by a
   hand-written runner rather than a content project.

`XNASWEEP-129` and `XNASWEEP-130` are answered rather than open, and so are the
15 `TextureProcessor` pairs, the 6 `FontTextureProcessor` references and the one
`PassThroughProcessor` document that stood here before: the classification of
2026-09-08 has no unexplained reference outside `.fbx` and `.x`.
