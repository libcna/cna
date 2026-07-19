# Audit: scripts/verify_hlsl_shaders_native_msvc.py

## Metadata
- Source file: `scripts/verify_hlsl_shaders_native_msvc.py` (91 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Python script (native-MSVC HLSL shader verification)
- XNA/FNA relevance: N/A (developer/CI tooling around the D3DCommon shader pipeline)
- Main related tests: invoked by `.github/workflows/d3d-windows-ci.yml`'s "Verify all 20 HLSL shaders against a real D3DCOMPILER_47.dll" step

## Purpose
Builds `hlsl_compiler_tool.cpp` with a real, native `cl.exe`/MSVC toolchain and recompiles every one
of this project's 20 checked-in HLSL shaders against a genuine `D3DCOMPILER_47.dll`, confirming
they're still real, compiler-accepted HLSL on an actual Windows machine — something the project's
Wine+DXVK-based dev loop could never independently prove.

## Executive Verdict
Correct and appropriately scoped, with a well-reasoned, explicitly-justified design choice: this
script deliberately does NOT byte-diff its output against the checked-in `hlsl_shaders.hpp`,
because a different `D3DCOMPILER_47.dll` version/build can legitimately produce different-but-
equivalent DXBC bytes — byte-diffing would create false-alarm failures on legitimate compiler
version differences, not real regressions. The actual signal this script provides ("does
`D3DCompile()` on real Windows still accept this HLSL at all") is precisely the one thing the
Wine-based dev loop cannot check, and the design correctly targets exactly that.

## Checklist Results
- Reuses the single canonical shader list (`SHADERS` from `compile_shaders_hlsl.py`) rather than
  maintaining a second, parallel list that could drift from the "real" one — good single-source-
  of-truth discipline.
- Verifies the compiled output starts with the real DXBC magic bytes (`b"DXBC"`), not just a
  nonzero exit code — a slightly stronger correctness check than merely trusting the compiler
  tool's own return code.
- Clear, itemized `[PASS]`/`[FAIL]` per-shader reporting with a final aggregate result line.

## Detailed Findings
None.

## Cross-File Observations
Directly invoked by `.github/workflows/d3d-windows-ci.yml` (audited in `build-ci`), gated to run
only on the D3D11 matrix leg since the HLSL sources/`hlsl_compiler_tool.cpp` are shared D3DCommon
content, not backend-specific — consistent cross-referencing between the two files.

## Missing or Weak Tests
N/A (this IS the verification mechanism, not itself under test).

## Positive Findings
The deliberate choice not to byte-diff DXBC output, with a clear technical justification for why
that would produce false alarms, shows good judgment about what this check should and shouldn't
assert.

## Final Assessment
No findings.
