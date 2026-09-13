#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uPalette;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uDepthParams;
    float unusedScalar;
} pc;

const float kBayer4x4[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
);

const float kBayer8x8[64] = float[64](
     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0
);

vec2 easyGlFragCoord()
{
    return vec2(gl_FragCoord.x, pc.viewportSize.y - gl_FragCoord.y);
}

float ditherThreshold()
{
    vec2 fragment = easyGlFragCoord();
    int ditherMode = int(pc.uDepthParams.y);
    if (ditherMode == 1)
    {
        int x = int(mod(fragment.x, 4.0));
        int y = int(mod(fragment.y, 4.0));
        return (kBayer4x4[y * 4 + x] + 0.5) / 16.0 - 0.5;
    }
    if (ditherMode == 2)
    {
        int x = int(mod(fragment.x, 8.0));
        int y = int(mod(fragment.y, 8.0));
        return (kBayer8x8[y * 8 + x] + 0.5) / 64.0 - 0.5;
    }
    return 0.0;
}

float quantizeChannel(float value, float levels)
{
    float dithered = value + ditherThreshold() / (levels - 1.0);
    return floor(clamp(dithered, 0.0, 1.0) * (levels - 1.0) + 0.5)
        / (levels - 1.0);
}

vec3 nearestPaletteColor(vec3 color)
{
    const float ditherStrength = 1.0 / 16.0;
    vec3 dithered = clamp(color + vec3(ditherThreshold() * ditherStrength), 0.0, 1.0);
    vec3 best = dithered;
    float bestDist = 1e9;
    int paletteSize = int(pc.uDepthParams.z);
    for (int i = 0; i < 256; ++i)
    {
        if (i >= paletteSize) break;
        vec3 candidate = texelFetch(uPalette, ivec2(i, 0), 0).rgb;
        vec3 difference = dithered - candidate;
        float distanceSquared = dot(difference, difference);
        if (distanceSquared < bestDist)
        {
            bestDist = distanceSquared;
            best = candidate;
        }
    }
    return best;
}

void main()
{
    vec4 texColor = texture(texture1, TexCoord) * SpriteColor;
    vec3 rgb = texColor.rgb;
    int mode = int(pc.uDepthParams.x);

    if (mode == 0)
    {
        rgb.r = quantizeChannel(rgb.r, 32.0);
        rgb.g = quantizeChannel(rgb.g, 64.0);
        rgb.b = quantizeChannel(rgb.b, 32.0);
    }
    else if (mode == 1)
    {
        rgb.r = quantizeChannel(rgb.r, 8.0);
        rgb.g = quantizeChannel(rgb.g, 8.0);
        rgb.b = quantizeChannel(rgb.b, 4.0);
    }
    else if (mode == 2 || mode == 3 || mode == 4)
    {
        float levels = mode == 2 ? 16.0 : (mode == 3 ? 4.0 : 2.0);
        float gray = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = vec3(quantizeChannel(gray, levels));
    }
    else
    {
        rgb = nearestPaletteColor(rgb);
    }

    FragColor = vec4(rgb, texColor.a);
}
