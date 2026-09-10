# Vulkan graphics renderer

## Status of this document

**Complete as of 2026-09-10 (`VULKAN-480`, updated by `REMED-GFX-203`, `MOD-2222`–`MOD-2236`, `MOD-2240`–`MOD-2244`), and written after the re-audits it depends on**
(`VULKAN-470`–`VULKAN-474`) so that what it claims was checked rather than remembered. Every
section names the row that put it there and the test that keeps it true; a claim with no test named
beside it is not in here.

The header this replaced said the document was deliberately incomplete, and it was — `VULKAN-098`
created the file because its own acceptance needed somewhere to write the depth-range divergence
down, and later rows added a section each. What was missing until now is the part below: the
capability boundary in one table, the formats, and the environment the numbers came from.

Select the renderer with:

```bash
cmake -S . -B cmake-build-vulkan -DCNA_GRAPHICS_RENDERER=VULKAN -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-vulkan -j
```

---

## The capability boundary

`VULKAN-480`. **Test:** `Vulkan_CapabilitySnapshot` — every row below is one line of a snapshot a
CTest asserts, so this table cannot drift from the renderer without a test going red.

`Origin` says where the answer comes from: **fixed** is a property of this renderer, **device** is
asked of the physical device at runtime and may differ on other hardware than the two this project
measures on (§*Environment* below).

| Capability | Answer | Origin |
|---|---|---|
| 3D pipeline, depth/stencil, stencil independent of depth | supported | fixed |
| `SpriteBatch`, render targets, render-target cube, MRT | supported | MRT: device |
| MSAA (backbuffer and per-target) | supported | device |
| Anisotropic filtering | supported | device |
| Wire-frame rasterization | supported | fixed |
| Occlusion queries, precise pixel counts | supported | fixed |
| Instanced drawing | supported | fixed |
| Base-instance drawing | supported | fixed |
| Additive blending | supported | fixed |
| `Texture3D` storage **and** sampling | supported | fixed |
| Source-based `ShaderEffect` (SPIR-V) | supported | fixed |
| `ShaderEffect` **source execution** | unsupported | fixed |
| Multi-stream vertex input | supported | fixed |
| Compiled XNA `.fx` effects | **unsupported** here | fixed |
| SPIR-V compute shaders, storage/constant buffers and exact format-qualified storage images | supported | device |
| XNA `Texture2D` / `RenderTarget2D` compute sampling | supported | device |
| Indirect drawing, including non-zero base instance | supported | device |
| XNA `Texture2D` / `RenderTarget2D` compute-image binding | supported when that exact allocation is storage-capable | device |
| Float32 / Float16 `RenderTarget2D`, half-float linear filtering | supported | device |
| GPU timers | supported when the selected graphics queue exposes timestamps | device |
| Stock-effect shadow sampling | supported | fixed |
| Stock PBR image-based lighting | supported | fixed |

Two entries need their sentence rather than a cell:

- **`ShaderEffectSourceExecution` is `false` while `ShaderEffects` is `true`**, and that is not a
  contradiction. A `ShaderEffect` object is accepted and its program runs; what this renderer does
  not do is compile the *source* it is handed. It takes SPIR-V — see the `ShaderEffect` section
  below, which is the whole of that contract.
- **Compiled `.fx` effects are a build option, not an absence.** `CNA_VULKAN_COMPILED_EFFECTS`
  exists and `plans/plan_fx.md` owns it; the capability reads `unsupported` in the ordinary build
  this document describes.

### Limits

| Limit | Value here | Origin |
|---|---|---|
| `MaxTextureDimension` | 16384 | device |
| `MaxVertexStreams` | 16 | fixed — public streams of each input rate are packed into one immutable native snapshot |
| Compute group counts, local sizes and invocations | selected device's `VkPhysicalDeviceLimits` | device |
| `MaxStorageBufferBytes`, `MaxUniformBufferBytes` | exact selected-device range limits | device |
| Storage/uniform-buffer offset alignment | exact selected-device alignment limits | device |
| `MaxTextureArrayLayers` | exact sampled-2D-array image limit | device |
| `MaxSampledTexturesPerShaderStage` | min(15 implemented slots, native limits) | device |
| `MaxStorageImagesPerShaderStage` | min(per-stage, descriptor-set storage-image limits) | device |

### Compute, storage buffers and storage textures

`MOD-2229`/`MOD-2230`/`MOD-2241`–`MOD-2242`. **Tests:**
`Vulkan_ComputeStorageBuffer` and
`ComputeTest.PortableConstantBufferExecutesRetainsLifetimeAndObservesUpdates` — raw SPIR-V compute bytecode runs on
the renderer’s existing graphics/compute queue. Internal reflection builds descriptor set 0 from
the uniform-buffer, storage-buffer, format-qualified storage-image and combined `sampler2D`
bindings the module actually declares, including sparse slot numbers; there is no public native
descriptor-set API and no fixed four-slot layout.
Image reflection accepts only set-0, non-arrayed, single-sample `image2D` declarations and retains
their SPIR-V format plus `NonReadable`/`NonWritable` access contract. Named signed-int32 and float32
members of a SPIR-V push-constant `Block` map to `ComputeShader::setUniform`, with names, offsets,
four-byte alignment, types, range size and the device limit validated before native object
creation. Other push-constant and descriptor shapes are rejected precisely rather than accepted
and ignored. The permanent oracle proves all 256 elements of `C = A + B`, then uses sparse
output slot 7 plus `uCount`/`uScale` to change only 173 elements.

Each program owns one pipeline layout and a bounded cache of immutable descriptor snapshots (at
most 64 distinct binding combinations). Repeated bindings reuse a snapshot, while a binding change
cannot rewrite a set already named by deferred work. The pending dispatch, rather than the cache,
owns the referenced renderer records until command recording. Destruction of a referenced buffer or
image view removes every matching compute snapshot immediately and fence-retires its descriptor set
beside the native resource. This keeps submitted work valid without extending the public resource's
lifetime to that of the compute program, and prevents recycled Vulkan handle values from matching a
stale cached set. Resource/scalar state is captured at dispatch issue time and remaining native
objects are fence-retired at program destruction. Test-only live/cumulative counters prove that 33
repeated dispatches allocate nothing further and that the live counts return to their baseline.
Invalid or name-stripped bytecode, a missing or undeclared slot, an
unknown/mistyped scalar, an access mismatch and unsupported descriptor shapes
all fail explicitly instead of becoming a silent no-op or a validation error.

`MOD-2233` adds the sampled XNA-resource half without a second image type. A reflected set-zero
combined sampler accepts the binding number passed to `ComputeShader::bindTexture`; source-language
renderers still receive the named sampler uniform, while SPIR-V uses the descriptor binding
directly. Only an unqualified float32 `sampler2D` is admitted, matching the numeric class exposed
by CNA's XNA colour textures; integer samplers fail during reflection rather than aliasing an
incompatible view. Both ordinary `Texture2D` and `RenderTarget2D` retain their exact internal
image/view and format. The immutable dispatch record retains the sampled resource through command
recording, then its native image/view and descriptor set share frame-fence retirement. It transitions
the image to `SHADER_READ_ONLY_OPTIMAL` at consumption and pulls every pending render-target
producer into a synchronous result readback's dependency closure. All five tests pass on RADV and llvmpipe; the
four backend-neutral cases also pass on EasyGL. The Vulkan runs enable Khronos validation and
report no message.

`MOD-2230` adds the independent `Constant` role to `StorageBufferDescriptor` and the thin
`ConstantBufferT<T>` view over that same tracked resource. The wrapper admits only trivially-copyable
standard-layout values, rounds allocation to the published uniform-buffer alignment, checks the
full `maxUniformBufferRange` and zeroes upload padding. SPIR-V `Uniform` + `Block` declarations in
set zero become `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`; descriptor count limits, slot declaration,
device ownership and role are checked before dispatch. The immutable command records a read-only
resource use and retains the native allocation even if the first public wrapper is destroyed after
dispatch. The generated cross-backend test copies two different `vec4` values into an SSBO across
two dispatches and proves the first deferred dispatch survived rebinding and destruction on RADV
and llvmpipe with zero validation messages.

`StorageBufferDescriptor` declares storage, transfer-source/destination, indirect, vertex, index
and constant roles separately from CPU read/write intent. Vulkan translates only the requested
roles to `VkBufferUsageFlags`. A CPU-none buffer requests device-local memory and is never mapped; direct
`setBytes`/`getBytes` therefore refuse it. Exact range copies allow a CPU-writable transfer source
to initialize it and a CPU-readable transfer destination to retrieve it without exposing a native
buffer or mapping. The same oracle now passes 17/17 on RADV and llvmpipe: it checks the exact seven
native usage bits, mapped/unmapped allocation policy, a 17-byte unaligned ranged copy through a
GPU-only buffer, a 256-float compute result copied back through staging, overflow-safe refusals and
zero new validation messages. The size-only constructor retains its former implicit storage,
two-way transfer, indirect and CPU read/write behavior.

`MOD-2228` adds `ComputeShader::bindStorageTexture`; `MOD-2251` first implemented the legal
`bindImage(Texture2D&)` bridge for a `Color` `RenderTarget2D`; `MOD-2244` completes the device-
qualified image paths. One table maps exactly fifteen CNA formats to their Vulkan storage and
SPIR-V Image Format: `Color`, `NormalizedByte2`, `NormalizedByte4`, `Rgba1010102`, `Rg32`,
`Rgba64`, `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`,
`HdrBlendable`, `ByteEXT` and `UShortEXT`. `Alpha8` deliberately does not alias `R8`: an R-only
storage view cannot preserve Alpha8's alpha-channel semantics. Compressed, sRGB, BGRA and packed
16-bit formats likewise have no invented representation.

The five baseline SPIR-V formats work without an optional feature. Every other entry additionally
requires the selected device to support and CNA to enable `shaderStorageImageExtendedFormats`;
all entries still require `VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT` and a successful
`vkGetPhysicalDeviceImageFormatProperties` query for the complete requested usage. The immutable
dedicated-resource declaration controls storage read/write, sampling/filtering and transfer flags.
A mip-zero storage view and optional full-chain sampled view remain internal.

An ordinary Vulkan `Texture2D` now requests `VK_IMAGE_USAGE_STORAGE_BIT` only when its exact
sampled/transfer allocation also admits storage. Binding validates device ownership, reflected
SPIR-V format and access qualifier; after first image use its uploads join the same deferred order
as compute and sampling. Ineligible allocations refuse explicitly. Every exact storage-capable
`RenderTarget2D` uses the same zero-copy rule; its attachment image/view is reused without exposing
a layout or native handle. The permanent `Vulkan_ShaderEffect_BoundTexture` oracle covers ordinary
texture compute → upload → standard sampling, core and extended SNORM dedicated images, an extended
`Rgba64` render target, and a cross-target render → compute-copy → destination-readback dependency.

`MOD-2247` makes dispatch and storage-buffer copies immutable entries in the same monotonic order as
clear, SpriteBatch, XNA 3D, indirect draws, timestamps and presentation. A compute/copy boundary
closes the current render-pass segment, records outside both passes and leaves the same target bound
for following graphics. Routine dispatch/copy therefore add no one-time submit or queue/device wait;
requested CPU buffer/image readback remains their synchronous fallback. `MOD-2248` replaces the coarse command-wide
barriers with an internal logical-use tracker. Buffers retain accumulated CPU, transfer, compute
and indirect intent; storage images retain the same state independently for every mip. Write
hazards and layout changes emit buffer/image barriers at the consuming command, while compatible
read-after-read uses merge their stage/access scopes and emit nothing. The intent vocabulary also
names render-target, sampled, vertex and index uses so later bridges do not expose native Vulkan
state. `MOD-2249` adds storage-image uploads as immutable staging-backed records in that order:
upload → compute → upload performs no immediate submit or wait, and a requested image readback
records that complete dependency chain plus its copy in one synchronous submission. The staging
allocation retires behind the consuming frame fence. `MOD-2250` reflects readonly storage-buffer
descriptors from SPIR-V set 2 for both vertex and fragment stages, builds an exact immutable draw
set, and retains every buffer in the accepted SpriteBatch/3D snapshot. The logical tracker emits a
compute-write to vertex/fragment-shader-read transition immediately before the consuming render
pass; an indirect command from the same dispatch independently transitions to indirect fetch. The
11-leg native oracle moves one GPU-authored instance left and right, changes its fragment result,
then writes a zero instance count, without CPU buffer readback or a routine submit. It reports the
exact twelve steady-state hazards, survives disposing both public buffers after the final draw was
accepted, and emits zero validation messages on RADV and llvmpipe. `MOD-2251` shares render-target
colour state with compute, transitions both render-target and dedicated storage images at the
consuming segment without an eager submit, and proves render → compute → sampled render through a
channel-shuffling image-load/store oracle. `MOD-2253` folds target readback's producer closure,
transitions and copy into one submission/fence and removes queue/device idle waits. Optional
format gates and ordinary/XNA storage-image bridges are completed by `MOD-2244`; `MOD-2233`
extends the readback closure to sampled render-target inputs as well. Reading a compute result can
therefore never run its dispatch before a sampled target's pending producer pass.

### Indirect drawing

`MOD-2245`. **Test:** `Vulkan_IndirectDraw` — both canonical command layouts are consumed directly
by `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect`; the renderer never reads the count or offsets
back to the CPU. Support is device-derived: CNA enables `drawIndirectFirstInstance` when the
selected device offers it and reports indirect drawing only then. This matters because both CNA
layouts expose `BaseInstance`; claiming a device that accepted only zero would make one documented
field silently conditional.

Vulkan's ordinary draws snapshot only the requested geometry window. An indirect command can be
written by compute after the draw is issued, so its future range cannot be known on the CPU. The
deferred path instead snapshots the complete remaining per-vertex and index ranges, plus every
logical per-instance record, before enqueue. Each copy is checked against the existing 4 MiB / 1
MiB / 1 MiB per-frame arenas before allocation. The native command then preserves the argument
byte offset, binding offset, `FirstVertex`, `FirstIndex`, `BaseVertex`, `BaseInstance`, instance
frequency and combined multi-stream layout independently.

The queued draw owns a share of the argument renderer record. Disposing its public
`StorageBuffer` after enqueue therefore remains logically immediate but cannot invalidate the
unrecorded `VkBuffer`; once the command has been recorded, the allocation enters the existing
frame-generation retirement queue and is destroyed only after the consuming fence. A single
automatic host/transfer/compute-write → indirect-command-read dependency is recorded before use.
Compute and copies now share the consuming frame submission (`MOD-2247`), so callers need no manual
barrier for correctness.

The eight-leg oracle uses non-zero command offsets for both routes, separates every geometry offset,
selects instance one, disposes an accepted argument before render-target flush, and has SPIR-V
compute write a command that is drawn without readback, then proves SpriteBatch → compute → GPU copy
→ XNA 3D → indirect → present with one frame submit and no new one-time command. It passes **8/8 on
RADV and llvmpipe** with zero new validation messages. Wireframe renders filled because the CPU cannot rebuild a line list
without the hidden count; compiled FX is refused because its current pipeline route likewise needs
that count.

### Multi-stream vertex input

`REMED-GFX-203`. Vulkan accepts CNA's full 16-slot `VertexBufferBinding` surface. Because this
renderer already copies vertex bytes into a host-visible arena when the draw is queued, it combines
all public per-vertex records into one immutable interleaved snapshot and does the same separately
for several per-instance streams. That preserves the existing one-native-binding-per-input-rate
pipeline model and adds neither an arena buffer nor a submit.

The copy applies every binding's `VertexOffset`, each per-instance `InstanceFrequency`, and the
draw's `vertexStart` or `baseVertex`. Indexed draws scan the selected 16- or 32-bit index range so
that `startIndex`, a declared `minVertexIndex` that differs from the actual minimum, and non-zero
`baseVertex` retain their XNA meanings. Declarations are combined before stock-family or custom
`ShaderEffect` layout selection, and the resulting bytes and declarations are owned by the queued
draw rather than by the source buffers.

**Tests:** the same `OrdinaryDrawMultiStreamTest` and `InstancedDrawMultiStreamTest` sources pass
**48/48 on Vulkan and 48/48 on EasyGL**. They cover non-indexed and indexed draws, both index
widths, offsets, deferred lifetime, mixed per-instance frequencies and invalid ranges. The Vulkan
`ShaderEffect` 3D test passes **8/8**, including split declarations on both ordinary routes and a
custom instanced draw, with no validation message.

### Surface formats

`MOD-2222` gives all 27 current `SurfaceFormat` values an explicit semantic `VkFormat` mapping and
classifies all thirteen detailed usage bits from the selected physical device. The mapping is not
itself a support promise. Nine formats currently have faithful `Texture2D` allocation paths:
`Color`, `Bgr565`, `Bgra5551`, `Bgra4444`, `NormalizedByte2`, `NormalizedByte4`, `Dxt1`, `Dxt3` and
`Dxt5`. The remaining formats are mapped so Vulkan properties can be inspected, but texture
storage/sampling/transfer flags stay false until the CNA allocation, transfer and view semantics
exist. The older construction verdict still **defers** such formats to `Texture::ValidateFormat`
rather than pretending the renderer attempted them (`VULKAN-170`).

DXT1/3/5 use native BC1_RGBA/BC2/BC3 storage for both `Texture2D` (`VULKAN-172`) and
`TextureCube` (`VULKAN-240`) when the device exposes `textureCompressionBC`. The cube route is
measured from exact block upload through decompressed readback and `EnvironmentMapEffect` sampling
by `Vulkan_DxtTextureCube`; the identical `EasyGL_DxtTextureCube` source is the parity control.
DDS and XNB loaders preserve the native blocks and complete mip chains on those devices
(`VULKAN-241`), while a device without BC retains the shared decode-to-`Color` fallback.

For a format it allocates, the verdict comes from the device's `VkFormatProperties` **and** an
exact `vkGetPhysicalDeviceImageFormatProperties` query for the usage combination CNA creates. The
detailed profile then intersects sampling, linear filtering, transfer, mip and attachment facts
with those implemented paths. Storage read/write is published for the fifteen exact
format-qualified paths implemented by `MOD-2244`, intersected with the enabled extended-format
feature and complete native image-usage facts; storage atomics remain unsupported.
Timestamp period is published in integer picoseconds only when the selected graphics queue exposes
timestamp bits and the period is positive; llvmpipe reports 1000 ps and RADV 10019 ps in the
permanent `MOD-2246` runs. Texture-array layers publish the sampled 2D-array image limit
implemented by `MOD-2226`. `Vulkan_FormatLimitQueries`
compares every answer with the raw properties at runtime and keeps the remaining native-only
negative controls. Since `MOD-2224`, it also attempts the
public base, full-mip-chain and highest-supported-MSAA `RenderTarget2D` constructor for all 27
formats at 7×5. The capability snapshot, public format predicate and constructor must agree; the
llvmpipe reference run creates the same nine formats and refuses the same eighteen on all three
paths, with zero validation messages.

`MOD-2223` adds a separate exact `RenderTarget2D` allocation table: `Color`, `Rgba64`, `Single`,
`Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4` and `HdrBlendable`. The table feeds
classification and allocation together, and each native format is carried through image/view
creation, format-aware render-pass and pipeline cache keys, mixed-format MRT, sampling, mip
generation and byte-width-correct readback. An MSAA request above one is admitted only when the
exact colour format and selected depth format share a supported sample count; unsupported pairs
throw instead of becoming single-sampled or `Color`. `Vulkan_FloatRenderTarget` proves the live
clear/draw/sample/readback path, values above 1.0, mixed MRT, MSAA resolve and mips. Its MSAA and
mip pixel legs use odd dimensions, including the complete `7×5 → 3×2 → 1×1` chain, so
the constructor contract is backed by rendered contents rather than allocation alone.

`MOD-2234` carries that same table into `CreateRenderTargetCubeEXT`. The factory additionally asks
the selected physical device about the exact cube-compatible six-layer image, complete mip chain
and requested colour/depth/MSAA combination before allocation. Image and face views, render passes,
framebuffers, resolve storage and pipeline metadata all retain the requested format; `Color` cubes
use canonical `R8G8B8A8_UNORM` storage rather than inheriting the swapchain's possibly BGRA format.
`Vulkan_FloatRenderTarget` constructs every advertised format and reads six independently rendered
`HdrBlendable` faces back as exact RGBA16F bytes. Public `TextureCube::GetData` remains the existing
RGBA8-only XNA route, so the wider-format assertion uses an internal exact-byte diagnostic rather
than pretending HDR data is `Color`.

**Colour transfer** — `GetData`/`SetData` shaped as `Color` — refuses
`NormalizedByte4` and `NormalizedByte2` explicitly even though the framework's four-byte rule would
admit the first: its bytes are signed and sample to [-1, 1], so a `Color`-shaped transfer would read
the wrong values while looking well-formed (`VULKAN-174`).

### Environment these numbers came from

Measured on the machine `plans/plan_vulkan.md` §7.1 describes: Vulkan instance API **1.4.309**,
`VK_LAYER_KHRONOS_validation` **1.4.309** present and **on** for the whole test suite
(`VULKAN-393`/`VULKAN-408` fail any CTest whose output contains a `[Vulkan Validation]` line), and
two devices — **AMD Radeon 780M (RADV PHOENIX)**, Mesa 25.0.7, conformance 1.4.0.0, and
**llvmpipe (LLVM 19.1.7)**, conformance 1.3.1.1. The `device`-origin rows above were read from
llvmpipe unless stated; where the two devices differ the difference is in the plan, not here — the
one that matters for a reader is MSAA, where llvmpipe offers up to 4× and RADV up to 8×.

### Device discovery and the modern-command queue

`MOD-2240`. The instance requests Vulkan 1.1, and the selected device is queried once through
`vkGetPhysicalDeviceProperties2` and `vkGetPhysicalDeviceFeatures2`. CNA keeps the supported and
enabled feature records separate. A native bit is enabled only when a complete renderer path uses
it: today those are `fillModeNonSolid`, `samplerAnisotropy`, `independentBlend`,
`occlusionQueryPrecise`, `textureCompressionBC` and `drawIndirectFirstInstance`, each conditional
on device support. The optional `VK_EXT_4444_formats` feature is queried and enabled through the
features2 `pNext` chain
only when the extension is advertised and its `formatA4R4G4B4` feature is true. A device missing
any optional feature still starts and the corresponding public capability under-claims or refuses.

Modern commands use the existing graphics submission queue in their first implementation. The
queue family flags are recorded at device selection; compute is advertised only when this queue
has `VK_QUEUE_COMPUTE_BIT`, the required storage-buffer slots exist, and the device publishes
usable compute limits. There is no second queue lifecycle and no asynchronous-compute promise.
This keeps compute, copy and graphics on the one queue that later synchronization rows bring into
the same deferred public-call ordering domain.

`MOD-2246` adds timestamps without adding a submission domain. Each `GpuTimer` owns one two-slot
query pool, recycles it across samples, and places reset/begin/end commands into the same monotonic
order as clear, SpriteBatch and 3D work. `poll()` first checks the range's frame fence with
`vkGetFenceStatus`, then uses explicit query availability and caches the first complete pair. This
also prevents a new pool's pre-reset undefined payload from becoming a false first sample. It
never requests a blocking result and timer creation/use/destruction adds no
queue/device-wide idle. A submitted pool is retired on the consuming frame fence.

The same row discovers optional `VK_EXT_debug_utils` independently of validation so RenderDoc-style
markers and per-pass regions remain useful in ordinary builds. Each recorded render-pass segment
has one balanced region, `SetStringMarkerEXT` inserts an in-stream marker, and validation/debug
messages are classified into `CNA::Logger`'s GPU category exactly once while the renderer keeps its
diagnostic capture. If the extension is absent, these debug operations remain honest no-ops and
the timestamp path is unaffected.

### Portable ordering and lifetime contract

`MOD-2202` is fixed by `docs/adr/0001-modern-gpu-ordering-lifetime.md`. Vulkan does not define a
separate application-visible ordering model: XNA work, modern copies/compute/indirect work and
presentation form one public-call order on the selected graphics/compute queue. The renderer, not
the application, owns pipeline barriers, image layouts, render-pass breaks and fence values.

The existing XNA Vulkan paths snapshot mutable draw state, retain deferred render-target
destinations independently of their public wrappers, evict descriptor entries that mention dying
views and retire images, buffers, views, pipelines, layouts, descriptors and queries only after the
consuming frame fence. `MOD-2252` applies and verifies the same path for storage buffers, compute
programs, dedicated, ordinary-texture and render-target storage images, compute-sampled XNA
textures/targets, texture arrays and timestamp pools. Commands
retain internal records through command recording; resize preserves their handles; terminal device
teardown releases handles and disconnects externally retained records before destroying `VkDevice`.
Applications neither receive the native device nor wait it idle before disposal.

`MOD-2225` supplies the public `Texture2DArray` facade and immutable descriptor. `MOD-2226` adds
strict layer/mip/rectangle transfers plus `ShaderEffect` binding, and Vulkan now reports its live
sampled-array layer limit, allocates every declared layer/mip, exposes only a full
`VK_IMAGE_VIEW_TYPE_2D_ARRAY` internally, transfers exact subresources and samples arrays through
descriptor set 1 bindings 16..18. Together with the twelve pre-existing set-1 samplers and set
0's sprite sampler, that keeps the fragment-stage layout at Vulkan's guaranteed limit of sixteen.
Unbound array slots use a dimensional 2D-array white view rather than the incompatible ordinary 2D
filler. The 7x5/two-layer native oracle covers distinct upload, readback and shader results with no
validation message. `MOD-2243` independently verifies all 27 formats against five usage masks,
exact native image/view identity, over-limit refusal, rebind retirement and a record that outlives
explicit `GraphicsDevice` teardown. It passes 11/11 on both RADV and llvmpipe with validation.

`MOD-2227`/`MOD-2228` similarly keep `StorageTexture2D` renderer-neutral and tracked. Compute binds
retain its shared internal record, SPIR-V reflection validates the exact descriptor slot, format and
access qualifier, and the sampled `ShaderEffect` route reuses the ordinary four `sampler2D` slots
rather than adding descriptors beyond the established fragment-stage ceiling. Its storage and
sampled views are evicted/retired with the owning image, and renderer teardown disconnects surviving
records before destroying the Vulkan device. `Vulkan_ModernResourceLifetime` verifies the complete
submit/resize/device-teardown matrix 10/10 on both RADV and llvmpipe with validation enabled.

`MOD-2253` also replaces every one-time `vkQueueWaitIdle` with a short-lived fence attached to the
specific submission. `Vulkan_RtFlushStallCost` pins off-screen readback at zero device waits, zero
extra one-time submissions and exactly one dependency-closure fence. The 2,048-frame
`Vulkan_ModernAllocatorStress` gate adds 32 resize requests and per-frame staging/buffer/timer churn:
descriptor and compute-pipeline counts remain constant, live staging peaks at 4, pending retirement
at 15 buckets/29 handles, then every transient count drains to zero on RADV and llvmpipe.

`MOD-2254` closes the recoverable swapchain-error gate. `Vulkan_ModernRecovery` injects an acquire
`VK_ERROR_OUT_OF_DATE_KHR` before image hand-off and proves its two queued modern commands are not
submitted or discarded; the next frame executes them to the exact result. It also runs the real
native present before replacing a successful handled result with `VK_SUBOPTIMAL_KHR` or
`VK_ERROR_OUT_OF_DATE_KHR`, so test injection cannot strand a semaphore or acquired image. Each
result takes the production recreation branch, preserves image/view/pipeline/descriptor-pool
identities and remains validation-clean on RADV and llvmpipe. Recovery, the complete 10-leg modern
lifetime matrix and the 2,048-frame allocator stress also pass under the project ASan+UBSan
configuration on both devices; leak detection was disabled, so that run is memory/UB safety
evidence rather than external-stack leak evidence.

Normal `Dispose()` must never add a queue/device idle. Device teardown, loss/recovery and a
requested synchronous readback are the only relevant completion boundaries, with readback waiting
on its narrow submission fence rather than the whole device.

**Test:** `Vulkan_ModernFeatureDiscovery` walks all 55 core feature bits and requires the enabled
record to be exactly the subset consumed by implemented CNA paths. It also verifies the property
snapshot and graphics queue flag. Since `MOD-2241`, its former negative control is positive and
device-dependent: capability, queue/storage availability and every published compute limit must
agree exactly.

`MOD-2222` also populates the detailed numeric profile from that same property snapshot. Texture
dimension, buffer ranges and alignments, compute SSBO bindings, sampled descriptors, vertex inputs
and colour attachments are clamped to the smaller of the native ceiling and CNA's implemented
public path. The permanent `Vulkan_FormatLimitQueries` test compares these values directly with
`VkPhysicalDeviceLimits`, then cross-checks the format snapshot against real construction, so
changing the renderer name or advertising an unusable format cannot produce a passing hard-coded
answer.

---

## Clip-space depth range: `[0, 1]`, and EasyGL differs

`VULKAN-098`, finding F-19. **Test:** `Vulkan_DepthRangeContract`
(`modules/renderers/vulkan/examples/vulkan_depth_range_contract_test.cpp`).

XNA is a Direct3D 9 programming model, and D3D9 maps clip space to depth over **`[0, 1]`** — a
vertex at `z = 0` is on the **near** plane, `z = 1` on the far plane. Vulkan's native range is the
same, so this renderer matches XNA without doing anything.

**EasyGL, the reference renderer, does not.** It leaves OpenGL's `[-1, 1]` clip depth in place, so
the same `z = 0` vertex lands at depth **0.5**. Measured on 2026-09-05 by compiling the identical
test source against both configurations:

| | Vulkan | EasyGL |
|---|---|---|
| 5×4 truth table (`z` × cleared depth, `LessEqual`) | **20/20** cells | 15/20 |
| `z = 0` survives a depth cleared to `0.4` | drawn | **rejected** |
| `z = -0.5` | **clipped** | drawn, as an ordinary depth of 0.25 |

Ordering is monotonic under both ranges, which is why this went unnoticed for so long: a test that
only asks "does `0.25` occlude `0.75`" passes on either. It takes an absolute comparison — a cleared
depth the two ranges fall on opposite sides of — to see it.

### What it costs, and who owns the other half

Two consequences worth knowing before writing anything that depends on depth:

- **An XNA scene loses half its depth precision on EasyGL.** Content authored for `[0, 1]` uses only
  the upper half of `[-1, 1]` there, so the depth buffer resolves half as finely as XNA's would.
- **A shared fixture cannot encode a depth value and be run on both.** It can compare depths, but
  the moment it asserts one, it is asserting a renderer-specific number. `Vulkan_DepthRangeContract`
  is therefore registered for Vulkan only, on purpose.

This renderer is the one that is right, so `plans/plan_vulkan.md` classifies it `VULKAN_STRONGER`
and changes nothing here. **The EasyGL side is owned by
[`plans/plan_graphics.md`](../plans/plan_graphics.md)** — its Phase 71, "EasyGL final gap closure" —
and it needs a row of its own there. `VULKAN-098` deliberately does not open one on another plan's
behalf; it names the owner so the divergence is not left implicitly nobody's.

---

## Cross-renderer conformance: what must match, what may differ, and how it is judged

`plans/plan_vulkan.md` `VULKAN-437`. **Tools:** `scripts/compare-easygl-vulkan-diagnostic.sh`
(`VULKAN-430`/`VULKAN-431`) and the 17 golden CTests `VULKAN-432`–`VULKAN-436` register.

This section exists because "the renderers agree" is not a measurement until someone says what
agreement means. Everything below is a rule this renderer is actually held to today, with the
number that was measured against it.

### What must match exactly

**Golden images.** The golden scenes are compared against the *same* PNGs under
`modules/renderers/easygl/examples/golden/` that EasyGL is compared against — the stock effects,
`EnvironmentMapEffect` and `SkinnedEffect`, the blend/depth-write/cull state goldens, the 2D
rotation and linear-filter goldens, the two PBR families and the golden-harness smoke tests. There
is no Vulkan-specific golden and there must not be one: a golden that each renderer keeps its own
copy of has stopped being a golden.

*Corrected 2026-09-07 by `VULKAN-014`, which counted them.* This paragraph used to say "seventeen
scenes" and "exact comparisons". Thirteen CTests whose name ends in `_Golden` are registered on
this renderer, and the comparison is **not** exact: `CompareGoldenImage` takes a per-channel
tolerance and every one of the thirteen passes a non-zero one except the harness smoke test. The
policy that actually governs them is the next section.

**The 2D corpus.** `cross_renderer_2d_corpus.cpp` is built once per renderer and its dumps are
compared byte for byte. Measured 2026-09-06: **max diff 0** between EasyGL and Vulkan. That is the
expected result, not a happy one — the corpus is deliberately built out of constructs where two
conforming 2D renderers have no licence to differ.

### What may differ, and why

**Rasterized coverage of a diagonal edge, by at most a channel step.** The 3D diagnostic scene
measured **max diff 1** at one pixel. Two hardware pipelines evaluating the same triangle's edge
and the same interpolation in a different order of floating-point operations will not always agree
on the last bit of an 8-bit channel. This is the only difference either tool has ever reported.

**What is excluded from the corpus, and stays excluded** (the list is in
`cross_renderer_2d_corpus.cpp` itself): linear filtering of a magnified sprite, mip selection and
mip-linear blending, anisotropy, MSAA, additive blending, and rotation by a non-right angle. Each
is a construct where two conforming renderers may legitimately disagree pixel-for-pixel. Including
one would force a tolerance wide enough to hide a real regression, which is the opposite of what a
conformance corpus is for.

**What is *not* on that list, and must never be added to it:** anything this renderer gets wrong.
`VULKAN-260` is the case in point — `EnvironmentMapEffect`'s Fresnel term was computed per fragment
here instead of per vertex, and the honest resolution was to fix the shader, not to declare Fresnel
a legitimate difference.

### The golden-image policy

`VULKAN-014`. **What is measured, as of 2026-09-07:** thirteen `*_Golden` CTests on this renderer,
**all thirteen** comparing against an EasyGL-authored PNG, **zero** Vulkan-specific goldens. Their
tolerances, per channel:

| Tolerance | Scenes | What the number is paying for |
|---|---|---|
| 0 | `GoldenImage_Smoke` | flat, unblended colour: nothing may differ |
| 8 | `BasicEffect`, `DualTextureEffect` | texture sampling and a single interpolated term |
| 10 | `BlendState_Additive`, `TextureFilter_Linear` | one blend or one filter step per pixel |
| 20–35 | `AlphaTestEffect`, `EnvironmentMapEffect`, `PbrEffect`, `SkinnedPbrEffect` | lit and PBR shading: several floating-point terms per pixel, evaluated in an order neither renderer promises |
| 30–40 | `RasterizerState_CullMode`, `SkinnedEffect` | shading plus geometry that lands on the same pixels by a different route |
| 60 | `DepthStencilState_WriteEnable`, `SpriteBatch_Rotation` | a rasterized *edge* moves: coverage of a diagonal is where two conforming pipelines are least alike |

**The rule that makes this safe is not the ladder, it is where the number lives.** The tolerance is
written in the **shared test source**, not in either renderer's registration — so a tolerance can
never be widened *for Vulkan*. Widening it widens EasyGL's own gate by exactly the same amount, in
the same commit, visible in the same diff. That is why "share the golden" and "share the tolerance"
are one rule rather than two.

**When a Vulkan-specific golden would be legitimate:** only for a difference this document already
names as a *semantic* divergence — the clip-space depth range, or a pixel-centre convention — and
the row that authors one must name that divergence and say why the scene cannot be written to avoid
it. A numeric difference is never a reason; it is what the tolerance is for. Today the count is
zero and the honest expectation is that it stays zero.

**Non-goal:** bit-identical pixels for floating-point lighting. Two pipelines evaluating the same
lighting term in a different order will differ in the last bits, and a policy that forbade it would
be a policy against having the test.

**The open half**, which `VULKAN-185` owns: nothing measures how much of each tolerance is actually
being *used*. The diagnostic script proves its own tolerance is idle by re-running at `--tolerance
2`; a golden CTest prints only pass or fail, so a scene that quietly drifted from a max difference
of 2 to 59 under a tolerance of 60 would still say `[PASS]`.

### How a tolerance is chosen

`cna_diag_compare` takes a per-channel tolerance. The gate uses **40**, and that number is
**inherited from the existing Software↔EasyGL comparison rather than picked for this pair**, so a
difference that matters here is one that would have mattered there.

The rule that matters more than the value: **a tolerance must not be doing any work.** The same run
that passes at 40 also passes at `--tolerance 2`, and the script records the measured maxima (1 and
0) beside the tolerance for exactly that reason. If a future run needs the tolerance raised, the
answer is an entry in the script's expected-difference list with its reason — never a wider number.
The list is currently empty.

### The standing rule: a conformance test must be able to fail

Every gate here carries its own proof that it can fail:

- `compare-easygl-vulkan-diagnostic.sh --perturb` corrupts two pixels of the Vulkan dump before
  comparing. Both pairs then report `max diff 255 … FAIL`, and the script **exits 3 if the
  perturbation is not detected** — a gate that cannot fail is treated as a failure of the gate.
- Every fix in this renderer's plan records a mutation that turns its test red, and several rows
  exist because the first mutation attempted **passed**: `VULKAN-172` (a mis-sized DXT block is
  invisible to pixels — it is a buffer over-read, so the assertion moved to where the two byte
  counts meet), `VULKAN-095` (a stale MSAA pipeline is invisible to a test that never draws through
  the MSAA path), `VULKAN-177` (a stale descriptor-set cache entry is invisible to both the pixels
  and the validation layer).
- Where a defect is reported at teardown and no in-process assertion can see it — `VULKAN-405`'s
  leaked framebuffer, `VULKAN-407`'s ten leaked handles — the discriminator is `VULKAN-393`'s
  output gate, which fails a CTest whose output contains a `[Vulkan Validation]` line. It covers
  every CTest in the configuration that can create a `VkDevice`.

### Where EasyGL is not the authority

EasyGL is the reference for *coverage and maturity*, never for semantics. Three rows in this plan
changed a shared test rather than this renderer, because the test encoded EasyGL's architecture as
if it were the contract: `VULKAN-095` (EasyGL cannot change MSAA post-construction; this renderer
can), `VULKAN-335` (`PresentationParameters` stores what the device *applied*, and EasyGL never
substitutes so the two look alike there), and `VULKAN-173`/`VULKAN-172`, which replaced
`#if defined(CNA_GL_PROFILE_*)` guards with the renderer's own `ClassifySurfaceFormatEXT` verdict
in three shared sources. And one row changed this renderer because EasyGL was right and it was not:
`VULKAN-260`.

---

## `ShaderEffect` takes SPIR-V, and the 37 GLSL tests are a divergence rather than a gap

`VULKAN-256`, closing `VULKAN-250`–`VULKAN-255`.
**Tests:** `Vulkan_ShaderDialectContract`, `Vulkan_ShaderEffect_SpirV`,
`Vulkan_ShaderEffect_BoundTexture`, `Vulkan_ShaderEffect_UniformArrays`, `Vulkan_ShaderEffect_3D`,
`Vulkan_Texture3DAddressW`

`ShaderEffect` takes a **renderer-specific payload** by contract, and this renderer's is compiled
**SPIR-V**. Since `VULKAN-264`, `GraphicsDevice::GetShaderDialectEXT()` reports `SpirV`, explicitly
distinguishing this bytecode intake from IGL's Vulkan backend, which reports `GlslVulkan` and
compiles source. Hand native CNA Vulkan GLSL text and the effect is refused with a message that says
so; the check is the SPIR-V magic word, not the payload's length. That distinction is not pedantry:
until `VULKAN-256`
the refusal was a *length* check, so GLSL whose byte count happened to be a multiple of four went
to `vkCreateShaderModule` — which **accepted it** on llvmpipe, leaving an effect that reported
itself valid and drew from text.

EasyGL's suite carries 37 tests whose payload is GLSL (`*_Shader`, `Bloom_*`, `ShaderEffect_*`).
They are **not** ported, and that is a decision rather than a backlog: the shader in each is the
part that cannot cross renderers, and making one source serve both is `plans/plan_csl.md`'s whole
purpose. What this renderer owes instead is an equally capable custom-effect surface, tested with
SPIR-V payloads that exercise the same capabilities:

| What the EasyGL family needs | Where this renderer proves it |
|---|---|
| A uniform reaches the shader and decides pixels | `Vulkan_ShaderEffect_SpirV` — two values, two colours |
| A sprite drawn through a custom shader | the same test, and every bound-texture leg |
| A texture bound explicitly, not the one the draw supplied | `Vulkan_ShaderEffect_BoundTexture` A/B — descriptor set 1, binding = unit |
| A `TextureCube` and a `Texture3D` in a custom shader | the same test, legs E and F — bindings 4+unit and 8+unit |
| Volume sampling that honours `SamplerState` | `Vulkan_Texture3DAddressW` — wrap vs clamp outside `[0,1]` |
| Array uniforms: kernels, palettes, coefficient sets | `Vulkan_ShaderEffect_UniformArrays` — four uniform-buffer ranges, 72 elements each |
| A 3D draw with the caller's own vertex layout | `Vulkan_ShaderEffect_3D` — a 48-byte five-element declaration |
| Multiple render targets from a custom shader | `Vulkan_MRT_MsaaResolve` — a four-output `ShaderEffect` |

One capability in that family is **not** available here and says so rather than approximating: the
shader source itself is never translated (`plans/plan_csl.md`). An instanced draw with a custom
effect was refused by name until `VULKAN-168`; it works now. `Vulkan_ShaderEffect_3D` covers the
ordinary and base-instance routes, reflected removal of unused declaration fields, and refusal when
a shader-required location is genuinely missing.

### Writing a `ShaderEffect` for this renderer

- **Ordinary named values** live in one 128-byte push-constant block with fixed slots. SPIR-V
  resource and vertex-input reflection exist, but the setter's *type* still selects this legacy
  value slot the way a name would elsewhere.
  `vec2 vpSize` at bytes 0–7, `mat4 uMatrix` at 16–79 (`SetUniformMat4`), `vec4 uColor` at 80–95
  (`SetUniformVec4`/`Vec3`/`Vec2`), eight floats at 96–127 (`SetUniformFloat`/`SetUniformInt`).
- **Textures** are descriptor set 1: `sampler2D` at bindings 0–3, `samplerCube` at 4–7,
  `sampler3D` at 8–11, by `SetTexture` unit. The `SpriteBatch` draw's own texture stays at set 0
  binding 0. A unit nothing was bound to reads the renderer's white 1×1 rather than undefined
  memory.
- **Sampler state** for unit *u* is `GraphicsDevice.SamplerStates[u]`, as in XNA — so two bound
  textures in one batch can be filtered and addressed differently. Unit 0 is the exception, and it
  is a deliberate one: a `SpriteBatch` owns slot 0 and publishes the state passed to `Begin()`
  there, so unit 0 always carries the batch's own sampler no matter what `SamplerStates[0]` held
  when the batch began. Since `VULKAN-194` the batch also *assigns* its sampler into
  `SamplerStates[0]` at the same publication points, matching XNA, so the value the collection
  holds after `End()` is the batch's and the **next 3D draw** samples with it.

### Image-based lighting (`MOD-2235`, 2026-09-10)

`PbrEffect` and `SkinnedPbrEffect` consume the same `ImageBasedLightEXT` bundle as EasyGL. A valid
bundle binds the irradiance cube, prefiltered-specular cube and BRDF LUT after the seven ordinary
PBR material images. The final `vec4` of the existing 512-byte PBR parameter stride carries the
enabled flag, prefiltered mip count and intensity, so this addition does not enlarge or misalign
the dynamic uniform-buffer records.

Both stock fragment programs execute the reference split sum: irradiance diffuse plus a
roughness-selected `textureLod` sample multiplied by the BRDF lookup, with ambient occlusion
applied only to the environment term. A complete bundle suppresses the flat ambient term; an
incomplete or cleared bundle uses valid neutral fallback descriptors and restores flat ambient.
Deferred draws retain all ten sampled-image identities and dependencies, including render-target
cube producers, so replacing or destroying an IBL product cannot leave a descriptor set pointing
at a stale view.

**Test:** the shared `CNAEXT_ImageBasedLighting` binary passes **8/8 checks** on RADV, llvmpipe and
EasyGL with Khronos validation on the Vulkan runs: rigid/skinned agreement, flat-vs-IBL,
roughness-selected reflection, occlusion, four white-furnace points and direct-only shadow
attenuation. The Vulkan capability
snapshot, both PBR golden suites, hand-derived oracle, texture-slot oracle and descriptor-cache
oracle also pass on both devices without validation messages.

### Stock shadow reception (`MOD-2236`, 2026-09-10)

The per-pixel BasicEffect, SkinnedEffect, PbrEffect and SkinnedPbrEffect fragment programs consume
the same directional, cascaded, point and spot state as EasyGL. One common descriptor set holds the
three maps and a dynamic std140 record, while each family's material and lighting set stays
unchanged. Disabled or missing maps bind valid white 2D/cube fallbacks. Deferred render-target
producers are recorded as dependencies, dying image views evict their shadow snapshots, and sampler
eviction clears the cache before retiring the native sampler.

The equations match EasyGL: directional PCF has radius 0–2, cascades select and optionally blend
2–4 atlas slices, the point path compares one cube distance sample, and the spot path takes a 3x3
projected filter. Directional visibility multiplies direct diffuse and specular only, never ambient,
emissive or IBL. A directional map forces BasicEffect and SkinnedEffect onto their per-pixel sibling
even when `PreferPerPixelLighting` is false; punctual-only behavior retains EasyGL's existing
selection rule.

**Test:** `CNAEXT_ShadowReceiver` is a caster-independent constant-map pixel oracle and passes
**11/11** on EasyGL/llvmpipe, Vulkan/RADV and Vulkan/llvmpipe. It covers all four families,
disabled-state isolation, forced per-pixel reception, cascade state/debug tint and point/spot
sampling. `CNAEXT_ImageBasedLighting` additionally proves the direct-only interaction, and the four
stock-family regressions plus `Vulkan_EffectDescriptorCacheIdentity` stay green with Khronos
validation.

Generation is supplied separately by `MOD-2237`; `ExecutesShaderEffectSourceEXT()` remains false
because Vulkan consumes the selected SPIR-V package payload rather than pretending to execute the
package's GLSL variants.

### Portable shadow generation (`MOD-2237`, 2026-09-10)

`ShadowMap`, `CascadedShadowMap`, `CubeShadowMap` and `SpotShadowMap` select one reproducibly
generated shader package with GLSL ES, desktop GLSL and checked-in SPIR-V payloads. Directional and
cascade casters include rigid and 72-bone skinned vertex variants; point-cube and spot casters share
the same distance-output fragment contract as EasyGL.

Vulkan set 1 binding 19 carries the light-or-face view-projection and per-object world matrices,
so the public `SetUniformMat4("uWorld", ...)` hook does not collide with the legacy one-matrix push
slot. Compile-time vertex-input reflection filters declaration attributes the selected caster does
not consume and rejects a missing required location. The off-screen caster intentionally keeps the
XNA light-space Y orientation: applying the swapchain Y flip here mirrors the stored shadow and was
detected by the directional centroid oracle.

**Test:** the shared directional, cascade and point/spot applications pass **8/8**, **5/5** and
**4/4** on RADV, Vulkan llvmpipe and EasyGL. The complete three shadow-visibility suites pass
**31/31** on both Vulkan devices, including posed skinned casting and one-mesh self-shadowing.
`Vulkan_ShaderEffect_3D` passes **11/11**, and the generated package passes its reproducibility
check. All Vulkan runs use Khronos validation and emit no validation messages.

### Portable skybox (`MOD-2238`, 2026-09-10)

`CNA::Graphics::Skybox` selects a reproducibly generated GLSL ES/desktop GLSL/SPIR-V package. The
Vulkan variant reuses the custom SpriteBatch contract: its dummy source remains set 0 binding 0,
cube texture unit 1 is set 1 binding 5, and the inverse view-projection, combined tint/intensity and
yaw occupy the existing matrix, `vec4` and scalar push slots. It does not add a descriptor layout or
claim that Vulkan executes source GLSL.

Sampler-unit selection is deliberately set before yaw. Vulkan's legacy custom-effect scalar slot
is name-independent, so the GLSL-only `SetUniformInt("uEnvironment", 1)` would otherwise overwrite
the later sky value. The selected shader also consumes SpriteBatch's white vertex colour, keeping
the compiled SPIR-V input signature aligned with the submitted declaration.

**Test:** the shared skybox suites pass **15/15** on RADV, Vulkan llvmpipe and EasyGL, including six
named cube faces, yaw, tint/intensity, HDR output above one and foreground visibility. The
application orbit/yaw oracle passes **3/3** on all three paths with identical measurements. Both
Vulkan paths run under Khronos validation and emit no validation messages; the package also passes
its write-free reproducibility check.

### Portable post-process rollout (`MOD-2239`, `MOD-2218`, `MOD-2219`, `MOD-2239a`, 2026-09-10)

`ChromaticAberrationPass` is the first engine post-process to use the portable package path. Its
internal package shares one fullscreen vertex contract across GLSL ES, desktop GLSL and SPIR-V;
Vulkan consumes SpriteBatch's source at set 0 binding 0 and the strength from the established
scalar push slot. No renderer API or descriptor layout changed.

The pass overrides the source-oriented base support answer with the result that matters for a
package: `CustomEffects` plus a successfully selected and compiled effect. Its shared three-case
oracle passes **3/3** on EasyGL, RADV and Vulkan llvmpipe, covering optical-axis invariance, strong
edge fringing, exact disabled output and settings. Both Vulkan runs emit no Khronos validation
messages. `PostProcessShaderPackageReproducibility` permanently checks the generated payloads.

`FxaaPass` is the second package consumer. Its Vulkan fragment uses the same sampled source at set 0
binding 0, reciprocal target size in the established `vec4` push slot and the edge threshold in the
scalar slot. The public/C fragment-source projection is emitted from the generated GLSL ES payload,
so the separate float-target early-out audit still mutates the shader that the pass actually ships.
The pass/quality suites are **11/11** on EasyGL, RADV and Vulkan llvmpipe; the EasyGL run is **12/12**
with the four-format early-out audit. The application oracle is **8/8** on all three paths: disabled
and flat-field output remain exact, aliased staircase contrast falls, and threshold presets remain
observable. Both Vulkan runs emit no Khronos validation messages.

`FilmGrainPass` is the third consumer. Frame width, height, intensity and time share the existing
custom-effect `vec4` push slot; this avoids name-dependent scalar packing and keeps the immediate
GLSL and deferred SPIR-V parameter snapshots equivalent. The shared suite passes **4/4** on EasyGL,
RADV and Vulkan llvmpipe, proving deterministic equal-time frames, temporal movement, stronger
midtone than near-black variation, inert zero intensity and the public clamp/name contract. The
fixture uses left/right luminance regions so it does not confuse Vulkan's render-target Y orientation
with a shader result. Both Vulkan paths remain validation-clean.

`TonemapPass` brings the main HDR-to-display step onto the same route. Mode, exposure, inverse gamma
and dither amplitude occupy one existing `vec4` push slot, with the exact 0–4 operator ordinal
converted from float back to integer in the shader. The combined tonemap/deband suite passes
**17/17** on EasyGL, RADV and llvmpipe: all five GPU curves agree with their CPU reference, exposure
and gamma order is retained, and dither increases a six-level ramp to 181/175 mean levels on the two
Vulkan devices while staying zero-mean and below one output step. The pipeline application passes
**7/7** on all three paths with exact inert HDR-off output and matching curve samples. No Vulkan
validation message is emitted.

Other engine post-process effects remain source-only and keep their exact copy-through fallback;
the shared package/helper is the landing point for their subsequent fragment variants.

### Instancing (`plans/plan_vulkan.md` VULKAN-217…VULKAN-234, 2026-09-08)

`DrawInstancedPrimitives` adds an **optional per-instance world matrix to every stock 3D program**,
which is EasyGL's design (`CNA_GL_INSTANCE_TRANSFORM_DECL`) reached by the one route Vulkan allows:
a declared vertex input cannot be left unbound, so where EasyGL toggles one program with a uniform,
this renderer compiles **two SPIR-V modules from the one source**. `compile_shaders.py` injects the
four columns at **locations 12..15** — EasyGL's own locations — and a family's instanced module is
that family's `.glsl` compiled again with `CNA_INSTANCED`. Without the define every macro expands to
the text that was there before, so each family's ordinary module is byte-identical SPIR-V.

`useInstanced` therefore selects **no program at all**. It adds binding 1, and the effect family a
draw belongs to is decided by exactly the cascade a non-instanced draw uses:

| Effect / declaration | Program family | Instanced module |
|---|---|---|
| `BasicEffect`, Position only (or a stride the table does not list) | `colored3d` | `colored3d` + `CNA_INSTANCED` + `CNA_NO_VERTEX_COLOR` |
| `BasicEffect`, Position + Colour | `colored3d` | `colored3d` + `CNA_INSTANCED` |
| `BasicEffect`, Position + TextureCoordinate | `textured3d` | `textured3d` + `CNA_INSTANCED` |
| `BasicEffect`, Position + Colour + TextureCoordinate | `colored_textured3d` | same source + `CNA_INSTANCED` |
| `BasicEffect` with `LightingEnabled` — all three lit shapes (textured, untextured, coloured), each in both `PreferPerPixelLighting` variants | `lit_textured3d` / `lit_untextured3d` / `lit_textured3d_color` and their `_vertexlit` siblings | same sources + `CNA_INSTANCED` |
| `AlphaTestEffect`, both vertex shapes | `alpha_test3d`, `alpha_test_colored3d` | same sources + `CNA_INSTANCED` |
| `DualTextureEffect`, both vertex shapes | `dual_texture3d`, `dual_texture_colored3d` | same sources + `CNA_INSTANCED` |
| `EnvironmentMapEffect` | `env_map3d` | same source + `CNA_INSTANCED` |
| `SkinnedEffect`, stride 52/56, both lighting variants | `skinned3d*` | same sources + `CNA_INSTANCED` |
| `PbrEffect` / `SkinnedPbrEffect`, every vertex record (48/60, 68/76/80) | `pbr3d`, `pbr3d_skinned` | same sources + `CNA_INSTANCED` |

**Fog works on all of them**, which it did not before `VULKAN-233`/`VULKAN-234`: the separate
instanced family's fragment shaders had no fog term, because that family used the one-descriptor
pipeline layout it shared with 2D `SpriteBatch` and the fog UBO is a second binding. Routing every
instanced draw into its ordinary family removed the constraint rather than working around it, and
`instanced3d.vert.glsl`, `GetOrCreatePipelineInstanced3D`, `pipelinesInstanced3D_` and
`pipelineLayoutExt3D_` no longer exist.

**Which shape a `BasicEffect` instanced draw takes is decided by the declaration** when there is
one, and by the stride only when there is not — which is stricter than the ordinary routes, on
purpose (`VULKAN-149`): a declaration naming only a `Position` binds no colour out of the four bytes
after it, and a declared `Position+Colour` record binds its colour at whatever offset it declares
rather than at the stride table's. The effect's `TextureEnabled` is **not** part of that choice; the
shaders gate their own sample on it, exactly as they do for a non-instanced draw.

**The per-instance matrix composes with `BasicEffect.World`** — the shader computes
`World × View × Projection × instanceMatrix × position`, so an instance transform is applied
inside the effect's own world transform, as on EasyGL. Before `VULKAN-219` this route passed
only `View × Projection` and `World` was silently dropped on every instanced draw, while the
same entry point's no-instance-stream fallback applied it. For a **skinned** draw the instance
matrix applies *after* the bone skin and before World/View/Projection — the bone poses the mesh in
its own object space and the instance places the posed mesh — which is EasyGL's composition too.

**One deliberate divergence from EasyGL, and this renderer is the stricter one.** EasyGL rotates a
skinned normal by `mat3(instanceMatrix)` directly; this renderer folds the instance matrix into the
world normal matrix, `transpose(inverse(mat3(World × instance)))`. The two agree for any rigid or
uniformly-scaled instance and differ only under a non-uniformly-scaled one, where the composed
inverse-transpose is correct. For **PBR** the instance matrix is folded into the tangent matrix and
into `cnaDirectionHandedness` as well, so a mirroring instance flips the bitangent exactly as a
mirroring `World` does — the same value EasyGL computes as a separate `instanceHandedness` factor.

**Base instance is an explicit modern feature** (`MOD-2232`).
`DrawInstancedPrimitivesBaseInstanceEXT` retains `firstInstance` in the same deferred record as the
ordinary draw and supplies it to `vkCmdDrawIndexed`. Because Vulkan's 1.1 core vertex input has no
arbitrary divisor state enabled here, the renderer already expands `InstanceFrequency` into a
divisor-one staging stream; for a non-zero base it retains the required prefix as well, so both the
native instance number and the selected per-instance record advance together. The public layer
range-checks every bound instance stream over the shifted interval. `Vulkan_ShaderEffect_3D` draws
one instance at base one and observes only instance one's translation and colour, with instance
zero's half of the target remaining at the clear colour.

**Pipeline-variant cost.** Every family's instanced pipeline is a distinct cache entry with its own
identity, and there is exactly one per (family, vertex shape) — never one per runtime value.
`BasicEffect.VertexColorEnabled` in particular travels in the push constant and creates no variant.
`GraphicsDevice`'s `GetInstancedPipelineCacheSizeEXT()` diagnostic counts pipeline creations whose
instanced flag was true, across every family.

**What an instanced draw still cannot do here** is what a non-instanced one cannot: a
`ShaderEffect` instanced draw takes the custom-effect route below rather than any of the above, and
compiled `.fx` instancing is `plans/plan_fx.md`'s. There is no longer a stock-effect shape that
instancing excludes.
  Set `SamplerStates[1..15]` **before** `Begin()`. Setting them afterwards reaches a `Deferred`
  batch, whose flush publishes them, but not an `Immediate` one, whose only publication point is
  `Begin()` — XNA re-applies device state per draw call and CNA's sprite path does not go through
  the device's draw entry points, so that narrow window is a CNA boundary rather than an XNA
  promise. This matters most for `sampler3D`, whose `AddressW` is the one axis a 2D sprite never
  exercises.
- **Array uniforms** are four more bindings in set 1 — 12 `float`, 13 `vec2`, 14 `vec3`, 15 `mat4`
  — each holding 72 elements, which is XNA's own `SkinnedEffect.MaxBones`. Declare only the one
  you use. std140 pads a `float`, `vec2` or `vec3` array element to 16 bytes and this renderer
  writes them the same way, so `float uWeights[72]` reads element *i* where it was written.
- **A 3D draw** binds the buffer's own `VertexDeclaration`, with attribute location = the
  element's index in that declaration (EasyGL's convention for a custom program). A buffer with no
  declaration is refused: a custom shader's inputs cannot be inferred from a stride. A
  **per-instance** stream continues at the locations after the mesh declaration's element count,
  from binding 1 — the same rule EasyGL's own instanced custom-shader path states. Note that every
  element the declaration carries becomes an attribute, so a shader that *ignores* one makes the
  validation layer warn that it is not consumed: only SPIR-V reflection could tell which elements
  a shader reads, and this renderer does not do it.
- **The transform** arrives in `uMatrix`, column-major, from the effect's `IEffectMatrices`
  properties — unless the game called `SetUniformMat4` itself, in which case the game's matrix
  stands.
- **Clip space is Vulkan's**, not OpenGL's or D3D9's: the shader is written for this renderer, so
  nothing flips Y for it. See the depth-range section above for the other half of that.

---

## Where this renderer differs from EasyGL, and why

`VULKAN-480`. EasyGL is this project's reference for **coverage and maturity**, never for
semantics — the rule and the three rows that applied it are in the conformance section above. What
follows is the list of places where the two renderers do not do the same thing, each with the
reason and where the evidence lives. `plans/plan_vulkan.md` §10 is the full matrix; this is the
short form a reader needs before opening it.

**Differences that are deliberate and permanent:**

| Difference | Why |
|---|---|
| `ShaderEffect` takes **SPIR-V**, EasyGL takes GLSL | `ShaderEffect` carries renderer-specific source by contract. The section above is the whole story, including why EasyGL's 37 GLSL-payload tests are not a Vulkan backlog. |
| Clip space is `[0, 1]`, EasyGL's is `[-1, 1]` | Vulkan's own convention; see the depth-range section, which also names who owns the half EasyGL does not cover. |
| Set numbering for a custom effect's textures | Set 0 is the `SpriteBatch` texture, set 1 the effect's own. EasyGL's unit 0 *is* the sprite texture. Both are internally consistent; the shader is renderer-specific anyway. |
| Array uniforms hold **72** elements | A fixed uniform-buffer block, where EasyGL's array is whatever length the GLSL declares. 72 is XNA's own `SkinnedEffect.MaxBones`; past it this renderer refuses by name rather than truncating. |

**Differences where this renderer does more:**

| Difference | Why it is not parity |
|---|---|
| MSAA can change at runtime | `ApplyMultiSampleCount` really tears down and rebuilds; EasyGL cannot change MSAA after construction, so the shared test asserted an echo until `VULKAN-095` corrected it. |
| Per-target MSAA sample counts | A `RenderTarget2D` carries its own count rather than the device's (`VULKAN-216`). |
| Real depth bias | `vkCmdSetDepthBias` as dynamic state on ten pipeline sites. |
| `SpriteSortMode::Immediate` honoured at the renderer boundary | EasyGL does not override `SetImmediateMode` at all. |
| Precise occlusion counts on real hardware | `VK_QUERY_CONTROL_PRECISE_BIT` with the feature enabled, answered honestly through `PixelCountIsPreciseEXT` (`VULKAN-370`). |

**Former shadow and skybox gaps now closed:** `MOD-2236` supplies stock-effect reception,
`MOD-2237` supplies portable directional, cascade, point and spot generation and `MOD-2238`
supplies the portable skybox without changing Vulkan's shader dialect.
`ExecutesShaderEffectSourceEXT()` stays false because the renderer executes packaged SPIR-V, not
caller-provided source text. Indirect execution is the additional device-gated path described above.

---

## Device loss: reported, never reset

`VULKAN-334`. **Test:** `Vulkan_DeviceLostContract`

XNA gives a game three events — `DeviceLost`, `DeviceResetting`, `DeviceReset` — plus
`GraphicsDeviceStatus` and `ContentLost` on its resources. CNA delivers them from the renderer
through `GraphicsRendererCreateArgs::deviceEventCallback`, and before `VULKAN-334` this renderer
never called it: `VK_ERROR_DEVICE_LOST` appeared nowhere in it, so a lost device surfaced as
whichever generic failure the next call raised.

**What happens now.** Every call that can report the loss — `vkQueueSubmit`, `vkQueuePresentKHR`,
`vkAcquireNextImageKHR`, and the two fence waits — is checked. On a loss the renderer reports
`RendererDeviceEvent::Lost` to the shared layer **once**, so the game's own `DeviceLost` handler
runs and `GraphicsDeviceStatus` becomes `Lost`, and the call then fails with a message naming the
entry point. `MOD-2254` made the ordinary `SubmitFrame` present path use the same check already
used by deferred present, closing the final unchecked route behind this statement.

**What does not happen, and why.** There is no reset. A lost `VkDevice` is unrecoverable by
specification: the device and every object created from it must be destroyed and rebuilt. Doing
that under live `Texture2D`, `RenderTarget2D`, `Effect` and buffer wrappers is a different feature
from D3D9's `Reset` — the one XNA's event pair was designed around — and this renderer does not
have it. It says so in the failure rather than pretending, and `DeviceReset` is therefore never
raised here.

**The three GL entry points stay unimplemented.** `SetContextRecoveryEnabled`,
`DebugSimulateContextLoss` and `DebugRestoreContext` describe an OpenGL context loss, which is a
different event with a different recovery model. Implementing them by analogy would be inventing a
state machine Vulkan does not have; the row that decided this named that as its explicit non-goal.

---

## `SpriteSortMode::Immediate` does not submit per sprite, and will not

`VULKAN-057`, opened by `VULKAN-050` and closed on the second arm of its acceptance — the
divergence is refused, and here is what it is, what it costs and why. **Test:**
`Vulkan_SpriteBatch_SortModeSemantics`, whose `[INFO]` lines carry both measurements.

XNA defines `Immediate` as *"each sprite is drawing at individual draw call, instead of
`SpriteBatch.End`"* (`SpriteSortMode.cs`), so a texture mutated in place between two `Draw` calls
must leave the first sprite showing the **old** contents. On this renderer both sprites show the
new contents, exactly as they do under `Deferred`. `SpriteBatch::flushSingle` does forward each
`Immediate` sprite straight through; `VulkanSpriteBatchRenderer::Draw` then records it into
`activeBatches_` for replay at `Present`, while `Texture2D::SetDataRGBA` reaches the image through
`UpdatePixels`, which submits and waits **immediately**. The upload therefore always wins the race,
and every sprite of the batch rasterizes against the texture's final contents.

**This is not a Vulkan-vs-EasyGL gap.** EasyGL does the same thing for its own reason — its sprite
renderer appends to a vertex batch flushed only on a texture change or at `End()` — so the two
renderers agree and the divergence is CNA-wide. Fixing it here alone would not fix CNA; it would
only make the two renderers disagree.

**What honouring it would cost here**, which is the half a refusal owes a reader:

- **On an off-screen target** the machinery exists — `FlushDeferredRenderTarget` records, submits
  and waits. Since `MOD-2253` it waits only the reusable frame-slot fence and one fence attached to
  the exact producer/copy submission; the historical measurement below predates that narrowing but
  still demonstrates the inherent cost of forcing one synchronous CPU readback per sprite.
  Measured on llvmpipe under Xvfb `:99` on 2026-09-07, at 64 one-sprite batches:
  three runs gave **443, 505 and 525 µs added per sprite**, making the
  forced-submission route **2.7× to 3.4×** the cost of the batched one. On real hardware it is
  far worse, not better: on RADV (AMD Radeon 780M, Xwayland `:150`) the same 64 sprites cost
  7.9 ms batched and 91.6 ms forced — **1.31 ms added per sprite, ×11.6**.
- **On the backbuffer** — where a `SpriteBatch` normally draws — there is no such path at all, and
  its absence is deliberate: `FlushDeferredRenderTarget` excludes backbuffer cycles because they
  need a swapchain image and `REMED-GFX-144`'s one-acquire-one-submit-one-present-per-frame
  contract must not change. Honouring `Immediate` there is not a cost question but a contract
  question, and the contract was written to fix a real defect.

The same leg runs on any renderer the test is registered for, so EasyGL's own figure is one
`ctest` away — it has not been taken here, because `cmake-build-easygl/` belongs to another session
on this machine and this campaign does not run in it.

**What a game can rely on instead.** `Immediate` still draws every sprite, in issue order, each
into its own destination rectangle — leg B of the test asserts exactly that and nothing more. What
it does not buy on CNA is a read-back of resource state between two `Draw` calls of one batch. A
game that needs the old contents must `End()` the batch before mutating the texture; that is one
submission boundary instead of one per sprite, and it is what the deferred model can honour.

---

## `RasterizerState.MultiSampleAntiAlias` is not carried, and XNA does carry it

`VULKAN-099`, opened by `VULKAN-096`. **Test:** `Vulkan_PipelineKeyStateCoverage`. **Probe:**
`spikes/xna-multisample-antialias-spike/`.

`IGraphicsRenderer::ApplyRasterizerState` takes `(cullMode, fillMode, scissorTestEnable, depthBias,
slopeScaleDepthBias)`. There is no sixth parameter, so `RasterizerState.MultiSampleAntiAlias`
reaches this renderer — and every other one — not at all.

**XNA does not drop it.** Measured on the XNA 4.0 runtime in `~/.wine-cna-xna40`: setting the
property to `false` makes the D3D9 layer write render state **161**, which `d3d9types.h` names
`D3DRS_MULTISAMPLEANTIALIAS`, and re-running the probe with `PROBE_SKIP_FALSE=1` — no leg setting
it false — produces that write **zero** times. The property is live upstream; CNA is where it
stops.

**What it does to a rendered edge is unmeasured**, and deliberately reported as unmeasured: this
machine reaches D3D9 only through DXVK, which logs state 161 as *unhandled*, so the probe's edge
counts are identical with the flag true and false (151 blended pixels either way on a 4×
multisampled triangle, against 0 on an unmultisampled one — multisampling itself worked, which is
what rules out the "MSAA never happened" reading). A native D3D9 stack would be needed to answer it.

**Why it stays dropped here.** A Vulkan graphics pipeline's `rasterizationSamples` must match the
render pass attachment's sample count, so "do not multisample this draw" is not a per-draw property
in this API: honouring it would mean changing the attachment, and `pSampleMask` restricts which
samples are *written* rather than reproducing a D3D9 rasterizer mode. The alternative — a sixth
parameter on an interface twelve renderer families implement, so that all twelve can refuse it — is
cost without a behaviour. And it is **not a parity gap**: no renderer receives the field, so no two
renderers disagree, and the campaign's gate is unaffected.

**The refusal is guarded rather than described.** `Vulkan_PipelineKeyStateCoverage` asserts that the
field does not fragment the pipeline cache *and* that `ApplyRasterizerState` has exactly five
parameters, with a `void_t` detector and a positive control so a renamed method cannot make the
negative assertion pass vacuously. Adding the sixth argument fails the build with a message naming
this section and `docs/rasterizerstate-support.md` §8.

---

## Which vertex layouts a lit `BasicEffect` can draw

`VULKAN-198` (the measurement), `VULKAN-199` (the fix). **Tests:**
`Vulkan_BasicEffect_PositionNormal`, `Vulkan_BasicEffectCombinations`.

This renderer picks a stock program from the vertex buffer's **declaration**, falling back to its
stride when there is none. For the lit `BasicEffect` family that rule used to be `stride == 32` and
nothing else, so exactly one layout could be lit — `VertexPositionNormalTexture`. Any other
declaration carrying a `Normal` reached no lit program, and the declaration-fidelity guard then
**refused the draw by name** rather than reading the normal's bytes as something else. Refusing is
the right failure, but a game using one of those layouts could not draw at all.

| Declaration | Lit `BasicEffect` |
|---|---|
| Position + Normal + TexCoord (32 bytes) | ✅ always |
| Position + Normal (24 bytes) | ✅ since `VULKAN-199` |
| Position + Normal + Colour + TexCoord (36 bytes) | ✅ since `VULKAN-200`, **when lighting is on** |
| the same 36 bytes with lighting **off** | ✅ since `VULKAN-201` — routed to the unlit colour+texture program, whose input table has no `Normal` |
| Position + Normal + anything else | ❌ refused |
| no declaration at all | the stride decides, as before |

The 24-byte case is XNA's own Primitives3D sample, and it is 24 bytes *exactly as
`VertexPositionColorTexture` is* — which is why the stride cannot decide it and the declaration
must. The 36-byte case is what the stock `ModelProcessor` emits for a mesh carrying a colour
channel; `VULKAN-200` gave the lit family a colour input for it. Its **unlit** twin is still drawn too, since `VULKAN-201` — but by the **unlit** colour+texture program rather than the coloured lit shaders' unlit branch. That branch does not clamp `inColor * DiffuseColor` at the vertex the way `colored_textured3d` has since `VULKAN-197`, so the easy route would have made two unlit coloured draws disagree about the D3D9 `oD0` saturate purely by stride — measured at **77 levels** by removing that clamp and watching the gradient midpoint move from 178 to 255.

**One element is ignored, and that is a decision.** The unlit route binds Position, Colour and
TexCoord and leaves the declared `Normal` bound by nothing — because with lighting off there is no
lighting for it to feed. The rule elsewhere is that no element is dropped without a nameable reason;
this is the case where the reason is nameable, and the lit routes above refuse rather than drop.

**The rule is set-exact, not "has a Normal".** The layout builder reports a declaration complete
when every input the *shader consumes* was supplied — it says nothing about a declared element the
shader ignores. So a looser rule would let Position+Normal+**Colour** satisfy the two-input
untextured program and render with the vertex colour silently discarded. Every layout in the table
above is matched as a whole set, and anything else is refused until a program exists for it.

The rule does not pin the *offsets*, only the element set — the declaration carries the offsets and
the pipeline is keyed on them. EasyGL's equivalent test additionally requires Position at 0 and
Normal at 12; this one does not, though only the 0/12 record has actually been measured.
