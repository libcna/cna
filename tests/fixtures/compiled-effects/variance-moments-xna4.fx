// CNA test fixture (cna-cs CSX-136): a two-tap blur of a variance shadow map's moments, a sprite
// pixel shader as willcraftia's LiSPSM demo blurs them. SpriteBatch supplies the vertex shader and
// the texture in sampler 0; the test sets the kernel. Direct3D 9 sampled a Vector2 texture at full
// 32-bit precision, so the blurred moments of (x, x * x) keep a variance of zero.
sampler MomentsSampler : register(s0);

float Weight;
float2 Offset;

float4 PS(float4 color : COLOR0, float2 texCoord : TEXCOORD0) : COLOR0
{
    return tex2D(MomentsSampler, texCoord) * Weight
         + tex2D(MomentsSampler, texCoord + Offset) * Weight;
}

technique Blur
{
    pass P0
    {
        PixelShader = compile ps_2_0 PS();
    }
}
