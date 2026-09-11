#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2 uTexelSize;
uniform float uEdgeThreshold;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec3 center = texture(texture1, TexCoord).rgb;
    float lumaCenter = luma(center);
    float lumaNW = luma(texture(texture1, TexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb);
    float lumaNE = luma(texture(texture1, TexCoord + vec2( uTexelSize.x, -uTexelSize.y)).rgb);
    float lumaSW = luma(texture(texture1, TexCoord + vec2(-uTexelSize.x,  uTexelSize.y)).rgb);
    float lumaSE = luma(texture(texture1, TexCoord + vec2( uTexelSize.x,  uTexelSize.y)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    if (lumaMax - lumaMin < uEdgeThreshold) {
        FragColor = vec4(center, 1.0);
        return;
    }

    vec2 direction = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                           ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
    float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + 0.125);
    direction = clamp(direction * scale, vec2(-8.0), vec2(8.0)) * uTexelSize;

    vec3 blended = 0.5 * (texture(texture1, TexCoord + direction * (1.0 / 3.0 - 0.5)).rgb +
                          texture(texture1, TexCoord + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 wider = blended * 0.5 +
                 0.25 * (texture(texture1, TexCoord + direction * -0.5).rgb +
                         texture(texture1, TexCoord + direction *  0.5).rgb);

    float lumaWider = luma(wider);
    FragColor = vec4((lumaWider < lumaMin || lumaWider > lumaMax) ? blended : wider, 1.0)
              * SpriteColor;
}
