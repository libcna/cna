# IEffectRenderer audit — IEFFECT-AUDIT-001

Measured 2026-11-01 against CNA `9d3807b77`, with the correction described below.
Scope: renderer-local implementation and call sites; no public API or common virtual interface
changes. The working tree was clean at entry. No stubs, dependencies or XNA behavior deviations
were introduced. FNA's local `src/Graphics/Effect/{Effect.cs,StockEffects/BasicEffect.cs}` was
consulted; `ShaderEffect` and `IEffectRenderer` are CNA-specific contracts, not FNA APIs.

## A. Confirmed problems

### Incorrect program for a texture flag — corrected

`modules/renderers/easygl/src/EasyGLRenderer.cpp`, `EasyGLEffectRenderer::BindTexture`:
`ShaderEffect::SetTexture` immediately forwards here, with no `Apply` prerequisite. Texture binding
and uniform-location lookup do not bind the owning program. Unlike `BindTextureCube` and every
scalar/vector/matrix/array setter, this function did not call `MakeProgramCurrent` before its
`glUniform4f` write. It could modify another program and then set `rtFlipVUploaded_ = true`,
suppressing the later correction even after the owner became current.

The extended `EasyGL_ShaderEffect_SpriteBatch_Uniform` test uses two independent programs with
`uRtFlipV` driving output RGB. With the second program current, the first receives a rendered
texture before its first `Apply`. Before the correction the first program drew black instead of
red, and the second drew red instead of green. Switching back to an uploaded texture also
corrupted the second program. The same test passes after the correction, on GLES and desktop GL.

The correction calls `MakeProgramCurrent` immediately before the existing uniform upload. There
is no extra bind for an unchanged flag or an absent uniform. Texture-unit handling is unchanged.

### Actual repeated lookups — confirmed, left unchanged

The configured dependency is local `../easy-gl`, version 0.1.1, commit
`b582bd85f646678d1de38473160defed4729f323`. Its `src/Program.cpp`:

- `uniform_location(const std::string&)` checks a nonzero handle, then calls
  `metagl::glGetUniformLocation` every time. It has no location cache, including for `-1`.
- Passing `const char*` implicitly constructs a string. Long names can allocate on every call.
- `use()` unconditionally calls `metagl::glUseProgram`.
- The configured meta-gl 0.4.1 (`795bcdcfe9e2c418ced189717d3699cbb0847cc0`),
  `src/Functions.cpp`, forwards both operations to the loaded driver functions without caching.

All ten EasyGL `SetUniform*` methods bind and look up on every call. `ArrayUniformLocation`
first tries the bare name; a miss constructs `name + "[0]"` and performs a second lookup, even
for a permanently missing uniform. `Vec3Array` and `Mat4Array` remain implemented and unchanged.
`uRtFlipV` and `uRtCubeFlipV` have value/upload flags, not location caches: an absent uniform
leaves the uploaded flag false, so every eligible texture bind retries its lookup. A present
uniform is queried when its per-unit flags change or have never been uploaded.

`BindCustomEffectMatrices` calls `Bind` plus three matrix setters for **every custom 3D draw**:
four program uses and three location queries, even if a matrix name is absent. Game uniform
updates add further calls; `ShaderEffect::OnApply` itself binds but does not resend arbitrary
user uniforms. The public setters immediately forward; there is no hidden dirty-value cache.

The stock 3D paths already resolve locations into `Prog3D::loc_*` in the lazy
`Ensure*Program` methods and upload by integer location in `BindDrawParams`.
This does **not** cover every built-in draw path: `EasyGLSpriteBatchRenderer::FlushBatch` still
looks up `projection`, and `ApplyChannelExpansion` looks up `uChannelMask` and `uChannelFill`
per flush, for stock and custom sprite programs. Stock sprite initialization resolves `texture1`
once. Thus repeated lookup is especially visible in custom 3D but is not exclusively custom.

## B. Rejected or unproven changes

### Why no EasyGL location cache was added

`GetProgram()` returns a mutable `easygl::Program&`. Its sole current in-repository caller,
`EasyGLSpriteBatchRenderer::FlushBatch`, binds the program and sets projection/channel uniforms;
it does not relink it. Nevertheless, an internal caller can retain that reference and later call
`link`, `load_binary`, `compile_from_sources`, move-assign another program or destroy/recreate it.

`Program::link()` and `load_binary()` do not change the native handle or creation generation.
`creation_generation()` tracks the **context**, not the program's executable/link generation.
Consequently, clearing only in `CompileProgram`, checking GL handle/context generation, or
clearing when `GetProgram` is called cannot detect a subsequent relink through a saved reference.
GL name reuse also prevents using a native handle as a unique lifetime identity.

A safe general cache needs control of these mutations or a reliable executable revision from the
dependency. Removing mutable access would alter the current renderer contract; disabling caching
forever once mutable access escapes would discard its benefit for custom SpriteBatch. Neither
change was justified for this bounded repair. No unsafe cache or global synchronization was added.
Negative entries, array aliases and flag locations therefore retain their current lookup behavior.

Lifecycle matters independently: `ShaderEffect` owns its renderer, `Clone()` compiles a separate
renderer, and disposal destroys it. EasyGL's custom effect is not a `RecoverableResource` and is
not registered in the resource registry. Context recreation rebuilds registered stock resources,
including SpriteBatch's program, but does not rebuild a live custom effect. `is_linked()` only
returns its stored boolean, not current-generation validity. This audit does not claim custom
shader recovery works. A future cache must not conceal or worsen that pre-existing limitation.
`CompileProgram` also reuses `program_` and does not reset the texture-flag upload booleans;
recompilation/replacement needs those values invalidated as well as locations. Public construction
and cloning create fresh renderers; repairing the general recompilation/recovery lifecycle is
outside this change.

### Program binding

Every scalar/vector/matrix/array setter and `BindTextureCube` already makes the program current.
`Bind()` also uses the program; `Unbind()` deliberately does nothing, because each draw binds its
own program. `BindTexture3D` updates texture state only and sends no program uniform. The missing
bind in `BindTexture` was the reproduced exception. A local last-program cache would be unsafe
without observing stock, sprite, compiled-effect, direct meta-gl and context-switch binds; it was
not added, and no per-setter `glGetIntegerv(GL_CURRENT_PROGRAM)` round trip was substituted.

### WebGPU

`WebGPUEffectRenderer::DeclareUniformBlockEXT` constructs `uniformOffsets_` once per declaration.
`WriteUniformBytes` searches it and copies into CPU staging memory; it performs no GPU lookup or
upload. A redeclaration clears both the name table and staging. Deferred lifetime transfer swaps
both into the retained effect. Names are owned by the map, so caller string lifetime is sufficient.

The nontransparent `unordered_map<std::string,int>::find(const char*)` constructs a temporary
string. The CPU probe below confirms one allocation for long names. Transparent lookup with a
single `string_view` hash and content equality can remove it without changing public signatures.
It is a reasonable future local candidate for applications with frequent long-name setters.
It is not shown to be a significant current frame cost: ordinary injected `World`, `View` and
`Projection` names fit SSO, while the descriptor-contract setters bypass this name map entirely.
The tiny-map short-name probe even slightly favored the original table (see D). No map rewrite
was made on this evidence alone.

Draw queueing snapshots legacy uniform bytes by value. Replay appends them into the uniform
arena; arena flushes batch GPU writes. Bind groups and pipelines already have caches. Staging
copies, queue snapshots, uploads and cache misses/pipeline creation require a full CPU profile
before calling any of them the bottleneck. This audit measures none of their GPU durations and
makes no new WebGPU throughput claim. Custom effects also block simulated device recovery while
alive, as documented in the renderer; a name-lookup change would not alter that contract.

### DirectX 9 and SDLGPU

`D3D9EffectRenderer::CompileProgram` parses compiler CTAB data once into vertex/pixel vectors.
`UploadEXT` pads the input to float4 registers, then linearly searches each vector by string
comparison on **every setter**, including misses. Matching constants issue the separate vertex
and pixel constant writes. Both tables are cleared on compilation. No string-key allocation or
compiler reflection occurs in this loop. The cost grows with table size, but no representative
large-table D3D9 profile was collected. A second name index was not justified, and D3D9 register
sets/counts must not become GL-style universal handles. No D3D9 production change or new Wine
performance claim was made.

`SdlGpuEffectRenderer` uses its established type-selected push-constant slots, arrays with std140
padding, and a short list of engine matrix-name comparisons. It has no GL-style name-location
lookup to cache. Array/texture snapshots are invalidated by their setters and reused by queued
draws. Its source intake compiles/remaps once, separate from draw replay. No interface rewrite.

`CompileProgram(const std::string&, const std::string&)` and value-returning `GetCompileError`
remain unchanged everywhere: existing string arguments bind to references without copying;
compiler diagnostics are outside ordinary draw loops.

## C. Changes and regression scope

- `modules/renderers/easygl/src/EasyGLRenderer.cpp`: one program activation before the existing
  `uRtFlipV` upload, with a comment explaining why it is necessary.
- `modules/renderers/easygl/examples/easygl_shader_effect_spritebatch_uniform_test.cpp`: extend
  the existing pixel test with two programs, pre-Apply texture assignment, rendered/uploaded row
  transitions, preservation of the other program, and an unchanged-texture repeat.
- This audit document; `handoff.md` links the remaining performance/lifecycle limitations.

No cache was introduced, so there is no new invalidation state. Existing program ownership,
uniform arrays, XNA APIs, CNA extensions and other renderer implementations are unchanged.

## D. Performance evidence and limits

The diagnostic probe used GCC 14.2, C++23 `-O2`, libstdc++, Linux x86_64, Radeon 780M/radeonsi,
Mesa 25.0.7, OpenGL ES 3.2. An EGL ES3 1×1 pbuffer ran through the private runner. Compilation,
context creation and linking happened before timing; no draw, swap, VSync or GPU wait was timed.
Each steady-state result is the median of seven batches of 300,000 calls, after 1,000 warmups.
This is a CPU microbenchmark on a shared machine, **not application FPS**.

| Operation | Median ns/call |
|---|---:|
| Repeated direct GL lookup, present short name | 30.77 |
| Repeated direct GL lookup, absent name | 42.65 |
| Repeated array bare-name lookup | 37.33 |
| Absent array, bare then `[0]` lookup | 105.53 |
| Repeated `glUseProgram` of the same program | 28.75 |
| `glUseProgram` + lookup + `glUniform1f` | 108.26 |
| `glUseProgram` + pre-resolved location + `glUniform1f` | 48.60 |

The last row is an experimental reference bound, **not an implemented cache or a before/after
CNA result**; an actual map hit also costs work. The probe called GLES entry points directly,
so it excludes easy-gl/meta-gl wrapper overhead and EasyGL's temporary strings. The one-shot
first-access sample was excluded as unreliable; no cold-access latency estimate is claimed.
This driver accepted `uArray` and `uArray[0]`
at the same location; no driver requiring a successful `[0]` fallback was available. The two-lookup
row tests a missing array, not a successful fallback. No cache behavior across programs or
relinks is claimed: no cache was implemented. The pixel regression does exercise two independent
programs with the same name.

For WebGPU's table candidate, separate noinline lookups on a two-entry map with the same keys and
values, a volatile result, and a counted global `operator new` gave:

| Probe | Original ns / allocations per call | Transparent ns / allocations per call |
|---|---:|---:|
| Short hit `World` | 11.33 / 0 | 12.76 / 0 |
| Long hit `uPreviousViewProjection` | 19.01 / 1 | 13.50 / 0 |
| Long missing name | 15.36 / 1 | 8.44 / 0 |

Both transparent key types hash through `std::hash<std::string_view>` and compare string contents;
no mixed pointer/content hashing. These are table operations, not measured WebGPU renderer calls.

The production correction adds **one `glUseProgram` per actual `uRtFlipV` upload**, none for absent
uniforms or unchanged flags. It fixes correctness and does not claim a speedup. Repeated location
lookups and scalar/matrix setter counts are unchanged. No real-application before/after FPS or
WebGL JavaScript-boundary measurement was taken.

## E. Verification

All builds used existing directories and at most four jobs; shared ccache configuration was kept.
Commands (run from the repository root):

```sh
cmake --build cmake-build-debug --target cna_test_easygl_shader_effect_spritebatch_uniform cna_test_easygl_shadereffect_3d cna_test_easygl_shader_effect_reflection cna_test_easygl_bloom_extract cna_test_easygl_bloom_gaussianblur --parallel 4
cmake --build cmake-build-debug --target cna_test_easygl_shadereffect_custom_vertex_layout cna_test_easygl_shadereffect_texturecube --parallel 4
tools/platform/run_gpu_tests_private.sh cmake-build-debug -R '^EasyGL_(ShaderEffect_|Bloom_|.*(RenderTarget.*(Orientation|Sampling)|Shader.*Array))' --output-on-failure -j4
cmake --build cmake-build-opengl33 --target cna_test_easygl_shader_effect_spritebatch_uniform cna_test_easygl_shadereffect_3d cna_test_easygl_shader_effect_reflection cna_test_easygl_bloom_gaussianblur --parallel 4
tools/platform/run_gpu_tests_private.sh cmake-build-opengl33 -R '^EasyGL_(ShaderEffect_(SpriteBatch_Uniform|3D|ReflectionContract)|Bloom_GaussianBlur)$' --output-on-failure -j4
git diff --check
```

- Original three core shader tests: **PASS, 3/3** before changing production code.
- Extended pixel regression with original production code: **FAIL as intended**, target black
  instead of red and other program overwritten. This establishes the bug before the fix.
- Final GLES selection: **PASS, 15/15**, including reflection, 3D, custom layout, cube/volume,
  sampler state, four bloom paths and render-target sampling orientation.
- Final desktop GL selection: **PASS, 4/4**. Both incremental builds: **PASS**.
- An intermediate run failed two stale example executables on the removed
  `ContentManager::TryReadAssetBytes` symbol. Rebuilding those two targets resolved both; no
  unrelated production edit was needed.
- The sandbox initially refused Weston's local socket and ccache writes. The authorized private
  runner/builds succeeded outside that restriction. No test used the owner's live desktop.
- No browser/WebGL, D3D9, WebGPU or SDLGPU live performance/regression run in this task; their
  production code was not changed. No real asynchronous context-loss experiment was run.

## F. Remaining profiling candidates

`sizeof(GpuDrawParams)` is **7,856 bytes** on this x86_64 configuration: bone palette 4,608 bytes,
vertex streams 1,792 bytes, plus the other initialized fields/matrices. `GraphicsDevice` creates
it per ordinary, indexed, instanced and `DrawUser*` draw; shared SpriteBatch also constructs it
for sprite validation/CPU paths, and compiled sprite replay has constructions per run/pass.
There is no per-triangle construction in the ordinary renderer path. Custom shaders mostly use
its custom-effect pointer/flags and stream/draw state; stock BasicEffect uses lighting/material/
texture state; SkinnedEffect fills the valid bone prefix; WebGPU/SDLGPU snapshot selected data for
replay rather than requiring all fields for every shader.

An `-O2` probe constructing the full default object and passing it to a noinline opaque consumer
cost **77.10 ns/object**, without heap allocation. Disassembly retains bulk zeroing in that escaped
case. An unused object can be eliminated; this does not imply elimination across the actual
virtual `FillGpuDrawParams` and renderer calls, where its address escapes. The probe is not a
profile of real draw initialization. Hot/cold splitting would change every consumer and the
cross-renderer deterministic-default contract, so the palette was neither made uninitialized
nor moved to a heap allocation. Profile aggregate initialization and copies in real scenes first.

`BasicEffect::FillGpuDrawParams` computes both cosines every call only when the punctual light's
kind is not `None`; the ordinary XNA path does neither. The whole light is retained between
`setPunctualLightEXT` calls, so repeated draws with unchanged point/spot lights repeat both
computations (including point lights, whose cones are not used). No representative enabled-light
profile demonstrates a material frame cost. No cosine cache, dirty bits or extra invalidation was
introduced.

Other concrete candidates are the stock SpriteBatch's three name queries per flush, missing
texture-flag queries, repeated custom-matrix program binds, and D3D9's two linear CTAB searches.
Prioritize them by an application profile, especially on WebGL where GL calls cross into
JavaScript. Safe custom-program location caching additionally requires a controlled mutation and
recovery contract; a driver handle/context counter alone is insufficient.
