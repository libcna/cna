#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uGrainParams;
} pc;

float cnaHash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 pixel = floor(TexCoord * pc.uGrainParams.xy);
    float noise = cnaHash(pixel + vec2(pc.uGrainParams.w * 71.0,
                                       pc.uGrainParams.w * 113.0)) - 0.5;

    float luma = dot(source.rgb, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - abs(clamp(luma, 0.0, 1.0) * 2.0 - 1.0);

    FragColor = vec4(source.rgb + noise * pc.uGrainParams.z * weight, source.a)
              * SpriteColor;
}
