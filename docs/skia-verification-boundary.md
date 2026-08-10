# Skia verification boundary

SKIA-107 closes the renderer-specific verification checklist against the selected CPU-raster
architecture. The implementation has no Ganesh or Graphite mode, so a CPU/GPU image comparison
would fabricate a second execution mode. Instead, the raster-mode coherence test makes the single
mode observable and immutable. Adding an accelerated mode must reopen this row and run the same
pixel corpus in both modes before any fallback or parity claim is accepted.

## Sabotage-sensitive coverage

| Boundary | Direct tests | Targeted defect that the assertions expose |
|---|---|---|
| Surface ownership | `Skia_Surface_Raster`, `Skia_RenderTargetBinding_Raster`, `Skia_Ownership` | Alias/null the active surface, remove identity validation, allow a foreign thread, or retain a renderer pointer after destruction; identity, diagnostic, unchanged-pixel, recovery and late-use assertions fail. |
| Context/presenter loss | `Skia_ContextRecovery`, `Skia_WindowLifecycle`, `Skia_RasterMode_Coherence` | Clear/reallocate a CPU surface, drop a texture/target snapshot, reorder reset events, emit false device loss, mutate capability state, or switch Skia execution mode during presenter rebuild; exact resources, pixels, events and mode checks fail. |
| CPU/GPU mode policy | `Skia_StartupDiagnostic_Raster`, `Skia_RasterMode_Coherence`, `Skia_GraphicsCapability` | Probe or silently select a Skia GPU surface, change samples/filter support, or switch mode after recovery; diagnostic, capability, pixel and immutability checks fail. There is deliberately no GPU parity result until such a mode exists. |
| Alpha conversion | `Skia_Texture_AlphaBoundary`, `Skia_Surface_Raster`, `Skia_RasterMode_Coherence`, four `Skia_BlendState_*` fixtures | Treat straight bytes as premultiplied (or vice versa), premultiply twice, change public readback to premultiplied bytes, consume row padding, or change top-row ordering; discriminating translucent bytes and stride/row assertions fail. |
| State leakage | `Skia_StateTransition`, `Skia_SpriteBatch_SamplerTransition`, `Skia_RenderTarget2D_Scissor`, `Skia_RuntimeBlender_Policy` | Retain target-local clip/viewport, let Clear mutate scissor, leak a rejected Begin/custom blender, retain a sampler across batches, or reset a same-size viewport at Present; the post-transition pixels fail. |
| Capability diagnostics | `Skia_StartupDiagnostic_Raster`, `Skia_GraphicsCapability`, `Skia_3D_Refusal`, `Skia_RasterMode_Coherence` | Advertise a rejected feature, hide transfer-only Texture3D, change revision/mode/alpha/sample fields, permit a 3D call, or change capabilities across recovery; the closed value set, stable diagnostic and refusal assertions fail. |

These are observable contracts rather than source-shape checks. Each sabotage reaches a pixel,
event sequence, runtime capability, diagnostic string or ownership failure asserted by the named
test, so a refactor may change implementation structure without weakening the gate.

## Mode-parity reopening rule

The current selected artifact has Ganesh, Graphite, OpenGL, Vulkan and Dawn disabled. SDL's
streaming renderer presents a completed CPU image and is not a Skia GPU execution mode. SDL may
internally choose an accelerated renderer (including OpenGL) and make its own presentation context
current; this is platform-dependent and must not be confused with a Skia surface/context claim.
Therefore SKIA-107 proves:

1. construction reports `surface=raster`, zero samples and unsupported anisotropy;
2. the live capability set agrees with that report;
3. semi-transparent straight-RGBA8 readback is exact at the public backbuffer boundary;
4. presenter recovery preserves pixels and the capability/diagnostic set; and
5. the SDL presenter remains renderer-owned without changing the reported Skia execution mode.

The accepted [`skia-surface-mode-adr.md`](skia-surface-mode-adr.md) selects raster for this release
and makes the following list a mandatory reopening gate. Any future accelerated implementation
must add a construction-time mode selector, mode-specific
startup diagnostics, context/device-loss policy, and pixel comparisons for Clear, Texture2D,
SpriteBatch, SpriteFont, blend/sampler/scissor, RenderTarget2D and readback. Until then, saying that
CPU and GPU output have parity is unsupported; saying that the raster mode cannot silently become
GPU-backed is tested.
