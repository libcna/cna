#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uRevealage;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 accumulation = texture(texture1, TexCoord);
    float revealage = clamp(exp(texture(uRevealage, TexCoord).r), 0.0, 1.0);
    if (revealage > 0.9999)
        discard;

    vec3 colour = accumulation.rgb / max(accumulation.a, 1e-5);
    FragColor = vec4(colour, 1.0 - revealage);
}
