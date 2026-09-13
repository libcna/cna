#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

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
    vec2 direction = pc.uBloomParams.xy;
    const float w0 = 0.2270270270;
    const float w1 = 0.1945945946;
    const float w2 = 0.1216216216;
    const float w3 = 0.0540540541;
    const float w4 = 0.0162162162;
    vec3 sum = texture(texture1, TexCoord).rgb * w0;
    sum += texture(texture1, TexCoord + direction * 1.0).rgb * w1;
    sum += texture(texture1, TexCoord - direction * 1.0).rgb * w1;
    sum += texture(texture1, TexCoord + direction * 2.0).rgb * w2;
    sum += texture(texture1, TexCoord - direction * 2.0).rgb * w2;
    sum += texture(texture1, TexCoord + direction * 3.0).rgb * w3;
    sum += texture(texture1, TexCoord - direction * 3.0).rgb * w3;
    sum += texture(texture1, TexCoord + direction * 4.0).rgb * w4;
    sum += texture(texture1, TexCoord - direction * 4.0).rgb * w4;
    FragColor = vec4(sum, 1.0) * SpriteColor;
}
