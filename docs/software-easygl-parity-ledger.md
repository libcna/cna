# Software / EasyGL parity evidence map

This page routes the evidence from the completed `SOFTWARE-*` campaign to current contracts,
fixtures and historical records. The maintained rendering contract is
[`software-renderer.md`](software-renderer.md); current defects and qualification gaps are in
[`handoff.md`](../handoff.md) §5. Software remains an **Experimental** renderer identity.

The authority order is measured Microsoft XNA 4.0/IL, the local FNA reference, then CNA's
implementations and their prose. EasyGL supplies a comparison implementation. A matching picture,
method name or passing unsupported-boundary test alone does not establish Microsoft parity.

## Historical record

The ledger began on 2026-09-07 at `cde325ecd4d771d50b7d8ec3ef99f1657ad217ac` on the
`software` branch. Its task-by-task tables and raw contemporary conclusions are preserved in Git:

```bash
git show 9b51bbab4:docs/software-easygl-parity-ledger.md
git log --oneline --grep='SOFTWARE-457'
```

That snapshot retains every original row, test total, staged failure and implementation
disposition, including the property-level state audit and exact skip lists. It is historical
evidence, not the current backlog. The deleted `plan_software.md` and individual fixes are also
in Git history; [the task index](task-id-index.md) routes the completed campaign.

The 2026-09-13 reconciliation recorded verdict **B — parity with documented language/platform
boundaries**, 160 Software-labelled CTests, a compiled-effect-enabled Software graphics run with
2,640 passes/48 skips out of 2,688, and an EasyGL compiled family of 679 passes. These are the
snapshot's dated results, not today's registration counts or universal release criteria. Its
then-open unrelated native/Sharp Runtime findings must be checked against the current handoff.

## Reading the old classifications

| Historical label | Meaning and current use |
|---|---|
| `IN-SCOPE-XNA` | A public XNA contract included in the campaign. |
| `IN-SCOPE-CORE` | Shared CNA plumbing needed to make that contract observable. |
| `DEFERRED-CNAEXT` | Outside that campaign's denominator; this does not mean the feature is absent today. |
| `NOT-APPLICABLE-SOFTWARE` | A physical window/GPU/driver operation without a CPU-renderer counterpart; related shared state can still require tests. |
| `ALREADY-PARITY` / `PARITY` | The named witnesses supported the campaign verdict at that point. |
| `REAL-GAP` / repaired finding | A contemporaneous defect and its recorded disposition; consult the handoff before treating it as still open. |

Scope and verdict were separate columns. Later corrections superseded some early green rows,
including invalid shader operands and programs whose old executables had not been relinked.
The archive preserves those changes rather than turning every intermediate conclusion into a
current promise.

## Current evidence routes

The native Software fixtures live under
[`modules/renderers/software/examples/`](../modules/renderers/software/examples/). Shared sources
are under [`modules/graphics/examples/parity/`](../modules/graphics/examples/parity/), with
registrations in each renderer's CMake files. Individual tests may be capability/profile guarded;
inspect the configured inventory and actual outcomes.

| Family | Evidence and boundary |
|---|---|
| CPU construction and capability/factory consistency | `Software_Smoke`, `Software_Rasterizer`, `Software_Effects` and `Software_CapabilityContract` exercise real CPU pixels and the no-window renderer contract. Common platform adapter enumeration is separate from renderer window ownership. |
| Primitive input, clipping and raster coverage | Shared topology, indexed/user/buffer, multi-stream, instancing, top-left, viewport/scissor, culling, depth-bias and wireframe fixtures. Bare C++ pointers carry no managed allocation length; invalid native GPU fetches do not have a defined pixel oracle. |
| MSAA, depth/stencil and blending | Shared coverage/resolve/mask, independent depth/stencil-sample, blend-equation and per-target write-mask fixtures. Device/profile/format and rasterizer-toggle boundaries remain explicit. |
| Sampling and texture transfer | Point/linear/anisotropic, LOD/max-mip, U/V/W, 2D/cube/volume, format-preservation and partial-transfer fixtures. Storage, sampling and a reported capability are distinct checks. |
| Targets and queries | RenderTarget2D/Cube, MRT, mip/readback/lifetime and query state/sample fixtures. Software query counts describe its surviving CPU samples; exact physical-GPU results need their own measurement. |
| Stock effects, sprites, fonts and models | Shared Basic/AlphaTest/DualTexture/EnvironmentMap/Skinned effect, SpriteBatch/Font and Model.Draw fixtures. Tests use named exact values or measured tolerances, not a generic assertion of all-image equivalence. |
| Shared resource/state lifetime | GraphicsResource event order, device disposal/reset, binding retirement, immutable-state aliases and SpriteBatch failure recovery regressions live with the public API tests. A renderer campaign does not close every current cross-module defect. |
| Compiled XNA effects | The optional CPU parser/interpreters and shared conformance fixture below. An ordinary opt-out Software run cannot qualify this path. |

For the exact public state contracts and invalid-enum application rules, use the maintained
[sampler](sampler-state-support.md), [rasterizer](rasterizerstate-support.md) and
[depth/stencil](depthstencilstate-support.md) guides, plus the public
[BlendState contract](../modules/graphics/include/Microsoft/Xna/Framework/Graphics/BlendState.hpp)
and its tests. The old
state tables are not a second specification or a reason to reimplement completed members.

## Compiled-effect closure

`SOFTWARE-161` was the original gap assessment, followed by parser/reflection/vertex work and
`SOFTWARE-164/165`'s enabled execution/conformance closure. With
`CNA_SOFTWARE_COMPILED_EFFECTS=ON`, Software advertises `CompiledEffects` and executes legacy
Effect Framework bytecode through its CPU runtime. Opt-out builds omit that runtime and report
false. The current input/profile limits belong to
[`fx-compiled-effects.md`](fx-compiled-effects.md).

Implementation and reusable evidence:

- [`SoftwareCompiledEffect.cpp`](../modules/renderers/software/src/SoftwareCompiledEffect.cpp)
  connects parsed effect state, parameters and draw execution.
- [`SoftwareShaderInterpreter.cpp`](../modules/renderers/software/src/SoftwareShaderInterpreter.cpp)
  and [`SoftwarePixelShaderInterpreter.cpp`](../modules/renderers/software/src/SoftwarePixelShaderInterpreter.cpp)
  implement the CPU shader stages; derivative-dependent pixel work uses quad execution.
- [`CompiledEffectConformance.hpp`](../tests/support/CNA/TestSupport/CompiledEffectConformance.hpp)
  and [`CompiledEffectFixtures.hpp`](../tests/support/CNA/TestSupport/CompiledEffectFixtures.hpp)
  contain the shared accepted/rejected programs and assertions.
- [`software_compiled_effect_runtime_test.cpp`](../modules/renderers/software/examples/software_compiled_effect_runtime_test.cpp)
  is the optional Software integration executable. Its CMake registration is conditional.

The `SOFTWARE-457` correction is a durable warning about the old operands: Microsoft's validator
rejects the earlier arbitrary ps_1_4 projective fixtures. The retained accepted controls are
`TEXCRD _dw.xyw`, `TEXLD _dz.xyz` and `TEXLD _dw.xyw`, paired with rejection cases. Successful
execution of the old invalid programs is not parity evidence. Later `FX-*` validation decisions
and the shared conformance source supersede earlier row-level assumptions.

`ShaderEffect` source/package handling has a separate contract. Software reports no source shader
dialect and does not execute a legacy retained source string. Construction of a compatibility
resource is not execution support. See
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md). Modern PBR/glTF and other
CNAEXT families were excluded from this original campaign; their present support belongs to
their current renderer/subsystem pages, not the historical deferral label.

## Boundaries that remain explicit

Physical GPU contexts, native swap timing and driver extension behaviour are outside a CPU
renderer comparison. Software can still expose shared presentation properties, source-rectangle
presentation, reset/resource events and an optional platform surface presenter; those have their
own positive/refusal contracts. Native C++ iteration/lifetime, pointer-array length and raw
resource identity substitutions remain subject to [CHECKLIST.md](../CHECKLIST.md).

The One/Zero shortcut was also present in Software: copying FNA3D's factor-only decision
suppressed the ReverseSubtract equation that genuine XNA evaluates. CNA-FIX-D24 corrects
EasyGL, Software and CNA's FNA3D adapter, with the independent colour/alpha pixel witness and
[measured decision](xna-fna-behavior-decisions.md). The remaining multi-profile MSAA toggle is
in the handoff. Agreement with EasyGL is not a replacement for a genuine-XNA discriminator;
a skip or N/A boundary is not a successful positive render.

## Reproduction and current verification

Use an existing configuration and rebuild its fixture executables with their shared libraries.
For the default Software tree:

```bash
cmake --build cmake-build-software --parallel 4
tools/platform/run_gpu_tests_private.sh cmake-build-software \
    -L Software --output-on-failure -j4
```

The private runner also keeps platform/window probes away from the owner's desktop. List the
current cases with `ctest --test-dir cmake-build-software -N -L Software`; an old row's fixed count
does not define the current inventory. Use a separately configured existing opt-in tree to test
compiled effects, following the renderer guide, and record its build options and actual skips.

Reconciled on 2026-11-01 against the archived ledger, current renderer/capability implementation,
shared fixtures, current state/renderer guides and configured CTests. The archive's full bytes
remain recoverable from the commit above; no historical rows were recreated as new runtime work.

For this edit all 195 executable targets behind the 196-case Software label were rebuilt in the
existing **Release** tree with `CNA_SOFTWARE_COMPILED_EFFECTS=OFF`. The private run completed
**194 passes, two skips, zero failures**, with no graphics-profile deaths. The skipped source-
ShaderEffect refusal/layout cases do not certify source execution support. No enabled compiled-
effect, fresh EasyGL differential, genuine-XNA, sanitizer or physical-device campaign was run.
