# Audit: cmake/Tests/D3D9Tests.cmake

## Metadata
- Source file: `cmake/Tests/D3D9Tests.cmake` (199 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake build script (test registration)
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: registers the D3D9 graphics backend's CTest suite (~17 tests: Common,
  ShaderDispatch, Smoke, Draw, DrawEx, Instanced, Pbr, SkinnedVertexColor, SpriteBatch,
  GraphicsProfile, ShaderCache, 4 shared BlendState/DepthStencilState/RasterizerState tests, an
  XNA-oracle corpus diff, ConstantTable, and (conditionally) EffectBackend/SpriteBatch_CustomEffect)

## Purpose
Registers the D3D9 backend's most extensive CTest suite of the three Windows-only D3D backends,
including a real-compiler-dependent constant-table parser validation test and a real XNA-oracle
corpus diff test that compares rendered output against checked-in reference PNGs.

## Executive Verdict
Correct and the most detailed of the three D3D backends' test files, with genuinely careful handling
of a real environment-specific hazard: `D3D9_ConstantTable`/`D3D9_EffectBackend`/
`D3D9_SpriteBatch_CustomEffect` (lines 148-198) explicitly override `CNA_D3D9_WINEPREFIX` to a
separate "spike" prefix, with an accurate, specific explanation (lines 160-163): the *default*
`CNA_D3D9_WINEPREFIX` only has Wine's own builtin `d3dcompiler_47.dll`, which cannot compile SM2/SM3
shaders — only the spike prefix has the real Microsoft compiler these specific tests depend on.

## Checklist Results
- `cna_d3d9_ctest_command()` (lines 18-24) correctly mirrors D3D11's identical macro shape.
- `D3D9_Common`/`D3D9_ShaderDispatch`/`D3D9_ConstantTable` all correctly set
  `CNA_D3D9_SKIP_DXVK_GATE=1` with accurate, specific rationale for each (pure-function mapping
  tables or pure compiler-library calls, never opening a real D3D9 device) — consistent with
  D3D11_Common's identical pattern in this shard.
- `D9-111`'s `cna_test_d3d9_effectbackend`/`D9-112`'s `cna_test_d3d9_spritebatch_customeffect` (lines
  176-198) are both correctly gated behind `if(TARGET cna_backend_graphics_d3d9_effect)` — matching
  the conditional isolated-effect-target design documented in `cmake/BackendLibraries.cmake`
  (already audited in `build-cmake`).
- `D3D9_XNA_Diff` (lines 143-149) correctly notes it does NOT need the XNA Wine prefix, only the
  regular D3D9 one — a precise scope distinction from the constant-table tests just below it, which
  DO need the special spike prefix.
- The 4 EasyGL-source-reuse tests (`BlendState_Opaque/AlphaBlend`, `DepthStencilState_StencilEnable`,
  `RasterizerState_CullMode`, lines 110-134) explicitly note (line 113-115) this is "the same 4-test
  subset D3D11_BlendState... already established, not Vulkan's full set" — an honest, precise scope
  statement rather than implying full parity with Vulkan's much larger reused-test set.

## Detailed Findings
None.

## Cross-File Observations
The `CNA_D3D9_WINEPREFIX` override pattern here is unique to this file among the three D3D backends
— D3D11/D3D12 have no equivalent real-compiler-dependent test requiring a separate Wine prefix,
consistent with D3D9 being the only backend whose `D3D9EffectBackend`/`D3D9ConstantTable` genuinely
call `D3DCompile()`/`D3DDisassemble()` (per `cmake/BackendLibraries.cmake`'s own D3D9-effect-isolation
comments, already audited).

## Missing or Weak Tests
Not applicable to a build script.

## Positive Findings
The WINEPREFIX-override handling for the 3 real-compiler-dependent tests is a genuinely careful,
well-justified environment-specific accommodation — precise about exactly which tests need it and
why, not a blanket override applied everywhere.

## Final Assessment
No findings.
