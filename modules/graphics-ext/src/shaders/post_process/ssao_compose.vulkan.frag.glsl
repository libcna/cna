#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uOcclusionSampler;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uSsaoComposeScalars[72];
};
layout(set = 1, binding = 13, std140) uniform Vec2Array
{
    vec2 uSsaoComposeVectors[72];
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

void main()
{
    float blurred = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            blurred += texture(
                uOcclusionSampler,
                TexCoord + vec2(float(x), float(y)) * uSsaoComposeVectors[0]).r;
        }
    }
    blurred /= 25.0;

    float visibility = clamp(
        1.0 - (1.0 - blurred) * uSsaoComposeScalars[0], 0.0, 1.0);
    vec4 scene = texture(texture1, TexCoord);
    FragColor = vec4(scene.rgb * visibility, scene.a) * SpriteColor;
}
