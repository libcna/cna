# SKIA-98 CPU stencil-state spike

SKIA-97 established a bounded CPU-owned RGBA8 plus floating-point depth target as the only
candidate bridge after direct `SkVertices` lost perspective and clipping information. SKIA-98
tests whether CNA's complete draw-time stencil state can be expressed on that bridge without
changing the public Skia backend.

The implementation lives only in the headless `Skia_CpuStencil_Spike` test. It does not add a
public attachment, connect any `Draw*` call, or change `HasRealDepthBuffer`, stencil, or `ThreeD`
capability reporting.

## EasyGL contract used as the oracle

The model follows `EasyGLGraphicsBackend::ApplyDepthStencilState` and its existing stencil
fixtures:

1. select the ordinary face state, or the counter-clockwise state when two-sided mode is enabled;
2. compare `(ReferenceStencil & StencilMask)` against `(stored & StencilMask)`;
3. on stencil failure, apply `StencilFail` and skip depth and colour;
4. otherwise test incoming depth against stored depth;
5. on depth failure, apply `StencilDepthBufferFail` and skip depth and colour;
6. on full pass, apply `StencilPass`, then eligible depth and colour writes; and
7. merge an operation through the write mask as
   `(stored & ~StencilWriteMask) | (operationResult & StencilWriteMask)`.

The eight operations map exactly as EasyGL maps them: keep, zero, replace, increment/decrement
with 8-bit wrap, increment/decrement with saturation, and bitwise invert. The same eight
`CompareFunction` values serve stencil and depth. The prototype deliberately models the low eight
bits of `Depth24Stencil8`; CNA's default signed masks `0x7fffffff` therefore reduce to `0xff`.

Clears are target operations rather than fragment operations. As in the corrected EasyGL clear
path, a stencil clear replaces the whole byte regardless of the active stencil write mask, and a
colour clear is not restricted by `ColorWriteChannels`.

## Matrix results

The test executes these independent checks:

- all eight compare functions over every reference/stored byte pair and eight discriminating read
  masks (`0x00`, single-bit, low/high nibble, alternating, and full): 4,194,304 cases;
- all eight stencil operations over every stored/reference byte pair and the same eight write
  masks: 4,194,304 cases, including the wrap and saturation boundaries;
- the distinct stencil-fail, depth-fail and full-pass branches, with assertions that rejected
  fragments cannot modify depth or colour;
- stencil-disabled bypass and independent depth writing;
- the EasyGL mask fixture's narrow-read/narrow-write result;
- the exact two-sided fixture: the counter-clockwise fail operation produces `0x06`, while the
  same face in one-sided mode uses the ordinary pass operation and produces `0x04`;
- all 16 `ColorWriteChannels` bit combinations, each proving that colour selection cannot suppress
  the successful depth/stencil writes; and
- a full colour/depth/stencil clear despite restrictive draw-state masks.

Every check passes in the CPU model. There is no Skia API blocker for storing or updating an 8-bit
stencil plane because the operation is completed before the final RGBA8 handoff.

## Cost and remaining boundary

Adding a byte stencil plane to SKIA-97's RGBA8 plus float depth representation raises persistent
CPU storage from eight to nine bytes per pixel: 2,073,600 bytes at 640×360 and 18,662,400 bytes at
1920×1080, before duplicate Skia colour storage, textures, clip buffers, or multisampling. A
production design would need to preserve the existing checked allocation limit across all three
planes.

Passing this state micro-suite is necessary but not sufficient for 3D support. It does not prove:

- format-accurate depth quantization or every public reference/mask value outside the eight-bit
  attachment domain;
- triangle front/back classification, culling, clipping, depth bias, shared-edge coverage,
  viewport/scissor transitions, or wireframe interaction;
- multisample per-sample depth/stencil, packed attachment behavior, or resolves;
- textures, blending, effects, vertex layouts, primitive/index ranges, or mixed 2D/3D ordering;
- acceptable memory/performance for real scenes; or
- a maintainable production ownership and synchronization design.

SKIA-99 may therefore reuse this ordering/state model only inside the isolated CPU feasibility
route while it audits all declared vertex/index layouts, primitive expansion, range validation,
culling and wireframe. Public depth/stencil and `ThreeD` capabilities remain false until the
SKIA-101 decision and the complete SKIA-95 contract say otherwise.
