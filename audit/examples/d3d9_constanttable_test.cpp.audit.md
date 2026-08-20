# Audit: examples/d3d9_constanttable_test.cpp

## Metadata

- Source file: `examples/d3d9_constanttable_test.cpp` (215 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — `D3D9ConstantTable`'s CTAB binary parser
  (`plans/plan_dx9.md` Phase D9-11 / D9-110).
- File type: standalone `main()`-based executable, no `Game`/device/window, but DOES link and call
  the real Windows `d3dcompiler_47.dll` (`D3DCompile`/`D3DDisassemble`) — CTest-registered as
  `D3D9_ConstantTable` (`cmake/Tests/D3D9Tests.cmake:164-174`), pinned to a specific Wine prefix
  (`CNA_D3D9_WINEPREFIX=~/.wine-cna-d3d9-spike`) that has the *real* Microsoft compiler rather than
  Wine's built-in stub (per that file's own comment, Wine's default d3dcompiler cannot compile
  SM2/SM3 shaders at all).
- XNA/FNA relevance: indirect — validates the register-table parser that ultimately drives every
  named-uniform upload (`WorldViewProj`, `DiffuseColor`, etc.) for all 5 XNA Stock Effects and any
  custom `ShaderEffect`.
- Related production code: `include/CNA/Internal/Backends/D3D9/D3D9ConstantTable.hpp`,
  `src/CNA/Internal/Backends/D3D9/D3D9ConstantTable.cpp` (174 lines, read in full).

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only, and this specific test
additionally requires a live `d3dcompiler_47.dll` (real or Wine-hosted) to run at all. No build or
execution was attempted in this Linux sandbox. This report is based entirely on static reading of
the test file and its production counterpart.

## Purpose

Validates `ParseConstantTableEXT()` (the CTAB chunk parser inside a compiled `vs_3_0` blob) against
a genuinely independent, dual-oracle scheme: (1) it compiles a small, deliberately-non-optimizable
3-constant HLSL vertex shader via the real `D3DCompile()`; (2) it separately runs the real
`D3DDisassemble()` on the identical bytecode and parses ITS OWN authoritative `// Registers:`
comment block with a regex the file's own comment states mirrors
`extract_shader_registers.py`'s `REGISTER_LINE_RE`/`parse_registers()` (the same tool this
project's checked-in stock-effect register tables were built with) — then cross-checks every
oracle-reported constant against the CTAB parser's own output, entry by entry. This is a
methodologically strong test-design choice: rather than hand-writing expected register
indices/counts (which risks the test author making the same offset-arithmetic mistake the parser
itself might make), it uses a second, independently-authored tool (the disassembler's own
human-readable text output) as ground truth.

## Executive Verdict

**Healthy** — the dual-oracle cross-check design is sound, the 3 constants chosen
(`float4x4`/`float4`/`float` — 4/1/1 registers respectively) are a genuinely discriminating set
for register-count arithmetic, and the file's 3 edge-case checks (null bytecode, garbage bytecode,
a well-formed-but-CTAB-less minimal shader stream) are real, non-degenerate negative tests.

## Checklist Results

### API / XNA / FNA parity
N/A directly — `D3D9ConstantTable`/`D3D9ShaderConstantEXT`/`D3D9RegisterSetEXT` are CNA-internal,
`NOXNA`-flavored backend plumbing (the "EXT" suffix convention this project uses throughout D3D9 to
mark backend-internal, non-XNA members), not `Microsoft::Xna` API surface.

### Behavioral correctness
- `kTestShaderSrc` (lines 54-69) declares `WorldViewProj` (float4x4), `TintColor` (float4), and
  `SomeScalar` (float), all three of which are genuinely READ in the shader body (`mul`,
  multiplication) — the file's own comment (line 52) correctly notes this prevents the compiler
  from dead-stripping any of them, which would otherwise silently reduce the oracle's own constant
  count and defeat the test's premise. This is a real, necessary precondition for the test to mean
  anything, and it is satisfied.
- `SomeScalar` (line 57) is specifically chosen to prove "D3D9 always allocates a full c-register
  per named constant even for a scalar" (line 51-52) — checked against `Find(parsed, "SomeScalar")
  ->registerCount == 1` (line 191-193). This is a genuine, non-obvious D3D9 constant-table
  convention (unlike a byte-packed layout, D3D9 SM2/3 constant registers are not sub-allocated
  below a float4 granularity for named constants), and worth calling out as a well-chosen
  discriminator rather than an arbitrary assertion.
- The oracle-vs-parser cross-check loop (lines 168-182) requires BOTH the name, the exact
  `registerIndex`, the exact `registerCount`, AND `registerSet == D3D9RegisterSetEXT::Float4` to
  match for each of the 3 oracle-reported constants — a strict, field-by-field equality, not a
  loose "found something with this name" check.
- Edge cases (lines 196-211): `ParseConstantTableEXT(nullptr, 0)` returns empty (not a crash);
  8 bytes of pure garbage (no CTAB comment token present) returns empty; and a carefully
  hand-constructed 8-byte "minimal, well-formed-but-CTAB-less" token stream
  (`{0x00,0x03,0xFE,0xFF,0xFF,0xFF,0x00,0x00}` — version token + `D3DSIO_END`) also returns empty.
  This third case is a meaningfully different code path from the second (garbage) — it exercises
  "valid shader stream structurally, but genuinely has no embedded comment at all" rather than
  "not a valid shader stream in the first place," which is exactly the distinction a parser bug
  (e.g. one that scans for *any* comment-opcode byte pattern rather than specifically the CTAB
  fourCC) could get wrong in only one of the two cases. Both are independently asserted.

### Logic
`ParseDisassemblyRegisters()` (lines 81-108) is a hand-transcribed mirror of the project's own
`extract_shader_registers.py` regex (`^//\s+(\S+)\s+c(\d+)\s+(\d+)\s*$`, confirmed identical shape
to the file's own comment claim at lines 78-80) plus the same table-boundary detection (`//
Registers:` start marker, `//   ---` separator skip, blank-comment-line-after-nonempty-result
termination). This duplication (C++ reimplementation of a Python regex/state machine) is a minor
maintenance liability — a future change to the Python tool's own table format would not
automatically keep this C++ mirror in sync — but is a defensible, explicit design tradeoff for a
test that specifically wants an *independent* implementation of the same parsing logic, not a
shared one (sharing the parser would defeat the "two different code paths agreeing" cross-check
premise).

### C++ correctness
`Find()` (lines 137-141) uses `std::find_if` correctly; `ParseDisassemblyRegisters` uses
`std::getline`/`std::istringstream` over the disassembly text without any raw-pointer arithmetic.
`CompileTestShader`/`DisassembleTestShader` (lines 110-135) correctly check `FAILED(hr)` before
touching the blob pointers and correctly extract the error blob's buffer only when non-null
(ternary at line 119, matching the same defensive pattern used in `d3d9_effectbackend_test.cpp`).
No lifetime issues — `ComPtr` (WRL) manages the `ID3DBlob` lifetimes throughout.

### Memory/resource lifetime
`ComPtr<ID3DBlob>` RAII throughout (lines 112, 130) — no manual `Release()` calls, no leak risk on
either the success or failure path of `CompileTestShader`/`DisassembleTestShader`.

### Thread safety, Performance
N/A — single-threaded, one-shot diagnostic binary; `D3DCompile`/`D3DDisassemble` calls are not hot
paths.

### Architecture
Correctly scoped to `CNA::Internal::Backends::D3D9::D3D9ConstantTable` alone — does not reach into
`D3D9GraphicsBackend`/`D3D9EffectBackend`, appropriately leaving that integration to
`d3d9_effectbackend_test.cpp` (this batch's companion file, which is exactly where
`ParseConstantTableEXT()`'s real caller, `D3D9EffectBackend::CompileProgram()`, is exercised
end-to-end).

### Maintainability
The header comment (lines 1-18) is unusually explicit about WHY this dual-oracle design was chosen
("Rather than trusting the parser's own output blindly... this ALSO calls D3DDisassemble()... A
wrong offset assumption in the binary parser would very likely either produce garbage... or wrong
register numbers here, not a subtly-plausible wrong answer") — a genuinely useful piece of
test-design reasoning for a future maintainer, not boilerplate.

### Robustness
Covered above under Behavioral correctness — the 3 edge cases are real and non-degenerate.

### Testing
This file is the primary/sole direct test of `D3D9ConstantTable.cpp`'s parsing logic found in this
batch. Coverage is good for the "happy path + malformed input" axes but does not attempt multiple
named-constant *types* beyond float4x4/float4/float (e.g. no `int`/`bool` register-set constant,
i.e. `D3D9RegisterSetEXT::Int4`/`Bool`/`Sampler` are declared in the production enum
(`MapRegisterSet` in `D3D9ConstantTable.cpp` maps raw values 0-3 to `Bool`/`Int4`/`Float4`/
`Sampler`) but this test never exercises a non-`Float4` register set at all — see Missing or Weak
Tests.

### Cross-file consistency
`D3D9ConstantTable.cpp` was read in full (174 lines). The CTAB fourCC constant
(`kCtabFourCC = 0x42415443` = 'C','T','A','B' little-endian), the comment-opcode/end-token
constants, and `MapRegisterSet`'s 0→Bool/1→Int4/2→Float4/3→Sampler mapping (matching real
`D3DXREGISTER_SET` values per the production code's own comment) are all consistent with what this
test implicitly exercises (every named constant in the test shader is a `float`/`float4`/`float4x4`
— all `D3DXRS_FLOAT4` register-set constants — so `MapRegisterSet`'s `raw==2` branch is the only
one exercised).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Missing or Weak Tests

- **LOW (confidence HIGH): no `bool`/`int`/`sampler` register-set constant is exercised.** The test
  shader (lines 54-68) declares only `float4x4`/`float4`/`float` constants, all of which map to
  `D3D9RegisterSetEXT::Float4`. `D3D9ConstantTable.cpp`'s `MapRegisterSet()` has 4 real branches
  (`Bool`/`Int4`/`Float4`/`Sampler`) but only the `Float4` branch is ever reached by this test. A
  regression in the `Bool`/`Int4`/`Sampler` mapping (e.g. swapped raw values 0/1) would not be
  caught here. A real XNA `Texture` sampler declaration (`sampler2D` / `Texture` + `sampler_state`
  block) would be a natural, still-simple addition to exercise the `Sampler` register set at least
  once. Not a defect in what exists — this is a coverage gap, and the file's own stated scope
  ("Unit-test against a shader with known constants before wiring it to anything") is satisfied for
  the Float4 case it does cover.

## Positive Findings

- The dual-independent-oracle design (compile once, verify two different tools' outputs against
  each other) is a notably strong test-engineering pattern — it specifically defends against the
  failure mode where a parser bug produces a *plausible-looking* wrong answer (e.g. an off-by-one
  register index that still "looks like" a valid answer to a hand-written expected-constant table),
  which a single-oracle unit test authored by the same person who wrote the parser would be poorly
  positioned to catch.
- The scalar-constant register-count check (`SomeScalar` occupies a full register, not a
  sub-allocated slot) targets a genuinely easy-to-get-wrong D3D9-specific convention rather than an
  obvious property.
- The three edge cases (null, garbage, well-formed-but-CTAB-less) are a well-chosen, mutually
  distinct set — not three variations on the same failure mode.

## Final Assessment

A well-designed, methodologically sound validation of a binary-format parser, whose only gap is
breadth of constant *types* exercised (only the Float4 register set), which is a reasonable and
disclosed scope choice rather than an oversight given the file's own stated goal of validating the
parser "before wiring it to anything" — the type-diversity gap is better exercised end-to-end by
`d3d9_effectbackend_test.cpp`'s real-shader integration test, this batch's other file, which does
not itself use non-Float4 constants either (see that file's own report).
