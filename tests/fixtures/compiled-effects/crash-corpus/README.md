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

`Fna3dCompiledEffectTest.CrashCorpusIsRejectedWithoutCrashing` replays all of them on every build.
They matter most when the FNA3D or MojoShader pin moves: the fixes live in a patch CNA applies to a
specific revision, and these are what notice if that patch stops covering what it used to.
