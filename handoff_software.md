# CNA Software Renderer Adversarial Parity Handoff

Updated: 2026-09-13, continued through SOFTWARE-198 classification

This document is the restart point for the hostile Software-versus-EasyGL classic XNA 4.0/Core
parity review. Do not interpret the large completed task count or green regression suites as proof
that the campaign is finished. The objective is to falsify the parity claim, repair every bounded
gap that can be proved, and keep any remaining gap explicit.

## Read before changing anything

Read these files completely, in this order:

1. `CLAUDE.md`
2. `AGENTS.md`
3. `plans/plan_software.md`
4. `docs/software-renderer.md`
5. `docs/software-easygl-parity-ledger.md`

Then inspect the relevant implementation and tests under:

- `modules/renderers/software/`
- `modules/renderers/easygl/`
- `modules/renderers/common/mojoshader/`
- `modules/graphics/`
- `tests/support/CNA/TestSupport/CompiledEffectFixtures.hpp`

Use the local FNA source at `/rv/data/library/github.com/FNA-XNA/FNA` as the second behavioral
authority after measured/recovered Microsoft XNA 4.0 evidence. EasyGL is the CNA reference renderer,
but this campaign has repeatedly proved shared EasyGL/MojoShader bugs, so do not treat it as an
infallible oracle.

## Repository snapshot

- Repository: `/rv/data/development/github.com/openeggbert/cnasoftware`
- Branch: `software`, tracking `origin/software`
- Adversarial challenge start: `92ed418b3dfcd4a2e03ea8c0d092a0b4caca9e9f`
- Last task commit before SOFTWARE-198: `6b3e27cae` —
  `fix(SOFTWARE-178): make EasyGL wireframe capability truthful`
- Campaign delta at that commit: 335 commits, 701 changed files, 67,390 insertions and 6,740
  deletions relative to the challenge start. Recompute rather than copying these numbers into
  a future final report because this handoff commit and subsequent work change them.
- `origin/software` was at `6861caf32` when this update was written. Before the SOFTWARE-198 task
  commit, the local branch was ten commits ahead. Do not push without a new explicit request in
  the active conversation.
- The only expected worktree entry after the handoff commit is the user's untracked `.junie/`.
  Preserve it. Do not stage it.

After any machine restart, establish the real state instead of trusting this snapshot:

```text
git status --short --branch
git log --oneline -12
git rev-parse HEAD
git rev-list --count 92ed418b3dfcd4a2e03ea8c0d092a0b4caca9e9f..HEAD
```

## Mission, authority and scope

The earlier claim was that Software had functional parity with EasyGL for classic XNA 4.0/Core.
The job is not to defend that claim. Try to disprove it through public behavior, unusual state
combinations, malformed inputs, lifecycle/error ordering, shader profile rules, and renderer
defaults that existing tests missed.

Behavioral authority is:

1. measured Microsoft XNA 4.0 behavior or recovered XNA implementation evidence;
2. FNA;
3. EasyGL;
4. current Software behavior;
5. CNA documentation and plans.

The scope includes classic XNA 4.0 graphics and renderer-neutral CNA Core behavior supported by
EasyGL. It excludes CNAEXT modern rendering, PBR, compute, storage buffers, indirect rendering,
modern HDR/post-processing/shadows/IBL, physical GPU handles, and headless-meaningless window or
swapchain behavior, unless one of those paths reveals a shared classic XNA/Core bug. Classic
compiled XNA Effects are in scope and are not the same API as CNAEXT `ShaderEffect`.

## Current verdict

The evidence-backed verdict remains:

**C — HIGH PARITY, SPECIFIC GAPS REMAIN.**

`SOFTWARE-164/165` are now complete. With `CNA_SOFTWARE_COMPILED_EFFECTS=ON`, Software advertises
`GraphicsCapability::CompiledEffects=true`, accepts the public compiled-Effect entry point and
passes the complete shared conformance surface plus the formerly skipped public Effect tests.
Opt-out builds retain the dependency-free false capability. The remaining plan boundaries are
`SOFTWARE-207`, the independently blocked repository-wide `SOFTWARE-100`, and the historical
optional/no-goal `SOFTWARE-85/86`; do not treat this handoff as
authority to stop before the active user's explicit completion goal is met.

The prompted sampler, AddressW, and multisample-rasterizer hypotheses were all confirmed and fixed;
the exact evidence and task mapping are in the `Final adversarial parity challenge` section of
`docs/software-easygl-parity-ledger.md`. The compiled-Effect audit proved that EasyGL receives and
executes classic Effect Framework data through MojoShader/FNA3D-compatible structures. It is a real
classic-XNA parity requirement, not a CNAEXT deferral. `SOFTWARE-178` removes EasyGL's partial
GLES/WebGL `GL_LINES` wireframe: native polygon mode is used where the active context exposes it,
and every triangle path is refused consistently where it does not.

## Latest completed work: SOFTWARE-198

The remaining graphics-state identity question is now recorded in `CHECKLIST.md` as an accepted
C++ mapping deviation. Public setters accept `const State&`, including legal stack objects and
temporaries, while getters return stable device- or collection-owned references. Returning the
caller's literal address would therefore create a dangling reference rather than CLR-style managed
identity.

`SOFTWARE-232/234/235/349/350` already preserve every portable observable: shared properties,
bind freeze, same-reference no-op, device rebinding, `Name`, `Tag`, disposal state and delivery,
canonical event sender, named defaults, sampler presets and source-wrapper lifetime. Literal
`ReferenceEquals` and removing one wrapper's event token through another would require a breaking
managed-handle API. The four focused identity tests pass on displayless Software and desktop
EasyGL.

## Earlier completed work: SOFTWARE-178

EasyGL now detects desktop `glPolygonMode`, native-GLES `GL_NV_polygon_mode` and WebGL's optional
`WEBGL_polygon_mode`. `GraphicsCapability::WireFrame` reports that runtime result. Contexts with no
native mode retain the requested state but reject triangle lists/strips at the central topology
boundary before ordinary, indexed, user, multi-stream, instanced, indirect, SpriteBatch or compiled
Effect submission; line and point draws remain legal.

Mesa GLES 3.2 exposes no polygon-mode extension and passes all nine new refusal contracts: five
stock/user/state cases and four compiled Effect routes. Mesa desktop OpenGL 4.5 retains native
support and passes the shared exact edge/interior, state-transition and polygon-line depth-bias
oracles. The WebGL glue is source-reviewed but was not browser-built because Emscripten is absent
from this environment.

## Earlier completed work: SOFTWARE-470 through SOFTWARE-484

`SOFTWARE-470` audited the Shader Model 2 `SINCOS` scratch operands.

Measured with the real Microsoft `d3dcompiler_47.dll` assembler:

- `ps_2_0 sincos r0.xy,c0.x,c1,c2` is accepted.
- Negating or nonidentity-swizzling either scratch constant (`c1` or `c2`) is rejected.
- Negating the scalar value source (`-c0.x`) is accepted.
- The scratch modifier/swizzle rejection also holds in `vs_2_0`.

Before repair, Software and EasyGL accepted all four invalid scratch variants. Managed MojoShader
patch 95 now requires source 1 and source 2 to be unmodified constants with identity swizzles. It
does not restrict source 0. Shared fixtures contain four negative cases and two positive controls.

Verification at `aa162d1ae`:

- Software compiled-Effect runtime: pass.
- Focused EasyGL `*SincosOperand*`: 5/5 pass.
- Complete Software CTest renderer label: 160/160 pass.
- Complete EasyGL compiled-Effect family: 539/539 pass on isolated Mesa/Xvfb.

The task changed the managed patch list, fixture generator, Software runtime test, EasyGL tests,
plan, renderer documentation and parity ledger in one task commit.

`SOFTWARE-471` immediately challenged the adjacent vertex `SGN` scratch contract. Microsoft accepts
a negated value source and destination aliasing either scratch temporary, but rejects modifier or
swizzle tokens on either scratch (X5472) and rejects value-source aliasing with either scratch
(X5471). Both CNA paths accepted all six invalid forms before managed patch 96. Six negative and
four positive controls now pass; Software remains 160/160 and the EasyGL compiled family is
546/546.

`SOFTWARE-472` challenged the matrix-family register-overlap rules left by SOFTWARE-408 and
SOFTWARE-465. Real Microsoft assembly accepts an `M4X3` destination equal to the explicit matrix
base, and accepts an `M3X2` vector source equal to the base or an additional implied row. It reports
X5570 when an `M4X3` destination aliases either additional matrix row implied by the base token.
Both CNA compiled paths accepted both invalid destination aliases before managed patch 97. The
patch rejects only `base+1..base+rows-1`, retaining all three Microsoft-positive overlaps. Two
negative and three positive shared fixtures now pass in both paths; Software remains 160/160 and
the EasyGL compiled family is 549/549.

`SOFTWARE-473` followed the remaining legacy `TEXM*PAD` sequence FIXME. Microsoft documentation
requires one source texture register and consecutive destination stages across the complete
`TEXM3X2` or `TEXM3X3` group. Real `d3dcompiler_47.dll` additionally rejects an extra pad and any
group left incomplete at shader end with X5056/X5057/X5058. Software and EasyGL both accepted the
four directly executed destination-gap, extra-2x-pad, changed-2x-source and second-3x-pad-gap
programs; source inspection also proved that the parser had no end-of-shader check for one or two
outstanding pads. Managed patch 98 now admits only one active family, checks shared sources and
consecutive destinations, rejects surplus pads, and rejects unfinished sequences. Eight negative
fixtures plus the existing valid sampled two-row and three-row controls pass in Software; focused
EasyGL is 9/9, the Software renderer label is 160/160, and the complete EasyGL compiled family is
558/558.

`SOFTWARE-474` then attacked legacy texture-address operand and destination modifiers adjacent to
that sequence validation. The real Microsoft assembler accepts plain and `_bx2` `TEXM` operands,
mixed signed/plain `TEXM` sequences, and plain/`_bx2` `TEXDP3`, while rejecting bias, negate,
negated-sign, swizzle, destination saturation and result-shift variants. It separately rejects all
modified `TEXBEM` operands and any modifier/swizzle on the `TEXM3X3SPEC` eye constant. Software and
EasyGL accepted 15 of 17 negative fixtures before repair, and SOFTWARE-385's old positive fixture
itself used the Microsoft-invalid `TEXBEM _bx2` form. Managed patch 99 now applies the exact
per-family allowlists, bans texture-address destination result modifiers/shifts and requires the
specular eye constant to remain plain. The bump fixture now uses legal operands with recalibrated
matrix/luminance inputs. Seventeen negative and three positive controls pass in Software; focused
EasyGL is 20/20, the Software renderer label is 160/160, and the complete EasyGL compiled family is
576/576.

`SOFTWARE-475` audited the separate generic pre-1.4 texture-instruction branch used by `TEX`,
`TEXCOORD`, `TEXKILL` and the source-bearing dependent operations. Real Microsoft
`d3dcompiler_47.dll` measurements across `ps_1_1`, `ps_1_2` and `ps_1_3` reject a partial texture
destination with X5041, saturation with X5042, a result shift with X5043, a repeated destination
stage with X5053 and descending destination stages with X5054. A lone `t1`, ascending consecutive
stages and an ascending gap remain legal. `TEXKILL` participates in the same ordering rule. Both
CNA paths already rejected the partial destination through SOFTWARE-419 but accepted saturation,
shift, duplicate and descending representatives. Managed patch 100 now rejects every result
modifier/shift and tracks one-use ascending texture stages across mixed instruction families.
Thirteen negative and three positive controls pass in Software and EasyGL. The audit also exposed
that SOFTWARE-418's four-slot ps_1_1 positive control repeated `TEXKILL t0` four times; that was
Microsoft-invalid, so it now uses legal stages `t0..t3`. The Software renderer label passes
160/160 and the complete isolated EasyGL compiled family passes 590/590.

`SOFTWARE-476` challenged the destination-register class left unchecked by that generic branch.
Real `d3dcompiler_47.dll` rejects `texcoord r0` under `ps_1_1`, `ps_1_2` and `ps_1_3` with X5040
while accepting `texcoord t0`; companion probes confirm the texture-register-only destination for
`TEX` and `TEXKILL`. Software and EasyGL accepted the malformed temporary destination before
managed patch 101 added the missing pre-1.4 `state_TEXCRD` guard. The shared negative fixture now
passes in both paths while Shader Model 1.4's distinct temporary-destination form remains legal.
The Software renderer label passes 160/160 and the complete isolated EasyGL compiled family passes
591/591.

`SOFTWARE-477` audited the unchecked result fields of Shader Model 1.4 `TEXDEPTH`. Microsoft accepts
plain full-mask `texdepth r5` after `r5.xy` initialization and `PHASE`, but rejects partial masks
with X5127, saturation with X5128, and result shifts with X5129. The shared generic mask validator
already rejected partial destinations; both Software and EasyGL nevertheless accepted saturation
and shift before managed patch 102 added the exact guards to `state_TEXDEPTH`. Three negative
fixtures cover the repaired modifier and shift plus the existing partial-mask control, and the
valid post-phase form prevents blanket rejection. The focused EasyGL pair passes 2/2, the complete
isolated EasyGL compiled family passes 591/591, and all 160 display-free Software CTests pass.

`SOFTWARE-478` then audited destination modifiers on every declaration register class. Real
`d3dcompiler_47.dll` accepts plain, `_pp`, `_centroid` and `_pp_centroid` declarations for
`ps_2_0` and `ps_3_0` interpolated inputs, but rejects saturation with X2000/X2024. It rejects all
modifiers on `ps_3_0 vPos` and sampler declarations with X5969 or the saturation syntax errors,
and rejects saturated `vs_3_0` input/output declarations with X2000/X2024. Software and EasyGL
accepted all eight forged invalid forms before managed patch 103, while both legal combined
controls passed. The shared validator now reserves `_pp`/`_centroid` for pixel interpolators and
requires zero modifiers for miscellaneous inputs, samplers and vertex declarations. Focused
EasyGL passes 10/10, the complete isolated compiled family passes 601/601, the display-free
Software runtime passes, and all 160 Software CTests pass.

`SOFTWARE-479` audited the unchecked modifier and selector fields of Shader Model 3 `IF` sources.
Real `d3dcompiler_47.dll` accepts plain/logical-NOT Boolean sources and plain/NOT predicate sources
using any replicate selector in both pixel and vertex profiles. It rejects arithmetic negate with
X5690/X5696 and vector predicate selectors with X5949/X5960. Software accepted all six forged
invalid forms; EasyGL accepted the two vector-predicate forms and rejected the other four only
during later GLSL translation. Managed patch 104 permits only `NONE`/`NOT` and requires predicate
replication. Six negative and six positive controls pass; focused EasyGL is 38/38, the complete
compiled family is 613/613, and Software remains 160/160.

`SOFTWARE-480` extended that audit to `CALLNZ` and `BREAKP`. Microsoft accepts plain/NOT replicate
predicates for both instructions and plain/NOT Boolean `CALLNZ` conditions, while rejecting
arithmetic negate and vector predicate selectors. The shared parser accepted eight of ten invalid
controls. EasyGL also formatted valid predicate `CALLNZ` conditions as Boolean vectors, so four
positive NOT/Y selector cases failed GLSL compilation. Managed patch 105 adds the missing condition
validation and scalar GLSL formatting. Ten negative and ten positive controls pass; focused EasyGL
is 58/58, the complete family is 633/633, and Software remains 160/160.

`SOFTWARE-481` audited all label-bearing source tokens. The real Microsoft assembler accepts only
plain label operands for `CALL`, `CALLNZ` and `LABEL` in `ps_3_0` and `vs_3_0`. It rejects negate
with X5690/X5696 and rejects `l0.x` with X2022 because scalar label registers cannot be swizzled.
Software and EasyGL both accepted all twelve forged stage/opcode/modifier combinations before
managed patch 106 required `SRCMOD_NONE` plus the identity selector in `check_label_register`.
Twelve negative controls and two stage-level positive programs now pass. Focused EasyGL is 38/38,
the complete isolated compiled family is 647/647, the display-free Software runtime passes, and all
160 Software CTests pass.

`SOFTWARE-482` extended the same plain-source audit to `LOOP` and `REP`. Microsoft emits identity
selectors for plain `aL`/`i#` controls and rejects arithmetic modifiers or explicit selectors.
Both compiled paths accepted all twelve forged stage/instruction variants before managed patch
107 required `SRCMOD_NONE` and identity selectors. Older synthetic loop controls were corrected
from replicate-X to the real identity encoding. Focused EasyGL is 72/72, the complete compiled
family is 661/661, the display-free Software runtime passes, and all 160 Software CTests pass.

`SOFTWARE-483` audited the instruction control byte on `SETP`, `IFC` and `BREAKC`. The D3D9 ABI
reserves values 0 and 7 and defines the six comparison relations at 1..6; Microsoft's assembler
rejects suffix-free forms and accepts comparison-suffixed controls. Software accepted all six
zero-control programs, while EasyGL rejected them only downstream. Managed patch 108 validates
the range in shared instruction state. Twelve negative and two all-six positive programs pass;
focused EasyGL is 86/86, the complete family is 675/675, and Software remains 160/160.

`SOFTWARE-484` completed the adjacent control-byte audit for ordinary opcodes. D3D9 assigns
opcode-specific controls only to the comparison instructions and Shader Model 2+ `TEXLD` forms;
Software and EasyGL accepted control value 1 on forged `NOP` and `MOV` instructions in both
Shader Model 3 stages. Managed patch 109 rejects nonzero controls unless that opcode/profile owns
the field, leaving the exact comparison and texture ranges to their existing validators. Four
negative probes pass with the retained legal controls. Focused EasyGL is 101/101, the complete
compiled family is 679/679, the display-free Software runtime passes, and all 160 Software CTests
pass.

## Recent compiled-Effect validation commits

These commits form one evidence chain. Preserve their distinctions when debugging regressions:

- `338fe752c` — SOFTWARE-463, scalar temporary-component reads.
- `452264f64` — SOFTWARE-464, dot/normalization/DP2ADD fixed-vector reads.
- `54b3d61fa` — SOFTWARE-465, matrix vector widths and implicit matrix rows.
- `bce833e79` — SOFTWARE-466, LIT/DST/CRS special-vector reads.
- `5a7955bbb` — SOFTWARE-467, Pixel Shader 1.x CND component reads.
- `2471a8b91` — SOFTWARE-468, restore Microsoft-valid `ps_2_x` uninitialized reads.
- `1c49cd93e` — SOFTWARE-469, exact `ps_2_0 TEXLD` coordinate components by sampler dimension
  and projective/bias control.
- `aa162d1ae` — SOFTWARE-470, Shader Model 2 SINCOS scratch operand identity.
- `2ba060087` — SOFTWARE-471, complete vertex SGN scratch modifier/swizzle and alias rules.
- `f59140696` — SOFTWARE-472, reject matrix destination aliases with implied source rows while
  retaining the Microsoft-valid base and vector overlaps.
- `9c7413c3d` — SOFTWARE-473, enforce legacy texture-matrix source, destination, pad-count and
  completion sequencing.
- `c9b7884ce` — SOFTWARE-474, enforce legacy texture-address source, swizzle, result-modifier and
  `TEXM3X3SPEC` eye-constant rules.
- `3417e6477` — SOFTWARE-475, enforce pre-1.4 texture result modifiers and one-use ascending
  destination-stage ordering across the generic texture family.
- `01d2b156c` — SOFTWARE-476, require texture-register destinations for pre-1.4 `TEXCOORD`.
- `0f878352a` — SOFTWARE-477, reject Shader Model 1.4 `TEXDEPTH` result modifiers and shifts.
- `c56bb9274` — SOFTWARE-478, enforce declaration modifiers by D3D9 register class.
- `1f08c30eb` — SOFTWARE-479, validate `IF` source modifiers and predicate selectors.
- `f63424552` — SOFTWARE-480, validate `CALLNZ`/`BREAKP` conditions and emit scalar GLSL
  `CALLNZ` expressions.
- `63be571e6` — SOFTWARE-481, require plain identity label sources for `CALL`, `CALLNZ` and
  `LABEL`.
- `7f36a0ad4` — SOFTWARE-482, require plain identity `LOOP` and `REP` control sources.
- `ed7b15c43` — SOFTWARE-483, validate comparison instruction control values 1 through 6.
- `1c8a231f7` — SOFTWARE-484, reject reserved controls on every other opcode/profile.

The lesson from SOFTWARE-468 is important: never generalize an exact `ps_2_0` assembler result to
`ps_2_x`, `ps_3_0`, or a vertex profile without measuring it. The Microsoft profile boundaries can
be counterintuitive.

## Open plan rows

The following are the only non-complete rows in `plans/plan_software.md` at this handoff:

| Task | Meaning and next treatment |
|---|---|
| `SOFTWARE-207` | Exact managed-reference identity for `VertexBuffer.VertexDeclaration`; likewise narrowed to a public ownership-model issue after practical resource identity was fixed. |
| `SOFTWARE-100` | Independently blocked repository-wide acceptance row. Its exact unrelated test and GDI/sharp-runtime blockers are documented in the plan; do not broaden the renderer campaign to hide them. |
| `SOFTWARE-85` | Historical optional CPU-framebuffer window blit. Not required for the headless Software parity target. |
| `SOFTWARE-86` | Historical optional performance work. Explicitly not a goal without a real impractical test. |

All rows through `SOFTWARE-206`, the completed later ranges stated in the plan, and
`SOFTWARE-315..484` are closed except for the rows above. Assign the next demonstrated issue as
`SOFTWARE-485`; never add a task merely to keep numbering moving.

## Recommended next audit direction

The adversarial opcode/profile review under SOFTWARE-164/165 is complete. For any future compiled
Effect regression, retain the established pattern:

1. inspect one MojoShader `state_*` validator, opcode-table restriction, FIXME, or Software
   interpreter branch;
2. construct paired Microsoft assembly programs that differ in exactly one operand/profile rule;
3. add a forged shared CNA fixture with negative and positive controls;
4. prove both Software and EasyGL red before changing production code;
5. make the narrowest shared or renderer-local repair;
6. run targeted and broad regressions;
7. update plan and ledger in the same task commit;
8. keep searching after the repair.

Continue checking opcode-specific operand forms and profile boundaries rather than only unsupported
opcodes. The recently closed liveness series demonstrates that a parser can execute an opcode and
still accept invalid classic bytecode.

One investigated lead should not be reopened mechanically: MojoShader's internal cube `TEXLD`
`instruction_count` adjustment looks suspicious, but real Microsoft assembly accepted tested 8/9
texture-instruction cube and 2D programs regardless of sampler number, while the separate Pixel
Shader 2 texture-slot budget counts each load once. No CNA public behavior was found. Do not change
that counter without a reproducible public XNA/Core impact.

Other high-value SOFTWARE-165 closure areas remain authentic EffectMaterial/model-content draws,
resource lifetime and clone/application failure ordering, malformed bytecode fuzzing with bounded
execution, and a final shared contract that can justify changing the capability flag. Do not enable
the flag incrementally.

## Relevant code map

- Managed MojoShader patch registration: `cmake/ThirdPartyFNA3D.cmake`
- Managed patches: `cmake/patches/mojoshader-6333f74-*.patch`
- Shared Effect translation: `modules/renderers/common/mojoshader/`
- Software Effect/parser and machines:
  `modules/renderers/software/src/SoftwareCompiledEffect.cpp`,
  `SoftwareShaderInterpreter.cpp`, and `SoftwarePixelShaderInterpreter.cpp`
- Software behavioral executable:
  `modules/renderers/software/examples/software_compiled_effect_runtime_test.cpp`
- EasyGL compiled-Effect tests:
  `modules/renderers/easygl/tests/CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffectTests.cpp`
- Synthetic bytecode/effect generator:
  `tests/support/CNA/TestSupport/CompiledEffectFixtures.hpp`
- Durable audit evidence: `plans/plan_software.md` and
  `docs/software-easygl-parity-ledger.md`

The patch series contains 109 ordered patches at this handoff. New patches must be appended in
dependency order and must apply to pinned MojoShader commit
`6333f74dbd5644789a63e903816441b16c1e8b60` through FNA3D pin `3240147`.

## Active build configurations

Keep these build directories unless a concrete configuration change requires replacement. They
are expensive, active, and were intentionally retained to avoid SSD write amplification:

- `cmake-build-software`: Debug, `CNA_GRAPHICS_RENDERER=SOFTWARE`,
  `CNA_SOFTWARE_COMPILED_EFFECTS=ON`.
- `cmake-build-easyglfx`: Debug, `CNA_GRAPHICS_RENDERER=OPENGL33`,
  `CNA_EASYGL_COMPILED_EFFECTS=ON`.

Both currently use:

- `CNA_SHARP_RUNTIME_ROOT=/rv/data/development/github.com/openeggbert/sharp-runtimenext`
- `FETCHCONTENT_SOURCE_DIR_FNA3D=/tmp/cna-fna3d-soft474.XKYa5y/FNA3D`

Build with `CCACHE_DISABLE=1` and at most `-j2`. Do not run a full content rebuild or full relink on
every commit when the changed inputs do not require it. A MojoShader patch legitimately recompiles
and relinks many consumers; ordinary test/document changes usually do not.

Typical commands from the repository root:

```text
env CCACHE_DISABLE=1 cmake -S . -B cmake-build-software
env CCACHE_DISABLE=1 cmake --build cmake-build-software --target cna_test_software_compiled_effect_runtime -j2
env SDL_VIDEODRIVER=dummy DISPLAY= ./cmake-build-software/cna_test_software_compiled_effect_runtime
env SDL_VIDEODRIVER=dummy DISPLAY= ctest --test-dir cmake-build-software --output-on-failure -L Software -j2

env CCACHE_DISABLE=1 cmake -S . -B cmake-build-easyglfx
env CCACHE_DISABLE=1 cmake --build cmake-build-easyglfx --target CnaRendererTests -j2
env DISPLAY=localhost:299 LIBGL_ALWAYS_SOFTWARE=1 SDL_VIDEODRIVER=x11 ./cmake-build-easyglfx/CnaRendererTests --gtest_filter='*EasyGLCompiledEffect*'
```

The EasyGL filter must contain the leading wildcard exactly as shown. Without it, parameterized
test-suite names can be omitted.

## Display safety

Never run renderer tests on the user's physical display. EasyGL tests in this campaign used only
the isolated Xvfb display `localhost:299` with Mesa software rendering and SDL's X11 driver:

```text
DISPLAY=localhost:299 LIBGL_ALWAYS_SOFTWARE=1 SDL_VIDEODRIVER=x11
```

At the handoff, `:299` was active. It may be gone after reboot. Verify or recreate an isolated
virtual display before any EasyGL run; do not fall back to `:0`, an inherited desktop `DISPLAY`, or
a visible window. Software tests should use `SDL_VIDEODRIVER=dummy DISPLAY=` unless a specifically
documented window-lifecycle supervisor requires isolated Xvfb.

## Shared FNA3D/MojoShader checkout caveat

The active local dependency checkout is `/tmp/cna-fna3d-soft474.XKYa5y/FNA3D`. It was created for
SOFTWARE-474 after another concurrent session repeatedly rewrote the former shared checkout and
left its source/stamp inconsistent. Both active build trees now point to this isolated checkout;
keep it while continuing with those builds. It is pinned to FNA3D `32401479a3ab5bd6b2e7f786e87bf4166aa03b0f`
and MojoShader `6333f74dbd5644789a63e903816441b16c1e8b60`, with all 109 managed patches applied.

Its FNA3D Git object alternate points to `/tmp/fx126-current/fna3d-src/.git/objects`, and its
MojoShader object alternate points to
`/rv/data/development/github.com/openeggbert/cnanext/build/_deps/fna3d-src/.git/modules/MojoShader/objects`.
Recheck both after reboot because `/tmp` is not durable.

The idempotence stamp is:

```text
/tmp/cna-fna3d-soft474.XKYa5y/FNA3D/MojoShader/.cna-mojoshader-patch-series.sha256
```

Occasionally the shared source became pristine while that stamp remained, causing configure to
skip required patches. Before trusting a configure, inspect a marker from the applied series, for
example:

```text
rg -n 'pixel1_texture_stages|TEXM3X3SPEC final arg must be unmodified|TEXDEPTH destination must not use result modifiers|pixel input DCL only permits|label source must not have|MOJOSHADER_RS_BUMPENVMAT00' /tmp/cna-fna3d-soft474.XKYa5y/FNA3D/MojoShader/mojoshader.c /tmp/cna-fna3d-soft474.XKYa5y/FNA3D/MojoShader/mojoshader.h
```

If source and stamp disagree, remove only that stamp through the repository's approved editing
mechanism and configure/build Software first, then configure/build EasyGL. Do not configure the two
build trees concurrently against this shared mutable source. Once both binaries are built, their
tests may run concurrently.

If the `/tmp` checkout no longer exists, recreate or select a valid pinned FNA3D checkout and update
the build configuration; do not rely on a dangling Git alternate and do not delete the active build
trees merely to make the error disappear.

## Microsoft assembler oracle

The local offline oracle uses the real `d3dcompiler_47.dll` under Wine:

- Wine prefix: `/home/robertvokac/.wine-cna-d3d9-spike`
- Compiler: `x86_64-w64-mingw32-g++`

Example build and execution:

```text
x86_64-w64-mingw32-g++ -O2 -static-libgcc -static-libstdc++ /tmp/cna_d3dassemble_special_probe.cpp -o /tmp/cna_d3dassemble_special_probe.exe -ld3dcompiler_47
env WINEPREFIX=/home/robertvokac/.wine-cna-d3d9-spike WINEDEBUG=-all wine /tmp/cna_d3dassemble_special_probe.exe
```

Useful temporary probe sources at handoff included:

- `/tmp/cna_d3dassemble_special_probe.cpp`
- `/tmp/cna_d3dassemble_texture_components_probe.cpp`
- `/tmp/cna_d3dassemble_components_probe.cpp`
- `/tmp/cna_d3dassemble_cnd_probe.cpp`
- `/tmp/cna_d3dassemble_phase_probe.cpp`
- `/tmp/cna_d3dassemble_texkill_probe.cpp`
- `/tmp/cna_d3dassemble_texld_probe.cpp`
- `/tmp/cna_d3dassemble_call_probe.cpp`
- `/tmp/cna_d3dassemble_tex11_probe.cpp`
- `/tmp/cna_d3dassemble_texcoord_probe.cpp`
- `/tmp/cna_d3dassemble_texdepth_probe.cpp`
- `/tmp/cna_d3dassemble_dcl_modifier_probe.cpp`
- `/tmp/cna_d3dassemble_predicate_flow_probe.cpp`

These files are not committed and may disappear after reboot. Preserve every material measurement
in the task row and ledger rather than treating `/tmp` output as durable evidence. Always include a
nearby Microsoft-positive control so an apparent rejection is not merely bad assembly syntax.

## Test and skip state

The last broad results relevant to the latest technical commit are:

- Software renderer CTest label: 160/160 pass, display-free.
- EasyGL compiled-Effect family: 679/679 pass on isolated Mesa/Xvfb.
- Focused SOFTWARE-470 EasyGL cases: 5/5 pass; focused SOFTWARE-471 cases: 7/7 pass;
  focused SOFTWARE-472 cases: 3/3 pass; focused SOFTWARE-473 cases: 9/9 pass; focused
  SOFTWARE-474 cases: 20/20 pass; focused SOFTWARE-475 plus the adjacent slot regression:
  22/22 pass; focused SOFTWARE-477 phase-state pair: 2/2 pass; focused SOFTWARE-478
  declaration-modifier matrix: 10/10 pass; focused SOFTWARE-479 flow family: 38/38 pass;
  focused SOFTWARE-480 flow family: 58/58 pass; focused SOFTWARE-481 call-graph/label family:
  38/38 pass; focused SOFTWARE-482 flow family: 72/72 pass; focused SOFTWARE-483 flow family:
  86/86 pass; focused SOFTWARE-484 flow family: 101/101 pass.
- The post-SOFTWARE-165 full Software `CnaGraphicsTests` run is 2,640/2,688 with 48 classified
  skips. All 15 formerly skipped compiled `Effect`/`EffectMaterial` tests are active and pass.
- The aggregate Software shared compiled-Effect conformance test and the independent display-free
  parser/interpreter/runtime executable pass. Focused EffectMaterial/model XNB coverage is 15/15.

The exact former 63-skip classification is in
`docs/software-easygl-parity-ledger.md` under `Exact Software CnaGraphicsTests skip classification`:

- The 15 classic compiled-Effect gaps owned by SOFTWARE-162..165 are now active passing tests.
- The remaining 48 are exact negative controls, foreign-renderer selectors, platform-only cases, or CNAEXT
  facilities.
- No classified skip identifies a second untracked Software stock-effect/state/resource gap.

Do not copy old total test counts into a new task report. Re-run the relevant suite and record its
actual count, because parameterized fixtures frequently increase the EasyGL total.

## Commit discipline and stopping conditions

- One demonstrated task equals one commit.
- Update `plans/plan_software.md` and `docs/software-easygl-parity-ledger.md` continuously with the
  implementation and tests they describe.
- Stage only explicit task files. Never use `git add .` or `git add -A`.
- Preserve unrelated work and the untracked `.junie/` directory.
- Run `git diff --check`, the focused test, and the relevant Software/EasyGL regressions before
  committing.
- Do not push unless the owner explicitly asks in the active conversation.
- Keep `CompiledEffects` conditional on `CNA_SOFTWARE_COMPILED_EFFECTS`; opt-out and reduced GDI
  builds must continue to report false.
- If a real gap is too large for a bounded implementation, document the architecture and remaining
  scope honestly instead of faking parity.

The eventual final report must choose exactly one of the owner's A/B/C/D verdict categories and
include start/end SHAs, branch, commit and LOC counts, new/completed/remaining tasks, prompted and
new findings, compiled-Effect verdict, exact skips, all test families run, and the reason for the
classification. At this handoff the correct classification is C.

The owner explicitly resumed the campaign and requested completion of the whole plan. Continue
through the remaining plan rows; do not reinstate the obsolete SOFTWARE-484 stop condition. Do not
push without a new explicit request for the current branch state.
