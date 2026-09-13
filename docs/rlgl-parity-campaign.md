# RLGL classic-XNA parity campaign

This is the evidence record for `plans/plan_rlgl.md` task `RLGL-019`. It compares the
renderer-neutral classic-XNA behavior of EasyGL and RLGL; it does not declare final parity. Only
`RLGL-022` may do that after the final implementation, error-path, CI, documentation, and platform
audit.

## Scope and environment

The campaign ran on 2026-09-13 with SDL3's `offscreen` video driver and Mesa llvmpipe 25.0.7. Both
renderers used the same revision of every shared fixture/golden source. EasyGL and
RLGL both requested desktop OpenGL 3.3 core; Mesa granted OpenGL 4.5 core. Compilation was limited
to four parallel jobs.

The comparison has four layers:

1. every non-skipped test in the shared `CnaGraphicsTests` binary;
2. all 32 sources in the central `CNA_PARITY_FIXTURES` registry, run unchanged under both renderers;
3. whole-frame RGBA8 comparison of those fixtures where the source declares that comparison valid;
4. all 11 classic EasyGL golden-image sources, compiled and run unchanged under RLGL.

The configured EasyGL CTest entries target an unavailable X11 display in this environment, so their
skip result is not used as evidence. The exact same executables were run directly with
`SDL_VIDEODRIVER=offscreen` and `LIBGL_ALWAYS_SOFTWARE=1`. RLGL's new registrations carry that
working environment themselves.

## Aggregate result

| Layer | EasyGL | RLGL | Result |
|---|---:|---:|---|
| Shared `CnaGraphicsTests` | 2,396 passed, 60 named skips, 0 failed (2,456 total) | 2,254 passed, 174 named skips, 0 failed (2,428 total) | Every enabled contract passed; totals differ with renderer capability/compile guards |
| Central parity fixtures | 32/32 | 32/32 | Same sources and assertion oracles passed |
| Applicable whole-frame comparisons | 30/30 | 30/30 | Every compared RGBA8 frame was byte-identical (`max diff = 0`) at tolerance 2 |
| Classic golden scenes | 11/11 | 11/11 | Same sources and checked-in golden oracles passed |
| Registered RLGL CTests | n/a | 134/134 | Renderer-local, shared parity, golden, workload, and compiled-effect registrations passed |

The extra EasyGL tests are not interpreted as RLGL omissions merely from their count. Feature-gated
and renderer-specific cases are reconciled by the capability matrix and final audit, while every
shared enabled assertion must pass.

## Central parity fixture inventory

`Pass` means the executable's complete internal oracle passed. `Exact` means a second comparison of
the complete RGBA8 framebuffer also had zero channel difference; the tolerance remained at 2 and
was never widened.

| Fixture | Observable contract | EasyGL | RLGL | Whole frame |
|---|---|---:|---:|---:|
| `backbuffer_msaa` | Real coverage resolve and preservation across a target switch | Pass | Pass | Diagnostic difference; see below |
| `vertex_semantics` | Declaration order, offsets, and semantic routing | Pass | Pass | Exact |
| `lit_untextured` | Position/normal BasicEffect lighting | Pass | Pass | Exact |
| `lit_vertex_color` | Model-style position/normal/color/UV input | Pass | Pass | Exact |
| `unlit_position_color` | Missing normal selects the valid unlit path | Pass | Pass | Exact |
| `dual_texture_uv1` | Independent UV0/UV1 and absent-UV1 default | Pass | Pass | Exact |
| `multi_stream_split` | Split streams and per-binding vertex offsets | Pass | Pass | Exact |
| `render_target_mip` | Generated readable RenderTarget2D mip chain | Pass | Pass | Exact |
| `hdr_render_target` | Above-one HdrBlendable value and sampled output | Pass | Pass | Exact |
| `compressed_cube` | DXT1 cube transfer/readback and internal sampling equivalence | Pass | Pass | Not applicable by fixture contract; see below |
| `basic_effect_light_terms` | Emissive, ambient, diffuse-light sum, and specular terms | Pass | Pass | Exact |
| `basic_effect_vertex_color` | Independent texture/vertex-color gates and saturation | Pass | Pass | Exact |
| `basic_effect_alpha_scale` | Default lighting, premultiplied alpha, large transforms | Pass | Pass | Exact |
| `alpha_test_sweep` | All eight alpha comparisons | Pass | Pass | Exact |
| `alpha_test_sources` | Null texture and vertex/diffuse color inputs | Pass | Pass | Exact |
| `dual_texture_terms` | Doubling, overlay, material, alpha, and null fallbacks | Pass | Pass | Exact |
| `env_map_terms` | Environment amount/specular/lights/eye/world/Fresnel | Pass | Pass | Exact |
| `skinned_terms` | Bone palette, weights, lights, specular, and vertex color | Pass | Pass | Exact |
| `sprite_geometry` | Source rectangles, transforms, fractional placement, flips | Pass | Pass | Exact |
| `sprite_state` | Sort modes, layer depth, transform, blend leakage, target coordinates | Pass | Pass | Exact |
| `sprite_font` | Glyphs, newline, and default-character substitution | Pass | Pass | Exact |
| `sampler_filters` | Min/mag/mip filters, anisotropy, and sampler slot | Pass | Pass | Exact |
| `blend_states` | Factors, functions, separate channels, constant, and mask | Pass | Pass | Exact |
| `depth_states` | All eight depth comparisons | Pass | Pass | Exact |
| `stencil_states` | Every stencil operation | Pass | Pass | Exact |
| `stencil_compare` | All comparisons and state restoration | Pass | Pass | Exact |
| `rasterizer_viewport` | Cull, scissor, viewport, targets, leakage, and depth bias | Pass | Pass | Exact |
| `fill_mode_wireframe` | Edge-only wireframe coverage | Pass | Pass | Exact |
| `sampler_max_mip_level` | Maximum mip selection | Pass | Pass | Exact |
| `sampler_lod_bias` | Mipmap LOD bias | Pass | Pass | Exact |
| `sprite_sampler_state` | SpriteBatch sampler propagation | Pass | Pass | Exact |
| `instanced_draw` | Per-instance placement and color | Pass | Pass | Exact |

Two full-frame diagnostics are intentionally not counted among the 30 applicable exact comparisons:

- `compressed_cube` explicitly documents that cross-renderer raw frames are inapplicable. Its
  reflection direction is sensitive to the independent pixel-center convention being tested
  elsewhere; its contract is the exact DXT1-versus-RGBA8 agreement within each renderer. Both legs
  pass that stronger storage/sampling oracle. The observed raw diagnostic was `max diff = 127`,
  `mean diff = 6.126`; widening the tolerance was rejected.
- `backbuffer_msaa` passes all four assertions under both renderers, and RLGL additionally passes
  the eight-check `4x -> 0 -> 4x` Reset sequence. The raw diagonals differ by a subpixel translation
  (`max diff = 191`, `mean diff = 2.950`). RLGL suppresses XNA's single-sample pixel-center
  translation whenever `GetCurrentSampleCount()` reports a multisampled destination. EasyGL's own
  source comment requires the same suppression, but its current test only recognizes multisampled
  render targets and misses its multisampled default backbuffer. Copying that reference-only
  deviation into RLGL would contradict the documented XNA behavior and was therefore rejected.

## Classic golden inventory

| Golden scene | EasyGL | RLGL |
|---|---:|---:|
| BasicEffect | Pass | Pass |
| SpriteBatch rotation/origin | Pass | Pass |
| Linear texture filtering | Pass | Pass |
| Additive BlendState | Pass | Pass |
| DepthStencilState write enable | Pass | Pass |
| RasterizerState CullMode | Pass | Pass |
| AlphaTestEffect | Pass | Pass |
| DualTextureEffect | Pass | Pass |
| EnvironmentMapEffect | Pass | Pass |
| SkinnedEffect | Pass | Pass |
| RenderTarget2D | Pass | Pass |

The PBR and SkinnedPBR EasyGL goldens are CNA extensions, not classic XNA 4.0, and are deliberately
outside this campaign. The generic EasyGL golden smoke wrapper is also not an additional scene.

## Gaps found and closed

The first RLGL run passed 28/32 central fixtures and 9/11 classic goldens. The campaign converted
each failure into a stable task:

| Task | Initial observation | Resolution |
|---|---|---|
| `RLGL-059` | Valid BasicEffect inputs without `Normal0`/`Color0` and DualTextureEffect input without `TextureCoordinate1` were rejected; the latter also broke two golden consumers | Effective stock gates now follow declared semantics; absent optional attributes use deterministic GL defaults while present wrong formats still fail |
| `RLGL-060` | A requested 4x default framebuffer reported samples but produced no intermediate coverage | Renderer-owned multisample backbuffer with explicit read/present resolve, target-switch preservation, Reset/resize/recovery rebuild |
| `RLGL-061` | The first complete registered run exposed two renderer-local tests that still asserted the pre-`RLGL-059` rejection behavior | Negative assertions became exact unlit/default-coordinate pixel checks |

After those tasks, the central fixture result is 32/32, the classic golden result is 11/11, and the
complete registered RLGL result is 134/134. No unresolved classic-XNA implementation gap was found
by this campaign. Performance, CI/platform documentation, and the independent final audit remain
`RLGL-020`, `RLGL-021`, and `RLGL-022` respectively.
