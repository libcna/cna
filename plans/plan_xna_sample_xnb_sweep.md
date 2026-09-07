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
| `XNASWEEP-117` | Two models that name the same texture are refused; XNA builds it once and lets both reference it. | [ ] Recorded, not fixed. `ReserveOutputs` in `tools/content/content.cpp` gives each output logical name exactly one owning node, so Spacewar's `p1_bfg` and `p1_dual`, which both name `textures/p1_back.tga`, collide on `textures/p1_back_0` and neither builds. XNA's own build produces that texture once and both models reference it; the coordinator needs nested nodes shared across parents by their `(source, processor, parameters)` key rather than owned by whichever parent reached them first. 85 of Spacewar's 154 references are still missing for this reason alone. |


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

### 11.4 Next

1. Re-run the sweep on the current binary: the four fixes after run 3 each move
   a category, and `.spritefont`'s 17 remaining "missing" are Tahoma, which the
   Wine prefix has not got.
2. Run `classify.py` and split `differs` into `payload-identical` (LZX) and the
   rest.
3. The `.jpg` and `.fbx` families are unmeasured; the `.x` one is now measured
   and green except for a bone whose name is empty rather than absent, which
   needs `ContentItem::Name` to be able to say "no name" at all.
