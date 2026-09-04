# Removed renderers

Renderer identities CNA no longer carries. Each was removed because it duplicated a
rendering strategy CNA keeps, added no platform CNA did not already reach, or could not
satisfy `IGraphicsRenderer` at all — never because the work was bad. Several were
excellent reconnaissance; what is kept is the finding, not the code.

**The code is not gone.** Every removal is one commit, tagged `removed/<family>`, so
`git show removed/<family>` is the whole implementation. What git does *not* preserve is
the third-party project each one wrapped, so every entry below records the exact pinned
dependency. That pairing — CNA's code in git history, the dependency's coordinates here —
is the archive.

**A revert is a reference, not a patch.** `IGraphicsRenderer` changed 65 times in the
three months before these removals. Beyond a few months the removal commit is a
specification to port forward, not a patch to apply. Expect to read it, not `git revert` it.

The ten identities removed alongside `SKIA` on 2026-08-30 — `LLGL`, `SOKOL`, `DILIGENT`,
`IGL`, `WICKED`, `MAGNUM`, `BLEND2D`, `NANOVG`, `OPENVG` and `TINYGL` — were restored on
2026-09-04 and are live again; only `SKIA` stays retired, and only its identity number is
a gap in the C ABI range.

---

## SKIA

| | |
|---|---|
| Identity | `SKIA` (enum `Skia`, C ABI `CNA_GRAPHICS_RENDERER_SKIA` = 19) |
| Family | `modules/renderers/skia` |
| Removed | 2026-08-30, tag `removed/skia` |
| Size | 32,563 lines (9,684 production, **0 tests**, 22,879 examples) — plus 34 `docs/skia-*.md`, `plans/plan_skia.md`, `NEXT_skia.md` and 6 `scripts/validate_skia_*.py` |
| Dependency | `https://skia.googlesource.com/skia.git` — **not pinned**; the developer build cloned it at whatever HEAD was, and CMake required `-DCNA_SKIA_ROOT=<checkout>` rather than fetching it |
| Build was | `-DCNA_GRAPHICS_RENDERER=SKIA -DCNA_SKIA_ROOT=<skia checkout>` |

**What it proved.** That an external CPU rasterizer can be driven as a CNA renderer, and —
more usefully — exactly where that stops. The 3D refusal was reasoned out rather than
assumed (`docs/skia-3d-refusal.md`, `docs/skia-3d-emulation-adr.md`), and the GLSL→SkSL
translator contract, the CPU depth/stencil/geometry spikes and the surface-format matrix
are all real findings about the cost of emulating a GPU pipeline on a 2D canvas API.

**Why removed.** It is 2D-only by construction, so it can never satisfy `IGraphicsRenderer`:
CNA's contract has 102 pure-virtual methods covering depth, render targets, MSAA, MRT and
stock 3D effects, and Skia never advertised any of them. That is a category error rather
than unfinished work — no amount of further effort finishes it. Meanwhile EasyGL already
renders CNA's 2D on every platform CNA targets, on the GPU. Skia was also the heaviest
dependency in the tree, and the only one with no pinned revision — the build was not
reproducible across machines or across time.

**Note for anyone restoring it.** Because the dependency was never pinned, this removal
commit does not identify the Skia revision it was written against. Reconstructing that
from `docs/skia-ganesh-artifact.md` and the commit dates is the first task.

