// Shader Model 5.0 (vs_5_0). Ported line-by-line from
// src/CNA/Internal/Renderers/Vulkan/shaders/colored3d.vert.glsl (Task 364/888/899 history).
//
// Matrix convention (plans/plan_dx.md design decision 14): cbuffer matrices are declared row_major so
// the exact same byte layout XNA's CPU-side Matrix (row-major) uploads for Vulkan/EasyGL/Bgfx can
// be uploaded here unchanged (no CPU-side transpose). Every `M * v` in the GLSL source becomes
// `mul(v, M)` here (row-vector convention) -- this is the standard XNA/HLSL idiom (matches
// MonoGame's own DX11 HLSL effect sources) and is applied consistently across all 10 shader pairs.
//
// D3D-specific deviation from the GLSL source (not a bug carried over, a genuine renderer
// difference): no `pos.y = -pos.y` flip. That flip exists only in the Vulkan shader to correct for
// Vulkan's inverted-Y NDC vs OpenGL; D3D11's clip space already matches XNA's own convention
// directly. Same for depth range: D3D11 (like Vulkan-with-this-project's depth-clip-control setup)
// uses a [0,1] depth range, so the GLSL comment "no remap needed" applies unchanged here.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    float4 DiffuseColor;
    float4 _UnusedAmbientLight;   // ambientColor.xyz + lightingEnabled.w -- unused by this variant
    float4 _UnusedLight0DirTex;   // light0Dir.xyz + textureEnabled.w -- unused by this variant
    float3 _UnusedLight0Diffuse;
    float VertexColorEnabled;
};

// Task 899: fog forwarded via the shared colored3d/textured3d/colored_textured3d bundle's second
// constant buffer (GLSL set=0 binding=1) -- PerDraw above has zero spare bytes.
cbuffer FogParams : register(b1)
{
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
};

struct VSInput
{
    float3 Position : POSITION0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position     : SV_Position;
    float4 Color        : TEXCOORD0;
    float  FogFactor    : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;

    // Mix vertex color and diffuse based on VertexColorEnabled flag (matches
    // colored_textured3d.vert.hlsl's convention for the same flag).
    output.Color = (VertexColorEnabled > 0.5) ? input.Color * DiffuseColor : DiffuseColor;

    // REMED-GFX-005/010/061: FNA view-space fog. FogVector carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
