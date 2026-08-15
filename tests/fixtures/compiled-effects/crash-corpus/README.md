# Compiled-effect crash corpus

Inputs that once crashed the process, kept so they cannot crash it again.

Each file is an artifact the coverage-guided fuzzer (`docs/fx-bytecode-fuzzing.md`) wrote when it
found a defect in the pinned MojoShader. Every one is a mutated stock effect binary, so there is
no third-party content here beyond what `../../../modules/renderers/fna3d/effects/` already
carries, and every one is now rejected as ordinary malformed input.

| File | Defect it pins |
|---|---|
| `clone-effect-null-parse-data.fxb` | `MOJOSHADER_cloneEffect` dereferenced a shader's parse data without checking the shader had compiled, and indexed its symbol and parameter tables unbounded |
| `print-float-stack-overflow.fxb` | `MOJOSHADER_printFloat` advances its cursor past the caller's buffer on purpose, to keep its return value meaningful, but then kept formatting through it -- a stack-buffer-overflow |
| `ctab-typeinfo-member-offset.fxb` | `parse_ctab_typeinfo` read a struct's member-table offset from the constant table without bounding it, so a structure could point its member list anywhere |
| `spirv-operand-type-mismatch.fxb` | The SPIR-V emitters asserted that their operands share a type. A load that was already refused returns a zeroed result, so `MAD`, `LRP`, `MOVA`, the `M3X*`/`M4X*` family and the exp/log helper all reached those asserts from untrusted bytecode |
| `spirv-branch-stack-underflow.fxb` | The SPIR-V emitter's branch and loop stacks are driven by the shader's own control-flow instructions, and were guarded only by asserts -- so an `ELSE`, `ENDIF` or `ENDLOOP` with no matching opener underflowed them, and deep nesting overflowed them |
| `empty-shader-object-unbounded-scan.fxb` | A shader object with a zero-length program. `MOJOSHADER_parse` reads a zero buffer size as "size unknown", sets its token count to 4 billion and scans for an end token that is not there -- straight off the payload |

`Fna3dCompiledEffectTest.CrashCorpusIsRejectedWithoutCrashing` replays all of them on every build.
They matter most when the FNA3D or MojoShader pin moves: the fixes live in a patch CNA applies to a
specific revision, and these are what notice if that patch stops covering what it used to.
