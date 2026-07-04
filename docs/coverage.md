# CNA — XNA 4.0 Coverage Report

**Date:** 2026-06-21  
**Branch:** develop  
**HEAD:** 04f0692  
**Analysis method:** static source inspection of `include/`, `src/`, `GRAPHICS_TASKS.md`,
`IGraphicsBackend.hpp`, and backend `.cpp` files. FNA class counts estimated from known
XNA 4.0 documentation and GRAPHICS_TASKS.md. Build was not run during analysis.

---

## Overall estimate

| Dimension | Estimate |
|---|---|
| API surface (headers + signatures present) | **~85 %** |
| Functional gameplay code — EasyGL / Vulkan | **~70 %** |
| Functional gameplay code — Bgfx | **~63 %** |

**Justification for ~70 % functional (EasyGL/Vulkan):**  
Graphics (the largest namespace) is ~92–93 % functional. Input is ~90 %. Audio
(`SoundEffect`/`SoundEffectInstance` plus real XACT `AudioEngine`/`SoundBank`/`WaveBank`/`Cue`
and `Microphone` capture — see `plan_audio.md` for the full file-by-file history) is ~90 %,
updated 2026-07-04 (Fáze 9 `P9-DOCS-003`; the XACT/Microphone stub status this figure used to
describe predates that branch's work by roughly two weeks). Media playback (Song/Video via
SDL3_mixer + FFmpeg) is ~55 %. Content is ~60 % for the custom JSON/PNG/OGG descriptor
format; the XNA binary `.xnb` format is entirely absent. Framework.Net is 0 %.
GamerServices is ~5 %.

Weighted by how commonly each namespace is used in real XNA 4.0 games (Graphics + Input
dominate; Net appears in fewer than 10 % of XNA titles; GamerServices only in Xbox Live
games), a game using only Graphics / Input / Audio / Content and no networking can run
at roughly **80–88 % fidelity**. A game depending on `.xnb` content loading will break
immediately (the XACT audio runtime itself no longer breaks — see the Audio row below). A
multiplayer game will not compile at all.

---

## Namespace-by-namespace breakdown

| Namespace | FNA .cs est. | CNA .hpp present | Classes % | Methods functional % | Notes |
|---|---|---|---|---|---|
| **Framework** (Game, math, collision, curves) | ~50 | 41 | ~95 % | ~90 % | Game loop, all math types, BoundingBox/Sphere/Frustum, Curve, MathHelper fully implemented |
| **Framework.Graphics** | ~75 | 117 | ~95 % | see per-backend table | Detailed in next section |
| **Framework.Input** | ~20 | 26 | ~100 % | ~90 % | Keyboard, Mouse, GamePad, Touch wired to SDL3; rumble/vibration untested |
| **Framework.Audio** | ~15 | 20 | ~100 % | ~90 % | SoundEffect/Instance real (SDL3_mixer, real filters, instance-tracking cascade); AudioEngine/Cue/WaveBank/SoundBank real (hand-written XACT parser, category/lifecycle/3D all functional); Microphone real (SDL3 capture). Remaining gaps are documented accepted deviations (no HRTF/Doppler, `instanceLimit`/fade parsed not enforced), not stubs — updated 2026-07-04, see `plan_audio.md` |
| **Framework.Media** | ~25 | 24 | ~100 % | ~55 % | MediaPlayer and VideoPlayer real (FFmpeg); Song/Album/Artist/Genre/Picture/MediaLibrary = pure stubs |
| **Framework.Content** | ~20 | 4 | ~20 % | ~60 % | ContentManager works with custom JSON/PNG/OGG descriptors; **no .xnb binary support** |
| **Framework.Storage** | ~5 | 3 | ~100 % | ~75 % | StorageDevice/Container with filesystem; async patterns simplified |
| **Framework.GamerServices** | ~15 | 3 | ~20 % | **~5 %** | Only Guide.Show (no-op warn) + IsTrialMode stub; Gamer, Achievement, Leaderboard absent |
| **Framework.Net** | ~20 | **0** | **0 %** | **0 %** | NetworkSession, NetworkGamer, PacketReader/Writer entirely absent |

---

## Per-backend Graphics capability

### EasyGL (OpenGL ES 3.2 — `cmake-build-debug`)

| Feature | Status |
|---|---|
| 2D SpriteBatch — all overloads, sort modes, scissor, blend | ✅ |
| SpriteFont DrawString / MeasureString | ✅ |
| Texture2D — SetData/GetData, DXT1/3/5 FromStream | ✅ |
| Texture3D / TextureCube SetData/GetData | ✅ |
| RenderTarget2D / RenderTargetCube / MRT | ✅ |
| BasicEffect (MVP, lighting, vertex color, texture, fog) | ✅ |
| AlphaTestEffect | ✅ |
| DualTextureEffect | ✅ |
| EnvironmentMapEffect | ✅ |
| SkinnedEffect (72-bone UBO) | ✅ |
| ShaderEffect (custom GLSL) | ✅ |
| SpriteBatch with custom Effect | ✅ |
| DrawPrimitives / DrawIndexedPrimitives / DrawInstancedPrimitives | ✅ |
| OcclusionQuery | ✅ |
| DepthStencilState / BlendState / RasterizerState | ✅ |
| Per-slot SamplerState (16 slots) | ✅ |
| ScissorRect / BlendFactor / Viewport | ✅ |
| MSAA 4× (FBO + glBlitFramebuffer resolve) | ✅ |
| GetBackBufferData pixel readback | ✅ |
| Context-loss recovery (CPU shadow copies + ResourceRegistry) | ✅ |
| FillMode::WireFrame | ❌ Not possible on GLES3 (no glPolygonMode) — known limit |
| .xnb content loading | ❌ Out of scope |

**EasyGL summary: ~92 % of XNA Graphics GPU capability functional.**

---

### Vulkan (`cmake-build-vulkan`)

| Feature | Status |
|---|---|
| 2D SpriteBatch (dedicated 2D pipeline + push constants) | ✅ |
| All 5 stock Effects (Basic/AlphaTest/DualTex/EnvMap/Skinned) | ✅ |
| ShaderEffect (custom SPIR-V, 128-byte push constants, std140) | ✅ |
| SpriteBatch with custom Effect | ✅ |
| DrawInstancedPrimitives (VK_VERTEX_INPUT_RATE_INSTANCE) | ✅ |
| RenderTarget2D / RenderTargetCube / MRT | ✅ |
| OcclusionQuery (vkCmdBeginQuery) | ✅ |
| FillMode::WireFrame (fillModeNonSolid + VK_POLYGON_MODE_LINE) | ✅ |
| Per-slot SamplerState (16 slots, sampler cache) | ✅ |
| MSAA 4× (resolve attachment, subpass auto-resolve) | ✅ |
| GetBackBufferData pixel readback | ✅ |
| SetStringMarkerEXT debug labels (vkCmdInsertDebugUtilsLabelEXT) | ✅ |
| Texture3D / TextureCube upload | ✅ |
| Non-zero vertexStart / startIndex / baseVertex | ✅ |
| .xnb content loading | ❌ Out of scope |

**Vulkan summary: ~93 % of XNA Graphics GPU capability functional.**

---

### Bgfx (`cmake-build-bgfx`)

| Feature | Status |
|---|---|
| 2D SpriteBatch | ✅ |
| BasicEffect (colored + textured + lit DrawPrimitivesEx) | ✅ |
| AlphaTestEffect | ✅ |
| DualTextureEffect | ✅ |
| SkinnedEffect (72-bone uniform array) | ✅ |
| DrawInstancedPrimitivesEx (bgfx::allocInstanceDataBuffer) | ✅ |
| RenderTarget2D / RenderTargetCube / MRT | ✅ |
| OcclusionQuery | ✅ |
| Texture3D / TextureCube SetData | ✅ |
| FillMode::WireFrame (BGFX_STATE_PT_LINES) | ✅ |
| Per-slot SamplerState | ✅ |
| **EnvironmentMapEffect** | ❌ No env_map3d shader pair; falls back to lit_textured3d |
| **ShaderEffect (custom GLSL/SPIR-V)** | ❌ IEffectBackend::CreateEffectBackend returns nullptr |
| GetBackBufferData readback | ⚠️ Implemented via requestScreenShot callback; not integration-tested |
| MSAA | ⚠️ Not wired — Bgfx supports it but MultiSampleCount is not forwarded to bgfx init flags |

**Bgfx summary: ~82 % of XNA Graphics GPU capability functional.**

---

## Biggest gaps (any backend)

| Gap | Severity | Notes |
|---|---|---|
| **Framework.Net — 0 %** | Blocking for multiplayer games | NetworkSession, NetworkGamer, PacketReader/Writer, LocalNetworkGamer entirely absent — no headers, no stubs |
| **Content pipeline (.xnb) — 0 %** | Blocking for most existing XNA games | XNA binary asset format not supported; ContentManager requires CNA custom JSON/PNG/OGG descriptors |
| **GamerServices — ~5 %** | Blocking for Xbox Live games | Achievements, leaderboards, Gamer profiles, FriendCollection absent |
| **XACT audio runtime — ~90 %** | Mostly closed (updated 2026-07-04) | Real hand-written `.xgs`/`.xsb`/`.xwb` parser + SDL3_mixer playback; remaining gap is documented accepted deviations (`instanceLimit`/fade parsed not enforced, no HRTF/Doppler), not stubbing — see `plan_audio.md` |
| **Microphone — ~95 %** | Minor (updated 2026-07-04) | Real SDL3 capture device enumeration, Start/Stop, GetData/GetQueuedBytes, BufferReady event |
| **Media library (Album/Artist/Genre) — ~5 %** | Minor for most games | Song/Video playback real; device media-library browsing = pure stubs |
| **Bgfx: EnvironmentMapEffect** | Medium | No cube-map reflection shader; falls back to lit shader |
| **Bgfx: ShaderEffect** | Medium | Custom GLSL/SPIR-V effects not wired in Bgfx backend |
| **Bgfx: MSAA** | Low | Framework supports it; just not forwarded to bgfx init |

---

## SpriteBatch stub status (as of 2026-06-21)

All 6 previously-stubbed `Draw` overloads and 3 `DrawString(StringBuilder,…)` overloads
are now implemented (Tasks 151–159). `End()` and `Begin()` guards match XNA spec.

---

## Task roadmap overview

| Phase | Tasks | Scope | Status |
|---|---|---|---|
| 1–8 | 1–100 | Core Graphics API, backends, math | ✅ All done |
| 9–18 | 101–150 | Effects, instancing, MSAA, MRT, test gaps | ✅ All done |
| 19 | 151–160 | SpriteBatch API completion | ✅ Done (this session) |
| 20 | 161–168 | SpriteBatch XNA behavior conformance | 🔄 Partly done (166 ✅, 161–165 deferred, 167–168 pending) |
| 21 | 169–176 | Texture SetData/GetData conformance | ⬜ |
| 22 | 177–183 | RenderTarget correctness | ⬜ |
| 23 | 184–190 | Effect system XNA accuracy | ⬜ |
| 24 | 191–196 | Stock effects backend parity | ⬜ |
| 25 | 197–200 | PackedVector exactness | ⬜ |
| 26–55 | 201–500 | GraphicsDevice lifecycle, validation, conformance, golden tests, FNA comparison harness, release gate | ⬜ |

Tasks 201–500 were added externally and cover deep conformance: GraphicsDevice validation,
resource lifecycle/disposal, draw-call parameter validation, PresentationParameters,
VertexDeclaration accuracy, all SurfaceFormats, BlendState/DepthStencilState exactness,
pixel golden-image tests, FNA comparison harness, and a final XNA 4.0 Graphics 1.0
compatibility milestone gate (Task 500).

---

## How to read the estimates

- **API surface %** = public types and method signatures present in `include/` headers.
- **Functional %** = method bodies do something correct at runtime (not stubs/throws).
- **XNA accuracy %** (not separately listed) would be lower — many implemented methods
  pass smoke tests but have not been verified against FNA edge cases, exact color math,
  fog equations, half-float packing, etc. Tasks 184–500 address this.
