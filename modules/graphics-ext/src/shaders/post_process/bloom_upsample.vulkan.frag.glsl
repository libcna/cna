#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uSmallerSampler;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uBloomParams;
} pc;

void main()
{
    vec2 smallerTexel = pc.uBloomParams.xy;
    vec3 larger = texture(texture1, TexCoord).rgb;
    vec3 smaller;
    if (pc.uBloomParams.z < 0.5)
    {
        smaller = texture(uSmallerSampler, TexCoord).rgb;
    }
    else
    {
        vec2 halfTexel = smallerTexel * 0.5;
        smaller  = texture(uSmallerSampler, TexCoord + vec2(-halfTexel.x, -halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, TexCoord + vec2( halfTexel.x, -halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, TexCoord + vec2(-halfTexel.x,  halfTexel.y)).rgb;
        smaller += texture(uSmallerSampler, TexCoord + vec2( halfTexel.x,  halfTexel.y)).rgb;
        smaller *= 0.25;
    }
    FragColor = vec4(larger + smaller, 1.0) * SpriteColor;
}
