#version 330 core
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 uUpscaleParams;
uniform float uIdentity;

float cnaLuma(vec3 colour) { return dot(colour, vec3(0.2126, 0.7152, 0.0722)); }

vec3 cnaFetch(vec2 texel)
{
    vec2 sourceSize = uUpscaleParams.xy;
    return texture(texture1, clamp(texel, vec2(0.5), sourceSize - 0.5) / sourceSize).rgb;
}

void main()
{
    if (uIdentity > 0.5)
    {
        FragColor = texture(texture1, TexCoord) * SpriteColor;
        return;
    }

    vec2 position = TexCoord * uUpscaleParams.xy - 0.5;
    vec2 base = floor(position);
    vec2 f = position - base;

    vec3 c00 = cnaFetch(base + vec2(0.5, 0.5));
    vec3 c10 = cnaFetch(base + vec2(1.5, 0.5));
    vec3 c01 = cnaFetch(base + vec2(0.5, 1.5));
    vec3 c11 = cnaFetch(base + vec2(1.5, 1.5));
    vec3 upscaled = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);

    if (uUpscaleParams.w > 0.5)
    {
        float l00 = cnaLuma(c00), l10 = cnaLuma(c10);
        float l01 = cnaLuma(c01), l11 = cnaLuma(c11);
        vec2 gradient = vec2((l10 + l11) - (l00 + l01),
                             (l01 + l11) - (l00 + l10));
        float strength = length(gradient);
        if (strength > 1e-4)
        {
            vec2 edge = normalize(vec2(-gradient.y, gradient.x));
            vec3 along = cnaFetch(base + vec2(0.5, 0.5) + f + edge)
                       + cnaFetch(base + vec2(0.5, 0.5) + f - edge);
            float trust = clamp(strength * 2.0, 0.0, 1.0) * 0.5;
            upscaled = mix(upscaled, along * 0.5, trust);
        }
    }

    if (uUpscaleParams.z > 0.0)
    {
        vec3 up = cnaFetch(base + vec2(0.5, -0.5));
        vec3 down = cnaFetch(base + vec2(0.5, 1.5));
        vec3 left = cnaFetch(base + vec2(-0.5, 0.5));
        vec3 right = cnaFetch(base + vec2(1.5, 0.5));
        vec3 neighbourhood = (up + down + left + right) * 0.25;
        vec3 sharpened = upscaled + (upscaled - neighbourhood) * uUpscaleParams.z;
        vec3 lowest = min(min(min(up, down), min(left, right)), upscaled);
        vec3 highest = max(max(max(up, down), max(left, right)), upscaled);
        upscaled = clamp(sharpened, lowest, highest);
    }

    FragColor = vec4(upscaled, 1.0) * SpriteColor;
}
