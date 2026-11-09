# Fuzzing compiled XNA effect bytecode

The compiled-effect fuzzer harness, the CI gates that run it, how to reproduce and minimize a
finding, and what a run does and does not qualify. The original FX-051 campaign's acceptance bar
and its limits are at the end of this page.

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
No libFuzzer-guided run is configured and no exhaustive safety claim is made; the separately
scheduled longer campaign is [below](#scheduled-long-campaign).

The workflow is the owning CI recipe; it lists Ubuntu packages, sibling/submodule
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

## Scheduled long campaign

The longer campaign (MTH-SAFE-007) is
[robustness-long-fuzz.yml](../.github/workflows/robustness-long-fuzz.yml): weekly (Saturday 01:23
UTC) and on dispatch, in the bounded gate's configuration (SOFTWARE with
`CNA_SOFTWARE_COMPILED_EFFECTS=ON`, ASan+UBSan at O0, leak detection on) plus `CNA_BUILD_C_API=ON`.
[run_long_campaign.py](../tools/fuzz/run_long_campaign.py) runs five standalone harnesses, two at
a time, each for its own wall-time budget over one contiguous seed range (`run_number * 2^32`
onwards, or the dispatch's `seed_start`):

| Target | Harness | Seed corpus | Must be reached (else the target fails) |
|---|---|---|---|
| `compiled-effect` | `cna_compiled_effect_fuzzer` (this page) | the 12 `.fxb` in `modules/renderers/fna3d/effects` and the 14 in `tests/fixtures/compiled-effects/crash-corpus` | CompiledEffects reported; construct, clone, apply, draw, readback |
| `xna-intermediate` | `cna_xna_intermediate_fuzzer` | the 351 `.xml` in `tests/reference/xna40/intermediate` | accepted, round trip, both refusals |
| `xna-model` | `cna_xna_model_fuzzer` | the 118 `.x`/`.fbx` in `tests/assets/xna40/model` | both readers, accepted, refused, nodes |
| `xnb-reader` | `cna_xnb_reader_fuzzer` | the `.xnb` fixtures of `tests/reference/xna40` (genuine XNA builds) and `tests/assets/xnb` (CNA, MonoGame LZX/LZ4/uncompressed), plus the video fixture | containers decoded (LZX and LZ4 among them), manifests parsed, accepted, refused, walked |
| `c-api-string-view` | `cna_c_api_string_view_fuzzer` | ten seeds, [C API fuzzing](c-api/FUZZING.md) | accepted/rejected text, arithmetic, overflow |

The harnesses share [seeded_harness.hpp](../tools/fuzz/seeded_harness.hpp): candidate N is a
seed-chosen corpus entry after the harness's own mutation (one round of several edits; one to four
stacked single-edit rounds for `.x`/FBX), derived from the seed N alone, so one recorded seed
regenerates one exact input (`--emit`) and a kept input replays on its own (`--replay-one`). Each
candidate is announced by a flushed `cna_fuzz_begin <seed>` line, which is how a crash, sanitizer
exit, timeout or RSS kill is attributed. The harness's own counters (operations and the "must be
reached" ones) close each process and are snapshotted once a second, so a process that dies still
accounts for what it reached; `unaccounted_attempts` counts the rest. SIGINT/SIGTERM finish the
current candidate first.

A target fails, never passes, on zero attempts, zero operations or a zero required counter; a
missing or empty corpus or binary; a translation unit without `-fsanitize=address,undefined` in
`compile_commands.json`; a harness exiting 3 (capability unavailable) or describing another
capability; a harness failing before its start line; or attempt counts that disagree with the
seeds. `tools/fuzz/test_run_long_campaign.py` (CTest `FuzzCampaignDriverTests`) pins each rule
against a fake harness, as well as a crash, a timeout, an RSS kill and a leak at exit on known
seeds, and interruption by SIGINT and SIGTERM.

Per target, `summary.json` records the binary SHA-256, CMakeCache/compile flags, sanitizer
options, every corpus file's SHA-256, the seed range, attempts, operations and counters, elapsed
time, peak RSS and the findings; `stderr.log` is capped at 4 MiB. Each finding keeps the input
with its exact seed (`findings/<kind>-seed-<seed>.<ext>` and `.json`), whether `--replay-one`
reproduced it in isolation, and the sanitizer report's tail. A failure at exit (a leak report) is
bisected to one seed after a zero-candidate control batch; when the control fails too, it is kept
once as input-independent. At most three inputs are kept per failure signature (a leak's
allocating frames, a UBSan report's source location, otherwise the sanitizer `SUMMARY` or
`std::terminate` line); later repeats are counted with their seeds, and five distinct signatures
stop a target. The job fails on any finding (exit 1) or any failed rule (exit 2), and
uploads everything for 30 days and the findings for 90.

Budgets (`tools/fuzz/campaign_targets.json`) were chosen from a 180-second measurement of each
target in an ASan+UBSan O0 Debug build (GCC 14), four targets at once on a shared 16-core host at
load 16-18, so a dedicated runner may be faster. Each run's summary records its own attempts/s.

| Target | Attempts/s measured | Peak RSS | Budget | Attempts per weekly run |
|---|---:|---:|---:|---:|
| `compiled-effect` | 39.9 | 890 MB | 60 min | about 1.4e5 |
| `xna-intermediate` | 1,061 | 550 MB | 45 min | about 2.9e6 |
| `xna-model` | 1,640 | 434 MB | 30 min | about 3e6 |
| `c-api-string-view` | 83,263 | 550 MB | 10 min | about 5e7 |
| `xnb-reader` | 319 (measured alone) | 1.2 GB | 30 min | about 5.7e5 |

With two targets at a time the plan is 94 minutes; `budget_scale` scales all five, and the plan is
refused before the build if it would exceed 210 minutes. The RSS limits (4 GiB, 2 GiB for the C
API target) and per-candidate timeouts (120 s for effects, 60 s/30 s otherwise) are generous
multiples of what was measured.

Each finding of the first campaigns is fixed and pinned by a named regression beside the code it
fixed (for example `XnaModelReaderHardening.*`, `MojoShaderEffectStateTranslation.*`,
`LzxDecoderMalformedStreamTest.*`, `IntermediateUtf8Tests`). Ill-formed UTF-8 that sharp-runtime's
`XmlReader` accepted is refused on read (GAP-22; minimized seed in `tests/fuzz/xna-intermediate/`).
MojoShader's duplicate small- and large-object leaks are repaired at the current fork pin
(FNA3D-2, FNA3D-6); their LeakSanitizer replay is still pending
([`fna3d-dependency-defects.md`](fna3d-dependency-defects.md#pending-qualification)). Any current
open finding is listed in `KNOWN_ISSUES.md`; the job fails while one reproduces.

Run it locally in an existing sanitizer tree, window-free (`--plan` prints the schedule only):

```sh
cmake --build build-asan --parallel 4 --target cna_compiled_effect_fuzzer \
  cna_xna_intermediate_fuzzer cna_xna_model_fuzzer cna_xnb_reader_fuzzer cna_c_api_string_view_fuzzer
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  python3 tools/fuzz/run_long_campaign.py --build-dir build-asan --output-dir <dir> \
  --require-sanitizer address --require-sanitizer undefined --jobs 2 --budget-scale 0.1
```

Replay a finding outside the driver with `python3 tools/fuzz/reproduce.py <finding>.json`: it runs
`<harness> --replay-one <input>` with the recorded options and limits and exits 1 while the
finding reproduces, 0 once it does not. `--regenerate` first proves the seed rebuilds the kept
bytes from the recorded corpus; `--minimize <file>` shrinks the input while the same failure
recurs. A kept input becomes a regression by joining the deterministic corpus its harness's test
replays: compiled effects in `tests/fixtures/compiled-effects/crash-corpus` (with a README row,
replayed by `Fna3dCompiledEffectTest.CrashCorpusPassesDrawOrRefuseWithoutCrashing`, plus a test
beside the renderer that owns the defect), `.x`/FBX bytes as a named `XnaModelReaderHardening`
case, XML as an `XnaIntermediateSerializerHardeningTests` case, C API bytes as a fixed case in
`Utf8OracleTest.cpp`. The next campaign then starts from it too.

What it does not qualify: it is seeded mutation without coverage feedback (no coverage is
measured, and attempts are not coverage); compiled effects reach MojoShader's effect parser (the
pinned fork) and SOFTWARE's CPU execution only, so no GPU driver, FNA3D or SDL_GPU apply/draw path
is exercised; CNB, CNJ, XNB, image, WAV and glTF
parsers are not targets (the bounded gate above covers their regressions); and a clean run
qualifies only the seeds and budget it records.

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

A pass that binds no shader, drawn while nothing else has bound one
(`crash-corpus/clone-effect-null-parse-data.fxb`), is refused with XNA's `CannotDrawNoShader`
`InvalidOperationException` (CNA-FIX-L14, `Fna3dDraw.cpp`).

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
The effects directory holds six redistributed stock effects plus CNA-specific effects. The crash
directory holds one artifact per retained input, not one per historical crash class; its
[README](../tests/fixtures/compiled-effects/crash-corpus/README.md) records the defect each pins.

Repeat driver-specific work with FNA3D_FORCE_DRIVER=SDLGPU. This is FNA3D's SDL_GPU driver,
distinct from selecting CNA's SDLGPU renderer. A single/default renderer build can use the same
recipe without the multi-tree runtime selector.

In an existing FNA3D tree (`cmake-build-fna3d` below), build only the harness and its support
target. Run the CLI gate on both drivers through the private compositor (the CTest registration
fixes OpenGL, so use the script directly for SDL_GPU):

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
for source, clone, and clone after source destruction; `CompiledEffectFuzzerCliTests` asserts these
counts and pixels. The unsupported STUB selector must fail with `not qualified` and no summary.
A single-renderer build checks an unavailable selector; checking an instantiated renderer without
CompiledEffects requires the existing multi-renderer tree with STUB selectable.

For a fixed file list with known operation counts, generate the three fixtures and a refusal into
a scratch directory, then replay exactly these four files, in this order, twice per driver
(`tools/safe_inputs_manifest.py` pins their generated bytes as `FX_REPLAY`):

```sh
# Set fx_replay_dir to an existing scratch directory.
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
# fx_fuzz_dir: a scratch directory for the seed copy and the findings.
mkdir -p "$fx_fuzz_dir/seeds" "$fx_fuzz_dir/findings"
cp modules/renderers/fna3d/effects/*.fxb "$fx_fuzz_dir/seeds/"
tools/platform/run_gpu_tests_private.sh --exec \
  env CNA_GRAPHICS_RENDERER=FNA3D FNA3D_FORCE_DRIVER=OpenGL SDL_ASSERT=abort \
  cmake-build-fx-fuzz/cna_compiled_effect_fuzzer \
  -detect_leaks=0 -max_len=1048576 -rss_limit_mb=4096 -timeout=30 \
  -max_total_time=2400 -artifact_prefix="$fx_fuzz_dir/findings/" "$fx_fuzz_dir/seeds"
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

Fna3dCompiledEffectTests.cpp separately exercises synthetic state-only, shader-bearing and stock
mutations, with failure traces and stricter bad_alloc handling. Its fixed counts/timing are source/
build-specific rather than a general per-commit performance guarantee. Current parser fixes are
commits in the pinned forks, including the effect-parser-robustness change, not a loose patch
assumed to apply to every upstream revision.

## Defects the original campaign fixed

The FX-051 campaign's parser and translator defects are fixed in the fork's
`effect-parser-robustness` commit on libcna/mojoshader's `cna` branch
(`git log --grep=effect-parser-robustness`), plus one CNA-side fix in `Fna3dCompiledEffect.cpp`
(`BuildSamplerMap`). The artifacts that still pin them, with the defect each one reproduces, are
in [`tests/fixtures/compiled-effects/crash-corpus/README.md`](../tests/fixtures/compiled-effects/crash-corpus/README.md);
`Fna3dCompiledEffectTest.CrashCorpusIsRejectedWithoutCrashing` and
`CrashCorpusPassesDrawOrRefuseWithoutCrashing` replay them on every build.

## Historical acceptance bar and present limits

FX-051 required one million coverage-guided executions on each exercised FNA3D driver, from
recorded seeds under ASan with asserts fatal and no new crash/timeout/allocation-budget artifact.
A finding ends that window; fixing it and restarting is distinct from reporting the interrupted
window as passed.

The retained record (CNA `17bab8ee2`) reports about 3.08 million OpenGL/GLSL and 2.67 million
SDLGPU/SPIR-V executions without a new artifact after forty-one crash classes were fixed, and a
separate sanitizer record attributed the remaining reports to the then-pinned dependencies. Those
runs used their recorded revision and seeds; they are not evidence that current CNA or current
dependency pins are clean under ASan, UBSan or LSan, and current parser hardening plus finite
replay do not prove safe behavior for all inputs.

Current pins are selected by cmake/ThirdPartyFNA3D.cmake and checked by the source-pin gate.
Parser hardening, bounded reflection and retained artifacts reduce known failure paths; finite
campaigns do not prove safe behavior for all bytecode, native drivers or future pins. Dependency
findings remain reachable through the public API and must be tracked with their actual revision.
