#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform vec4 uBloomParams;

void main()
{
    vec3 color = texture(texture1, TexCoord).rgb;
    float threshold = uBloomParams.x;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(threshold * 0.5, 1e-4);
    float contribution = clamp((luminance - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    contribution *= contribution;
    FragColor = vec4(color * contribution, 1.0) * SpriteColor;
}
