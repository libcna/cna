# Fuzzing compiled XNA effect bytecode

Current harness/build scope checked on 2026-11-01. FX-051's original campaigns and defects are
retained below as dated evidence; a later dependency pin or replay is not a new million-run pass.

## What the entry point actually exercises

tools/graphics/compiled_effect_fuzzer.cpp exports LLVMFuzzerTestOneInput. It reuses one device
and, for nonempty inputs of at most 64 MiB with CompiledEffects available:

1. constructs Effect from the bytes;
2. walks parameter names/semantics/classes/types/dimensions, annotation names and nested
   element/member metadata through depth eight;
3. selects each technique and applies each exact pass;
4. clones the effect, repeats that walk/application, destroys the source, then repeats on the clone.

This is a useful constructor/reflection/clone/apply path, not the entire public API. It does not
call every typed value/annotation accessor or setter, issue draws/read pixels, recreate the device
or exercise every sampler/native shader execution path. An earlier exception ends that candidate's
remaining stages. The deterministic unit mutation corpus has its own exercise/assertion helper.

Empty/oversized inputs return without constructing Effect. An unsupported selected renderer also
returns without parsing: a zero exit alone can therefore be a false coverage signal. Verify the
selected renderer, build option, capability and emitted driver before interpreting a run.
Device startup and the input-vector allocation occur outside the candidate's catch.

The entry point catches std::exception from construction/walking/application, including bad_alloc.
It does not itself turn every allocation failure into a finding. Native crashes, sanitizer failures
and externally detected timeout/RSS failures remain findings. The standalone loader reads complete
files/corpora into memory before the entry-point size check and has no built-in process budget.

## Standalone replay

The target exists with CNA_BUILD_TESTS, outside Android/Emscripten, in its ordinary main shape.
It is not registered as a standalone CTest; the renderer test suite separately replays a committed
crash corpus and deterministic mutations.

```sh
cmake --build cmake-build-multi --target cna_compiled_effect_fuzzer --parallel 4
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER=OpenGL SDL_ASSERT=abort \
  cmake-build-multi/cna_compiled_effect_fuzzer \
  modules/renderers/fna3d/effects tests/fixtures/compiled-effects/crash-corpus
```

The directory reader takes regular files, not only .fxb; text/source files exercise rejection.
For a precise seed inventory, copy selected binaries to a scratch corpus and record its hashes.
On 2026-11-01 the effects directory contains twelve .fxb files: six redistributed stock effects
plus CNA-specific effects. The crash directory contains fourteen .fxb artifacts, not one artifact
per historical crash class. Preserve its README/provenance.

Repeat driver-specific work with FNA3D_FORCE_DRIVER=SDLGPU. This is FNA3D's SDL_GPU driver,
distinct from selecting CNA's SDLGPU renderer. A single/default renderer build can use the same
recipe without the multi-tree runtime selector.

## Coverage-guided campaigns

Use a separate Clang build and verify that CNA and its actual MojoShader dependency objects carry
coverage/sanitizer flags. System drivers and reused prebuilt dependencies are not instrumented
merely because flags were added to the root configure.

```sh
cmake -S . -B cmake-build-fx-fuzz \
  -DCMAKE_BUILD_TYPE=Debug -DCNA_BUILD_TESTS=ON \
  -DCNA_GRAPHICS_RENDERER=FNA3D -DCNA_FX_FUZZER_ENTRY_POINT=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build cmake-build-fx-fuzz --target cna_compiled_effect_fuzzer --parallel 4
mkdir -p /tmp/cna-fx-seeds /tmp/cna-fx-findings
cp modules/renderers/fna3d/effects/*.fxb /tmp/cna-fx-seeds/
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER=OpenGL SDL_ASSERT=abort \
  cmake-build-fx-fuzz/cna_compiled_effect_fuzzer \
  -detect_leaks=0 -max_len=1048576 -rss_limit_mb=4096 -timeout=30 \
  -max_total_time=2400 -artifact_prefix=/tmp/cna-fx-findings/ /tmp/cna-fx-seeds
```

CNA_FX_FUZZER_ENTRY_POINT removes main; Clang links libFuzzer for this target. Other compatible
drivers need their own verified toolchain/link setup; changing a compiler name alone is not an
AFL++ qualification. The CMake option does not instrument the tree by itself.

The 1 MiB campaign max is smaller than the public 64 MiB constructor limit. The historical
campaign disabled leak detection to separate crash discovery from known leak reports; this
does not prove a clean LSan run or justify silently suppressing present findings. Record exact
sanitizers/options, both dependency revisions, seed hashes, selected native driver and execution
count. Replay an artifact directly under the same configuration before attributing a failure.

## Deterministic mutation mode and unit gates

Standalone main also accepts --campaign <corpus-dir> <iterations> [seed], selecting a corpus
entry and applying one to six bit/byte/aligned-word/truncation mutations. It reports progress and
generator state; retain the initial seed, input set/order and iteration to reproduce it. Directory
iteration is not a portable sorted corpus order. This mode is not coverage-guided and does not
automatically retain each crashing candidate.

Fna3dCompiledEffectTests.cpp separately exercises synthetic state-only, shader-bearing and stock
mutations, with failure traces and stricter bad_alloc handling. Its fixed counts/timing are source/
build-specific rather than a general per-commit performance guarantee. Current parser fixes are
commits in the pinned forks, including the effect-parser-robustness change, not a loose patch
assumed to apply to every upstream revision.

## Historical defect record (2026-08-14/15)

All but one were in the pinned MojoShader, fixed by the `effect-parser-robustness` commit on CNA's
fork libcna/mojoshader (then `cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch`); the
last one is CNA's own and is
fixed in `Fna3dCompiledEffect.cpp`. Each fix was confirmed by the campaign running measurably
deeper afterwards: on the GLSL profile the first crash moved from iteration 0 to 100, 300, 500,
700, 1,200, 3,700, 4,000, 6,400, 8,700 and then away entirely.

| Site | Defect |
|---|---|
| `mojoshader_effects.c` `readlargeobjects` | `MOJOSHADER_parsePreshader`'s NULL result dereferenced on the next line -- upstream's own `// !!! FIXME: check for errors.` |
| `mojoshader_profile_spirv.c` `spv_add_attrib_fixup` | `assert(r != NULL)` on a fixup with no matching attribute register; a release build would dereference NULL |
| `mojoshader_effects.c` `copy_parameter_data` | Copies sized by the shader constant table's register count rather than by the parameter storage that was actually parsed |
| `mojoshader.c` `parse_preshader` | Instruction operand count taken from tokens with no bound against the fixed four-entry operand array |
| `mojoshader.c` `parse_preshader` | `assert(0)` on an unrecognised operand type; a release build would carry on with an unassigned type |
| `mojoshader.c` `parse_preshader` | Instruction count used to allocate and zero a buffer *before* the block-size check that bounds it |
| `mojoshader.c` `parse_preshader` | `assert()` on a constant-table symbol in the wrong register set, and an unbounded array-register count allocated before validation -- upstream's other FIXME |
| `mojoshader_effects.c` `MOJOSHADER_effectCommitChanges` | Shader-array selector used as an unchecked index into a parameter's values and then into the object table, and register copies unbounded by the preshader's register file |
| `mojoshader_effects.c` `run_preshader` | Every literal, input, output and temp index in the preshader interpreter guarded only by asserts; the output register span was not even reported to it, so a one-float selector output could be written past |
| `mojoshader_effects.c` `copy_parameter_data` | Destination register index never bounded against the register file it writes into, and the int/bool files written through NULL when a preshader's float-only file was the target |
| `mojoshader_effects.c` `MOJOSHADER_effectBeginPass` | A pass's shader object index taken from parsed content with no range check, and no check that the object is a shader at all -- `MOJOSHADER_effectObject` is a union, so a string or sampler object read as a shader yields garbage pointers |
| `mojoshader_effects.c` shader-array selector | The selector a preshader computes was converted to an integer before being range-checked. Converting an out-of-range float is undefined and yields `INT_MIN` on x86, which then indexed far below the parameter's values. It is now range-checked as a float, which also rejects NaN |
| `mojoshader_effects.c` `run_preshader`, preshader register copy | The bounds added above were written as `index + span > count`, which wraps for an index near `UINT_MAX` and passes. All of them now subtract instead |
| `mojoshader_profile_spirv.c` `spv_check_read_reg_id` | `assert()` on a sampler or texture register in a shader model that cannot declare one |
| `mojoshader_effects.c` `readstring` | Took a base pointer and an offset but no length, so the file's own length prefix chose how many bytes to copy and from where -- upstream's `// !!! FIXME: sanity checks!` and `// !!! FIXME: verify this doesn't go past EOF looking for a null.` The payload length is now threaded through the parser, and the copy is NUL-terminated, which it never was |
| `mojoshader_effects.c` `MOJOSHADER_compileEffect` | Parameter, technique and object counts read from the file, used both to size allocations and to bound the loops that fill them, with no bound and no overflow check on the size multiplication |
| `mojoshader_effects.c` `MOJOSHADER_cloneEffect` | Its local `COPY_STRING` dereferenced without a NULL check, unlike the file's other one -- and `readstring` returns NULL for a zero-length string, which is ordinary content rather than hostile content |
| `mojoshader.c` `parse_preshader` | The array-register bound added earlier was written as `(count * 2) + 5 > tokcount`, which wraps for a 32-bit count from the file and let an absurd one through into an allocation |
| `mojoshader_profile_spirv.c` `spv_loadreg` | Dereferenced the result of `reglist_find` for a sampler register the shader never declared, and asserted rather than failed when a register had no declaration id |
| `mojoshader_profile_spirv.c` `spv_swizzle` | Asserted on a zero type id, on a write mask outside the four legal values, and on a type it could not classify -- all three come straight from a destination token |
| `mojoshader_profile_spirv.c` `emit_SPIRV_dotproduct` | Asserted that both operands share a type; either can be a value whose load was already refused |
| `mojoshader_profile_spirv.c` `spv_output_color_location`, `spv_output_attrib_location` | A colour-output register number, a vertex attribute usage and its register number all index fixed sixteen-entry tables with no bound, so an out-of-range one wrote past the patch table into whatever followed it |
| `mojoshader_effects.c` `MOJOSHADER_cloneEffect` | Dereferenced a shader's parse data without checking the shader compiled at all, and indexed its symbol and parameter tables unbounded |
| `mojoshader_effects.c` sampler binding | A shader's sampler symbol was matched to an effect parameter by name alone, and its value storage then reinterpreted as an array of sampler states. A parameter that is not a sampler makes that count and that pointer disagree by a factor of the struct size |
| `mojoshader_profile_common.c` output stack, `mojoshader_profile_spirv.c` `emit_SPIRV_attribute` | The emitters' output-section stack was guarded by asserts, and how often an emitter pushes depends on the shader it translates; and an attribute declared for a register the shader never used dereferenced a null register-list entry |
| `mojoshader.c` / `mojoshader_effects.c` preshader temps | The temp-register count is grown from operand indices and `run_preshader` `alloca()`s one double per temp, so an invented index blew the process stack. Bounded structurally at parse time, with a ceiling in the interpreter as a second line of defence. Found after 536,926 SDLGPU executions |
| `mojoshader_effects.c` struct member tables | `readvalue` and `copysymboltypeinfo` both allocated member tables without zeroing them, and neither assigns every field: the parser leaves the nested-struct fields alone (upstream's FIXME) and the copy assigns nothing when a member name is NULL. A consumer then read a garbage name pointer and a garbage nested member count -- the first finding that surfaced inside CNA's own frame, and one CNA could not have defended against |
| `mojoshader_profile_spirv.c` `emit_SPIRV_sampler` | Dereferenced the register list's result for a sampler stage the shader never declared, and indexed the per-stage extras array with an unbounded stage. Found after 400,319 SDLGPU executions |
| `mojoshader_profile_spirv.c` operand-type asserts | `emit_SPIRV_MAD`, `emit_SPIRV_LRP`, `emit_SPIRV_MOVA`, the matrix-instruction helper and the exp/log helper asserted that their operands share a type, or that a destination shape is one of the legal ones. Both come from parsed bytecode, and a load that was already refused returns a zeroed type |
| `mojoshader_profile_spirv.c` branch/loop stacks | `spv_branch_push/get/pop` and `spv_loop_push/get/pop` were guarded only by asserts, so a shader closing a branch or loop it never opened underflowed the stack and deep nesting overflowed it. Found by the SDLGPU campaign after 56,261 executions |
| `mojoshader_effects.c` object tables | Three related holes that all end in the shader parser running off the payload: an object block's length was never bounded against what remains, the XNA4 wrapper skip *added* to the remaining-length counter instead of subtracting (and every real XNA4-wrapped effect takes that branch), and a zero-length shader program told `MOJOSHADER_parse` the size was unknown, which sets its token count to 4 billion and scans for an end token that is not there. Found after 395,357 coverage-guided executions |
| `mojoshader.c` `parse_ctab_typeinfo` | A struct's member-table offset was read from the constant table and never bounded, so a structure in untrusted bytecode could point its member list anywhere. Found after 275,896 coverage-guided executions |
| `mojoshader_common.c` `MOJOSHADER_printFloat` | Advances its cursor past the buffer on purpose, so the return value stays the length the caller needed -- but then kept formatting *through* that cursor, writing past the end. Each step now asks `snprintf` for the needed length instead of writing once the cursor is out of bounds |
| `mojoshader.c` `parse_preshader` | How many preshaders a shader declares is content, and it was guarded by an assert -- upstream's own `// !!! FIXME: can you have more than one?` is still unanswered. A second declaration also overwrote the first and leaked it. Found after 8,192 SDLGPU executions |
| `Fna3dCompiledEffect.cpp` `BuildSamplerMap` | **The one finding whose root cause is CNA's own.** Same defect as the row below, in CNA's code and with every fact needed to avoid it in hand: the sampler-texture map selected parameters by sampler *type* and then walked `valuesSS`, so a parameter naming a sampler type in a non-object class had its much smaller value buffer read as an array of sampler states. Found after 1,627,248 SDLGPU executions -- the deepest finding so far, and past the point where the driver had already run a million clean |
| `mojoshader_effects.c` `freevalue` | A value's storage layout is chosen by its class first and its type second: only an OBJECT of a sampler type is allocated as an array of sampler states. `readvalue` and `copyvalue` both ask that two-part question; `freevalue` asked about the type alone, so a parameter naming a sampler type in some other class was freed as an array of much larger structures -- `free()` on whatever the heap held past the end. The two-part test is now one shared predicate, used by `freevalue`, by both shader-sampler binding sites and by the object table's sampler-state lookup, which also gained the range checks its content-supplied indices never had. Found after 660,870 SDLGPU executions |

The one pre-existing fix in the same patch -- a missing shader-to-effect parameter match, which
asserted and then dereferenced -- was found earlier by the deterministic in-build corpus.

## Historical acceptance bar and present limits

FX-051 required one million coverage-guided executions on each exercised FNA3D driver, from
recorded seeds under ASan with asserts fatal and no new crash/timeout/allocation-budget artifact.
A finding ends that window; fixing it and restarting is distinct from reporting the interrupted
window as passed.

The retained 2026-08-15 record at 17bab8ee2 reports 3,079,834 OpenGL/GLSL and 2,669,555
SDLGPU/SPIR-V executions without a new artifact after forty-one crash classes were fixed.
Its separate 340-case sanitizer record attributed remaining reports to the then-pinned dependencies.
Those are historical observations, not evidence that current CNA/dependencies are universally
clean under ASan, UBSan or LSan.

Current pins are selected by cmake/ThirdPartyFNA3D.cmake and checked by the source-pin gate.
Parser hardening, bounded reflection and retained artifacts reduce known failure paths; finite
campaigns do not prove safe behavior for all bytecode, native drivers or future pins. Dependency
findings remain reachable through the public API and must be tracked with their actual revision.

The 2026-11-01 audit builds the harness and runs focused regressions/replay as recorded in
[compiled effects](fx-compiled-effects.md). It does not regenerate the historical campaign,
claim exhaustive public-API fuzz coverage or change parser/runtime implementation.
