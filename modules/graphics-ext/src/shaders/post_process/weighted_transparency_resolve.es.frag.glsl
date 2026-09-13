#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uRevealage;

void main()
{
    vec4 accumulation = texture(texture1, TexCoord);
    float revealage = clamp(exp(texture(uRevealage, TexCoord).r), 0.0, 1.0);
    if (revealage > 0.9999)
        discard;

    vec3 colour = accumulation.rgb / max(accumulation.a, 1e-5);
    FragColor = vec4(colour, 1.0 - revealage);
}
