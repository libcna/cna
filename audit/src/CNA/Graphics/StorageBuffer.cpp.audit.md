# Audit: src/CNA/Graphics/StorageBuffer.cpp

## Metadata

- Source file: `src/CNA/Graphics/StorageBuffer.cpp`
- Physical location: `modules/graphics-ext/src/StorageBuffer.cpp`
- Audit status: COMPLETE
- Subsystem: `cna-graphics` shard — the `CNA::Graphics` engine layer
- File type: C++ source
- XNA/FNA relevance: N/A — `CNA::Graphics`, not `Microsoft::Xna`. The whole layer is behind the
  `CNA_CNAEXT` CMake option (default OFF) and every file in it is `#ifdef CNA_CNAEXT`-guarded,
  which `scripts/check_cnaext_guards.sh` enforces.
- Graphics renderer relevance: none directly — the engine layer talks to `GraphicsDevice` and the renderer contracts, never to a renderer implementation
- Plan rows: `MOD-1520`, `MOD-2229`

## Purpose

Validation, renderer allocation, exact transfers, copying and tracked disposal for StorageBuffer.

## Executive Verdict

Complete for `MOD-2229`. Intrinsic descriptor errors, disposed/cross-device resources, missing
usage/CPU access, live size limits and invalid ranges are rejected before renderer mutation.

## Checklist Results

- Failed construction allocates the renderer record before `GraphicsResource` registration, so no
  half-registered resource escapes.
- The compatible path deliberately uses the legacy factory and default mask; descriptor creation
  uses the separate false-by-default factory.
- CPU-none upload/readback refuses before dereferencing a mapping.
- GPU copy validates both usage halves, devices, ranges and same-buffer overlap.
- Explicit and device disposal are idempotent and release renderer work first.

## Detailed Findings

`ComputeShader` checks the storage role and `GraphicsDevice` checks the indirect-argument role. The
Vulkan backend maps every usage bit exactly and performs GPU-only transfers with `vkCmdCopyBuffer`.

## Cross-File Observations

Construction reads the already-cached renderer limit through `GraphicsDevice` and never queries a
native API directly. The legacy factory remains separate so existing EasyGL callers keep their
source and behavior while unsupported descriptor combinations fail through the new null default.

## Missing or Weak Tests

No known gap for the row. Shared validation/tracking tests compile as their own object; the expanded
15-case native oracle passed on both physical RADV and llvmpipe with zero validation messages.

## Positive Findings

The prefix overloads delegate to the range overloads, keeping one validation path. Both zero-length
and maximum-size offsets are handled without arithmetic wraparound.

## Final Assessment

Complete for `MOD-2229`; automatic deferred ordering remains separately tracked in the later
Vulkan synchronization rows.
