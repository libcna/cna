#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uVector;
    float uStrength;
} pc;

void main()
{
    vec2 fromCentre = TexCoord - vec2(0.5);
    vec2 redUv = vec2(0.5) + fromCentre * (1.0 + pc.uStrength);
    vec2 blueUv = vec2(0.5) + fromCentre * (1.0 - pc.uStrength);
    FragColor = vec4(texture(texture1, redUv).r,
                     texture(texture1, TexCoord).g,
                     texture(texture1, blueUv).b,
                     texture(texture1, TexCoord).a) * SpriteColor;
}
