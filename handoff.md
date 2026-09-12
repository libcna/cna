# CNA Software Renderer Adversarial Parity Handoff

Updated: 2026-09-12, continued through SOFTWARE-473

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
- Last technical commit before this handoff update: `9c7413c3d` —
  `fix(SOFTWARE-473): validate legacy TEXM sequences`
- Campaign delta at that commit: 314 commits, 687 changed files, 64,164 insertions and 6,592
  deletions relative to the challenge start. Recompute rather than copying these numbers into
  a future final report because this handoff commit and subsequent work change them.
- `origin/software` was still at `d3a38f0f7` when this update was written. Before this handoff
  commit, the local branch was four commits ahead: `2ba060087` (SOFTWARE-471), `f59140696`
  (SOFTWARE-472), `19f7cabe2` (the preceding handoff snapshot), and `9c7413c3d` (SOFTWARE-473).
  The owner explicitly requested pushes, but the execution environment rejected the attempted
  `git push origin software` in remote-safety review; no workaround was attempted. Re-establish
  the live branch/remote state and obtain whatever explicit approval the current environment
  requires before pushing these commits.
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

Do not upgrade that verdict. Software deliberately still reports
`GraphicsCapability::CompiledEffects=false`, and the public compiled-Effect entry point rejects
otherwise valid bytes. The opt-in Software executor now implements a large fraction of the actual
D3D9 Effect surface, but complete profile, malformed-input, lifecycle, content/model and stress
closure has not been proved. `SOFTWARE-164` and `SOFTWARE-165` remain open for that reason.

The prompted sampler, AddressW, and multisample-rasterizer hypotheses were all confirmed and fixed;
the exact evidence and task mapping are in the `Final adversarial parity challenge` section of
`docs/software-easygl-parity-ledger.md`. The compiled-Effect audit proved that EasyGL receives and
executes classic Effect Framework data through MojoShader/FNA3D-compatible structures. It is a real
classic-XNA parity requirement, not a CNAEXT deferral.

## Latest completed work: SOFTWARE-470 through SOFTWARE-473

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

The lesson from SOFTWARE-468 is important: never generalize an exact `ps_2_0` assembler result to
`ps_2_x`, `ps_3_0`, or a vertex profile without measuring it. The Microsoft profile boundaries can
be counterintuitive.

## Open plan rows

The following are the only non-complete rows in `plans/plan_software.md` at this handoff:

| Task | Meaning and next treatment |
|---|---|
| `SOFTWARE-164` | Primary active backlog: finish compiled pixel/profile behavior required by the enabled EasyGL classic Effect contract. Many independently useful phases are already complete; continue by proof, not by assuming the umbrella is nearly done. |
| `SOFTWARE-165` | Final compiled-Effect conformance/capability gate: remaining SM1/2/3 rules, malformed input, lifecycle, EffectMaterial/model content, stress/fuzz and performance bounds. Enable `CompiledEffects` only after this row's full acceptance criteria are genuinely satisfied. |
| `SOFTWARE-178` | Classic EasyGL GLES/WebGL wireframe completion. This is an EasyGL ES/WebGL limitation rather than a silently missing Software feature, but it remains a classic behavior row and must not be called complete from desktop GL evidence. |
| `SOFTWARE-198` | Exact managed-reference identity for graphics-state wrappers. Practical resource identity is already shared; literal C++ wrapper address/event-token identity would require a public ownership-model change. Do not attempt a renderer-only fake. |
| `SOFTWARE-207` | Exact managed-reference identity for `VertexBuffer.VertexDeclaration`; likewise narrowed to a public ownership-model issue after practical resource identity was fixed. |
| `SOFTWARE-100` | Independently blocked repository-wide acceptance row. Its exact unrelated test and GDI/sharp-runtime blockers are documented in the plan; do not broaden the renderer campaign to hide them. |
| `SOFTWARE-85` | Historical optional CPU-framebuffer window blit. Not required for the headless Software parity target. |
| `SOFTWARE-86` | Historical optional performance work. Explicitly not a goal without a real impractical test. |

All rows through `SOFTWARE-163`, the completed later ranges stated in the plan, and
`SOFTWARE-315..473` are closed except for the rows above. Assign the next demonstrated issue as
`SOFTWARE-474`; never add a task merely to keep numbering moving.

## Recommended next audit direction

Resume the adversarial opcode/profile review under SOFTWARE-164/165. A productive pattern has been:

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

The patch series contains 98 ordered patches at this handoff. New patches must be appended in
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
- `FETCHCONTENT_SOURCE_DIR_FNA3D=/tmp/cna-fna3d-audit.gFfGPT`

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

The active local dependency checkout is `/tmp/cna-fna3d-audit.gFfGPT`. Its FNA3D Git object
alternate was previously broken because it pointed at a deleted build checkout; it was repaired to
`/tmp/fx126-current/fna3d-src/.git/objects`. Its MojoShader submodule alternate currently points to
`/rv/data/development/github.com/openeggbert/cnanext/build/_deps/fna3d-src/.git/modules/MojoShader/objects`.
Both facts must be rechecked after reboot because `/tmp` is not durable.

The idempotence stamp is:

```text
/tmp/cna-fna3d-audit.gFfGPT/MojoShader/.cna-mojoshader-patch-series.sha256
```

Occasionally the shared source became pristine while that stamp remained, causing configure to
skip required patches. Before trusting a configure, inspect a marker from the applied series, for
example:

```text
rg -n 'TEXLD reads uninitialized|SINCOS src%d must have no modifiers|MOJOSHADER_RS_BUMPENVMAT00' /tmp/cna-fna3d-audit.gFfGPT/MojoShader/mojoshader.c /tmp/cna-fna3d-audit.gFfGPT/MojoShader/mojoshader.h
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

These files are not committed and may disappear after reboot. Preserve every material measurement
in the task row and ledger rather than treating `/tmp` output as durable evidence. Always include a
nearby Microsoft-positive control so an apparent rejection is not merely bad assembly syntax.

## Test and skip state

The last broad results relevant to the latest technical commit are:

- Software renderer CTest label: 160/160 pass, display-free.
- EasyGL compiled-Effect family: 558/558 pass on isolated Mesa/Xvfb.
- Focused SOFTWARE-470 EasyGL cases: 5/5 pass; focused SOFTWARE-471 cases: 7/7 pass;
  focused SOFTWARE-472 cases: 3/3 pass; focused SOFTWARE-473 cases: 9/9 pass.
- The last full Software `CnaGraphicsTests` checkpoint documented in the ledger is
  2,625/2,688 with 63 classified skips. It was not rerun for SOFTWARE-470/471/472/473 because those
  tasks changed only compiled-Effect test/validation inputs covered by the focused runtime, full
  Software label, and complete EasyGL compiled family.

The exact 63-skip classification is in
`docs/software-easygl-parity-ledger.md` under `Exact Software CnaGraphicsTests skip classification`:

- 15 skips are real classic compiled-Effect gaps, all owned by SOFTWARE-162..165.
- 48 are exact negative controls, foreign-renderer selectors, platform-only cases, or CNAEXT
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
- Do not enable `CompiledEffects` or declare full parity because one more audit slice is green.
- If a real gap is too large for a bounded implementation, document the architecture and remaining
  scope honestly instead of faking parity.

The eventual final report must choose exactly one of the owner's A/B/C/D verdict categories and
include start/end SHAs, branch, commit and LOC counts, new/completed/remaining tasks, prompted and
new findings, compiled-Effect verdict, exact skips, all test families run, and the reason for the
classification. At this handoff the correct classification is C.

The owner requested that work stop after SOFTWARE-473 and this handoff were committed. Do not open
SOFTWARE-474 until the owner explicitly resumes the campaign in the active conversation.
