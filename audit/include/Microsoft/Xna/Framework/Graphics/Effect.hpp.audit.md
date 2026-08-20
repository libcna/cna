# Audit: include/Microsoft/Xna/Framework/Graphics/Effect.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/Effect.hpp`
- Audit status: AUDITED (full read, 202 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Effect/Effect.cs`
- Main related tests: not independently located in this pass

## Purpose
Base class for an effect containing shader programs and render-state parameters
(`CurrentTechnique`, `Parameters`, `Techniques`, `Clone()`).

## Executive Verdict
Correct, and a deliberately, radically different internal implementation from FNA's own
`Effect.cs` for good reason: FNA's real `Effect` is a thin C# wrapper around FNA3D/MojoShader's
native compiled-bytecode effect runtime (`FNA3D_CreateEffect`, `MOJOSHADER_effect*` parsing,
runtime render-state application from parsed `.fx` bytecode); CNA has no MojoShader-equivalent
bytecode compiler, so its `Effect` base is instead a thin wrapper around this project's own
pluggable `IGraphicsBackend`/`GpuDrawParams` abstraction, with concrete stock effects
(`BasicEffect`, etc.) populating `GpuDrawParams` directly in C++ rather than through a
runtime-interpreted shader-parameter blob. The public XNA-facing surface (`CurrentTechnique`,
`Parameters`, `Techniques`, `Clone()`) is preserved; only the internal mechanism differs.

## Checklist Results
- The compiled-bytecode constructor (`Effect(GraphicsDevice&, const std::vector<bytecs>&)`)
  correctly and explicitly throws `System::NotImplementedException` with a clear, actionable
  message (naming the tracked follow-up, `docs/fx-bytecode-support-plans/plan.md` Phase 74) rather than
  silently no-op'ing or crashing — a well-handled, honestly-disclosed real gap (compiled `.fx`
  bytecode support genuinely doesn't exist yet), not a silent one.
- `Clone()` is correctly pure virtual (`= 0`), forcing every concrete subclass to implement it —
  matches FNA's own `virtual Effect Clone()` being overridden by every stock effect.
- `GetTypeName()` correctly overridden with `NOXNA`, returning `"Microsoft.Xna.Framework.Graphics.Effect"`.
- Copy constructor/assignment correctly deleted (`Effect(const Effect&) = delete`) — appropriate
  given the class owns backend/GPU resources through `GraphicsResource`.

## Detailed Findings
None.

## Cross-File Observations
`Apply()` is `NOXNA` (FNA has no public `Effect.Apply()`; only `EffectPass.Apply()`, which
internally calls `Effect.OnApply()`) — a disclosed, deliberate API-surface widening for this
project's simplified single-pass-per-technique model, not a silent divergence.
`FillGpuDrawParams()`/`GetVertexSource()`/`GetFragmentSource()`/`GetEffectBackendPtr()` are all
`NOXNA` extensions with correct, safe default (no-op/empty-string/nullptr) base implementations,
overridden by concrete effects (`FillGpuDrawParams`) or `ShaderEffect` (`GetVertexSource`/
`GetFragmentSource`/`GetEffectBackendPtr`) as appropriate.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The compiled-bytecode-unsupported constructor's exception message is a model example of how to
disclose a real, current limitation: it explains what's missing, why, where it's tracked, and what
to use instead — rather than silently degrading or crashing uninformatively.

## Final Assessment
No findings.
