// Regression fixture: the vertex sampler intentionally has no texture or sampler-state
// assignment. The draw must therefore resolve GraphicsDevice.VertexTextures[2] and
// VertexSamplerStates[2] after EffectPass.Apply.
sampler VertexSampler : register(s2);

float Amount;

struct VertexInput
{
    float3 Position : POSITION0;
    float2 Sample : TEXCOORD0;
};

struct VertexOutput
{
    float4 Position : POSITION0;
    float4 Color : COLOR0;
};

VertexOutput VS(VertexInput input)
{
    VertexOutput output;
    float height = tex2Dlod(VertexSampler, float4(input.Sample.x, 0.5, 0.0, 0.0)).r;
    output.Position = float4(input.Position.x,
                             lerp(-0.78, -0.58 + height * Amount, input.Sample.y),
                             0.0, 1.0);
    output.Color = lerp(float4(0.15, 0.45, 0.95, 1.0),
                        float4(1.0, 0.78, 0.18, 1.0), height);
    return output;
}

float4 PS(VertexOutput input) : COLOR0
{
    return input.Color;
}

technique VertexTexture
{
    pass P0
    {
        VertexShader = compile vs_3_0 VS();
        PixelShader = compile ps_2_0 PS();
    }
}
