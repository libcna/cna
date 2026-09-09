// SPDX-License-Identifier: MS-PL
// The effect x_effect_instance.x names. Nothing about the importer's answer depends on what it
// compiles to; it has to exist because the reference is resolved against the file beside it.
float3 Tint;
float Strength;
float2 Offset;
float4 Corners;
float4x4 World;
int Passes;
texture DiffuseTexture;
technique Replace
{
    pass Single
    {
    }
}
