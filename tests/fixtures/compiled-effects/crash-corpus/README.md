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
| `sampler-symbol-names-non-sampler.fxb` | A shader's constant table matched a sampler symbol to an effect parameter by name alone. When that parameter is not a sampler, its value storage is not an array of sampler states -- and reinterpreting it as one reads far past the end, in CNA's own `ApplySamplers` |
| `spirv-output-stack-and-attribute.fxb` | Two at once: `push_output`/`pop_output` guarded their fixed stack with asserts, and how often an emitter pushes depends on the shader being translated; and `emit_SPIRV_attribute` dereferenced the register list for an attribute declared on a register the shader never used |
| `preshader-temp-count-stack-overflow.fxb` | The preshader's temp-register count is grown from operand indices, and `run_preshader` `alloca()`s one double per temp -- so an invented index asked for a stack frame the process does not have |
| `clone-uninitialised-struct-member-name.fxb` | Two struct member tables were allocated without being zeroed. The effect parser never assigns the nested-struct fields (upstream's own FIXME), and `copysymboltypeinfo` assigns nothing when a member name is NULL -- which it legitimately is -- so both left heap garbage where CNA later read a name and a nested member count |
| `spirv-undeclared-sampler-stage.fxb` | `emit_SPIRV_sampler` dereferenced the register list's result for a sampler stage the shader never declared, and indexed its per-stage extras array with an unbounded stage number |
| `spirv-operand-type-mismatch.fxb` | The SPIR-V emitters asserted that their operands share a type. A load that was already refused returns a zeroed result, so `MAD`, `LRP`, `MOVA`, the `M3X*`/`M4X*` family and the exp/log helper all reached those asserts from untrusted bytecode |
| `spirv-branch-stack-underflow.fxb` | The SPIR-V emitter's branch and loop stacks are driven by the shader's own control-flow instructions, and were guarded only by asserts -- so an `ELSE`, `ENDIF` or `ENDLOOP` with no matching opener underflowed them, and deep nesting overflowed them |
| `empty-shader-object-unbounded-scan.fxb` | A shader object with a zero-length program. `MOJOSHADER_parse` reads a zero buffer size as "size unknown", sets its token count to 4 billion and scans for an end token that is not there -- straight off the payload |
| `sampler-parameter-class-mismatch.fxb` | The same mismatch as the row below, but reached through CNA's own `BuildSamplerMap`: it picked parameters by sampler type alone and then walked `valuesSS`. The one crash artifact here whose root cause is CNA's code rather than the pinned MojoShader |
| `sampler-value-class-mismatch.fxb` | A value's storage layout is chosen by its class first and its type second -- only an OBJECT of a sampler type is allocated as sampler states -- but `freevalue` asked about the type alone. A parameter that names a sampler type in a non-object class was freed as an array of much larger structures, calling `free()` on whatever the heap held past the end |

`Fna3dCompiledEffectTest.CrashCorpusIsRejectedWithoutCrashing` replays all of them on every build.
They matter most when the FNA3D or MojoShader pin moves: the fixes live in a patch CNA applies to a
specific revision, and these are what notice if that patch stops covering what it used to.
