#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 uGrainParams;

float cnaHash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 pixel = floor(TexCoord * uGrainParams.xy);
    float noise = cnaHash(pixel + vec2(uGrainParams.w * 71.0,
                                       uGrainParams.w * 113.0)) - 0.5;

    float luma = dot(source.rgb, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - abs(clamp(luma, 0.0, 1.0) * 2.0 - 1.0);

    FragColor = vec4(source.rgb + noise * uGrainParams.z * weight, source.a)
              * SpriteColor;
}
