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
| `XNASWEEP-001` | Audit the live tree: branch, HEAD, upstream, untracked files preserved; read the parity plan, its final report, the differential tooling and the `.contentproj` route before changing anything. | [ ] |
| `XNASWEEP-002` | Take the read-only baseline of both roots and put every campaign artefact under the gitignored `build/xna-sample-sweep/`. | [ ] |

### Phase 1 — freeze the reference corpus

| ID | Task | State |
|---|---|---|
| `XNASWEEP-010` | `corpus_inventory.py`: every genuine `.xnb`, with size, SHA-256, container header, compression, root reader and complete type-reader table; non-genuine output excluded by directory and the synthesized song excluded by name. | [ ] |

### Phase 2 — map every reference to its original source

| ID | Task | State |
|---|---|---|
| `XNASWEEP-020` | `contentproj.py` + `map_sources.py`: a second reader of the `.contentproj`, the asset-name-as-path-suffix match, and the build units it yields. | [ ] |

### Phase 3 — the sweep

| ID | Task | State |
|---|---|---|
| `XNASWEEP-040` | `sweep.py`: build every unit through `cna-content`, compare every mapped reference, classify, emit JSON and a table. | [ ] |

### Phase 5 — defects the corpus exposed

| ID | Task | State |
|---|---|---|
| `XNASWEEP-100` | `<FontName>` is a font *family* name and CNA matched a file name, so a `.spritefont` could only be built where a font file happened to be called after its family; and there was no way at all to tell a build where a game's fonts are. | [ ] |
| `XNASWEEP-101` | The build-tool services a command line selects -- effect compiler, XMA encoder, font directories -- reached nothing on a `.contentproj` build. | [ ] |
| `XNASWEEP-102` | `--xnb-platform`, `--xnb-profile` and `--xnb-compress` were accepted on a `.contentproj` build and ignored. | [ ] |
| `XNASWEEP-103` | One asset with no component refused the whole project, three times over. | [ ] |
| `XNASWEEP-104` | A sprite-font atlas's height is the graphics profile's rule, and CNA used Reach's for both. | [ ] |
| `XNASWEEP-105` | A font family was matched against the typographic family, not the one Windows matches. | [ ] |
| `XNASWEEP-106` | A `.x` material's specular power of zero is not a value; the genuine importer writes none. | [ ] |
| `XNASWEEP-107` | A `.x` mesh with no `MeshNormals` got a constant normal instead of a generated one. | [ ] |
| `XNASWEEP-108` | Every effect was compiled optimized, because nothing carried the build configuration. | [ ] |
| `XNASWEEP-109` | A PNG's own `gAMA` chunk was ignored; GDI+, and therefore XNA, applies it. | [ ] |
| `XNASWEEP-110` | A content build needed write access to the content it was reading. | [ ] |
| `XNASWEEP-111` | An FBX camera is not a node, and what makes a scene's root is what the scene names. | [ ] |
| `XNASWEEP-112` | An FBX vertex is a control point *and* its channel values. | [ ] |
| `XNASWEEP-113` | `PreRotation` and the scene's own unit, both of which decide where a model stands. | [ ] |
| `XNASWEEP-114` | A `.x` names its materials as often as it nests them, and its texture paths are Windows paths. | [ ] |
| `XNASWEEP-115` | A path a source file names is matched the way Windows matches one; the *name* stays as authored. | [ ] |
| `XNASWEEP-116` | An external reference is written relative to the asset that carries it, with Windows separators. | [ ] |
| `XNASWEEP-117` | Two models that name the same texture are refused; XNA builds it once and lets both reference it. | [ ] Half fixed. `ReserveOutputs` in `tools/content/content.cpp` gives each output logical name exactly one owning node, so Spacewar's `p1_bfg` and `p1_dual`, which both name `textures/p1_back.tga`, collide on `textures/p1_back_0` and neither builds. XNA's own build produces that texture once and both models reference it; the coordinator needs nested nodes shared across parents by their `(source, processor, parameters)` key rather than owned by whichever parent reached them first. 85 of Spacewar's 154 references were missing for this reason alone. Two nodes may now own one output *when the two outputs are equal* -- same path, same reader, same SHA-256 -- which is what a nested build keyed by its source and its processing produces; two nodes whose outputs differ under one name are still refused, which is the case the check exists for. What is left is the same collision *within* one node, which still refuses five of Spacewar's models. |
| `XNASWEEP-118` | Every processor property XNA lets a game override was `final` in all but name. | [ ] The pipeline is extended by derivation, and a *property* is overridden as often as a method: the Normal Mapping sample writes `public override bool GenerateTangentFrames { get { return true; } }` so that no project setting can switch off what its effect requires. Read from the CLR metadata already in `tests/reference/xna40/content-pipeline-api.json`, XNA marks 45 processor properties virtual; CNA declared all 45 non-virtual, across `ModelProcessor` (14), `MaterialProcessor` (7), `TextureProcessor` (6), `ModelTextureProcessor` and `SpriteTextureProcessor` (5 each, inherited), `FontTextureProcessor` (3), `EffectProcessor` (2) and the two content attributes (1 each) -- 78 declarations. A non-virtual accessor fails silently: the derived class compiles, its override is never called, and the asset is built with the base's answer. The declarations are now virtual *and* each processor's own work reads through its getter rather than its field (31 call sites), which is the half that makes an override take effect. 26 build units in the corpus derive from a built-in processor. |
| `XNASWEEP-119` | A dictionary asset was refused for its value type, and written in the wrong order. | [ ] Two defects, one asset. Movipa's `App.config.xml` (SAMPLE-133) declares `Generic:Dictionary[string,string]` and the genuine build wrote `App.config.xnb`, 692 bytes, rooted at `DictionaryReader`2[[System.String],[System.String]]`. CNA refused it -- *`importer returned undeclared output type`* -- because only `Dictionary<string,int>` was a known built-in; a dictionary of primitives is pipeline infrastructure rather than a type the game owns, so the missing instantiation is registered on both sides, writer and runtime reader, exactly as the `string,int` one already was. With it registered the file came out the right size and the wrong order: XNA's `DictionaryWriter` writes what enumerating the dictionary gives it, and a .NET `Dictionary<K,V>` that has only ever been added to enumerates in **insertion order**, so a built asset stands in its document's order. CNA held dictionaries in `std::map` and the XNB writer sorted on top of that, so both the intermediate and the `.xnb` came out in key order. The pipeline's dictionary is now `System::Collections::Generic::OrderedDictionary`, and the writer writes a container's own order (an unordered one is still sorted, so the same input still gives the same file). `App.config.xnb` is byte-identical. The committed `xml_dictionary.xml` fixture could not have caught this -- its keys were listed in alphabetical order, so document order and key order were the same file -- so `xml_dictionary_order.xml` lists `zulu, mike, alpha, tango`, and the genuine XNA build of it, taken through the differential oracle, carries exactly that. |
| `XNASWEEP-120` | Three runners set processor parameters the reconstruction could not see, so CNA was blamed for them. | [ ] `runner_provenance.py` attributed a `SetMetadata("ProcessorParameters_X", "V")` call to *the nearest file-like string literal before it*, which is right only when the call sits beside the literal that names the asset. Three runners put it in a helper instead, where there is no literal to find: Audio3D's `Asset(file, name, importer, processor, bool generateMipmaps)` sets `GenerateMipmaps` only when its flag argument is `true`; RimLighting's six-argument overload takes the parameter's name and value as arguments; ShadowMapping's takes `params string[]` and walks it two at a time. All three came out with no parameters at all, so the sweep built Audio3D's three textures with one mip level against a nine-level reference and recorded the difference as CNA's -- 10 rows accused of a defect that belonged to the reconstruction. The extractor now finds the method enclosing each metadata call, and where that method has no literal of its own it reads the call sites instead, binding arguments to parameters by position and honouring an `if (parameter)` guard. `head.fbx` gets `DefaultEffect=EnvironmentMapEffect`, `grid.fbx` and `dude.fbx` get `CustomEffect` and `Scale`, and Audio3D's three textures get `GenerateMipmaps=True`. No other sample's provenance changed. |
| `XNASWEEP-121` | Every model standing on an exporter's quarter turn was rotated in `float`. | [ ] `FbxImporter::LocalTransform` composed scaling, `PreRotation`, `Lcl Rotation` and translation through `Matrix::CreateRotation*`, which are `float` and call the `float` `std::cos`. XNA's FBX SDK composes in double and narrows once at the end, and a quarter turn is exactly where the two part company: `cos` of a `float` pi/2 is `-4.371e-08`, of a `double` pi/2 it is `6.123e-17`. SAMPLE-003's `Cube.fbx` carries `PreRotation -90` under `UnitScaleFactor 2.54`, and XNA's own `Cube.xnb` holds `2.54 * 6.123233995736766e-17 = 1.555301383669155e-16` on the two matrix entries the turn zeroes; CNA held `-1.11e-07` there, seven orders of magnitude away, on every model a Z-up exporter wrote. Composed in double and narrowed once, `Cube.xnb` goes from a whole-matrix disagreement to three bounding-sphere centre components differing in the seventh digit -- float accumulation order in `CreateFromPoints`, which is its own question. The genuine importer's own answer for `Cube.fbx`, read through `run-model-oracle.sh` with `CNA_MODEL_FIXTURES` pointed at the sample, confirmed CNA's importer was right about the transform in every other respect. Note that `Content-built/Cube.xnb` in that sample is a *different* genuine build from the other three copies (2,137 bytes against 2,044, root bone identity against the scaled one), so a sample can hold two references for one source that do not agree with each other. |
| `XNASWEEP-122` | An unnamed bone and a bone with an empty name were the same thing, and XNA writes them differently. | [ ] XNA's `ContentItem.Name` is a nullable string, and the two empty values reach a built `.xnb` as different bytes: a null object against a zero-length string. The `.x` importer produces both -- a file with no enclosing frame gets a synthesized root that carries *no* name (measured: the genuine importer answers `/<null>` for `bare_mesh.x`), while a `Frame {` declares one that happens to be *empty* (measured over SAMPLE-028's `Car.x`, whose unnamed frames answer `""`). CNA held the name in a `std::string`, so the two collapsed, and the writer guessed: it wrote null whenever the name was empty. That is right for `bare_mesh.x` and wrong for the 17 corpus references whose `.x` files declare unnamed frames. `ContentItem`, `ModelBoneContent`, the canonical bone record and the XNB writer now carry the distinction; `Car.xnb`'s eight leading bone names match the reference exactly and `model_x_bare_mesh` still writes its null. The one place it is deliberately not carried is the *file* format of CNB Model schema 2, which is frozen and stores a name string: the flag rides beside it in memory -- which is what the `.xnb` route needs, since every built model passes through the canonical model -- and a model that has been through a `.cnb` file comes back with an empty name rather than none. |
| `XNASWEEP-123` | The mip residual is a dither in XNA's own filter, and now says so. | [x] `ACCEPTED_DIFFERENCE`, measured rather than assumed. `plans/plan_xnapipeline_parity.md` `XNAPP-254` had already found XNA's mip kernel -- a separable four-tap `{1/16, 7/16, 7/16, 1/16}` with clamped edges, and an exact area average on an odd dimension -- and recorded a residual of 61 bytes in 2,944 differing by one. On a real photograph the same residual is 9% of the bytes, so it was re-measured here over SAMPLE-059's 256x256 `CatTexture.tga`: level 0 is byte-identical, every generated level differs, every differing byte differs by exactly one, and **the sign is decided by the accumulator's own fraction**. Sorted by distance from the rounding tie the disagreement rate rises monotonically -- 0.5% at 112/256 from the tie, 19% at 80, 33% at 48, 45% at 16 -- and never crosses over: below the tie XNA is always the higher value, above it always the lower. That is a dither of about half a least-significant bit applied before quantisation. Nothing else reaches it: the kernel was re-fitted over every symmetric six-tap with integer weights and `{1,7,7,1}` is the optimum (a six-tap with any non-zero outer tap is worse by 2,500 bytes), and float32, float64, normalised and pairwise-summed arithmetic, and truncation, half-up, half-even and half-down rounding, move the count by at most eight bytes in 65,536. |
| `XNASWEEP-124` | A `.wav`'s loop was one frame short, because RIFF's loop end is the last frame played. | [ ] `CnbSourceImport` read a `smpl` chunk's first loop as `loopLength = End - Start`. RIFF's `End` names the last frame *played*, so the region is inclusive and one frame longer. Spacewar's `Menu_Loop.wav` declares 67693-859611 and XNA's own `Menu_Loop.xnb` carries 791919; CNA wrote 791918. With the fix that file is byte-identical, all 3,438,557 of it. No committed audio-oracle case could have caught it: every one of them has no `smpl` chunk at all, so every recorded `loopStart` is 0 and every `loopLength` is the whole buffer. The two CNA fixtures that do declare a loop asserted the arithmetic rather than the format, and now assert the measured rule. |
| `XNASWEEP-125` | The `Song` rows, classified: XNA re-encodes to WMA and CNA cannot. | [x] `ACCEPTED_DIFFERENCE`, and two distinct shapes inside it. Where the source is not already WMA -- SAMPLE-060's `Music.mp3`, SAMPLE-065's `NinjAcademy_Music.wav` -- XNA's `SongProcessor` **re-encodes it**, and its `.xnb` names `Music.wma` where CNA's names the source it passed through; the duration follows the encode. There is no free WMA encoder, and `plans/plan_xnapipeline_parity.md` `XNAPP-202` already records that Wine carries no Windows Media Format runtime to measure one against. Where the source *is* WMA -- SAMPLE-062's `One Step Beyond.wma` -- the path matches and only the duration differs, by 19 ms in 366 seconds: the file's ASF File Properties Object says `PlayDuration` 367,645 ms and `Preroll` 1,579 ms, whose difference is exactly CNA's 366,066, while XNA answers 366,085, which is `PlayDuration` less 1,560 -- a number that appears nowhere in the header, so XNA is reporting what the codec decoded rather than what the container declared. Same reason, same conclusion. |
| `XNASWEEP-126` | A model's meshes were listed parent-first and each carried its own buffers; XNA does neither. | [ ] SAMPLE-030's `tank.fbx` settles both halves. Its twelve meshes hang three deep under `tank_geo`, and XNA's own `tank.xnb` lists them `r_back_wheel, r_front_wheel, r_steer, r_engine, l_back_wheel, ..., canon, hatch, turret, tank` -- **every child before its parent** -- while its bone table is the plain depth-first order, parent first, which CNA already matched exactly. So the two lists are not the same walk, and CNA emitted meshes in the bone's order. The same file has **four** shared resources: one vertex buffer, one index buffer, and the two distinct effects. CNA merged batches only within one mesh, so it wrote a buffer pair per mesh and twenty-six shared resources; XNA merges every batch whose vertex declaration agrees into one pair for the whole model and lets each part address it with a vertex offset and a start index, which is what those fields are for. Walking the children before building the node's own mesh gives both at once -- the descendants' geometry reaches the shared buffers first and their meshes are listed first. `tank.xnb` goes from 158 recorded differences to 57, its shared-resource table from 26 entries to XNA's 4, and what is left is float noise on bounding-sphere centres and two bone translations. |
| `XNASWEEP-127` | An FBX material's colours ignored their factors, and its texture was never read at all. | [ ] Three things, all on SAMPLE-030's `tank.fbx` and all confirmed against the genuine importer. **A colour is its `<Name>Color` times its `<Name>Factor`**: that file's `DiffuseColor` is (1,1,1) with a `DiffuseFactor` of 0.8, its own `Diffuse` compatibility property is (0.8,0.8,0.8), and XNA answers 0.8 where CNA answered 1. **A texture reaches the material through the model**, not through the material: the connections read `Texture::steamroller_tank61_file3 -> Model::r_engine_geo`, the `Texture` object carries the `RelativeFilename`, and it counts only where the geometry declares a `LayerElementTexture` -- the same fixture built without one answers a material with no texture. CNA parsed no `Texture` object at all, so every textured FBX model lost its texture and the nested build that produces it. **And a material is converted once**: `MaterialProcessor` replaces a texture reference with the built one *in place*, so converting `engine_phong` a second time handed it `engine_diff_tex_0.xnb` to build, which is outside the content root and refused the whole model. `tank.xnb` goes from 158 differences to 49 -- 45 bounding-sphere centres and two bone translations at 1e-7, and the two buffer digests that follow from them -- with its two effects carrying XNA's colours and XNA's texture names. A fourth thing fell out of the new fixture: a mesh that declares no normals gets generated ones **after** the channels it did declare, where CNA put them first. |
| `XNASWEEP-128` | An FBX carries one of *two* material property tables, and a texture belongs to one batch. | [ ] SAMPLE-033's `Ship.fbx` is an FBX **6.0** whose materials are `Version: 101`: they name a bare `Diffuse`, `Specular` and `Emissive`, and their `Shininess` really is the specular power -- the genuine importer answers 29.54, 20 and 6.4 for its three materials. SAMPLE-030's `tank.fbx` is a 6.1 whose materials are `Version: 102`: `DiffuseColor` times `DiffuseFactor`, and `ShininessExponent`. Which table a file uses is what decides, not which properties are present -- the committed `fbx_two_materials.fbx` is a newer material that *also* carries `Shininess: 2`, and XNA answers the newer table's default of 20 for it. Reading only the newer table gave every 6.0 material a black diffuse and a specular power of 20. And a texture belongs to **its own batch**: `Ship.fbx` has three materials and one texture, and XNA puts that texture on the first batch and leaves the other two without one, where CNA had fallen back to the first texture for every batch. `Ship.xnb` goes from 27 differences to 5 -- three bounding-sphere components at 1e-7 and the two buffer digests that follow. A second fixture, `fbx_material_legacy.fbx`, carries the older table so both are measured. |
| `XNASWEEP-117` | *(second half)* Two batches of **one** model that name the same texture. | [ ] Closed by the same rule the first half used, one level down. A twelve-mesh tank whose meshes share a material shares that material's texture with them, so one node asks for the same nested output nine times; the writer refused the repeat outright. It is now allowed when the two outputs really are the same -- same name, same asset type and schema, same reader, same bytes -- and still refused when they are not, which is the case the check exists for. Ten build units that had started failing the moment FBX textures were read build again. |
| `XNASWEEP-129` | What is left of the model family: the vertex order inside a batch. | [ ] **Open, measured, not fixed.** With `XNASWEEP-126`, `127` and `128` in, SAMPLE-033's `Ship.xnb` differs from XNA's in five values: three bounding-sphere components at 1e-7 and the two buffer digests. The digests are not float noise. The two vertex buffers are the same size, the two part tables are identical to the field -- `(0, 6679, 0, 11857)`, `(6679, 2368, 35571, 4128)`, `(9047, 81, 47955, 133)` -- and 2,740 of the 9,128 vertices differ as *values*, not merely in position: the first vertex of XNA's buffer is `(-759.1, -214.3, 267.2)` and of CNA's `(54.5, 227.8, -90.1)`. Narrowed further, and it is not the order: the two buffers hold **the same positions with the same multiplicities**, every vertex is a permutation away, and the part table's vertex counts agree batch by batch, so the grouping that turns polygon corners into vertices is identical. What differs is one channel. Compared as multisets: positions equal, **675 texture coordinates differ**, and the 2,250 differing normals follow them -- every XNA vertex whose position *and* texture coordinate appear in CNA's buffer carries CNA's normal exactly, so the normals are right and the UVs of 7% of the vertices are not. Ship's UV layer is `ByPolygonVertex` with `IndexToDirect`, the shape CNA reads everywhere else, and its normals are `ByVertice`, which tank.fbx's are not -- one of those two is the lead. A single-batch mesh is unaffected: SAMPLE-003's `Cube.xnb` matches its reference's buffer byte for byte. |
| `XNASWEEP-130` | The sprite-font packer chooses a *wide* sheet where CNA chooses a tall one, 74 times out of 74. | [x] Evidence toward a rule the differential's one measurement could not settle. `decisions.json`'s `fonttexture_sheet_edge_touch` records the atlas width as the single thing about XNA's packer that two measurements disagree on -- two 5x7 glyphs go into an 8-wide sheet and three into a 16-wide one. The corpus is a much larger sample: over the 74 fonts whose atlas dimensions differ from CNA's, **CNA's sheet is taller than it is wide every single time**, and XNA's is wider than it is tall in 49 of them, 35 of which are the same area transposed -- `hudFont` is 128x64 against CNA's 64x128, `BigFont` 512x256 against 256x512. So the two packers do not disagree about area; they disagree about which way round to grow. It stays an `ACCEPTED_DIFFERENCE` because the glyph boxes differ first -- GDI+ against FreeType -- so matching the sheet would not make one of these files identical. Recorded because the next person to work on the packer should start from 74 measurements rather than two. |
| `XNASWEEP-131` | `FontTextureProcessor` gave the runtime a cell where XNA gives it the ink. | [ ] Mostly fixed. SAMPLE-062's `NetRumbleFont.png` is a 256x256 sheet of 95 glyphs. XNA's `NetRumbleFont.xnb` carries a **128x156** atlas; CNA's carries the source sheet unchanged at 256x256. Glyph 0 shows both halves of why: XNA's bounds are `[124, 1, 1, 1]` with a cropping rectangle of `[7, 26, 8, 27]` -- a single ink pixel, its offset inside the original 8x27 cell recorded in the crop -- where CNA's are `[34, 117, 8, 27]` and `[0, 0, 8, 27]`, the whole cell with no trim. So XNA **trims each glyph to its non-transparent bounds, records the trim in the cropping rectangle, and repacks the trimmed glyphs into a new sheet**; CNA finds the glyph columns and stops there. The five `fonttexture/*` cases in the differential corpus all pass, because their glyphs fill their cells and their sheets are already as small as a repack would make them. The trim is now done, and the rule it needed was not the one the cell scan uses: a cell is bounded by the *separator* colour, and the ink inside it is bounded by **alpha** -- NetRumbleFont's cells are separated by magenta and padded with transparent *white*, so trimming on the separator alone finds no border at all. An empty cell -- the space -- answers a 1x1 box at the bottom-right corner, which is what a scan whose initial minimum is the last texel and whose extent is clamped to one produces. All 95 cropping rectangles now match, the line spacing matches (21, the tallest **trimmed** glyph, not the cell height), the atlas width matches, and every glyph's size matches; `NetRumbleFont.xnb` goes from 572 differences to 167. What is left is the packer: 163 placements and the atlas height. Three more rules fell out of it and the family is **closed**: `NetRumbleFont.xnb` is byte-identical, all 79,872 texels of it. **The packer is not a shelf.** CNA placed each glyph on a row as tall as its tallest member; XNA places each at the lowest row it fits in and the leftmost column of that row. Ordered tallest-then-widest -- which CNA already had -- that reproduces **every** placement of all five measured sheets: 95 of 95, 10 of 10, 3, 3 and 2. **The height is the rows used rounded up to a multiple of four**, and then to a power of two while it is still no more than 32. Thirteen real fonts in the corpus say the first half exactly -- `DebugFont` 64x112 from 111 rows, `NetRumbleFont` 128x156 from 154, `LargeGameFont` 512x348 from 346, and only one of the thirteen is a power of two at all -- and the four small differential sheets say the second: 16 rows answer 16, and 18 and 30 both answer 32. Where the threshold really is, this campaign cannot say: "no more than 32 rows" and "narrower than 64 texels" separate the same seventeen files. **And the atlas is premultiplied**, which is this processor's own `PremultiplyAlpha` defaulting to True: CNA validated that parameter and never applied it, so the sheet's transparent *white* padding reached the atlas as (255,255,255,0) where XNA writes (0,0,0,0). The product truncates -- `c * a / 255` -- which is the last byte between the two files. |
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

### 11.2 The sweep, run 10 (2026-09-08)

Run with everything through `XNASWEEP-131`. Run 3 is kept because it is what
`XNASWEEP-104`--`110` were measured against.

| | run 1 | run 2 | run 3 | run 6 | run 8 | run 10 |
|---|---:|---:|---:|---:|---:|---:|
| byte-identical | 922 | 3,524 | 3,619 | 3,711 | 3,718 | **3,738** |
| differing | 343 | 1,444 | 1,547 | 1,510 | 1,679 | 1,659 |
| missing (CNA produced nothing) | 6,102 | 2,399 | 2,201 | 2,146 | 1,970 | **1,970** |
| build units that finished | -- | -- | 156 | 156 | 156 | 156 |

*Differing* going up while *missing* goes down is the shape of progress here: an
asset that could not be built at all is now built and compared. 231 references
moved out of "nothing was produced" between run 3 and run 10.

By source extension, run 10 (`identical / differing / missing`):

| extension | identical | differing | missing | identical % |
|---|---:|---:|---:|---:|
| `.png` | 3,171 | 781 | 33 | 79.6 |
| `.xml` | 2 | 0 | 1,218 | 0.2 |
| (model side output) | 0 | 204 | 542 | 0.0 |
| `.fbx` | 0 | 175 | 125 | 0.0 |
| `.wav` | 290 | 5 | 0 | 98.3 |
| `.spritefont` | 0 | 250 | 9 | 0.0 |
| `.tga` | 167 | 7 | 0 | 96.0 |
| `.fx` | 0 | 92 | 11 | 0.0 |
| `.jpg` | 0 | 88 | 8 | 0.0 |
| `.bmp` | **71** | 14 | 8 | 76.3 |
| `.x` | **15** | 37 | 11 | 23.8 |
| `.wma` | 14 | 1 | 0 | 93.3 |
| `.dds` | 8 | 3 | 0 | 72.7 |

`.x` had no identical reference at all in run 3, and `.bmp` -- the font sheets --
went from 54 to 71 when `XNASWEEP-131` landed.

### 11.3 Every reference, classified

`taxonomy.py` puts all 7,734 into one class each, with the reason beside it.

| class | count | what it means |
|---|---:|---|
| `IDENTICAL` | **3,738** | CNA's build is the reference, byte for byte. |
| `SEMANTICALLY_IDENTICAL` | 735 | Everything a runtime reads is equal. 725 are LZX -- two conforming encoders, one payload -- and 10 are numbers agreeing to within 6e-08 of the larger magnitude. |
| `ACCEPTED_DIFFERENCE` | 726 | Measured, understood, and not closable from here. |
| `CUSTOM_PIPELINE_GAP` | 1,370 | The asset needs a component the *sample* defines, in a .NET assembly C++ cannot load. |
| `ENVIRONMENT_GAP` | 15 | This machine's, not CNA's: 10 effects the June-2010 fxc refuses and XNA's own D3DX9 accepted, and 5 fonts Windows has and this machine has not. |
| `CORPUS_GAP` | 946 | The reference is in the corpus and the sweep has no way to build it. |
| `UNEXPLAINED` | **204** | Everything else. |

The accepted differences, by reason:

| count | reason |
|---:|---|
| 250 | Two rasterizers disagree about a glyph's ink by a pixel: GDI+ against FreeType. |
| 244 | Generated mip levels only, from the dither in XNA's own filter (`XNASWEEP-123`). |
| 92 | The effect blob carries its compiler's version string (`XNASWEEP-108`). |
| 88 | Two conformant JPEG decoders inside an IDCT's tolerance. |
| 48 | An Xbox 360 target: XMA has no publicly implementable encoder. |
| 4 | XNA re-encodes a song to WMA (`XNASWEEP-125`). |

The custom-pipeline gap is 1,215 `.xml` documents naming a game's own type and
155 assets whose project names one of 25 processors or importers no XNA assembly
defines -- `NormalMappingModelProcessor` (26), `SkinnedModelProcessor` (20),
`TrianglePickingProcessor` (20), `CustomEffectModelProcessor` (8) and twenty-one
more. The corpus gap is 542 model side outputs whose model is itself missing,
367 references under no build unit this run reaches, and 37 whose project item
names a source file the tree has not got.

### 11.3.1 What is still unexplained

All 204, by what produced them:

| count | processor | what differs |
|---:|---|---|
| 182 | `ModelProcessor` | `XNASWEEP-129`: bounding-sphere centres and bone translations at 1e-7, and the vertex-buffer digests that follow from 7% of one mesh's texture coordinates. |
| 15 | `TextureProcessor` | `.png`, `.dds` pairs whose difference is neither the mip dither nor the payload. |
| 6 | `FontTextureProcessor` | What is left of `XNASWEEP-131` after 34 of its 40 references became identical. |
| 1 | `PassThroughProcessor` | One `.xml` asset. |

### 11.4 The read-only roots, re-audited 2026-09-08

`build/xna-sample-sweep/audit/` holds the `find . -printf '%y %s %T@ %p'`
baseline of both roots, taken before any work at 17:54 on 2026-09-07, and the
audit re-takes and diffs it. **Take it from inside each root**, as the baseline
was: an absolute-path re-take differs from it on every line and says nothing.

| root | result |
|---|---|
| `/rv/tmp/XNAGameStudio/Samples` | **0 differing lines.** Every type, size, modification time and path identical. |
| `/rv/tmp/samples` | Changed, and none of it by this campaign. |

The second row needs its evidence rather than an assurance, because this machine
runs many sessions at once and that tree is not only this campaign's.

Every file that changed outside the `cna-*` and `CNA_BUILD` build trees -- 1,028
of them -- is under **`SAMPLE-070-RolePlayingGame_4_0_Win_Xbox` (1,027)** and
`SAMPLE-068-CatapultWarsTrainingKit_4_0` (1), and they arrive with an
`evidence/space-key-probe/` directory holding a `chrome.log` and a `server.log`,
a `scripts/probe-space.sh`, and a new `MANIFEST.md`: another session's browser
probe, regenerating that sample's own `xna4-build` outputs. Nothing this campaign
does writes into either root -- the sweep builds into
`build/xna-sample-sweep/out/units/`, the model oracle into
`build/xna-sample-sweep/model-probe*/`, and the differential oracle into
`build/xna-pipeline-oracle/`. The one path that ever did was `BuildContent`'s own
configuration file, which `XNASWEEP-110` moved into the intermediate directory;
`find` over both roots now returns no `cna-buildcontent.json` at all.

### 11.5 Next

1. Re-run the sweep on the current binary after each batch of fixes; every fix
   from `XNASWEEP-118` on moves a category.
2. Run `classify.py` and split `differs` into `payload-identical` (LZX), the
   float-tolerance rows and the rest.
3. `.jpg` is measured and accepted; `.fbx` is measured through the genuine
   importer (`XNASWEEP-121`) and `.x` through it too (`XNASWEEP-122`).

---

## 12. What the campaign found

**The denominator.** 7,734 genuine `.xnb` files, produced by Microsoft's own XNA
4.0 Content Pipeline from public XNA samples, present on this machine. Frozen by
`corpus_inventory.py` before any work; not one was generated, added or dropped
for this campaign.

**The answer.** 3,738 of them are byte for byte what CNA's product pipeline
produces from the same source, up from 922 when the sweep first ran and 3,619 at
the run the earlier fixes were measured against. Another 735 differ only in ways
nothing a runtime does can see. 726 differ for a reason that is written down and
cannot be closed from here. 1,370 need a component the *sample* defines and .NET
loads from an assembly. 946 the sweep has no way to build at all, and 15 are this
machine's fault rather than CNA's. **204 are unexplained**, and §11.3.1 says
exactly which and what differs in each.

### 12.1 What the corpus found that one project could not

Fourteen framework defects, each fixed in the component that owns it, each with
its own regression test. None of them is sample-specific and none of them was
visible from the API surface, the differential corpus, or Platformer:

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

In the order the measurements suggest:

1. `XNASWEEP-129` -- 7% of one FBX mesh's texture coordinates. 182 of the 204
   unexplained references sit behind it, and the channel is already isolated.
2. The last six `FontTextureProcessor` references, which `XNASWEEP-131` did not
   reach when the other 34 became identical.
3. The 15 `TextureProcessor` pairs whose difference is neither the mip dither nor
   the container.

`XNASWEEP-130` is now answered rather than open: the sprite-font packer grew tall
where XNA grows wide because it was a shelf packer, and `XNASWEEP-131` replaced it
with XNA's own. Those atlases will not make a `.spritefont` identical while the
rasterizer differs, but the packer underneath them is no longer the reason.

