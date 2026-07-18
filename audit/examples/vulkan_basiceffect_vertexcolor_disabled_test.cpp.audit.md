# Audit: examples/vulkan_basiceffect_vertexcolor_disabled_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_vertexcolor_disabled_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `BasicEffect` `VertexColorEnabled=false` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered integration test (Task 364)
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled`/`DiffuseColor` default behavior.
- FNA reference: `HLSL/Common.fxh`'s `ComputeCommonVSOutput()` (`vout.Diffuse = DiffuseColor`, no
  vertex-color multiply unless the `Vc` shader variant is selected) and `BasicEffect.cs` (both
  `vertexColorEnabled` and `textureEnabled` private fields default to C#'s implicit `false`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`/`.hpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/colored3d.vert.glsl`,
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`GetOrCreatePipelineFogColored3D()` line 4797, dispatch at line 6480-6483).

## Purpose

Two-check pixel test proving that with `BasicEffect`'s real FNA defaults (`LightingEnabled=false`,
`TextureEnabled=false`, `VertexColorEnabled=false`), the shader outputs `DiffuseColor*Alpha` only and
**ignores** any per-vertex colour attribute in the vertex buffer. Deliberately draws
`VertexPositionColor` vertices carrying a bright-red per-vertex colour
(`kVertexRed(255,0,0,255)`) while `DiffuseColor` is set to a distinctive teal `(0.2,0.6,0.9)`, so a
regression that accidentally multiplies in vertex colour (or worse, uses it exclusively) is
distinguishable from the correct DiffuseColor-only result.

## Executive Verdict

**Healthy** — the shader dispatch, gating logic, and expected constant were all independently
confirmed correct against the current Vulkan pipeline code and FNA's shader semantics.

## Checklist Results

### API / XNA / FNA parity
Confirmed `BasicEffect`'s real defaults directly in `BasicEffect.hpp`: `VertexColorEnabled = false`
(line 48), `textureEnabled_ = false` (line 369), `lightingEnabled_ = false` (line 367),
`diffuseColor_ = Vector3{1,1,1}` (line 361) — all match FNA's `BasicEffect.cs` (`bool
lightingEnabled/textureEnabled/vertexColorEnabled` implicit-`false` C# fields, `diffuseColor =
Vector3.One`). The file correctly leaves these three flags untouched (comment lines 92-93) rather than
redundantly re-asserting the default, which is the right way to test "is the *actual* default correct",
not "does explicitly setting false work".

### Behavioral correctness
Traced the dispatch: `VertexPositionColor` (stride 16), no texture, no lighting → BasicEffect's draw
sets `useFogTex3D=true` (`VulkanGraphicsBackend.cpp:7414`, `!needsAlphaTest && !needsDualTex &&
!needsEnvMap && !needsSkinned && !needsPbr && !needsLitTextured`), and stride==16 routes to
`GetOrCreatePipelineFogColored3D()` → `colored3d.vert.glsl`. That shader's actual body: `fragColor =
(pc.vertexColorEnabled > 0.5) ? inColor * pc.diffuseColor : pc.diffuseColor;` — when
`vertexColorEnabled` is false (as here), the per-vertex `inColor` (the bright red) is provably never
read into `fragColor` at all, not merely overwritten later. `kExpected(51,153,230,255)` is `(0.2, 0.6,
0.9) * 1.0 * 255` rounded — `0.2×255=51`, `0.6×255=153`, `0.9×255=229.5→230` (round-half-up) — matches
exactly.

### Logic
`matchesDiffuse()` (tolerance ±10) and `looksRed()` (`R>=200 && G<=60 && B<=60`) are two independent,
non-overlapping predicates: a shader bug that multiplied vertex colour in even partially (e.g.
half-strength) would fail `matchesDiffuse` without necessarily tripping `looksRed`, and a shader bug
that used vertex colour *exclusively* would trip `looksRed` directly — good coverage of two distinct
failure modes with two separate assertions (lines 121-123), not just one composite check.

### C++ correctness
`RasterizerState::CullNone` is explicitly set (line 113) per the file's own comment referencing "Task
896 finding" (the real default `RasterizerState.CullMode=CullCounterClockwiseFace` culling the
standard NDC quad winding used in this pixel-test family). Independently confirmed:
`GraphicsDevice::setRasterizerStateProperty()` (`GraphicsDevice.cpp:1715-1725`) forwards the cull mode
unmodified to the backend, and prior to the Task 896 fix (`b6a00bc6` in `git log`) the ctor set the
C++-level default field but never pushed it to the backend, so this workaround requirement is real
history, not a fabricated justification.

### Testing
Both checks are genuine discriminating assertions (see Logic above), not "just doesn't crash" checks.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM/LOW findings recorded for this specific file — see Cross-File
Observations for a related, class-wide API-consistency issue that surfaces more directly in the sibling
`vulkan_basiceffect_vertexcolor_enabled_test.cpp` (which actually *writes* to `BasicEffect.VertexColorEnabled`).

## Cross-File Observations

- `BasicEffect.VertexColorEnabled` (`BasicEffect.hpp:48`) is a bare public field with **no**
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` wrapper at all — unlike every
  other property on the same class (`DiffuseColor`, `TextureEnabled`, `LightingEnabled` all have
  get/set wrapper methods) and unlike `World`/`View`/`Projection` (also public fields on this class,
  but each *additionally* has a matching `getWorldProperty()`/`setWorldProperty()` wrapper,
  `BasicEffect.hpp:40-42` + accessor methods below). This file itself doesn't touch
  `VertexColorEnabled` (it deliberately leaves the default alone), so the inconsistency doesn't surface
  here — see `vulkan_basiceffect_vertexcolor_enabled_test.cpp`'s audit report for the full finding,
  which that sibling file's `fx.VertexColorEnabled = true;` (direct field write) exercises directly.
- Confirmed via `git blame`/`git log -p` that this field has been a bare field (never a property)
  across every revision of `BasicEffect.hpp`, including the commit that fixed its wrong default value
  — this is a longstanding gap, not a recent regression.

## Missing or Weak Tests

None — this file's own two checks are sufficient for what it claims to test.

## Positive Findings

- Choosing a bright, maximally-distinguishable "wrong" colour (`255,0,0`) for the ignored per-vertex
  attribute, combined with a non-primary-colour `DiffuseColor`, makes both the positive
  (`matchesDiffuse`) and negative (`!looksRed`) assertions meaningfully strong rather than coincidental.
- Explicitly *not* re-setting the flags under test to their own defaults is the correct testing
  discipline for a "verify the real default" test.

## Final Assessment

A solid, correctly-targeted default-behavior test with no defects of its own. The one substantive issue
uncovered while auditing this file — `BasicEffect.VertexColorEnabled`'s inconsistent field-vs-property
API surface — belongs to the production `BasicEffect.hpp` header, not to this test, and is reported in
full against the sibling file that actually exercises the write path.
