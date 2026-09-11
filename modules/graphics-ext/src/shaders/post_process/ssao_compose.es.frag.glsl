#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uOcclusionSampler;
uniform vec2 uSsaoComposeVectors[1];
uniform float uSsaoComposeScalars[1];

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
