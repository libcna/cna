# Native Metal Graphics Backend

## Scope

`METAL` is a native Apple Metal backend. SDL is used only for CNA's existing window lifecycle and
`SDL_Metal_CreateView` / `SDL_Metal_GetLayer` platform glue. Rendering itself must never be routed
through SDL_Renderer or SDL_GPU.

## Implemented initial foundation

- Compile-time backend selection: `CNA_GRAPHICS_BACKEND=METAL` and `CNA_BACKEND_METAL`.
- Apple-only CMake hard gate and Objective-C++ enablement only when METAL is selected.
- Native `MTLDevice`, `MTLCommandQueue`, `CAMetalLayer`, drawable acquisition and presentation.
- BGRA8 swapchain drawable rendering.
- Native depth32+stencil8 attachment.
- Color/depth/stencil clear combinations.
- Native `MTLBuffer` vertex and 16/32-bit index buffers.
- Native RGBA8 `MTLTexture` creation and updates.
- Runtime-compiled Metal Shading Language library.
- Colored 3D and basic textured 3D draw paths.
- Triangle list/strip and line list/strip topology mapping.
- Basic cull/fill/depth/scissor/viewport/depth-bias state plumbing.
- Native SpriteBatch path with texture sampling, source/destination rectangles, tint, rotation,
  origin and flip effects.
- Metal debug signposts via `insertDebugSignpost`.
- Dedicated macOS GitHub Actions compile job.

## Required next parity work

The first implementation is intentionally native but is not yet feature-complete with CNA's mature
Vulkan/WebGPU/SDL_GPU backends. Before calling METAL production-complete, implement and validate:

1. Correct pipeline-state cache keyed by the full CNA blend/depth-stencil/rasterizer state.
2. Exact XNA enum-to-Metal mappings for Blend/BlendFunction/CompareFunction/StencilOperation,
   TextureFilter and TextureAddressMode.
3. Vertex-descriptor cache driven by CNA `VertexDeclaration`, not fixed stride assumptions.
4. All BasicEffect shader variants including vertex-color, texture, fog and all lighting modes.
5. AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect and SkinnedEffect parity.
6. CNA NOXNA PBR and instancing paths, including normal maps and skinned PBR.
7. RenderTarget2D, RenderTargetCube, MRT, MSAA resolve and mip generation.
8. TextureCube and Texture3D including SetData/GetData and mip levels.
9. Backbuffer, texture and render-target GPU readback via `MTLBlitCommandEncoder`.
10. Occlusion queries using Metal visibility result buffers.
11. Custom ShaderEffect support with a defined MSL source contract or a cross-compiler pipeline.
12. Sampler cache and anisotropic filtering.
13. Accurate virtual-resolution/letterbox coordinate transforms.
14. Runtime resize, fullscreen and drawableSize handling including Retina scaling.
15. Frame pacing/presentation policy for CNA swapInterval semantics.
16. Resource lifetime and command-buffer synchronization audit.
17. Argument buffers / bindless-oriented NOXNA path where supported and beneficial.
18. Indirect command buffers and GPU-driven rendering as optional NOXNA extensions.
19. MetalFX integration as an optional Apple-only NOXNA upscaling path where available.
20. Apple GPU counter capture / signpost diagnostics and Xcode GPU Frame Capture documentation.

## Testing strategy

Real Metal execution requires Apple hardware or an Apple GPU exposed to macOS. A normal macOS guest
under QEMU on a Linux PC does not provide a usable virtual Metal GPU, so it is not a meaningful
replacement for real hardware testing. Use Linux for source/static checks, GitHub-hosted macOS
runners for native Apple compilation and basic runtime smoke tests, and at least one physical Mac
for visual correctness, GPU validation, frame capture, performance and device-specific testing.
