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
    vec3 color = texture(texture1, TexCoord).rgb;
    float threshold = pc.uBloomParams.x;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(threshold * 0.5, 1e-4);
    float contribution = clamp((luminance - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    contribution *= contribution;
    FragColor = vec4(color * contribution, 1.0) * SpriteColor;
}
