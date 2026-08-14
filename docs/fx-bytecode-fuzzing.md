# Fuzzing compiled XNA effect bytecode

A compiled Effect Framework binary is untrusted input handed to a native parser (MojoShader) and
then read back through a large public reflection surface. `plan_fx.md` FX-051 therefore requires a
fuzz entry point that covers the whole surface, not only the constructor.

## What is covered

`tools/graphics/compiled_effect_fuzzer.cpp` exports a single `LLVMFuzzerTestOneInput` that runs one
candidate binary through:

1. `Effect(GraphicsDevice&, byte[])` construction;
2. every reflected parameter, including semantics, dimensions, annotations, array elements and
   structure members, to a bounded nesting depth;
3. every technique and every pass, selected and applied by exact index;
4. `Clone()`, the clone's own reflection and pass application, and application again **after** the
   source effect has been destroyed — the ordering a use-after-free between an effect and its
   clone would surface in;
5. destruction of everything.

Any `std::exception` is an accepted outcome: rejecting malformed, truncated, foreign-format or
over-limit content is the contract. Only a crash, a sanitizer report, a hang, or an unbounded
allocation is a finding.

The device is created once and reused, because device creation dwarfs the cost of the code under
test.

## Standalone replay (any compiler)

The harness builds in every ordinary test configuration and replays files or directories:

```sh
cmake --build cmake-build-debug --target cna_compiled_effect_fuzzer -j3
SDL_VIDEODRIVER=offscreen ./cna_compiled_effect_fuzzer modules/renderers/fna3d/effects
```

This is how the committed seed corpus is exercised and how a crashing input found by a campaign is
reproduced. Non-effect files in a seed directory are fine — they simply take the rejection path.

## libFuzzer campaign (clang)

```sh
cmake -S . -B cmake-build-fx-fuzz \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=FNA3D \
  -DCNA_FX_FUZZER_ENTRY_POINT=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address,undefined" \
  -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined"
cmake --build cmake-build-fx-fuzz --target cna_compiled_effect_fuzzer -j3

mkdir -p fx-corpus && cp modules/renderers/fna3d/effects/*.fxb fx-corpus/
SDL_VIDEODRIVER=offscreen ./cmake-build-fx-fuzz/cna_compiled_effect_fuzzer \
  -max_len=1048576 -rss_limit_mb=4096 -timeout=30 fx-corpus
```

`CNA_FX_FUZZER_ENTRY_POINT=ON` drops the harness's own `main()` so the driver owns the loop.
AFL++ picks the same entry point up through `afl-clang-lto`'s libFuzzer compatibility mode; pass
`-DCMAKE_C_COMPILER=afl-clang-lto -DCMAKE_CXX_COMPILER=afl-clang-lto++` instead and drive it with
`afl-fuzz -i fx-corpus -o findings -- ./cna_compiled_effect_fuzzer`.

## Seed corpus

The seeds are the six provenance-tracked stock binaries in `modules/renderers/fna3d/effects/`
(`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`,
`SpriteEffect`). They cover real shader objects, samplers, textures, matrix arrays, multiple
techniques and preshaders, which is the structure a mutation-based campaign needs to start from.

A campaign is not the only mutation coverage. `Fna3dCompiledEffectTests.cpp` runs a deterministic,
seeded corpus on every build — 1,024 mutations of the state-only synthetic fixture, 192 of a
shader-bearing one that reaches the object table, constant table and preshader selection path, and
192 of a real stock binary — through exactly the same construction/reflection/clone/apply path. It
fails on an escaped `std::bad_alloc`, a non-`std::exception` throw, or a crash, and keeps the seed,
iteration, size and mutation description in every failure trace, so a finding there is reproducible
without the corpus directory. Its mutator includes the whole-aligned-word overwrite that found most
of the parser crashes above, and the whole thing costs about 130 ms.

## Mutation campaign

The standalone harness can also drive a deterministic mutation campaign, which is what actually
found the parser defects listed below. It needs no clang, no libFuzzer and no new build tree:

```sh
mkdir -p fx-corpus && cp modules/renderers/fna3d/effects/*.fxb fx-corpus/
SDL_VIDEODRIVER=offscreen SDL_ASSERT=abort ASAN_OPTIONS=detect_leaks=0   ./cmake-build-fna3d-asan/cna_compiled_effect_fuzzer --campaign fx-corpus 10000 [seed]
```

It picks a seed at random, applies one to six mutations (bit flip, byte set, whole little-endian
word -- offsets and counts live in those -- or truncation), and runs the result through the same
entry point. Progress is printed every hundred iterations with the generator state, so a finding
reproduces from the printed seed and iteration without writing candidates to disk.

`SDL_ASSERT=abort` matters: MojoShader's asserts otherwise open an interactive prompt and stall
the campaign.

## What the campaign found (2026-08-14)

Every finding was in the pinned MojoShader, none in CNA. Each is now fixed by
`cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch`, and each fix was confirmed by
the campaign running measurably deeper afterwards: on the GLSL profile the first crash moved from
iteration 0 to 100, 300, 500, 700, 1,200, 3,700, 4,000, 6,400, 8,700 and then away entirely.

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

The one pre-existing fix in the same patch -- a missing shader-to-effect parameter match, which
asserted and then dereferenced -- was found earlier by the deterministic in-build corpus.

## Exposure that remains

Two distinct areas, and they differ by driver.

**On FNA3D's OpenGL driver (GLSL profile)** three independent seeds now run clean --
10,000 / 10,000 / 12,000 iterations with no crash, no assert and no sanitizer report. Two of them
only became clean after the run that first exposed them, which is the point of running more than
one: a single clean seed proves very little.

A longer soak on a fourth seed (80,000 iterations) reached a use-after-free at iteration ~27,900,
and this one surfaces in CNA's own frame rather than upstream's:
`Fna3dCompiledEffect::ApplySamplers` reads `change.sampler_states`, and that pointer is what
`MOJOSHADER_effectBeginPass` publishes from `effect->current_pixl_raw` -- a raw pointer to whatever
shader object the backend had bound, which can belong to an effect that has since been destroyed.
CNA validates the count and the null-ness of what FNA3D hands back but cannot tell that the pointer
itself is stale. Reproduce with:

```sh
./cna_compiled_effect_fuzzer --campaign fx-corpus 28000 0x4658534F414B
```

**On FNA3D's SDL_GPU driver (SPIR-V profile)** the campaign stops much earlier, in the SPIR-V
emitter's own asserts (`spv_loadreg`, and others behind it). That emitter validates untrusted
shader bytecode with `assert()` throughout, so hardening it is a systematic pass of its own rather
than a handful of checks. Reproduce by dropping `FNA3D_FORCE_DRIVER=OpenGL`.

Neither is a CNA defect, but both are reachable through CNA's public API, which is why the porter
guide states the trust boundary plainly instead of promising safe failure.

So the honest statement is: **CNA's own compiled-effect code is clean under ASan, UBSan and LSan,
and the parser paths reached so far are hardened, but CNA cannot yet promise that arbitrary
hostile compiled-effect content fails safely.** Ship your own effects; do not load one a user
supplied. `plan_fx.md` FX-051 tracks continuing the campaign until it runs dry.

## Current status

What has run (2026-08-14) is a full ASan+UBSan+LSan pass over the 340 FX, Effect, XNB, capability
and content-reader tests on the SDL_GPU/Vulkan driver. All pass. AddressSanitizer reports nothing.
Every UBSan report and every leak record belongs to third-party code -- `SpirvPatchTable` alignment
and null-argument reports plus SPIR-V emitter leaks in the pinned MojoShader, one shift overflow in
FNA3D's pipeline cache, and 32 bytes per device inside `FNA3D_CreateDevice`. None is attributable
to CNA. They are recorded as upstream findings rather than presented as a clean third-party gate.
