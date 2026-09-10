#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uBloomSampler;

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
    vec4 scene = texture(texture1, TexCoord);
    vec3 bloom = texture(uBloomSampler, TexCoord).rgb;
    FragColor = vec4(scene.rgb + bloom * pc.uBloomParams.x, scene.a) * SpriteColor;
}
