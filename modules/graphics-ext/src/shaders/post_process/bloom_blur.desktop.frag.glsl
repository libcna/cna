#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform vec4 uBloomParams;

void main()
{
    vec2 direction = uBloomParams.xy;
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
