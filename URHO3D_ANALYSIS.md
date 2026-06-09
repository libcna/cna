# Urho3D + CNA Integration Analysis

## Goal

Evaluate whether it is realistic to build a fork of Urho3D that internally uses CNA
for graphics, audio, and other subsystems.

---

## CNA 3D API Readiness (as of 2026-06-09)

| Area | API Surface (headers) | Functional Implementation |
|---|---|---|
| Math (Vector, Matrix, Quaternion, Bounding*) | 100 % | 100 % |
| Game loop, GraphicsDevice setup | 100 % | ~80 % |
| VertexBuffer / IndexBuffer / VertexDeclaration | 100 % | ~50 % |
| BlendState / DepthStencilState / RasterizerState / SamplerState | 100 % | ~60 % |
| Texture2D / Texture3D / TextureCube | 100 % | ~70 % |
| RenderTarget2D / RenderTargetCube | 100 % | ~40 % |
| Effect / EffectParameter / EffectTechnique / EffectPass | 100 % | ~25 % |
| BasicEffect / SkinnedEffect / EnvironmentMapEffect | 100 % | ~20 % |
| Model / ModelBone / ModelMesh / ModelMeshPart | 100 % | ~40 % |
| SpriteBatch / SpriteFont | 100 % | ~85 % |
| Viewport | 100 % | ~90 % |

**Overall: API surface ~92 %, functional 3D rendering ~35 %, functional 2D rendering ~85 %.**

---

## Approach A — Fork Urho3D, Replace Graphics Backend with CNA

### Core Problem: Abstraction Level Mismatch

Urho3D calls graphics at a **low level**:

```cpp
// Urho3D internal calls
graphics->SetVertexBuffer(buffer);
graphics->SetShaders(vs, ps);          // raw shader handle
graphics->Draw(TRIANGLE_LIST, 0, count);
```

CNA provides a **high-level XNA abstraction**:

```cpp
// CNA
graphicsDevice.SetVertexBuffers(bindings);
effect->Apply();                        // high-level
graphicsDevice.DrawPrimitives(PrimitiveType::TriangleList, 0, count);
```

The direction is wrong: Urho3D would be calling a *high-level* API as the backend
for its own *low-level* renderer. Any bridge layer would be either leaky (Urho3D
internals bleeding through the XNA abstraction) or a performance bottleneck (every
draw call passing through an unnecessary extra layer).

### Additional Problems

| Problem | Severity |
|---|---|
| Urho3D shader system (GLSL/HLSL with its own preprocessor and variant cache) vs CNA Effect (.xnb bytecode style) | Critical |
| Math type conflicts: `Urho3D::Vector3` vs `Microsoft::Xna::Framework::Vector3` | Medium |
| CNA 3D pipeline is ~35 % complete — building on an incomplete foundation | Critical |
| Hundreds of internal Urho3D Graphics calls to remap, each requiring a design decision | High |
| Urho3D expects direct access to GPU objects (raw texture handles, shader variant cache) that CNA does not expose | High |

### Verdict

**Technically conceivable, practically unrealistic.** More time would be spent on
integration glue than on actual game development. Even with Claude Code handling all
mechanical coding, the architectural mismatch cannot be resolved by writing code alone
— it requires a design decision that eliminates the impedance mismatch.

---

## Approach B — Build Urho3D-Style Features on Top of CNA

### Architecture

```
┌─────────────────────────────────────────┐
│  Scene graph, ECS, ResourceManager,     │  ← new layer, Urho3D-inspired
│  Renderer, PhysicsWorld, ...            │
├─────────────────────────────────────────┤
│  CNA  (GraphicsDevice, Effect,          │  ← existing
│         SpriteBatch, Audio, Input...)   │
├─────────────────────────────────────────┤
│  SDL3 + EasyGL / Vulkan backend         │  ← existing
└─────────────────────────────────────────┘
```

CNA is the natural foundation, not a plugged-in backend. The XNA-style API becomes
the graphics contract that the scene graph depends on — exactly how MonoGame-based
engines are structured.

### Why This Works

- **No impedance mismatch** — the scene graph calls CNA in the direction it is designed to be called.
- **Incremental and testable** — each new class (Node, Component, Scene, Renderer) can be unit-tested in isolation, using the same checklist methodology as CNA porting.
- **CNA completeness grows in parallel** — as `BasicEffect`, `DrawPrimitives`, and `VertexBuffer` are completed, the scene graph immediately benefits without any bridging work.
- **Math types are shared** — `Vector3`, `Matrix`, `Quaternion` from SharpRuntime/CNA are used directly; no type conversion layer needed.

### Feasibility for Claude Code

Approach B maps directly to the proven FNA→CNA porting methodology:

| Task | Approach | Same as |
|---|---|---|
| `Node`, `Component`, `Scene` | Add new classes on top of CNA | Adding GameComponent on top of Game |
| `StaticModel`, `AnimatedModel` | Wrap `Model` + `VertexBuffer` + `Effect` | Wrapping existing CNA types |
| `Light`, `Camera` | Data classes + matrix math | Pure math, fully testable |
| `ResourceCache` | Wrap `ContentManager` | Extend existing CNA class |

Each step produces a complete, tested, committed unit — exactly the "make and forget"
standard established in `CHECKLIST.md`.

### Verdict

**Realistic and the correct architecture.** Build *up* from CNA, not *around* Urho3D.

---

## Recommended Path for a 3D Speedy Blupi Remake

Before building the scene graph, complete the 3D vertical slice in CNA:

1. `VertexBuffer` → `DrawPrimitives` → lit triangle on screen (fix EasyGL backend)
2. `BasicEffect` with World/View/Projection matrices actually uploaded to GLSL uniforms
3. `Model` loading (OBJ or GLTF via `ContentManager`, bypassing `.xnb`)

Once the vertical slice works, scene graph development on top of CNA is straightforward
and Claude Code can execute it efficiently class by class.

---

## Summary

| | Fork Urho3D → CNA backend | Scene graph on top of CNA |
|---|---|---|
| Architectural fit | Poor (wrong abstraction direction) | Excellent |
| CNA 3D readiness required | High (exposes every gap immediately) | Low (grow together) |
| Testability per step | Low | High |
| Feasibility for Claude Code | Low (context, impedance mismatch) | High (proven methodology) |
| **Recommendation** | **No** | **Yes** |
