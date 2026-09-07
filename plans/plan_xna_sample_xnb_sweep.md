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

### 11.2 The sweep, run 3 (2026-09-07)

Run with the binary as of `34506a3f8`, so it does **not** carry the PNG gamma,
`.x` normal, `.x` specular-power or effect debug-mode fixes committed after it.

| | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| byte-identical | 922 | 3,524 | **3,619** |
| differing | 343 | 1,444 | 1,547 |
| missing (CNA produced nothing) | 6,102 | 2,399 | 2,201 |

By source extension, run 3 (`identical / differing / missing`):

| extension | identical | differing | missing | identical % |
|---|---:|---:|---:|---:|
| `.png` | 3,096 | 834 | 55 | 77.7 |
| `.xml` | 1 | 0 | 1,219 | 0.1 |
| (model side output) | 0 | 16 | 730 | 0.0 |
| `.fbx` | 0 | 174 | 126 | 0.0 |
| `.wav` | 279 | 9 | 7 | 94.6 |
| `.spritefont` | 0 | 242 | 17 | 0.0 |
| `.tga` | 167 | 7 | 0 | 96.0 |
| `.fx` | 0 | 89 | 14 | 0.0 |
| `.jpg` | 0 | 88 | 8 | 0.0 |
| `.bmp` | 54 | 30 | 9 | 58.1 |
| `.x` | 0 | 52 | 11 | 0.0 |
| `.wma` | 14 | 1 | 0 | 93.3 |
| `.dds` | 8 | 3 | 0 | 72.7 |

### 11.3 What the categories are

* **`.xml`, 1,219 missing** -- `CUSTOM_PIPELINE_GAP`. Every one names a
  game-defined type (`RolePlayingGameData.Armor`, `ParticlesSettings.ParticleSystemSettings`,
  `MovipaLibrary.LayoutInfo`), which is what the extension is *for*. XNA loads
  the game's pipeline assembly; C++ has none, and the documented route is a C++
  equivalent registered in code. `modules/content-pipeline/examples/xna-custom-pipeline.cpp`
  is the shape one takes.
* **730 model side outputs missing** -- a texture a `ModelProcessor` builds for a
  material, named `<stem>_<n>`, which no project item names. They are missing
  because their *model* is missing, which is mostly the custom-processor gap
  above.
* **LZX** -- 719 of the 862 differing `.png` references of run 2 are compressed,
  and the first one examined has a byte-identical decompressed payload. Two
  conforming LZX encoders do not agree on a byte; `classify.py` separates that
  from a real difference.
* **`.spritefont`, 242 differing** -- the atlas and the glyph boxes.
  `decisions.json`'s `xbox_font_description` records the measurement: over 95
  glyphs, 40 boxes identical, 50 one pixel wider, four two wider, never
  narrower, while every ABC width and the line spacing agree exactly. XNA draws
  through GDI+ and CNA through FreeType.
* **`.fx`, 89 differing** -- the compiler's own version string is inside the
  blob (`XNASWEEP-108`), and XNA's is a D3DX9 this machine has not got.
* **`.jpg`, 88 differing** -- GDI+'s JPEG decoder against stb_image's.

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
