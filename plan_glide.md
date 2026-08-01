# Glide 3.x backend plan

## Goal and non-goal

The goal is authentic **Glide 3.x** submission to a caller-installed emulator such as dgVoodoo2,
not an EasyGL/OpenGL compatibility facade. `glide3x.dll` is loaded dynamically and never shipped by
CNA. The supported application ABI is 32-bit Windows/x86.

Glide is a fixed-function Voodoo-era API. CNA therefore exposes a small, reliable subset and throws
for functionality with no faithful native counterpart instead of silently falling back to a different
renderer.

## Completed

- [x] Native `grSstWinOpen`, back-buffer clear/present, LFB readback, and loader support for both
  undecorated and x86 stdcall-exported Glide DLLs.
- [x] `SpriteBatch` texture upload and quad submission through TMU0 plus `grDrawTriangle`.
- [x] Fixed-function `VertexPositionColor` triangle lists/strips, including indexed draws and CPU
  `world * view * projection` transforms.
- [x] Native 16-bit Z buffer, depth clear/test/write state, source/destination alpha blending, and
  a manual dgVoodoo smoke target.

## Next implementable work

- [ ] CPU homogeneous frustum clipping for colored triangles. Current code safely rejects a
  triangle with non-positive clip W or a vertex outside the near/far Z interval; it does not split
  crossing triangles.
- [ ] Textured 3D `VertexPositionTexture` and `VertexPositionColorTexture` through TMU0, including
  perspective-correct `s/w`, `t/w`, and `1/w`. This remains compatible with real Glide hardware.
- [ ] Fixed-function per-vertex fog and the subset of `BasicEffect` that maps exactly to unlit or
  vertex-lit Voodoo combiners. Document every approximation before enabling it.
- [ ] Native Glide culling, alpha test, dither and depth-compare state mapping once CNA exposes
  the required state information to this backend.
- [ ] Texture tiling and true mip chains so images beyond a single 256-pixel Glide texture do not
  need the current downsample-to-fit policy.
- [ ] Query the emulator's supported resolution/TMU limits at startup instead of the conservative
  640x480/800x600 and 256-pixel defaults.

## Explicitly unsupported by this backend

- Programmable shaders, arbitrary `Effect` source, PBR, and GPU skinning: those are outside
  Glide's fixed-function model.
- Render-to-texture, MRT, MSAA, cube/volume textures, stencil, and occlusion queries: no faithful
  baseline Glide 3.x implementation exists. Any future approximation needs separate opt-in design
  approval, because it would no longer be a real Glide backend.
- Native 64-bit applications: historical Glide's window-handle ABI is 32-bit. Use the supplied
  i686 MinGW toolchain and an x86 emulator DLL.
