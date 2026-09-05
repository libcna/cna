# Shared cross-renderer parity fixtures

`plans/plan_webgpu.md` **`WEBGPU-207`**.

## Why this exists

A behavioural claim about a renderer used to be verified by a test written for that renderer. Two
renderers therefore got two tests of the same behaviour — two oracles, which can disagree without
either of them failing, and which have to be kept in step by hand for as long as both exist.

A **parity fixture** is one renderer-neutral source that states a behaviour *and its expected
result*, executed unchanged by every renderer that opts in. There is one oracle, so "EasyGL and
WebGPU agree" is something the suite can actually establish rather than something a reader infers
from two green tests.

This is not a new harness. It extends the renderer-agnostic dump-and-compare mechanism that
`modules/graphics/examples/cross_renderer_diagnostic_scene.cpp` and `cross_renderer_2d_corpus.cpp`
already used, and it writes its frames in exactly the format `cross_renderer_diagnostic_compare.cpp`
(`cna_diag_compare`) already reads.

## Adding a fixture

1. Write `modules/graphics/examples/parity/parity_<name>.cpp`:
   - derive from `CNA::Parity::ParityFixture` (`parity/ParityFixture.hpp`) and implement
     `RunFixture()`;
   - use only public XNA API — no `#ifdef`, no renderer include, no renderer name anywhere;
   - state the expected semantics as `Expect*` assertions **in the fixture**;
   - end with `CNA_PARITY_FIXTURE_MAIN(<YourFixture>)`.
2. Append `<name>` to `CNA_PARITY_FIXTURES` in
   `modules/graphics/examples/parity/ParityFixtures.cmake`.

That is the whole cost. Every renderer whose `examples/CMakeLists.txt` calls
`cna_register_parity_fixtures()` — EasyGL and WebGPU today — builds the new source into
`cna_parity_<name>_<renderer>` and registers `<Renderer>_Parity_<name>` as a CTest automatically.

## Running them

Each renderer's own build runs its half as an ordinary test:

```bash
ctest --test-dir cmake-build-debug  -R 'EasyGL_Parity'  --output-on-failure   # OPENGL33 build
ctest --test-dir cmake-build-webgpu -R 'WebGPU_Parity' --output-on-failure    # WEBGPU build
```

The cross-renderer pixel comparison needs both builds, because `CNA_GRAPHICS_RENDERER` is a
compile-time choice:

```bash
scripts/run-parity-fixture.sh vertex_semantics            # both legs + cna_diag_compare
scripts/run-parity-fixture.sh vertex_semantics cmake-build-debug cmake-build-webgpu 2
```

Each fixture executable also takes an optional output path and writes its whole backbuffer there as
raw R,G,B,A bytes, so `cna_diag_compare <a> <b> <tolerance> <WxH>` can diff any two renderers' frames
by hand.

## The two layers, and which one is the oracle

| Layer | What it is | When it decides |
|---|---|---|
| The fixture's `Expect*` assertions | The shared oracle. Runs in both builds' CTest suites. | Always. A behavioural row's acceptance is these. |
| The raw frame dump + `cna_diag_compare` | A pixel-for-pixel cross-check of the whole frame. | When a difference the assertions do not happen to sample still matters. |

## The tolerance convention

Rasterization fill rules legitimately differ at primitive **edges** — `WEBGPU-123` measured EasyGL
diverging from three other renderers on 57 triangle-edge pixels and nowhere else. So a fixture:

- samples **interior** regions, never a boundary pixel (`CNA::Parity::kEdgeInset`, and
  `ParityGrid::Interior()` applies it);
- compares a region **average** rather than one texel, so a stray edge pixel cannot decide a verdict;
- uses tolerance **0–2** for a flat, unshaded, unfiltered colour (2 covers the
  unorm8 → f32 → unorm8 round trip and nothing else), and `CNA::Parity::kShadingTolerance` for a
  lit, filtered or blended one — with the call site saying *why* the wider one is justified.

A difference that needs more than `kShadingTolerance` is a **finding**, not a tolerance argument. It
belongs in `plans/plan_webgpu.md` (or the reference renderer's own plan), not in the fourth argument
of an assertion.

**When the whole-frame dump does not apply.** A fixture whose picture is *entirely* boundary — a
wireframe is one-pixel lines and nothing else — cannot be judged by `cna_diag_compare`, because the
only pixels it has are the ones two conforming rasterizers may legitimately disagree about.
`fill_mode_wireframe` is the current example: EasyGL and WebGPU differ on 367 of 32768 pixels
(1.12%, measured 2026-09-05), all of them on an edge and none anywhere else. The right answer is
**not** a tolerance wide enough to swallow that; it is to say so in the fixture and let its
assertions — interior coverage and per-edge counts, which agree exactly — be the comparison. A
fixture that opts out of the dump comparison must say why in its header, as that one does.

## Making a fixture prove something

Two renderers that both draw nothing agree perfectly. Every fixture therefore has to show that the
thing under test **changed the output**:

- `ExpectDistinct(label, regionA, regionB, minDelta)` — the attribute materially altered the result;
- `ExpectBrighter(label, lit, unlit, minDelta)` — a relational claim that is exactly true on every
  conforming renderer, unlike an absolute shade;
- `ExpectFlat(label, region, maxSpread)` — the sampled region really is the flat interior the
  fixture claims, so a later average is not quietly reading across a geometry edge.

Prefer a relational assertion wherever the exact value is a legitimate implementation detail; use an
absolute one where XNA's own arithmetic makes the value exact (an unlit vertex colour, an `N·L` of
exactly 1 or 0, a point-sampled texel).

## Current fixtures

| Fixture | Row | What it establishes |
|---|---|---|
| `vertex_semantics` | `WEBGPU-155` | Element order, element offset and buffer stride do not change the pixels: one mesh through four legal declarations of the same semantic content. |
| `lit_untextured` | `WEBGPU-156` | A `Position+Normal` declaration lights — at stride 24 and padded to 32 — and the normal materially drives the shading. |
| `lit_vertex_color` | `WEBGPU-157` | The stock `ModelProcessor`'s stride-36 `Position+Normal+Color+TextureCoordinate` vertex renders lit, with `VertexColorEnabled` gating the tint. |
| `unlit_position_color` | `WEBGPU-158` | A stride-32 declaration with no `Normal` renders unlit and keeps its colour, while a real `VertexPositionNormalTexture` at the same stride still lights. |
| `dual_texture_uv1` | `WEBGPU-159` | `DualTextureEffect` samples its two textures with `TEXCOORD0` and `TEXCOORD1` independently, and an absent `TEXCOORD1` reads `(0,0)`. |
| `multi_stream_split` | `WEBGPU-172` | A vertex split across two `VertexBufferBinding`s (position-only at stride 12, colour-only at stride 4) renders the same picture as the same vertex in one packed stride-16 buffer, and each binding's `VertexOffset` is converted with its own stride. |
| `render_target_mip` | `WEBGPU-164` | A `mipMap: true` `RenderTarget2D` has a real chain: `GetData` at both ends returns the drawn quadrants and their regenerated mean, and sampling the target unrestricted versus pinned to its coarsest level gives materially different pictures. |
| `hdr_render_target` | `WEBGPU-199` | An `HdrBlendable` target keeps values above 1.0: drawn at half tint, a target holding 2.0 saturates at 255 while one holding 1.0 gives 128, so a target that clamped its own storage renders both columns the same and fails. |
| `fill_mode_wireframe` | `WEBGPU-153` | `FillMode::WireFrame` draws all three triangle edges and leaves the interior empty, while `FillMode::Solid` fills it. |

## Adding a renderer

In that renderer's `examples/CMakeLists.txt`, inside the block that already builds its example
tests:

```cmake
include(${CNA_GRAPHICS_EXAMPLES_DIR}/parity/ParityFixtures.cmake)
cna_register_parity_fixtures(
    BUILDER      cna_<renderer>_test      # the module's own "add an example executable" macro
    TARGET_SUFFIX <renderer>
    TEST_PREFIX   <Renderer>
    LABELS       "Parity;<Renderer>"
    ENVIRONMENT  "SDL_VIDEODRIVER=x11;DISPLAY=${CNA_TEST_DISPLAY}")
```

A renderer that cannot execute a given fixture at all should not be added until it can: a fixture
that is skipped everywhere defends nothing.
