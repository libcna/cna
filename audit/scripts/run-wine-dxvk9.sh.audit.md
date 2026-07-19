# Audit: scripts/run-wine-dxvk9.sh

## Metadata
- Source file: `scripts/run-wine-dxvk9.sh` (82 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (Wine/DXVK test-execution wrapper for the D3D9 backend)
- XNA/FNA relevance: N/A (developer/CI tooling)
- Main related tests: wraps every D3D9 CTest binary invocation (`cmake/Tests/D3D9Tests.cmake`) except the offline shader compiler

## Purpose
D3D9 counterpart to `run-wine-dxvk.sh`, sharing the same DXVK-engagement-gate design but for the
D3D9 backend, deliberately implemented as its own independent file rather than a shared edit.

## Executive Verdict
Correct, and notably precise about a real, easy-to-miss scope boundary: the file's own comment
explicitly warns NOT to route the offline HLSL shader compiler (`fxc_tool.exe`) through this
script, since compiling a shader never opens a D3D9 device (so the DXVK marker would never appear,
causing a false gate failure for an unrelated reason), and separately notes `fxc_tool.exe` needs a
different Wine prefix entirely (`~/.wine-cna-d3d9-spike`, the one with the real Microsoft
`d3dcompiler_47.dll`) — this distinction is also correctly reflected in
`cmake/Tests/D3D9Tests.cmake`'s own `CNA_D3D9_WINEPREFIX` override for `D3D9_ConstantTable`/
`D3D9_EffectBackend`/`D3D9_SpriteBatch_CustomEffect` (already audited in `build-cmake-tests`).

## Checklist Results
- Explicitly documents the deliberate naming distinction between `CNA_D3D9_WINEPREFIX` and D3D11's
  own `CNA_D3D11_WINEPREFIX` (same physical default prefix, `~/.wine-cna-d3d11`, but intentionally
  separate env-var names) — so D3D9 test infrastructure never silently inherits a D3D11-specific
  contract by accident.
- Same `PIPESTATUS[0]` exit-code-capture correctness and same two-tier escape-hatch design
  (`CNA_D3D9_ALLOW_WINED3D`/`CNA_D3D9_SKIP_DXVK_GATE`) as its D3D11 sibling.

## Detailed Findings
None.

## Cross-File Observations
Directly cross-verified consistent with `cmake/Tests/D3D9Tests.cmake`'s own `WINEPREFIX` override
comments (both files independently arrive at, and correctly document, the same real constraint:
the default DXVK-carrying prefix's Wine-builtin `d3dcompiler_47.dll` cannot compile SM2/SM3
shaders, only the dedicated spike prefix's real Microsoft compiler can).

## Missing or Weak Tests
N/A (this IS the test-execution mechanism, not itself under test).

## Positive Findings
The explicit "do NOT route the shader compiler through this script" warning, with the precise
reason why, prevents a plausible, easy-to-make integration mistake before it happens.

## Final Assessment
No findings.
