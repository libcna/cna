// Shader Model 5.0 (vs_5_0). Per-vertex-lit sibling of lit_textured3d.vert.hlsl/.frag.hlsl --
// plans/plan_graphics.md Phase 80 (Task 1106/1107): real XNA renders BasicEffect's lit path per-vertex
// by default (PreferPerPixelLighting == false), not per-pixel. Identical Blinn-Phong math to the
// per-pixel sibling (FNA's Lighting.fxh ComputeLights()), moved into this vertex stage instead of
// the pixel shader -- the interpolated result (not the interpolated inputs) is what reaches the
// pixel shader, giving real Gouraud shading. Only selected when lightingEnabled is already true
// (see each renderer's own dispatch site), so unlike the per-pixel sibling this shader has no
// runtime lightingEnabled branch -- it is unconditionally lit, matching a real XNA compiled
// shader family's own "one shader per feature combination" model.
// Stride 32: VertexPositionNormalTexture -- float3 pos + float3 normal + float2 uv.

cbuffer PerDraw : register(b0)
{
    row_major float4x4 Mvp;
    float4 DiffuseColor;
    float3 AmbientColor;
    float  LightingEnabled;
    float3 Light0Dir;
    float  TextureEnabled;
    float3 Light0Diffuse;
    float  VertexColorEnabled;
};

cbuffer LitLightParams : register(b1)
{
    float4 Light1DirPad;
    float4 Light1DiffusePad;
    float4 Light2DirPad;
    float4 Light2DiffusePad;
    float4 EmissiveColorPad;
    row_major float4x4 World;
    float4 EyePosPad;
    float4 Light0SpecularPad;
    float4 Light1SpecularPad;
    float4 Light2SpecularPad;
    float4 SpecularColorPower;
    float4 FogColor;  // xyz = FogColor, w = reserved padding
    float4 FogVector;      // CPU-prepared FNA view-space fog vector
};

struct VSInput
{
    float3 Position : POSITION0;
    float3 Normal   : NORMAL0;
    float2 UV       : TEXCOORD0;
};

struct VSOutput
{
    float4 Position    : SV_Position;
    float2 UV          : TEXCOORD0;
    float4 Tint        : TEXCOORD1;
    float  FogFactor   : TEXCOORD2;
    float3 LitRGB      : TEXCOORD3;
    float3 SpecularRGB : TEXCOORD4;
};

// Returns transpose(inverse(m)) directly -- same deliberate deviation from a 1:1 GLSL port that
// lit_textured3d.vert.hlsl's own copy already documents (HLSL has no built-in inverse()).
float3x3 InverseTranspose3x3(float3x3 m)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    return float3x3(c0, c1, c2) / det;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 pos = mul(float4(input.Position, 1.0), Mvp);
    output.Position = pos;
    output.UV = input.UV;
    output.Tint = DiffuseColor;

    float3x3 normalMatrix = InverseTranspose3x3((float3x3)World);
    float3 N = normalize(mul(input.Normal, normalMatrix));
    float3 worldPos = mul(float4(input.Position, 1.0), World).xyz;
    float3 E = normalize(EyePosPad.xyz - worldPos);

    float3 nL0 = normalize(Light0Dir);
    float3 nL1 = normalize(Light1DirPad.xyz);
    float3 nL2 = normalize(Light2DirPad.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdotL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdotL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdotL2 = max(dotL2, 0.0);
    float3 lightSum = AmbientColor + NdotL0 * Light0Diffuse
                     + NdotL1 * Light1DiffusePad.xyz + NdotL2 * Light2DiffusePad.xyz;

    float3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, SpecularColorPower.w);
    float3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, SpecularColorPower.w);
    float3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, SpecularColorPower.w);
    output.SpecularRGB = (spec0 * Light0SpecularPad.xyz + spec1 * Light1SpecularPad.xyz
                          + spec2 * Light2SpecularPad.xyz) * SpecularColorPower.xyz;

    output.LitRGB = lightSum * DiffuseColor.rgb + EmissiveColorPad.xyz;

    // REMED-GFX-005/010: FNA view-space fog. FogVector now carries EffectHelpers.SetFogVector
    // (World*View 3rd column baked CPU-side); keep = 1 - saturate(dot(pos, fogVector)) is the
    // corrected (non-mirrored) FNA factor in eye-space Z, not object-space. Zero vector = no fog.
    output.FogFactor = 1.0 - saturate(dot(float4(input.Position, 1.0), FogVector));

    return output;
}
