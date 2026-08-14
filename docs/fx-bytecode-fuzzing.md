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
seeded corpus on every build — 512 mutations of the synthetic fixture and 128 of a stock binary —
through exactly the same construction/reflection/clone/apply path, and fails on an escaped
`std::bad_alloc`, a non-`std::exception` throw, or a crash. It keeps the seed, iteration, size and
mutation description in every failure trace, so a finding there is reproducible without the corpus
directory.

## Current status

The standalone replay shape and the deterministic in-build corpus are green. A sustained
coverage-guided campaign under ASan/UBSan is tracked by FX-051's remaining acceptance criteria and
by FX-052; note that LeakSanitizer cannot run under this project's managed environment (its ptrace
policy blocks the tracer), and the pinned upstream MojoShader has its own known UBSan findings in
float formatting and zero-length clone copies. Those are recorded rather than presented as a clean
third-party gate.
