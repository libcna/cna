# Metal custom shader-effect boundary

## Current status

Custom effects are **not supported** by the adapted Metal backend. This is an intentional runtime
contract, not a missing capability declaration:

- `SupportsCapability(CNA::GraphicsCapability::CustomEffects)` returns `false`;
- `MetalGraphicsBackend::CreateEffectBackend` throws `System::NotSupportedException`;
- `MetalSpriteBatch::SetCustomEffect` throws for every non-null effect;
- ordinary 3D draws reject a non-null `GpuDrawParams::customEffectBackend` before submission;
- the historical `Metal_SpriteBatch_CustomEffect` diagnostic is not registered in the supported
  native test set.

Callers must query the capability and use a supported built-in effect path. They must not depend on
the dormant `MetalEffectBackend` implementation in `MetalGraphicsBackend.mm`; it is unreachable
implementation scaffolding, not a public contract.

## Why it is disabled

Historical `feature/metal` implemented a SpriteBatch-scoped MSL path and compiled it successfully
on the `macos-14` runner. The final production run, GitHub Actions `29814126178` at
`e0f42426836ce9f2d4823d50732850877020aef1`, could not prove its pixels: the custom-effect test's
readback returned the frame's clear color instead of draw output. The same symptom affected
unrelated 2D and 3D tests, so the evidence did not isolate the custom shader as the cause, but it
also did not establish correct custom-effect execution.

The current backend has since been adapted to new graphics interfaces and has no post-adaptation
Apple compile or runtime result. Advertising custom effects until that evidence exists would make
an unverified historical path part of the supported API.

## Historical design, retained only for future revalidation

The dormant code targeted a deliberately narrow SpriteBatch ABI:

- vertex and fragment sources were separate runtime-compiled MSL libraries;
- each source was expected to contain one stage function;
- sprite vertices used buffer index 0, the automatic logical-viewport transform used index 1,
  and fixed user-uniform slots used indices 2 through 4;
- the sprite texture and sampler used texture/sampler index 0;
- render-pipeline creation baked the current blend state and BGRA8 color format.

Those details describe the historical implementation only. They are not guaranteed for callers and
may change during a future reimplementation.

## Requirements before enabling

Enabling `CustomEffects` requires one coherent change that supplies all of the following:

1. a successful build of the then-current Objective-C++ backend and runtime MSL compilation on the
   supported macOS toolchain;
2. Metal validation with no API, resource-binding, or shader diagnostics;
3. a native test proving stock SpriteBatch output and custom-effect output without relying on the
   known-broken historical backbuffer readback path;
4. tests for uniform slots, texture/sampler binding, blend-state changes, render-target use, and
   effect/resource lifetime;
5. updated capability, native test registration, workflow, and this contract in the same change.

Until those conditions are met, deterministic rejection is the complete Metal custom-effect
contract.
