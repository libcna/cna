# Audit: include/CNA/Graphics/StorageBuffer.hpp

## Metadata

- Source file: `include/CNA/Graphics/StorageBuffer.hpp`
- Physical location: `modules/graphics-ext/include/CNA/Graphics/StorageBuffer.hpp`
- Audit status: COMPLETE
- Subsystem: `cna-graphics` shard — the `CNA::Graphics` engine layer
- File type: C++ header
- XNA/FNA relevance: N/A — `CNA::Graphics`, not `Microsoft::Xna`. The whole layer is behind the
  `CNA_CNAEXT` CMake option (default OFF) and every file in it is `#ifdef CNA_CNAEXT`-guarded,
  which `scripts/check_cnaext_guards.sh` enforces.
- Graphics renderer relevance: none directly — the engine layer talks to `GraphicsDevice` and the renderer contracts, never to a renderer implementation
- Plan rows: `MOD-1520`, `MOD-2229`

## Purpose

A tracked renderer-neutral byte buffer, immutable usage/CPU-access descriptor and typed view.

## Executive Verdict

Complete for `MOD-2229`. The public surface exposes no native buffer, mapping, memory property,
barrier or renderer identity. The original constructor retains its prior operations while the new
descriptor makes every additional role immutable and explicit.

## Checklist Results

- SPDX marker and CNAEXT guard are present.
- Every public declaration and enum member has a Doxygen block or permitted one-line block.
- The descriptor rejects zero sizes, empty/unknown usage and unknown CPU-access bits.
- `StorageBuffer` is a non-copyable/non-movable `GraphicsResource`; disposal releases renderer work
  before removing the public resource from device tracking.
- Full-prefix and exact-range transfers, copy, descriptor/size access, type identity and the typed
  wrapper have permanent tests.

## Detailed Findings

The renderer seam carries fixed-width raw masks to avoid coupling the graphics-core contract to the
engine-extension module. Values are pinned by shared and native tests. Existing indirect and
compute routes additionally validate the relevant declared usage before native work.

## Cross-File Observations

The fixed-width masks are intentionally mirrored at the renderer seam and pinned by tests; the
public enum types stay in `graphics-ext`, so ordinary builds do not acquire a reverse dependency.
`ComputeShader` and `GraphicsDevice` consume only the roles relevant to their own operations.

## Missing or Weak Tests

No known public-method coverage gap. `StorageBufferTests.cpp` covers descriptor fields/flags,
invalid masks, both constructors, all getters, range operations, copy validation, renderer refusal,
tracking/events/disposal, type name and both typed-view constructors. The Vulkan live oracle covers
the real device-local and CPU-visible paths on RADV and llvmpipe.

## Positive Findings

Range validation uses `offset > capacity || size > capacity - offset`; no unchecked addition is
used to decide bounds. A GPU-only allocation cannot accidentally become CPU-readable or writable.

## Final Assessment

Complete for the current public contract. Deferred command retention and removal of Vulkan's
transitional synchronous submissions remain explicitly owned by `MOD-2247`–`MOD-2253`, not by this
facade.
