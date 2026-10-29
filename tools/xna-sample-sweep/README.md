# The XNA sample `.xnb` sweep

These tools rebuild every `.xnb` that Microsoft's own XNA 4.0 Content Pipeline produced from a
public XNA sample, from the sample's original source asset, through CNA's product pipeline, and
compare the two byte for byte. The campaign that built them (`XNASWEEP-*`, 2026-09-07..11) is in
Git history; comments cite its rows as `XNASWEEP-nnn`.

## What counts as a genuine reference

The corpus is `/rv/tmp/samples`, 157 per-sample artefact roots, read-only. Only some of what they
hold came out of Microsoft's pipeline, and each root's own directory convention (documented in its
`MANIFEST.md`) says which:

| Directory | Verdict |
|---|---|
| `xna4-build/`, `xna4-diagnostic/`, `xna4-diag/` | genuine: `BuildContent` under Wine with the real XNA 4.0 assemblies |
| `win7-export/`, `win7-build/`, `win7-work/`, `SAMPLES-DEC-007-Win7-SongProcessor/export/` | genuine: built on the offline Windows 7 virtual machine |
| `xnb-staging/` | genuine: staged output of the same runs |
| `xna4-original/`, `xna-original/`, `original/`, `xbox-refs/` | genuine: `.xnb` Microsoft shipped inside the sample |
| `cna-*/`, `fna-*/` | **not** a reference: CNA's and FNA's own output |
| `evidence/`, `fixtures/`, `mgcb/`, `diagnostic*/` | **not** a reference: captures, hand-made fixtures, another tool |

One file is excluded by name: `SAMPLE-013-Platformer_4_0/xna4-build/Content/Sounds/Music.xnb` (and
its copy under `bin/`) was written by the sample's own hand-written `BinaryWriter`, not by
`BuildContent`, and carries one type reader where XNA writes two. `corpus_inventory.py` freezes the
rest by digest into `tests/reference/xna40/sample-xnb-corpus.json`: 7,726 references.

## How a reference is rebuilt and judged

* **Mapping** (`map_sources.py`). An asset's logical name is a suffix of its output path, so the
  item and the output root that built it can be read off the reference path. A project scored
  against each root owns the one it explains best; that pair is a *build unit*. Seven samples ship
  no `.contentproj`, and `synthesize_projects.py` writes one for each from
  `tests/reference/xna40/build-units/`.
* **Rebuild** (`sweep.py`). Only through the product path: `cna-content build <project> -o <dir>
  --format xnb --only-configured-assets --xna-compatible`, with the target read off the
  reference's header. Never through an internal serializer, never by copying a reference.
* **Comparison** (`classify.py`, `taxonomy.py`). Raw bytes first; where they differ, the semantic
  diff of `tools/xna-pipeline-oracle/differential/compare.py` over the independent parser
  `tools/xnb/xnb_conformance.py`. Every reference ends in exactly one class, and the gate fails
  while `UNEXPLAINED` is above zero.
* **Audits.** A class is only as good as the check under it: `accepted_audit.py` proves every
  accepted reference's differing fields fall inside its stated reason (a reference with no
  recorded fields fails too), `tolerance_mutation.py` mutates the floats the tolerance judged, and
  `semantic_mutation.py` mutates the bytes a payload-identical claim covers. A run that skips them
  has not been checked.

A component the *sample* defines in its own C# assembly is `CUSTOM_PIPELINE_GAP` until an
equivalent exists under `tests/sample-pipeline/`, written against the public XNA façade and never
inside a core processor. A framework deficiency a sample exposes is fixed generally, in the
component that owns it, with its own regression test.

**The corpus is read-only, timestamps included.** `build/xna-sample-sweep/audit/` takes a
`find -printf '%y %s %T@ %p'` baseline of the corpus before a run and the final audit diffs it
(`XNASWEEP-110` is the one violation found, and fixed: a build wrote its configuration into the
source root).

## Running it

A full sweep is 433 build units and about five hours. Every classification is a full
re-measurement, because a cached one has been wrong twice (`XNASWEEP-208`, `XNASWEEP-226`).

```bash
python3 tools/xna-sample-sweep/corpus_inventory.py \
        --out build/xna-sample-sweep/manifest/sample-xnb-corpus.json
python3 tools/xna-sample-sweep/runner_provenance.py \
        --out build/xna-sample-sweep/manifest/runner-provenance.json
python3 tools/xna-sample-sweep/synthesize_projects.py
python3 tools/xna-sample-sweep/map_sources.py \
        --manifest build/xna-sample-sweep/manifest/sample-xnb-corpus.json \
        --provenance build/xna-sample-sweep/manifest/runner-provenance.json \
        --synthesized build/xna-sample-sweep/synthesized \
        --out build/xna-sample-sweep/manifest/sample-source-map.json
cp cmake-build-multi/cna-content build/xna-sample-sweep/bin/cna-content   # freeze it
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

then `taxonomy.py`, `accepted_audit.py`, `tolerance_mutation.py` and `semantic_mutation.py` over
the classified results; `status_table.py` prints a run-by-run table. `--synthesized` is not
optional: without it the map falls from 433 build units to 327. **Never rebuild `cna-content`
while a sweep runs** -- the sweep runs the frozen copy for exactly that reason, and the first run
was invalidated by a mid-run relink.

## The last full run (run 66, 2026-09-11)

| Class | References |
|---|---:|
| `IDENTICAL` (byte for byte) | **4,337** |
| `SEMANTICALLY_IDENTICAL` (729 in the LZX stream alone; 18 floats within 1.94e-16..3.89e-07 relative) | 747 |
| `ACCEPTED_DIFFERENCE` | 815 |
| `CUSTOM_PIPELINE_GAP` (an importer or processor the sample defines, 43 kinds) | 1,797 |
| `ENVIRONMENT_GAP` (5 uninstalled fonts, 4 sources this D3DX9 refuses) | 9 |
| `REFERENCE_REMOVED` (gone from the read-only tree, itemised by `XNASWEEP-203`) | 21 |
| `UNEXPLAINED` | **0** |
| total | 7,726 |

The 815 accepted differences have seven reasons, each audited field by field:

| References | Reason | What may differ |
|---:|---|---|
| 276 | GDI+ and FreeType disagree about a glyph's ink by a pixel | the ink, its box and the sheet; not the metrics or the atlas format |
| 263 | Two DXT compressors choose different endpoints | the compressed blocks only |
| 140 | Two conformant JPEG decoders (D3DX, stb_image) inside an IDCT's tolerance | the decoded pixels only |
| 102 | The effect blob carries its compiler's version string (`XNASWEEP-201`/`225`) | that string and the padding behind it |
| 18 | XNA's mip filter dithers before it quantizes (`XNASWEEP-123`/`223`) | generated levels 1 and up, by exactly one |
| 12 | A song re-encoded to WMA, for which no encoder exists here | the re-encoded media and what describes it |
| 4 | Xbox 360 audio: XMA has no publicly implementable encoder | the audio payload and its `WAVEFORMATEX` |

## What is left

* **Not reachable from here:** the 1,797 sample-defined components (building one means running the
  sample's own code), the 815 accepted differences (four are unpublished Microsoft encoders or
  rasterizers), the 9 environment gaps and the 21 removed references.
* **Reachable and small**, none visible in the corpus: the sign of a zero in a generated `.x`
  normal (13 of 1,291 probe normals and one fixture normal; `XNASWEEP-231` names the basis matrix
  as the likely cause, and generating in the file's own space would move every generated normal's
  rounding, so it waits for a probe that separates the two orders on magnitudes); the FBX bone
  ulp residue (SAMPLE-142's thirteen models went from 3,049 differing bone elements to 597, all
  within tolerance; what remains accumulates with depth); and `MeshHelper.OptimizeForCache`'s
  strip termination, which reproduces the corpus but is still empirical (9 of 20 probes exact).
* **Owed by the harness:** the three audits should be one step of the recipe rather than three a
  session has to remember.
