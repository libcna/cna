# Audit: examples/easygl_animsprite_shader_test.cpp

## Metadata

- Source file: `examples/easygl_animsprite_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — custom `ShaderEffect` HLSL→GLSL port proof
  (`AnimSprite.fx`, ShipGame sample content)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_animsprite_shader …)` /
  `cna_register_backend_test(NAME EasyGL_AnimSprite_Shader …)`,
  `cmake/Tests/EasyGLTests.cmake:479-481`).
- XNA/FNA relevance: indirect — not a stock `Microsoft::Xna` effect, but a 1:1 GLSL port of a
  third-party XNA sample's custom `.fx` shader (`ShipGame_4_0/ShipGame/Content/shaders/
  AnimSprite.fx`), exercised through CNA's `NOXNA` `ShaderEffect`/`.cnj` content pipeline.
- Related production code: `include/.../Graphics/ShaderEffect.hpp` (`SetUniformMat4`/
  `SetUniformVec2`/`SetUniformVec4`/`SetUniformInt`/`SetTexture`/`IsEffectValid` — all confirmed
  present at lines 43-93), `src/Microsoft/Xna/Framework/Content/ContentManager.cpp`
  (`ReadCustomGlslEffect()`, lines 765-786 — the `.cnj` `"type":"Effect"` dispatch this test's
  fixture exercises).

## Purpose

Proves a specific HLSL→GLSL shader port (`AnimSprite.fx`'s frame-cross-fade formula) is
byte-for-byte correct by writing a temporary `.vert.glsl`/`.frag.glsl`/`.cnj` fixture to disk at
runtime, loading it through the real `ContentManager`/`ShaderEffect` pipeline (not a hand-rolled GL
program), and checking 3 distinct `FrameBlend` values against independently-derived expected pixel
colors.

## Executive Verdict

**Healthy.** The GLSL fragment shader embedded in this test (lines 117-137) is a line-for-line
translation of the FNA/ShipGame HLSL source quoted in the header comment (lines 12-28) — verified
term-by-term — and all 3 expected pixel values were independently re-derived from that GLSL and
match the test's assertions exactly, including the halfway-blend case (`Check C`) that specifically
proves the `.w *= FrameBlend.y` alpha-scale term is live (the file documents an actual
mutation-test verification of this in its own header comment, lines 58-62).

## Checklist Results

### API / XNA / FNA parity
N/A in the strict sense (this is a `NOXNA` custom-shader sample port, not a `Microsoft::Xna` stock
effect) — but the HLSL-to-GLSL translation is checked term-by-term below since fidelity to the FNA
sample source is the entire point of the file.

**Vertex shader** (lines 105-115) vs. FNA `AnimSpriteVS` (header comment lines 12-18):
`gl_Position=ViewProj*vec4(aPosition,1.0)` vs. HLSL `OutPosition=mul(InPosition,ViewProj)` — same
operation (row-vector-on-left HLSL `mul` is equivalent to column-vector-on-left GLSL `M*v` for a
matrix uploaded consistently via `ToColumnMajor`, confirmed used at line 213); `vTexCoord=aTexCoord`
matches `OutTexCoord=InTexCoord` exactly. No separate `World` matrix, matching the header comment's
own note (lines 30-34) that this shader takes an already-combined `ViewProj` only, mirroring
`ShatterEffect.fx`'s established pattern in this codebase.

**Fragment shader** (lines 125-136) vs. FNA `AnimSpritePS` (header comment lines 19-28):
- `tx1 = FrameSize*(FrameOffset.xy+TexCoord)` / `tx2 = FrameSize*(FrameOffset.zw+TexCoord)` — exact
  match, including which offset field (`xy` vs `zw`) feeds which sample.
- `color1=texture(TextureSampler,tx1)` / `color2=texture(TextureSampler,tx2)` matches `tex2D(...)`.
- `blendColor=mix(color1,color2,FrameBlend.x)` matches `lerp(color1,color2,FrameBlend.x)` exactly
  (GLSL `mix` and HLSL `lerp` have identical semantics/argument order).
- `blendColor.w *= FrameBlend.y` matches `blend_color.w *= FrameBlend.y` verbatim.
- No other transformation — a faithful, minimal 1:1 port with no logic added or dropped.

### Behavioral correctness
Independently re-derived all 3 checks from the GLSL above:
- **Check A** (`FrameBlend=(0,1)`): `mix(color1,color2,0)=color1`. With the fixture's 2-texel
  texture (`texel0=(200,100,50,255)`, `texel1=(50,150,200,255)`, lines 185-188) and
  `FrameOffset=(0,0,1.0,0)`, `FrameSize=(0.5,1.0)`, sampling at the quad-center `TexCoord=(0.5,0.5)`:
  `tx1=(0.5,1.0)*((0,0)+(0.5,0.5))=(0.25,0.5)` — exactly texel0's own center (texel0 spans
  `u∈[0,0.5]`, center `u=0.25`), so `color1=texel0=(200,100,50,255)` with no interpolation
  ambiguity. `.w*=FrameBlend.y=1` is a no-op. Matches expected `~(200,100,50,255)` (line 250-251)
  exactly.
- **Check B** (`FrameBlend=(1,1)`): `mix(...,1)=color2`; `tx2=(0.5,1.0)*((1.0,0)+(0.5,0.5))=
  (0.75,0.5)` — exactly texel1's own center — `color2=texel1=(50,150,200,255)`. Matches expected
  `~(50,150,200,255)` (line 252-253) exactly.
- **Check C** (`FrameBlend=(0.5,0.5)`): `mix(color1,color2,0.5)` = channel-wise average:
  `R=(200+50)/2=125`, `G=(100+150)/2=125`, `B=(50+200)/2=125`, `A=(255+255)/2=255`, then
  `.w*=0.5→127.5≈128` (byte rounding). Matches expected `~(125,125,125,128)` (line 254-255) exactly
  — this is the case that specifically discriminates a genuine `lerp`+alpha-scale from either
  "just returns one frame" or "ignores the alpha-scale term", and the file's header comment (lines
  58-62) documents an actual mutation test (temporarily removing the `.w *=` line, confirming Check
  C alone then fails outside tolerance, then reverting) — a rigorous, executed verification of the
  test's own discriminating power, not merely an assertion.

`close()`'s `±6` per-channel tolerance (line 249) is tight enough that none of the three expected
triples (200/100/50, 50/150/200, 125/125/125) can be confused with one another or with a "wrong
frame only" / "no alpha scale" failure mode.

### Logic
Quad-center-samples-at-UV-(0.5,0.5) relies on: (a) the quad being symmetric around the origin with
symmetric corner UVs (`(0,1)`,`(0,0)`,`(1,0)`,`(1,1)`, lines 172-177 — bilinear average of the 4
corners is exactly `(0.5,0.5)`), and (b) the camera (`eye=(0,0,3)`, `target=Zero`,
`up=(0,1,0)`, symmetric `PiOver4` FOV, line 203-206) being perfectly centered so the quad's
geometric center projects to the viewport's pixel center — both hold for this fixture, verified by
inspection of the actual `Matrix::CreateLookAt`/`CreatePerspectiveFieldOfView` call arguments.

`DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2)` (line 224) passes `2` as the
final primitive-count argument for a 2-triangle quad (6 indices) — correct (this project has a
known prior defect class of passing vertex/index count instead of primitive count in other test
files; this file does not repeat it).

### Memory/resource lifetime
Writes 3 temporary files under `std::filesystem::temp_directory_path() / "cna_animsprite_test_
<this-pointer>"` (lines 156-167) — the per-instance-pointer suffix avoids collision with concurrent
test runs of the same binary, but the directory and its 3 files are never cleaned up in a
destructor or at the end of `Draw()` — a small, harmless leak of temp-directory content across test
runs (not memory, not GPU resources), consistent with this being a short-lived CTest process where
OS temp-directory accumulation is a minor, low-priority housekeeping concern rather than a
correctness issue.

### Robustness
`Draw()` checks `fx || !fx->IsEffectValid()` (line 238) and fails cleanly with a `[FAIL]` message
plus `Exit()` rather than dereferencing a null/invalid effect — good defensive handling of the
"shader failed to compile" case, which is a real possible outcome for a GLSL string embedded
directly in a C++ source file with no external validation.

### Testing
Three genuinely distinct, independently-derived pixel checks plus a documented mutation-test
verification of the test's own discriminating power — this is one of the more rigorous test files
in this shard; clearly exceeds "compiles and doesn't crash."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temporary test-fixture directory never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource hygiene
- Location/symbol: `Initialize()` (lines 151-189), `root` temp directory (line 156-158)
- Evidence: `std::filesystem::create_directories(root)` followed by 3 `WriteFile()` calls, with no
  matching `remove_all(root)` anywhere in the file (checked `Draw()`, destructor — there is no
  explicit destructor, and the default one does not touch `root`).
- Why it matters: purely a housekeeping nit for repeated local CTest runs (each run creates a new
  uniquely-named directory under the OS temp path, accumulating over time on a dev machine that
  never clears `/tmp`); not a functional defect and not a leak of any resource this project's
  `CLAUDE.md` lifetime rules are concerned with (no GPU/native handles involved).
- FNA/XNA comparison: N/A.
- Suggested future action (not implemented by this audit): if this file is next touched, consider
  an RAII temp-directory helper or an explicit cleanup at the end of `Draw()`/in a destructor.

## Cross-File Observations

- Confirmed `ContentManager.cpp::ReadCustomGlslEffect()` (lines 765-786) is the real, current
  dispatch path for a `.cnj` with `"type":"Effect"` and `vertex`/`fragment` fields — this test's
  fixture format is not a stale/aspirational schema, it is the schema the production loader
  actually expects today.
- This is one of several "HLSL sample shader → GLSL port proof" tests in this codebase (the header
  comment references `easygl_shadowmapping_drawwithshadowmap_shader_test.cpp`'s established
  texel-center-sampling convention, line 44) — the pattern of writing a temp `.cnj` fixture at
  runtime rather than checking in a permanent content asset appears consistently applied across
  this family, which is a reasonable, self-contained way to test the content pipeline without
  adding permanent binary/test-asset files.

## Missing or Weak Tests

None specific to this file — the 3-point sweep with a verified mutation test already gives strong
confidence in the specific formula under test.

## Positive Findings

- The documented mutation test (header comment lines 58-62) — deliberately breaking the shader,
  confirming the expected check fails, then reverting — is genuinely strong verification practice,
  rarely seen this explicitly in a test file, and this audit did not need to redo that work since
  it was already performed and recorded.
- Picking `FrameOffset`/`FrameSize` values that land exactly on texel centers (avoiding bilinear
  interpolation ambiguity) without needing a `PointClamp` sampler override shows careful test
  design consistent with this session's established convention (as the header comment itself notes).

## Final Assessment

A rigorous, well-verified HLSL→GLSL port proof; the shader translation was checked term-by-term
against the quoted FNA/ShipGame source and found faithful, and all 3 expected pixel values are
exact re-derivations that match. Only a minor, non-functional temp-file cleanup nit (F1) was found.
