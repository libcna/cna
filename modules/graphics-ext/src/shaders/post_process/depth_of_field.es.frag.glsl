#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform float uDofScalars[6];

const float kCnaSensorHeightMm = 24.0;
const vec2 kDisc[16] = vec2[16](
    vec2( 0.2165,  0.0745), vec2(-0.1863,  0.2549), vec2(-0.0851, -0.3777), vec2( 0.3966,  0.2154),
    vec2(-0.4644,  0.1509), vec2( 0.2260, -0.4707), vec2( 0.1751,  0.5411), vec2(-0.5527, -0.2338),
    vec2( 0.6033, -0.2467), vec2(-0.2249,  0.6414), vec2(-0.3225, -0.6321), vec2( 0.7108,  0.2116),
    vec2(-0.7357,  0.2420), vec2( 0.2941, -0.7628), vec2( 0.2497,  0.8004), vec2(-0.8098, -0.2646)
);

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uDofScalars[5] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

float cnaCircleOfConfusionMm(float depthWorld)
{
    float focusDistance = uDofScalars[1];
    float focalLength = uDofScalars[2];
    float fNumber = uDofScalars[3];
    if (depthWorld <= 0.0 || focusDistance <= 0.0 || fNumber <= 0.0)
        return 0.0;
    float focusMm = focusDistance * 1000.0;
    float depthMm = depthWorld * 1000.0;
    if (focusMm <= focalLength)
        return 0.0;
    return (focalLength * focalLength / (fNumber * (focusMm - focalLength)))
         * abs(depthMm - focusMm) / depthMm;
}

float cnaBlurRadius(float linearDepth)
{
    float diameterMm = cnaCircleOfConfusionMm(linearDepth * uDofScalars[0]);
    return min(0.5 * diameterMm / kCnaSensorHeightMm, uDofScalars[4]);
}

void main()
{
    vec3 centerColor = texture(texture1, TexCoord).rgb;
    float centerDepth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
    if (centerDepth <= 0.0 || centerDepth >= 0.999)
    {
        FragColor = vec4(centerColor, 1.0) * SpriteColor;
        return;
    }

    float centerRadius = cnaBlurRadius(centerDepth);
    vec3 sum = centerColor;
    float weight = 1.0;
    for (int i = 0; i < 16; ++i)
    {
        vec2 offset = kDisc[i] * centerRadius;
        vec2 tapUv = TexCoord + offset;
        if (tapUv.x < 0.0 || tapUv.x > 1.0 || tapUv.y < 0.0 || tapUv.y > 1.0)
            continue;
        float tapDepth = cnaDecodeLinearDepth(texture(uDepthSampler, tapUv));
        if (tapDepth <= 0.0 || tapDepth >= 0.999)
            continue;
        float tapRadius = cnaBlurRadius(tapDepth);
        float accept = smoothstep(0.0, max(length(offset), 1e-5), tapRadius);
        sum += texture(texture1, tapUv).rgb * accept;
        weight += accept;
    }
    FragColor = vec4(sum / max(weight, 1e-5), 1.0) * SpriteColor;
}
