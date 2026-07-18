# Audit: include/CNA/Internal/Backends/D3DCommon/D3DConstantBuffers.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3DCommon/D3DConstantBuffers.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: C++ header, header-only (no `.cpp`), 263 lines
- XNA/FNA relevance: mirrors the constant-buffer layouts consumed by every stock XNA effect (BasicEffect,
  AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect) plus the NOXNA PbrEffect/SkinnedPbrEffect
- Graphics backend relevance: shared POD constant-buffer struct definitions for both D3D11 and D3D12
- FNA reference: N/A directly (a layout-matching header, not effect logic itself)
- Main related tests: none found exercising these structs directly; correctness is indirectly covered by whatever
  D3D11/D3D12 draw-path tests exist

## Purpose

Declares 10 `alignas(16)` POD structs (`D3DPerDrawConstants`, `D3DFogConstants`, `D3DLightingConstants`,
`D3DAlphaTestConstants`, `D3DBoneConstants`, `D3DSkinnedExtraConstants`, `D3DEnvMapPerDrawConstants`,
`D3DEnvMapConstants`, `D3DSprite2DConstants`, `D3DPbrPerDrawConstants`, `D3DPbrLightConstants`), each documented as
matching a specific HLSL `cbuffer` declaration byte-for-byte, with `static_assert`s enforcing size/offset/16-byte-
alignment for every one.

## Executive Verdict

**Mostly healthy — rigorous, verified layout definitions; two header comments are stale (documentation rot).**

## Checklist Results

### API / XNA / FNA parity
N/A directly — internal data-layout plumbing, not an XNA-facing API surface.

### Behavioral correctness / Logic
Every struct's field offsets were spot-checked against the corresponding HLSL `cbuffer` declaration read directly
in this audit (`D3DPerDrawConstants` vs `colored3d.vert.hlsl`'s `PerDraw`, `D3DSkinnedExtraConstants` vs
`skinned3d.vert.hlsl`'s `FogParams`, `D3DEnvMapConstants` vs `env_map3d.vert.hlsl`/`.frag.hlsl`'s `EnvMapParams`,
`D3DPbrPerDrawConstants`/`D3DPbrLightConstants` vs `pbr3d.vert.hlsl`'s `PerDraw`/`PbrLights`) — every offset and
total size matches exactly. `D3DSkinnedExtraConstants` was specifically checked for an `EmissiveColor` field given
this audit's cross-cutting finding that D3D11/D3D12's `SkinnedEffect` shaders lack one: **confirmed absent from
this struct too**, corroborating the shader-side finding at the C++ layout level (not just missing from the
shader's own read, but genuinely never allocated a byte in the buffer at all).

### Documentation currency (stale-comment findings)
**F1 (LOW-MEDIUM):** `D3DLightingConstants`'s doc comment (line 69-71) states "NOT YET WIRED into any draw call —
DX-60 defines this layout ahead of DX-63 (lit_textured3d pipeline wiring) landing it." **This is stale**: directly
confirmed via `grep` that `D3D11GraphicsBackend.cpp` (line 1655) and `D3D12GraphicsBackend.cpp` (line 2001) both
actively construct and use `D3DLightingConstants` today.
**F2 (LOW-MEDIUM):** `D3DBoneConstants`'s doc comment (line 132-133) makes the identical stale claim ("NOT YET
WIRED into any draw call — ahead of DX-67"), also directly contradicted: both backends' `.cpp` files construct
`D3DBoneConstants bones{}` at multiple call sites (`D3D11GraphicsBackend.cpp:1531,1577`;
`D3D12GraphicsBackend.cpp:1865,1926`).
This is a 5th-6th independent instance of this audit's already-tracked recurring pattern ("header comments
describing 'not yet done'/'known limitation' status are not revisited once the underlying code catches up") — see
`AUDIT_CROSS_CUTTING_FINDINGS.md`.

### C++ correctness
Every struct correctly uses `alignas(16)` and a `static_assert(... % 16 == 0)`, matching D3D11's hard requirement
that a constant buffer's `ByteWidth` be a 16-byte multiple. `static_assert(offsetof(...))` checks are exhaustive
for every non-trivial field, not just spot-checked — a genuinely strong verification discipline that would catch
a future accidental reordering at compile time.

### Memory/resource lifetime / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

**F1 (LOW-MEDIUM):** `D3DLightingConstants`'s "NOT YET WIRED" doc comment is stale — the struct is actively used by
both D3D11 and D3D12.
**F2 (LOW-MEDIUM):** `D3DBoneConstants`'s "NOT YET WIRED" doc comment is stale for the same reason.

## Cross-File Observations

`D3DSkinnedExtraConstants`'s lack of an `EmissiveColor` field independently corroborates, at the C++ constant-
buffer-layout level, the shader-side finding already recorded in
`src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.frag.hlsl.audit.md` and
`AUDIT_CROSS_CUTTING_FINDINGS.md` — this is not a case of the shader silently discarding a value the C++ layer
does send; the byte simply was never allocated for it anywhere in the pipeline.

## Missing or Weak Tests

No test found directly asserting these structs' `sizeof`/`offsetof` values independently of the `static_assert`s
(which only run at compile time, not as part of a test executable's own reported results) or their end-to-end
correctness via an actual draw call.

## Positive Findings

The `static_assert`-driven verification discipline (every struct's size and every non-trivial field's offset
checked against the real HLSL layout at compile time) is a strong, proactive defense against silent layout drift
— a notably more rigorous approach than a same-shaped bug (byte-layout mismatch) would require in most of this
project's other backends, which rely on manual review alone.

## Final Assessment

Two LOW-MEDIUM documentation-rot findings (stale "NOT YET WIRED" comments); the actual data-layout content is
rigorously verified and correct, including independent corroboration of the missing-EmissiveColor
cross-cutting finding.
