# CNA Post-Audit Graphics Boundaries

**Created:** 2026-08-03 as the deferral plan of the post-audit remediation campaign.
**Reconciled and condensed:** 2026-10-14, when the campaign's `remediation/` archive was reconciled
against the current tree and deleted. Every ticket it carried has a final disposition; this file
keeps only what is still true today. The narratives, measurements and dates of the individual
findings live in Git history (the pre-2026-10-14 version of this file and the deleted
`remediation/` directory).

## 1. Status

Nothing in this plan is open. The last implementation items it carried closed on 2026-10-14:

- `RenderTarget2D::SetData` reaches the surface on every current renderer (DIRECTX9 and FNA3D
  were the last two without an upload path); `RenderTargetUploadRoundTripTests` pins it on all of
  them, whole level and partial rectangle, with an explicit `NotSupportedException` as the only
  other acceptable answer.
- A `mipMap` `RenderTargetCube`/`RenderTarget2D` on DIRECTX9 allocates its whole chain and
  regenerates it on every transition away from the target (StretchRect per level), so
  `LevelCount` and `GetData` agree (`DirectX9_RenderTargetCube_GetDataContract` L3 reads level 1).
- Headless accounts an instanced draw as `instanceCount` draws in `primitiveCount`
  (`Headless_CoverageGaps` check K).
- `DisplayMode` has `Equals(object)`/`GetHashCode`; `Texture2D`, `DynamicVertexBuffer`,
  `DynamicIndexBuffer` and `GamerServicesComponent` report their own fully qualified type names.

## 2. Capability boundaries that are part of the supported contract

Each row is a truthful, tested answer, not a gap waiting for work. A renderer that reports a
capability false refuses the corresponding request deterministically before any native submission.

| Boundary | Current behaviour | Pinned by |
|---|---|---|
| Multi-stream vertex input | `MultiStreamVertexInput` is true on EasyGL, Software, WebGPU, SDL_GPU and FNA3D; false on DIRECTX9 and Headless, where `GraphicsDevice::ValidateVertexStreamCapability` rejects a second stream of the same input rate before the renderer sees it | `OrdinaryDrawMultiStreamTests`, `InstancedDrawMultiStreamTests` (both directions on every renderer) |
| Hardware instancing | `GraphicsCapability::Instancing` is queryable and true on every current renderer that rasterizes | `GraphicsDeviceCapabilityTests` |
| Vertex-declaration fidelity | A renderer that still picks a native layout from the stride refuses, with `NotSupportedException`, any declaration that layout cannot represent (`RequireFaithfulDeclarationEXT`); EasyGL, WebGPU, Software, SDL_GPU and the ordinary D3D9 route translate the declaration itself | `VertexDeclarationFidelityTests` |
| `FillMode::WireFrame` | EasyGL reports it only with a native polygon mode (desktop GL, `GL_NV_polygon_mode`, WebGL polygon-mode extension); a GLES3 context emulates the single-stream stock position/colour route with `GL_LINES` and refuses every other wireframe triangle route. WebGPU re-expands triangle edges into lines and refuses the routes it cannot cover. Headless has no pixel route | `EasyGLUnsupportedWireFrameTest`, `WebGpuWireFrameContractTests`, `parity_fill_mode_wireframe` |
| Custom shader source | `CustomEffects` means the renderer accepts an effect; SDL_GPU consumes SPIR-V and WebGPU WGSL, and `ExecutesShaderEffectSourceEXT()` says whether GLSL source actually runs (`docs/renderer-capability-profiles.md`) | `CnjStockEffectTest.CustomGlslEffectStillWorks` |
| Headless cube/volume storage | `TextureCube`/`Texture3D` store no pixels on HEADLESS: `SetData` throws `NotSupportedException`, `GetData` refuses (`docs/headless-renderer.md`) | `Headless_CubeVolume_*Contract` |
| `BlendState.MultiSampleMask` on SDL_GPU | Not applied: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask` (`handoff.md` §5.2) | — (documented limitation) |
| `RenderTarget2D` format outside the profile list | Falls back to `SurfaceFormat::Color`, as XNA's preferred-format rule does (SOFTWARE-216) | `RenderTargetFormatAgreementTests` |
| `TextureCube` format rule | A cube never carries `NormalizedByte2`/`NormalizedByte4` at either profile; the profile-tier half of XNA's cube list is deliberately not enforced because CNA allows float cubes at Reach | `Texture::IsCubeFormatAllowedByProfileEXT` tests |
| XNA pixel centres | Integer pixel centres, measured on the real XNA runtime (`spikes/xna-pixel-center-spike/`), are the contract on EasyGL, Software and Headless; D3D9's native convention already matched | `xna_pixel_center_contract_test` |

## 3. Findings this plan admitted after the campaign, all closed

| Ticket | Finding | Closed |
|---|---|---|
| GFX-220 | Static initialization order between `BlendState` presets and `Color` | 2026-08-05 |
| GFX-221 | `GestureDetector` statics copied `Vector2::Zero` across translation units | 2026-08-09 (FINAL-STAB-001) |
| GFX-222 | `SetVertexBuffers` rejected FNA-legal null bindings | 2026-08-06 |
| GFX-223 | A cache-reconstructed texture claimed to be a render target | 2026-08-07 |
| GFX-224 | An EasyGL render target silently discarded `SetData` | 2026-10-14 (see §1; every renderer) |
| GFX-225 | `ITextureCubeBackend::GetSizeEXT` broke four renderers | 2026-08-07 |
| GFX-226..228 | Glide sampler-slot and TMU lifetime defects | renderer retired |
| GFX-229..231 | Software undersized pitch, ignored row pitch, `SourceAlphaSaturation` using alpha twice | 2026-08-08 |
| GFX-232 | DX3 stencil hook contradicted its depth-only surface | renderer retired |
| GFX-233 | Legacy empty-declaration persistent buffers reused vertex zero | 2026-08-08 |
| GFX-234 | A stride-32 declaration without Normal lost its colour | 2026-08-29 |
| GFX-235 | The pixel-centre correction removed multisample coverage | 2026-08-29 |
| GFX-236 | EasyGL ignored `GraphicsDevice.ReferenceStencil` | 2026-08-29 |
| GFX-237 | A clear left the depth/stencil write masks open | 2026-08-29 |
| BUILD-017/018 | GDI workflow targets; capability test used an incomplete type | renderer retired / 2026-08-08 |
| GFX-238..241 | Pixel-centre and signed-normalized contract/test defects | 2026-08-30 |
| GFX-242, 244, 245 | Texture-format legality asks the profile, EasyGL stores the six 16-bit/normalized formats, cube and render-target format rules (§2) | 2026-08-30 |
| GFX-243 | The vsync case skipped and defended nothing | 2026-08-30 |
| GFX-246 | The pixel-centre contract reaches a third renderer | 2026-08-30 |

## 4. Admission rule, kept

A new graphics finding is recorded in `handoff.md` §5 if it is a defect, or in the owning
renderer's document if it is a deliberate boundary. A silent wrong result on a supported path is
never deferred as a "capability boundary": the renderer must either implement the behaviour or
refuse it before native submission, and a test must assert whichever it does.
