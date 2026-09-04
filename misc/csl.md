# CSL — the CNA Shader Language and the "Shader Rosetta Stone" subsystem

> **This is the original architectural proposal, kept for its rationale. It is not a specification
> and it is not a status report.** Nothing described below exists in the repository yet.
>
> The executable task backlog derived from this document is **[`plans/plan_csl.md`](../plans/plan_csl.md)**,
> whose §0.2 lists the twelve corrections it makes to this file (the `spec/` directory of §34, the
> module layout, the "46 renderers" count, the renderer-declares-a-profile hook of §12, and the
> capability vocabulary of §10, among others). Where this document and that plan disagree, **the
> plan is right and this one is history**; the same applies to any specification later written under
> `docs/csl/`.

Yes. I would make this a standalone CNA subsystem, not just a "shader transpiler". The goal would
be:

> Describe a shader and its meaning once, then run it deterministically on any CNA renderer whose
> capabilities allow it.

And above all: 46 renderers must not mean 46 shader compilers. A renderer signs up to one of a
small number of shader target profiles.

## 1. Proposed names

Working names:

**CSL** — CNA Shader Language
the native CNA shader language

**CSIR** — CNA Shader Intermediate Representation
the normalized representation of a shader

**CSE** — CNA Shader Effect
an optional layer for techniques/passes/pipeline state

**CSP** — CNA Shader Package
the compiled package consumed at runtime

**CNA Shader Conformance Suite**

Command-line tools:

```text
cna-shaderc
cna-shader-info
cna-shader-test
cna-shader-diff
```

I would use the name "Shader Rosetta Stone" as the name of the project/subsystem rather than of any
one artifact.

---

## 2. Core architecture

Not this:

```text
HLSL -> GLSL
HLSL -> MSL
HLSL -> WGSL

GLSL -> HLSL
GLSL -> MSL
...
```

That would end in a catastrophe of N×M converters.

Instead:

```text
                +----------+
CSL ----------> |          |
HLSL/FX ------> | Frontend |
GLSL ---------->|          |
                +----+-----+
                     |
                     v
               +-----------+
               |   CSIR    |
               +-----------+
                     |
        +------------+------------+
        |            |            |
        v            v            v
      GLSL         HLSL        SPIR-V
        |            |            |
        v            v            v
     OpenGL      D3D11/12      Vulkan
        |
        +-------> GLES

CSIR -> MSL  -> Metal
CSIR -> WGSL -> WebGPU
```

So, for example, 46 CNA renderers may in reality use only some 8–12 shader target profiles.

---

## 3. Two levels of IR

Here I would be careful: I would not, in a first version, implement my own alternative to SPIR-V
with hundreds of instructions.

I would give CSIR two layers.

### CSIR-H — high-level semantic IR

Owned by CNA.

It contains:

```text
types
functions
expressions
control flow
shader stages
vertex inputs
fragment outputs
interpolation
resources
bindings
uniform blocks
textures
samplers
storage buffers
specialization constants
source locations
reflection metadata
capability requirements
```

For example:

```text
Function VSMain
Stage: Vertex

Input:
    POSITION : float3 location=0
    TEXCOORD : float2 location=1

Resources:
    Camera : uniform-buffer group=0 binding=0
    Diffuse : texture2D group=1 binding=0
    LinearSampler : sampler group=1 binding=1

Output:
    Position : builtin(position)
    TexCoord : location=0
```

### CSIR-L — lowered, executable IR

Here I would make maximum use of a SPIR-V-like model — or of SPIR-V itself as one of the canonical
serializations — instead of inventing my own machine-level shader language.

CSIR-H therefore defines the *meaning*.

CSIR-L is:

```text
typed operations
explicit control flow
explicit resource bindings
explicit layouts
explicit stage I/O
explicit capabilities
```

and the individual backends are generated from it.

---

## 4. CSL — the CNA Shader Language

I would deliberately make CSL fairly similar to HLSL/WGSL/C++ so that a CNA developer does not have
to learn an exotic language.

For example:

```text
struct VertexInput
{
    @location(0)
    float3 position;

    @location(1)
    float3 normal;

    @location(2)
    float2 texCoord;
};

struct VertexOutput
{
    @builtin(position)
    float4 position;

    @location(0)
    float3 normal;

    @location(1)
    float2 texCoord;
};

@group(0) @binding(0)
uniform CameraData
{
    float4x4 viewProjection;
};

@group(1) @binding(0)
Texture2D<float4> diffuseTexture;

@group(1) @binding(1)
SamplerState diffuseSampler;

@vertex
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;

    output.position =
        float4(input.position, 1.0) * viewProjection;

    output.normal = input.normal;
    output.texCoord = input.texCoord;

    return output;
}

@fragment
float4 PSMain(VertexOutput input)
{
    return diffuseTexture.sample(
        diffuseSampler,
        input.texCoord);
}
```

The result could be:

```text
Vulkan     -> SPIR-V
D3D11      -> HLSL SM5 -> bytecode
D3D12      -> HLSL SM6 -> DXIL
OpenGL 3   -> GLSL 330
OpenGL 4   -> GLSL 450
OpenGL ES  -> GLSL ES
Metal      -> MSL
WebGPU     -> WGSL
```

---

## 5. CSL must not be "C++ for the GPU"

This is important.

I would not try to cram into the language:

```text
classes
inheritance
general C++-style templates
exceptions
dynamic allocation
RTTI
pointers of any kind
the STL
```

Otherwise you stop developing CNA and start developing your own Clang.

CSL v1:

```text
scalar
vector
matrix
array
struct

if
switch
for
while

functions

vertex
fragment
compute

uniform/storage resources
textures
samplers
atomics
```

And that is it.

---

## 6. A single CNA Shader ABI

In my view this matters even more than CSL's syntax.

CNA must define precisely what a shader *means*, independently of the renderer.

For example, the coordinate system. CNA can define:

```text
clip depth:        0 .. 1
texture UV origin: top-left
fragment origin:   top-left
```

Does OpenGL need different rules? The compiler/backend inserts the correction.

A developer must not have to write:

```text
#ifdef VULKAN
    ...
#elif OPENGL
    ...
#endif
```

unless it really is a platform-specific optimization.

---

## 7. Matrices need an absolutely precise specification

This is the source of an enormous number of cross-API bugs.

The CSL specification must settle:

```text
what a * b means
vector × matrix
matrix × vector
row-major vs column-major storage
uniform buffer layout
alignment
padding
bool representation
matrix stride
```

For example, CNA can standardize:

```text
float4 transformed =
    float4(position, 1) * worldViewProjection;
```

and the backend guarantees the same result everywhere.

Never leave the meaning to depend on an HLSL or GLSL default.

---

## 8. Binding model

I would use a modern abstraction:

```text
group
binding
```

For example:

```text
@group(0) @binding(0)
uniform Camera;

@group(1) @binding(0)
Texture2D Albedo;

@group(1) @binding(1)
SamplerState LinearSampler;
```

The mapping:

| CSIR    | Vulkan         | WGSL    | HLSL           | Metal                |
| ------- | -------------- | ------- | -------------- | -------------------- |
| group   | descriptor set | group   | register space | argument group       |
| binding | binding        | binding | register       | buffer/texture index |

The OpenGL backend performs its own mapping.

This means that the application deals only with:

```text
ShaderBinding(1, 0)
```

and does not care at all about:

```text
register(t3)
layout(binding = 4)
[[texture(2)]]
```

---

## 9. Keep textures and samplers separate

I would design CSIR the way modern APIs do:

```text
Texture
Sampler
```

separately.

Not:

```text
combined texture+sampler
```

A legacy OpenGL backend can combine them itself if it needs to.

Going

```text
separate -> combined
```

is far easier than reconstructing

```text
combined -> separate
```

after the fact.

---

## 10. Shader capabilities

A CSL shader must carry a list of requirements.

For example:

```text
requires:
    vertex_shader
    fragment_shader
    derivatives
    texture_array
    mrt >= 4
```

Or:

```text
@requires(storage_buffers)
@requires(compute)
```

The renderer then says, before compilation:

```text
Supported
```

or:

```text
Shader requires FEATURE_STORAGE_BUFFER,
but renderer OpenGL21 does not provide it.
```

I would never do the silent

> "we'll approximate it somehow"

---

## 11. Shader tiers

For dozens of renderers I would define something like the following.

### CNA Shader Core

```text
vertex
fragment

float/int
vectors/matrices
uniforms
2D textures
samplers
basic control flow
```

Extremely portable.

### CNA Shader Extended

In addition:

```text
MRT
derivatives
texture arrays
integer textures
instancing
advanced interpolation
```

### CNA Shader Compute

```text
compute shaders
storage buffers
atomics
shared memory
```

### CNA Shader Advanced

```text
subgroups
bindless
ray tracing
mesh shaders
etc.
```

Old renderers simply support Core only.

---

## 12. A renderer will not implement a compiler

This is a fundamental architectural rule.

A renderer declares, for example:

```cpp
ShaderTargetProfile Renderer::shaderTarget()
{
    return ShaderTargetProfile::GLSL_450;
}
```

Another:

```cpp
return ShaderTargetProfile::SPIRV_VULKAN_13;
```

Another:

```cpp
return ShaderTargetProfile::HLSL_SM50;
```

So that, for instance:

```text
Renderer VulkanSDL
Renderer VulkanGLFW
Renderer VulkanWhatever
```

can all use the same

```text
SPIRV_VULKAN
```

shader backend.

---

## 13. Target profiles

For example:

```text
spirv-vulkan-1.0
spirv-vulkan-1.3

hlsl-sm4
hlsl-sm5
hlsl-sm6

glsl-120
glsl-330
glsl-450

glsles-100
glsles-300
glsles-310

msl

wgsl
```

This shrinks dozens of renderers down to a fairly small number of real shader backends.

---

## 14. What about the existing XNA `.fx`?

I would definitely not throw this away. On the contrary:

```text
                     +----------+
.fx / HLSL --------> | frontend |
                     +----+-----+
                          |
                          v
                         CSIR

.csl --------------> CSL frontend
                          |
                          v
                         CSIR
```

That is:

> FX is the compatibility frontend, CSL is the native frontend.

This would connect your XNA work directly to the Rosetta Stone.

---

## 15. Separate techniques/passes from the shader language

FX contains, for example:

```text
technique Basic
{
    pass P0
    {
        VertexShader = ...
        PixelShader = ...
    }
}
```

That is no longer just GPU programming; it is also a pipeline definition.

So I would have two IRs:

```text
ShaderIR
EffectIR
```

EffectIR:

```text
Effect
 └ Technique
    └ Pass
       ├ vertex shader
       ├ fragment shader
       ├ blend state
       ├ depth state
       ├ rasterizer state
       └ topology
```

Then:

```text
XNA FX
   |
   v
EffectIR + ShaderIR
```

And a native CNA effect syntax can use exactly the same thing.

---

## 16. CSP — the compiled shader package

I would not ship individual `.glsl`, `.spv` or `.hlsl` files to the runtime.

The content pipeline produces something like:

```text
StandardLit.csp
```

or a shader asset directly inside CNB.

The structure:

```text
CNA Shader Package
│
├── metadata
│   ├ version
│   ├ source hash
│   ├ compiler version
│   └ CSIR version
│
├── reflection
│   ├ entry points
│   ├ inputs
│   ├ outputs
│   ├ resources
│   ├ uniforms
│   └ capabilities
│
└── variants
    ├ spirv-vulkan
    ├ hlsl-sm5
    ├ hlsl-sm6
    ├ glsl-330
    ├ glsl-450
    ├ msl
    └ wgsl
```

And at runtime:

```cpp
auto shader =
    content.Load<Shader>("StandardLit");
```

CNA determines the renderer:

```text
Vulkan
```

and takes the

```text
spirv-vulkan
```

variant.

In my view this fits perfectly into your CNB system.

---

## 17. Not every variant has to be stored

The package builder can be told:

```bash
cna-shaderc standard.csl \
    --targets vulkan,d3d12,metal
```

and, for universal distribution:

```bash
--targets all
```

So a Windows D3D12 game does not have to drag along:

```text
WGSL
MSL
GLSL ES
```

---

## 18. Runtime compilation for development only

Development:

```text
edit CSL
    ↓
compile
    ↓
hot reload
```

Release:

```text
.csl
   ↓
content pipeline
   ↓
CNB
   ↓
precompiled shader binaries
```

A production application should not necessarily need a shader compiler at all.

---

## 19. Hot reload

I would build this into the Rosetta Stone right away.

For example:

```bash
cna run --shader-watch
```

You change:

```text
terrain.csl
```

CNA:

```text
1. compile
2. validate reflection compatibility
3. create new GPU pipeline
4. atomically swap
5. destroy old pipeline
```

If compilation fails:

```text
ERROR terrain.csl:77
float3 cannot be assigned to float4
```

and the application keeps using the last working shader.

---

## 20. Reflection must be a first-class API

For example:

```cpp
shader->GetEntryPoints();
shader->GetResources();
shader->GetUniformBlocks();
shader->GetVertexInputs();
shader->GetRequiredCapabilities();
```

A resource:

```text
ShaderResource
{
    name       = "Albedo";
    group      = 1;
    binding    = 0;
    type       = Texture2D;
    arrayCount = 1;
}
```

This is then used by:

```text
the renderer
the material system
the editor
the debugger
the conformance suite
hot reload
the asset inspector
```

---

## 21. `cna-shader-info`

Then you can build something beautiful:

```bash
cna-shader-info StandardLit.cnb
```

Output:

```text
Shader: StandardLit

Stages:
  VSMain     vertex
  PSMain     fragment

Resources:
  0:0 Camera        UniformBuffer
  1:0 Albedo        Texture2D
  1:1 AlbedoSampler Sampler

Requirements:
  CNA_SHADER_CORE

Variants:
  SPIR-V Vulkan
  HLSL SM5
  HLSL SM6
  GLSL 330
  GLSL 450
  MSL
  WGSL
```

---

## 22. And now the craziest part: the conformance corpus

I would not write 100 large shaders. I would write thousands of microscopic tests.

For example:

```text
arithmetic/add-float
arithmetic/divide-vector
vector/swizzle-xyzw
vector/swizzle-zyx

matrix/mat3-multiply
matrix/mat4-vector
matrix/vector-mat4

control/if
control/nested-if
control/for
control/switch

texture/sample-nearest
texture/sample-linear
texture/sample-lod

interpolation/flat
interpolation/perspective

builtin/vertex-id
builtin/instance-id
builtin/frag-coord

depth/write
discard/basic

buffer/uniform-layout
buffer/array-layout
```

Each individual test can be some 30 lines of shader.

---

## 23. Every test must be extremely deterministic

For example, the test:

```text
vector/add-float3
```

draws:

```text
R = x
G = y
B = z
```

into, say, a

```text
64 × 64
```

offscreen framebuffer. Not a beautiful scene.

An image like that:

```text
expected = [0.25, 0.50, 0.75, 1]
```

is very easy to check automatically.

---

## 24. Not a swapchain screenshot

For genuine shader conformance I would not primarily use a screenshot of the screen.

Better:

```text
Shader
  ↓
offscreen framebuffer
  ↓
RGBA8 / RGBA16F
  ↓
readback
  ↓
canonical image
```

That removes:

```text
the OS compositor
DPI scaling
the window manager
display gamma
the monitor
the screenshot API
```

and only afterwards, if you want, do you generate a PNG from the result.

---

## 25. Three kinds of image comparison

### Exact

For example an integer shader:

```text
expected:
FF 00 7F FF
```

must be bit-for-bit identical.

### Numeric tolerance

Floating point:

```text
abs(a-b) <= epsilon
```

for example:

```text
1e-5
```

per test.

### Perceptual

For large demonstration scenes:

```text
RMSE
SSIM-like metric
max pixel error
```

But micro-conformance tests should be numeric wherever possible, not perceptual.

---

## 26. A CPU reference shader interpreter

And now something even more interesting.

For CNA_SHADER_CORE I would write a simple CSIR interpreter on the CPU. It does not have to be
fast.

For example:

```text
CSIR shader
       |
       +---- Vulkan
       |
       +---- OpenGL
       |
       +---- D3D
       |
       +---- CPU reference
```

Then Vulkan is not "the truth". The truth is defined by:

```text
CSIR semantics
```

and the CPU implementation.

This would be fantastic for:

```text
float arithmetic
vectors
matrices
control flow
interpolation
simple texture sampling
```

---

## 27. N-way differential testing

On top of that:

```text
Reference CPU = A
Vulkan       = A
D3D12        = A
Metal        = A
OpenGL       = B
```

The Rosetta Stone immediately reports:

```text
OPENGL OUTLIER
test:
matrix/mat4-vector-0074
```

With 46 renderers this would be fantastically useful.

---

## 28. The test manifest

Each test:

```yaml
name: matrix/multiply/mat4-vector

tier: core

stages:
  - vertex
  - fragment

resolution:
  width: 32
  height: 32

format: rgba32f

tolerance:
  absolute: 0.00001

requires:
  - vertex_shader
  - fragment_shader
```

And a negative test:

```yaml
name: errors/type-mismatch-001

expect:
  compilation: failure

diagnostic:
  code: CSL1007
```

---

## 29. Conformance is not only rendering

I would split the corpus at least like this:

```text
Parser tests                  1000+
Semantic compiler tests       1000+
Negative compiler tests       1000+
CSIR tests                    1000+
Reflection tests               500+
Codegen tests                 1000+ × target
Runtime render tests          1000+
Compute tests                  500+
Stress/fuzz tests             practically unlimited
```

You can comfortably reach

> more than 10,000 shader conformance cases

without the repository becoming absurdly large.

---

## 30. Generated tests

For example, a generator takes:

```text
float
float2
float3
float4
```

and the operators:

```text
+
-
*
/
min
max
clamp
dot
```

and automatically produces the combinations. For example:

```text
dot-float2
dot-float3
dot-float4

min-float
min-float2
...
```

So you do not have to write thousands of tests by hand.

---

## 31. Shader fuzzing

The Rosetta Stone can generate valid CSIR:

```text
random expression
random vectors
random branches
random loops with fixed bounds
```

then run it on:

```text
CPU
Vulkan
OpenGL
D3D
```

and compare the results. If one backend is an outlier:

```text
store failing seed
```

for example:

```text
seed = 0x81FA7642
```

and turn it into a regression test.

In my view this would be absolutely brutal at finding driver/backend/compiler bugs.

---

## 32. A conformance dashboard

Eventually you can turn this into a page:

```text
libcna.com/conformance
```

For example:

| Renderer   | Core      | Extended | Compute |
| ---------- | --------- | -------- | ------- |
| Vulkan     | 1832/1832 | 924/924  | 441/441 |
| D3D12      | 1832/1832 | 924/924  | 441/441 |
| OpenGL 4.6 | 1832/1832 | 919/924  | 430/441 |
| OpenGL 2.1 | 1750/1832 | N/A      | N/A     |
| WebGPU     | 1832/1832 | 920/924  | 441/441 |

And you click:

```text
OpenGL 4.6: 5 failures
```

and you get:

```text
texture/gather/002
interpolation/centroid/004
...
```

That would be one of the most interesting things on the whole CNA website.

---

## 33. Even better: renderer certification

A formal concept could emerge:

```text
CNA Shader Core Certified
```

A renderer receives it only if 100 % of the mandatory tests pass.

For example:

```text
Vulkan renderer
CNA Shader Core Certified
CNA Shader Extended Certified
CNA Shader Compute Certified
```

An old renderer:

```text
OpenGL 1.x
Shader pipeline: unsupported
```

And that is entirely legitimate.

---

## 34. What I would put in the repository

For example:

```text
modules/
  shader/
    include/
    src/

    frontend/
      csl/
      hlsl/
      fx/

    ir/
      high/
      lower/

    backends/
      spirv/
      hlsl/
      glsl/
      msl/
      wgsl/

    reflection/
    optimizer/
    validator/
    package/

tools/
  cna-shaderc/
  cna-shader-info/
  cna-shader-test/
  cna-shader-diff/

tests/
  shader/
    parser/
    semantic/
    ir/
    reflection/
    codegen/
    conformance/
    fuzz/

spec/
  csl.md
  csir.md
  shader-abi.md
  shader-profiles.md
  shader-package.md
  conformance.md
```

---

## 35. The compiler pipeline

The complete path:

```text
source
  ↓
lexer
  ↓
parser
  ↓
AST
  ↓
name resolution
  ↓
type checking
  ↓
semantic validation
  ↓
CSIR-H
  ↓
capability analysis
  ↓
optimization
  ↓
lowering
  ↓
CSIR-L
  ↓
target legalization
  ↓
target backend
  ↓
external/native compiler
  ↓
binary/source
  ↓
reflection validation
  ↓
CSP / CNB
```

---

## 36. Every backend has exactly three duties

For example the GLSL backend:

```cpp
class GlslShaderBackend
{
    bool Supports(const ShaderProfile&);

    GeneratedShader Generate(
        const CsirModule&,
        const ShaderTarget&);

    BindingMap GenerateBindingMap(...);
};
```

And nothing else.

A renderer must not know:

```text
how CSL is parsed
how FX works
how CSIR is optimized
```

---

## 37. Diagnostics

I would make the compiler diagnostics stable and testable:

```text
CSL1001 UnexpectedToken
CSL1104 UnknownIdentifier
CSL1207 TypeMismatch
CSL2003 UnsupportedCapability
CSL3011 RecursiveFunction
CSL4002 BackendLimitation
```

Output:

```text
terrain.csl:51:17 CSL1207:
cannot convert float3 to float4

    output.position = position;
                      ^~~~~~~~
```

This matters for IDE support too.

---

## 38. A Language Server later on

Then you can build:

```text
cna-shader-lsp
```

and CLion/VS Code can have:

```text
completion
diagnostics
goto definition
hover type information
resource binding inspection
CSIR preview
```

But I would leave that far behind the MVP.

---

## 39. Shader debugging

Later:

```bash
cna-shaderc foo.csl --dump-ir
```

can print:

```text
AST
CSIR-H
CSIR-L
GLSL
SPIR-V disassembly
WGSL
HLSL
```

That is the real Rosetta Stone:

```text
        CSL

 HLSL   CSIR   GLSL

      SPIR-V

 MSL           WGSL
```

---

## 40. One beautiful GUI application

Finally I would build:

```text
CNA Shader Laboratory
```

On the left:

```text
CSL source
```

In the middle:

```text
preview
```

On the right, tabs:

```text
CSIR
SPIR-V
HLSL
GLSL
MSL
WGSL
Reflection
Capabilities
```

And at the top:

```text
Renderer:
[ Vulkan ▼ ]
```

You switch to:

```text
OpenGL 3.3
```

and immediately see the difference in the generated shader.

That would already be a fantastic standalone CNA tool.

---

## 41. But I would keep the MVP brutally small

I would not start with:

> "Implement CSL + FX + GLSL + HLSL + SPIR-V + MSL + WGSL + 10,000 tests."

An AI agent would most likely produce a giant unmaintainable mass.

### Milestone 0 — specification

Only:

```text
CSL semantics
CSIR model
coordinate conventions
resource binding ABI
type/layout rules
shader capability model
target profile model
```

No compiler.

---

## 42. Milestone 1 — a minimal vertical slice

Only:

```text
CSL
 ↓
CSIR
 ↓
GLSL
```

Shader:

```text
vertex + fragment
float/vector/matrix
uniform buffer
texture2D
sampler
```

And, say, one OpenGL renderer.

50 conformance shaders.

---

## 43. Milestone 2

Add:

```text
CSIR -> SPIR-V
```

Vulkan.

Now you can do the first real

```text
OpenGL vs Vulkan
```

cross-renderer conformance.

500 tests.

---

## 44. Milestone 3

Add:

```text
HLSL
D3D11
D3D12
```

And this is exactly where the Rosetta Stone starts to show its real value:

```text
GL
Vulkan
D3D
```

the same shader.

---

## 45. Milestone 4

Integration of the existing:

```text
.fx
```

that is:

```text
FX/HLSL
   ↓
unified effect/shader representation
```

In my view this is the moment when you join CNA's XNA compatibility to modern CNA graphics.

---

## 46. Milestone 5

```text
MSL
WGSL
```

Metal/WebGPU.

At that point the Rosetta Stone has practically every major current shader ecosystem.

---

## 47. Milestone 6

A CPU reference interpreter plus generative conformance.

And here it can start to grow:

```text
500
1000
2500
5000
10000
```

tests.

---

## 48. What I would explicitly forbid in the first specification

So that the project does not collapse:

```text
no in-house SPIR-V replacement
no full C++ frontend
no full hand-written HLSL parser
no automatic magical emulation of everything
no backend-specific #ifdef as an everyday mechanism
no renderer-specific shader compiler
no requirement to parse shaders at runtime
no beauty screenshots as the main conformance mechanism
```

---

## 49. The single biggest architectural rule

I would even write it in `shader-architecture.md` in capital letters:

> A CNA shader has one defined semantic meaning. Backends are implementations of that meaning, not
> alternate definitions of it.

That means:

```text
GLSL is not the reference.
HLSL is not the reference.
Vulkan is not the reference.
D3D is not the reference.
```

The CSL + CSIR specification is the reference.

---

## 50. The resulting flow in CNA

In the end the whole system would look like this:

```text
                SOURCE
                  │
      ┌───────────┼───────────┐
      │           │           │
     CSL        HLSL         FX
      │           │           │
      └───────────┼───────────┘
                  ↓
              CNA CSIR
                  │
          Capability pass
                  │
        Optimization/lowering
                  │
    ┌──────┬──────┼─────┬──────┐
    ↓      ↓      ↓     ↓      ↓
 SPIR-V   HLSL   GLSL  MSL    WGSL
    │      │      │     │      │
 Vulkan   D3D     GL   Metal WebGPU
    │      │      │     │      │
    └──────┴──────┼─────┴──────┘
                  ↓
         CNA renderer abstraction
```

And alongside it:

```text
                    CSIR
                      │
           ┌──────────┴──────────┐
           ↓                     ↓
      CPU reference       renderer under test
           │                     │
           ↓                     ↓
       expected               actual
           └──────────┬──────────┘
                      ↓
                 comparison
                      ↓
             conformance result
```

## What is most valuable about this for CNA

In truth I would consider CSL only the *second* most important thing.

The most valuable outcome of the Rosetta Stone would be:

> a unified CNA Shader ABI + CSIR + conformance framework.

Because then you can have 5, 20 or 46 renderers, and the framework has an automatic mechanism that
verifies whether they really all implement the same graphics semantics.

And that solves exactly the problem that arises when a framework has an absurd number of renderers:
it is not enough that each one "draws something". You need to prove that the same program means the
same thing everywhere.

In the end I would see the Rosetta Stone + the GPU Museum + frame capture/replay as three systems
that can merge into one truly insane CNA graphics validation stack.
