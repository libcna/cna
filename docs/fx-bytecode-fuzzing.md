# Fuzzing compiled XNA effect bytecode

Current harness/build scope checked on 2026-11-02 (CNA-FIX-L14). FX-051's original campaigns and
defects are retained below as dated evidence; a later dependency pin or replay is not a new
million-run pass.

MTH-SAFE-003 extends the harness controls below. Its new GPU gates require an owner-run replay;
the historical execution counts further down describe the previous harness.

## Repeatable CPU sanitizer gate

[Robustness CI](../.github/workflows/robustness-sanitizers-ci.yml) runs the CPU harness
controls alongside Safe002 parser and Safe006 residency/failure regressions on SOFTWARE with
ASan+UBSan and leak detection enabled. Selections fail when empty, undersized or skipped;
per-test timeouts and job/step budgets bound execution. Artifacts retain GoogleTest JSON
(including recorded operation counts), CTest XML/logs, revisions and compile commands for 14 days.
[The input manifest](../tools/safe_inputs_manifest.py) pins file provenance and SHA-256,
including the ordered four-file replay recipe below; `SafeInputProvenance` (label `tools`)
regenerates and checks its bytes without executing effects. Update hashes deliberately with inputs.
This gate does **not** qualify GPU parsing/apply/draw/readback or dependency/driver cleanliness.
No libFuzzer-guided run is configured, no exhaustive safety claim is made, and a scheduled longer
campaign remains deferred until its target throughput and execution capability are measured.

The workflow is the owning CI recipe (`2183762f4`); it lists Ubuntu packages, sibling/submodule
setup, configure/build commands and artifact checks. To replay its bounded CPU selection in an
existing SOFTWARE tree configured with `CNA_SOFTWARE_COMPILED_EFFECTS=ON`,
`CNA_CNB_ZSTD=ON`, `CNA_ENABLE_DRACO=ON`, tests and `CNA_SANITIZE=address,undefined`
(`CNA_SANITIZE_OPTIMIZATION=O0`), first build `CnaTests` and
`cna_compiled_effect_fuzzer_support_tests`, then run:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir build-sanitize --no-tests=error -j1 --timeout 120 -V \
  -R '^(SafeInputProvenance|CompiledEffectFuzzerSupportTests)$|Safe002|ContentResource(Pressure|Failure)'
```

Here `build-sanitize` is the workflow's configured tree, not a shipped directory. This needs
GCC sanitizer runtimes and the workflow's pinned inputs/dependencies. Keep the tree exclusive;
for CI-equivalent evidence use the workflow's per-selection XML/JSON capture and count checks. `LeakSanitizer does not work under ptrace`
requires rerunning outside the debugger/tracer for leak evidence. `detect_leaks=0` permits only
an explicitly disclosed address/UB diagnostic run. A missing/undersized/skipped XML or inner
GoogleTest result fails CI even if CTest exited zero; inspect the retained XML, JSON and logs,
restore prerequisites and rerun. A changed input hash requires reviewing the fixture and manifest,
not bypassing `SafeInputProvenance`.

## What the entry point actually exercises

tools/graphics/compiled_effect_fuzzer.cpp exports LLVMFuzzerTestOneInput. It reuses one device
and, for nonempty inputs of at most 64 MiB with CompiledEffects available:

1. resets blend/depth/rasterizer, blend factor, sample mask, reference stencil, all active pixel
   and vertex samplers/textures, target/viewport/scissor and inherited shaders to known state;
   draws a textured baseline triangle and verifies its white center pixel, then constructs Effect.
   The red/white texture is sampled beyond its edge so stale mirror addressing is observable;
2. walks every parameter through depth eight under a node budget (256 parameters, 256 annotations
   per candidate): names, semantics, classes, types and dimensions, every typed `GetValue*` getter
   (scalars, vectors, matrices, quaternions, strings, the three texture kinds) and every array
   getter with counts 0, 1 and 4, then every typed `SetValue*`/`SetValueTranspose` setter,
   including null textures of each kind; annotations get the same name/class/type/getter walk;
3. selects each technique and revisits the first technique after the others (at most 32 selections,
   64 visited passes per walk);
4. for each visited pass, rebinds all reflected Texture/Texture2D parameters to non-null 2D
   textures A/B/A and switches destinations backbuffer/target A/target B. Each binding is followed
   by Apply and, if accepted, `DrawUserPrimitives` and a center-pixel readback. Targets are unbound
   before `GetData`; the backbuffer route uses `GetBackBufferData`;
5. clones the effect, repeats the walk/application/draw on the clone, destroys the source, then
   repeats on the clone again.

Each operation is attempted on its own: an ordinary `std::exception` refusal is counted and the
candidate continues with the next operation, so one refused getter does not hide the rest.
`std::bad_alloc` is not a refusal: it escapes the candidate as a finding (libFuzzer reports it;
standalone replay exits 1). Baseline/reset/resource-setup failures are fatal harness failures,
never input refusals. The baseline draw/read is counted only as `state_resets`, independently of
candidate applies/draws/readbacks. A refused constructor still allows the next input to run.

Bounds are per walk: the source and two clone walks can together visit 192 passes and attempt
576 applies/draws/readbacks. Non-null rebinding walks use the same depth-eight/256-node bound;
they currently cover 2D textures, not volume/cube resources. This uses a fixed
`VertexPositionColorTexture` triangle and does not exercise every sampler or native shader path.
The shared device is deliberately retained between candidates. Fresh-device crash-corpus coverage
remains in `Fna3dCompiledEffectTest.CrashCorpusPassesDrawOrRefuseWithoutCrashing`, rather than
duplicating that existing test in this tool.

The first candidate creates the device, which must report CompiledEffects. An unsupported
selected renderer or unavailable device reports `not qualified` explicitly (exit 1, no summary). Empty and
oversized inputs are counted as skipped without constructing Effect. Native crashes, sanitizer
failures and externally detected timeout/RSS failures remain findings.

The 2026-11-02 extension found a defect on its first replay: a pass that binds no shader, applied
while nothing else had bound one (`crash-corpus/clone-effect-null-parse-data.fxb`), left FNA3D
without a program, and the draw dereferenced it -- SIGSEGV on the OpenGL driver, an SDL assertion
in `MOJOSHADER_sdlGetShaders` on SDL_GPU. CNA's FNA3D renderer now refuses such a draw with XNA's
`CannotDrawNoShader` `InvalidOperationException` (`Fna3dDraw.cpp`), pinned by
`Fna3dCompiledEffectTest.CrashCorpusPassesDrawOrRefuseWithoutCrashing`.

## Standalone replay

The target exists with CNA_BUILD_TESTS, outside Android/Emscripten, in its ordinary main shape.
The standalone reader enumerates regular files only, sorts them, refuses an empty, missing,
oversized (64 MiB) or over-budget file, and bounds a corpus to 4,096 files and 256 MiB. It prints
`seed <sha256> <bytes> <path>` per input, flushes `fx_fuzz_begin <index> sha256 <hash>` before
each candidate (including every mutated candidate), prints a `fx_fuzz_candidate {…}` JSON delta
after it, and ends with one `fx_fuzz_summary {…}` JSON line
(renderer, candidates, skipped, constructed, clones, operations, rejections, parameters,
annotations, applies, draws, readbacks, state_resets, texture_bindings, target_bindings,
technique_selections). Each candidate JSON also includes its `sha256` and a bounded `pixels`
array of successful center readbacks in packed AABBGGRR order (at most 576 entries). These
allow comparison of rendered results as well as operation counts. `qualification` is `execution_reached` only when construction, clone,
apply, draw and readback all succeeded; otherwise it is explicitly `no_execution`. Aggregate
successes do not qualify every candidate: inspect the per-candidate records. A run that cannot
write output fails. Fatal native crashes leave the current candidate's hash in the flushed log.

Two CTests cover the tool itself in builds with tests and outside cross-compilation:
`CompiledEffectFuzzerSupportTests` (mutation bounds, allocation-failure escape, numeric argument
parsing, corpus bounds and order) and, when FNA3D is compiled in, `CompiledEffectFuzzerCliTests`
(`tools/graphics/test_compiled_effect_fuzzer.py`: a stock and an annotated synthetic effect reach
clone, typed walk, apply, draw and readback; STUB is refused; bad arguments and inputs fail;
campaigns repeat exactly; sampling fixtures draw all later passes and return to the first
technique with swapped resources; exact pixels distinguish alternate and sampling programs,
and poison/valid and poison/refusal/valid sequences match isolated valid execution).
The renderer test suite separately replays the committed crash corpus,
including a draw after every pass that applies, and deterministic mutations.

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

For MTH-SAFE-003, use the assigned existing `cmake-build-fna3d` tree. Build only the harness and
support target with `CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv` and `--parallel 4`. Run the CLI
gate on both drivers through the private compositor (the CTest registration fixes OpenGL, so use
the script directly for SDL_GPU):

```sh
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER=OpenGL SDL_ASSERT=abort \
  python3 tools/graphics/test_compiled_effect_fuzzer.py \
  --binary cmake-build-fna3d/cna_compiled_effect_fuzzer \
  --support cmake-build-fna3d/cna_compiled_effect_fuzzer_support_tests \
  --stock modules/renderers/fna3d/effects/SpriteEffect.fxb
# Repeat with FNA3D_FORCE_DRIVER=SDLGPU.
```

The synthetic sampling fixture has two techniques with 2+1 passes. Forward traversal followed
by returning to the first technique, three bindings per visited pass, and three effect walks
must yield exactly 1 constructed, 1 clone, 9 technique selections and 45 each of applies, draws,
readbacks and target bindings; texture bindings must be at least 45. A candidate has one verified
state reset. The poison/refusal/valid sequence requires three resets and the last candidate's
counts and pixels equal its isolated run. The Vector4 setter leaves Tint=(1,1,0,1), so P0 must
produce magenta for all three bindings and StatePass/P1 must produce red, green, red. The
15-pixel sequence (magenta × 3, red/green/red, red/green/red, magenta × 3, red/green/red) repeats
for source, clone, and clone after source destruction. These are assertions awaiting GPU execution,
not measured results. The unsupported STUB selector must fail with `not qualified` and no summary.
A single-renderer build checks an unavailable selector; checking an instantiated renderer without
CompiledEffects requires the existing multi-renderer tree with STUB selectable.

For a fixed file list with known operation counts, generate the three fixtures and a refusal into
an assigned scratch directory, then replay exactly these files twice per driver:

```sh
# Set fx_replay_dir to an existing, assigned scratch directory.
cmake-build-fna3d/cna_compiled_effect_fuzzer_support_tests --write-poison-fixture "$fx_replay_dir/a-poison.fxb"
printf 'refused effect' > "$fx_replay_dir/b-refused.bin"
cmake-build-fna3d/cna_compiled_effect_fuzzer_support_tests --write-sampling-fixture "$fx_replay_dir/c-sampling.fxb"
cmake-build-fna3d/cna_compiled_effect_fuzzer_support_tests --write-fixture "$fx_replay_dir/d-annotated.fxb"
for fx_driver in OpenGL SDLGPU; do
  for fx_run in 1 2; do
    tools/platform/run_gpu_tests_private.sh --exec \
      env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER="$fx_driver" SDL_ASSERT=abort \
      cmake-build-fna3d/cna_compiled_effect_fuzzer \
      "$fx_replay_dir/a-poison.fxb" "$fx_replay_dir/b-refused.bin" \
      "$fx_replay_dir/c-sampling.fxb" "$fx_replay_dir/d-annotated.fxb" \
      > "$fx_replay_dir/$fx_driver-$fx_run.log" 2>&1
  done
  diff -u <(rg '^fx_fuzz_' "$fx_replay_dir/$fx_driver-1.log") \
          <(rg '^fx_fuzz_' "$fx_replay_dir/$fx_driver-2.log")
done
```

The shell comparison uses Bash. Each synthetic valid file should report constructed=1, clones=1,
technique_selections=9, applies=draws=readbacks=target_bindings=45 and state_resets=1. The refusal
must report constructed=clones=applies=draws=readbacks=0, rejections=1 and state_resets=1. Aggregate:
4 candidates, 3 constructed, 3 clones, 135 applies/draws/readbacks, 4 resets. These counts require
all expected fixture operations to succeed; a smaller count is failed coverage, not a new baseline.
SDLGPU here is FNA3D's selectable SDL_GPU driver, not CNA's separately compiled SDLGPU renderer.
Reuse the existing fresh-device test through the private runner after building CnaTests:
`tools/platform/run_gpu_tests_private.sh --exec env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER=OpenGL SDL_ASSERT=abort cmake-build-fna3d/CnaTests --gtest_filter=Fna3dCompiledEffectTest.CrashCorpusPassesDrawOrRefuseWithoutCrashing`.
Repeat with `FNA3D_FORCE_DRIVER=SDLGPU`; this lane does not duplicate that device recreation test.

For bounded corpus evidence, select binary `.fxb` files only, retain their provenance, record
SHA-256 hashes and replay twice on each driver, then run `--campaign <corpus> 500 0x4c3134` twice.
Compare per-candidate records and hashes within each driver; do not assume counts match between
drivers. Use an external timeout/memory supervisor. Isolate findings by replaying the hash-matched
seed alone with the same driver; dependency crashes remain findings attributed to their actual
revision, without editing FNA3D, MojoShader or SDL.

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

Standalone main also accepts --campaign <corpus-dir> <positive-iterations> [seed], selecting a
corpus entry and applying one to six bit/byte/aligned-word/truncation mutations (a word mutation
never writes past a short input). It prints every seed's hash first, then
`campaign-v2 <seeds> seeds, <iterations> iterations, initial seed 0x…`, and every hundredth
iteration's generator state and candidate SHA-256. The corpus is sorted, so the same corpus,
iteration count and seed (decimal count, decimal or `0x` seed) reproduce the same candidates;
zero, signed, partial or out-of-range numbers are refused with exit 2. This mode is not
coverage-guided and does not automatically retain each crashing candidate.

On 2026-11-02 (cmake-build-multi, FNA3D, `SDL_ASSERT=abort`) the replay of the twelve stock/CNA
effects plus the crash corpus and a 500-iteration campaign over the effects (seed `0x4c3134`) both
completed on the OpenGL and the SDL_GPU driver: 34 inputs, 188,951 operations, 129 applies and 42
draws/readbacks per replay; 500 candidates, 314,636 operations and 63 draws per campaign.

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
