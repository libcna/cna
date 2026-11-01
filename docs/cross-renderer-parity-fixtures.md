# Shared cross-renderer parity fixtures

**Status: reconciled with the registry and runners on 2026-11-01.** A parity fixture is one
renderer-neutral public-XNA program whose assertions run unchanged through every participating
renderer. It prevents two renderer-specific tests from becoming two subtly different oracles for
the same behavior.

The authoritative inventories and registration functions are in
[`ParityFixtures.cmake`](../modules/graphics/examples/parity/ParityFixtures.cmake):

- `CNA_PARITY_FIXTURES` is the shared rendered-parity corpus;
- `CNA_CONFORMANCE_FIXTURES` is the separate renderer-neutral conformance-program corpus;
- `cna_register_parity_fixtures()` registers both corpora for a rasterizing family;
- `cna_register_conformance_programs()` registers only the conformance programs for a family that
  cannot participate in whole-frame parity.

Do not copy either inventory into another CMake file. The original `WEBGPU-207` task established
the shared registry; its task record is historical, while this page describes the current
infrastructure.

## The two test families

| Family | Source shape | Oracle and output | Current registration |
|---|---|---|---|
| Parity fixture | `modules/graphics/examples/parity/parity_<name>.cpp`, derived from `CNA::Parity::ParityFixture` | In-program `Expect*` assertions are primary; the optional RGBA8 frame dump supports a second cross-renderer comparison | EasyGL, WebGPU and SDLGPU |
| Conformance program | `modules/graphics/examples/conformance/<name>_test.cpp`, normally a complete `PixelTestGame` | Public-API assertions and pixels; exits 77 with a named capability reason when genuinely inapplicable | EasyGL, WebGPU, SDLGPU, Software, Headless and FNA3D |

Conformance programs preserve behavior first measured in many older renderer campaigns. They are
not parity fixtures merely because the same CMake file registers them: they do not derive from
`ParityFixture`, do not participate in `run-parity-corpus.sh`, and may make a capability-based
skip. Keeping the lists separate prevents a renderer without whole-frame output from weakening the
shared behavioral coverage.

## Current parity surface

`CNA_PARITY_FIXTURES` currently contains 32 sources. The names below are grouped for navigation;
the CMake list, not this prose count or grouping, decides what is built.

| Area | Fixture names |
|---|---|
| Presentation and resources | `backbuffer_msaa`, `render_target_mip`, `hdr_render_target`, `compressed_cube` |
| Vertex input and draw routing | `vertex_semantics`, `lit_untextured`, `lit_vertex_color`, `unlit_position_color`, `multi_stream_split`, `instanced_draw` |
| Stock effects | `dual_texture_uv1`, `basic_effect_light_terms`, `basic_effect_vertex_color`, `basic_effect_alpha_scale`, `alpha_test_sweep`, `alpha_test_sources`, `dual_texture_terms`, `env_map_terms`, `skinned_terms` |
| SpriteBatch and SpriteFont | `sprite_geometry`, `sprite_state`, `sprite_font` |
| Sampling and render state | `sampler_filters`, `sampler_max_mip_level`, `sampler_lod_bias`, `sprite_sampler_state`, `blend_states`, `depth_states`, `stencil_states`, `stencil_compare`, `rasterizer_viewport`, `fill_mode_wireframe` |

Each entry has its detailed behavioral claim beside it in `ParityFixtures.cmake` and in the fixture
source. Adding or removing an entry there changes EasyGL, WebGPU and SDLGPU registration together.
The CMake function rejects a registered name whose source is missing.

## Running registered tests

Every parity fixture is a GPU/window test. Run the registered renderer leg through the private GPU
runner; never call these `ctest` selections on the owner's desktop:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-opengl33 \
  -R '^EasyGL_Parity_' --output-on-failure -j6
tools/platform/run_gpu_tests_private.sh cmake-build-webgpu \
  -R '^WebGPU_Parity_' --output-on-failure -j6
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu \
  -R '^SdlGpu_Parity_' --output-on-failure -j6
```

These runs execute the fixture's own assertions. They are sufficient to accept or reject the
behavior each source promises; they do not compare every output pixel between renderer families.

## Running cross-renderer frame comparisons

The same fixture can also write its full backbuffer as tightly packed RGBA8. The comparison layer
looks for differences outside the sampled assertion regions:

```bash
tools/platform/run_gpu_tests_private.sh --exec \
  scripts/run-parity-fixture.sh vertex_semantics \
  cmake-build-opengl33 cmake-build-webgpu 2

tools/platform/run_gpu_tests_private.sh --exec \
  scripts/run-parity-corpus.sh \
  cmake-build-opengl33 cmake-build-webgpu webgpu

tools/platform/run_gpu_tests_private.sh --exec \
  scripts/run-parity-corpus.sh \
  cmake-build-opengl33 cmake-build-sdlgpu sdlgpu
```

`run-parity-fixture.sh` compares one EasyGL/WebGPU fixture with `cna_diag_compare`.
`run-parity-corpus.sh` reads `CNA_PARITY_FIXTURES` directly and compares EasyGL with either WebGPU
or SDLGPU. Both scripts inherit the private runner's `DISPLAY`; they refuse to run without a
display instead of falling back to a hard-coded server. `CNA_PARITY_DISPLAY` remains an explicit
test-infrastructure override and must never be pointed at the live desktop.

The frame reference must be a desktop `OPENGL33` EasyGL build. `OPENGLES3` runs the same fixture
assertions, including profile-specific limitations such as the absence of sampler LOD bias, but
its frame is not interchangeable with the desktop reference. Both scripts inspect each build's
`CMakeCache.txt` and refuse a mismatched renderer identity.

The scripts require the fixture executables and `cna_diag_compare` to have been built in the named
trees. A cross-renderer comparison cannot come from one multi-renderer process: renderer selection
is a compile-time property for these trees, so each leg is its own executable.

## Which result is authoritative

| Layer | Role |
|---|---|
| Fixture `Expect*` assertions | Primary behavioral oracle. They run in every registered renderer leg and decide the fixture's contract. |
| Full-frame dump comparison | Secondary cross-check. It detects an unexpected difference outside the assertion samples. |

Two broken renderers can produce identical empty frames, so byte equality alone never establishes
correctness. Every fixture must prove that the feature materially changed the result before its
frame is useful as parity evidence.

The corpus runner owns the reviewed full-frame policy:

- a fixture not listed in its exception table is strict: no pixel may differ by more than the
  per-channel rounding tolerance;
- `allow:<count>:<reason>` permits a measured number of differing pixels for a named rasterization
  boundary;
- `internal:<reason>` means the fixture's within-renderer assertions are the complete oracle
  because a whole-frame comparison is not meaningful.

The WebGPU and SDLGPU policy tables are independent. Do not inherit an exception merely because
both implementations often run over Vulkan, and never widen a budget just to make a run green.

## Assertion and tolerance rules

Rasterization fill rules legitimately differ at primitive edges. A fixture should therefore:

- sample interiors rather than boundary pixels (`CNA::Parity::kEdgeInset` and
  `ParityGrid::Interior()` provide the shared convention);
- compare a region average when a single edge texel could otherwise decide the result;
- use tolerance 0–2 only for flat, unfiltered values where that allowance covers numeric
  round-trip behavior;
- use `CNA::Parity::kShadingTolerance` only for lit, filtered or blended values, with the reason
  visible at the assertion;
- treat a larger unexplained difference as a renderer finding, not a tolerance choice.

Prefer relational assertions where the exact shade is implementation-dependent:

- `ExpectDistinct()` proves that two cases do not collapse to the same output;
- `ExpectBrighter()` proves a directional lighting or blend relationship;
- `ExpectFlat()` proves that a sampled region is actually the uniform interior it claims to be.

Use an absolute expected value only where XNA's arithmetic makes it exact, such as an unlit vertex
color, an `N·L` of exactly zero or one, or a point-sampled authored texel.

## Adding a parity fixture

1. Add `modules/graphics/examples/parity/parity_<name>.cpp`.
2. Derive from `CNA::Parity::ParityFixture` and implement `RunFixture()`.
3. Use only public XNA API: no renderer include, renderer name or renderer-specific `#ifdef`.
4. Put the expected semantics and a non-degeneracy assertion in the fixture itself.
5. End with `CNA_PARITY_FIXTURE_MAIN(<FixtureType>)` so the optional dump path is supported.
6. Add the name once to `CNA_PARITY_FIXTURES`, with a concise behavioral claim.
7. Build and run all three registered renderer legs through the private runner.
8. Run both corpus comparisons. If a whole-frame exception is legitimate, add a measured,
   renderer-specific policy and reason to `run-parity-corpus.sh`.
9. Mutation-check the fixture: deliberately break the behavior, prove the intended assertion goes
   red, revert, and reconfirm the registered legs.

A fixture skipped by every renderer proves nothing. If the contract is capability-conditional or
cannot provide comparable whole-frame output, it probably belongs in `CNA_CONFORMANCE_FIXTURES`
instead.

## Adding a conformance program

Add one complete public-API program under `modules/graphics/examples/conformance/` and register its
base name in `CNA_CONFORMANCE_FIXTURES`. A capability precondition may exit 77, but it must identify
the missing capability; an unexpected renderer/profile refusal is not a successful skip. The
shared function registers the program for every participating family, including conformance-only
Software, Headless and FNA3D.

## Adding a renderer family

A rasterizing family joins the parity corpus by including `ParityFixtures.cmake` and calling
`cna_register_parity_fixtures()` inside its examples/tests block with its builder, target naming,
test prefix, labels, platform environment and validation-fatal patterns. Follow the existing
EasyGL, WebGPU and SDLGPU registrations rather than copying an old generic snippet.

A family that cannot emit meaningful parity frames may call
`cna_register_conformance_programs()` directly, as Software, Headless and FNA3D do. It must not
join the parity list by turning unsupported fixtures into blanket skips.
