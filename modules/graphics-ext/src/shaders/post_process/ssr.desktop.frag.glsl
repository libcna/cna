#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform sampler2D uNormalSampler;
uniform vec2 uSsrVectors[1];
uniform mat4 uSsrMatrices[2];
uniform float uSsrScalars[9];

const float kCnaSkyDepth = 0.999;
const int kCnaRefineSteps = 6;

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uSsrScalars[8] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec3 cnaViewPositionFromDepth(vec2 uv, float linearDepth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray = uSsrMatrices[1] * clip;
    vec3 direction = ray.xyz / ray.w;
    return direction * (linearDepth / max(-direction.z, 1e-6));
}

vec2 cnaSnapToTexel(vec2 uv)
{
    return (floor(uv * uSsrVectors[0]) + 0.5) / uSsrVectors[0];
}

void main()
{
    vec3 sourceColor = texture(texture1, TexCoord).rgb;
    float centerDepth = cnaDecodeLinearDepth(
        texture(uDepthSampler, cnaSnapToTexel(TexCoord)));
    if (centerDepth <= 0.0 || centerDepth >= kCnaSkyDepth)
    {
        FragColor = vec4(sourceColor, 1.0) * SpriteColor;
        return;
    }

    vec4 normalTexel = texture(uNormalSampler, cnaSnapToTexel(TexCoord));
    float roughness = clamp(normalTexel.a, 0.0, 1.0);
    vec3 rawNormal = normalTexel.xyz * 2.0 - 1.0;
    vec3 normal = length(rawNormal) > 1e-4
                ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);

    vec3 position = cnaViewPositionFromDepth(TexCoord, centerDepth);
    vec3 incident = length(position) > 1e-6
                  ? normalize(position) : vec3(0.0, 0.0, -1.0);
    vec3 reflected = normalize(reflect(incident, normal));
    float stepLength = uSsrScalars[1] / uSsrScalars[7];
    vec3 hitColor = vec3(0.0);
    float hit = 0.0;
    vec3 lastClear = position;

    for (int i = 1; i <= 64; ++i)
    {
        if (float(i) > uSsrScalars[7])
            break;
        vec3 samplePosition = position + reflected * (stepLength * float(i));
        if (samplePosition.z >= -1e-6)
            break;

        vec4 clip = uSsrMatrices[0]
                  * vec4(samplePosition * uSsrScalars[0], 1.0);
        if (clip.w <= 0.0)
            break;
        vec2 sampleUv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 || sampleUv.y > 1.0)
            break;

        vec2 snappedUv = cnaSnapToTexel(sampleUv);
        float sceneDepth = cnaDecodeLinearDepth(textureLod(uDepthSampler, snappedUv, 0.0));
        if (sceneDepth <= 0.0 || sceneDepth >= kCnaSkyDepth)
            continue;

        float difference = -samplePosition.z - sceneDepth;
        if (difference > uSsrScalars[2] && difference < uSsrScalars[3])
        {
            vec3 nearPoint = lastClear;
            vec3 farPoint = samplePosition;
            for (int k = 0; k < kCnaRefineSteps; ++k)
            {
                vec3 middle = (nearPoint + farPoint) * 0.5;
                vec4 middleClip = uSsrMatrices[0]
                                * vec4(middle * uSsrScalars[0], 1.0);
                if (middleClip.w <= 0.0)
                    break;
                vec2 middleUv = cnaSnapToTexel(
                    (middleClip.xy / middleClip.w) * 0.5 + 0.5);
                float middleDepth = cnaDecodeLinearDepth(
                    textureLod(uDepthSampler, middleUv, 0.0));
                bool behind = middleDepth > 0.0 && middleDepth < kCnaSkyDepth &&
                              (-middle.z - middleDepth) > uSsrScalars[2];
                if (behind)
                    farPoint = middle;
                else
                    nearPoint = middle;
            }

            vec4 hitClip = uSsrMatrices[0]
                         * vec4(farPoint * uSsrScalars[0], 1.0);
            vec2 hitUv = cnaSnapToTexel((hitClip.xy / hitClip.w) * 0.5 + 0.5);
            vec3 rawHitNormal = texture(uNormalSampler, hitUv).xyz * 2.0 - 1.0;
            vec3 hitNormal = length(rawHitNormal) > 1e-4
                           ? normalize(rawHitNormal) : vec3(0.0, 0.0, 1.0);
            if (dot(reflected, hitNormal) > 0.0)
                break;

            vec2 toEdge = min(hitUv, vec2(1.0) - hitUv);
            float fade = uSsrScalars[5] > 0.0
                ? min(smoothstep(0.0, uSsrScalars[5], toEdge.x),
                      smoothstep(0.0, uSsrScalars[5], toEdge.y))
                : 1.0;
            vec2 blur = vec2(roughness * uSsrScalars[6]);
            vec3 gathered = texture(texture1, hitUv).rgb
                          + texture(texture1, hitUv + vec2( blur.x, 0.0)).rgb
                          + texture(texture1, hitUv + vec2(-blur.x, 0.0)).rgb
                          + texture(texture1, hitUv + vec2(0.0,  blur.y)).rgb
                          + texture(texture1, hitUv + vec2(0.0, -blur.y)).rgb;
            hitColor = gathered * 0.2;
            hit = fade;
            break;
        }

        lastClear = samplePosition;
    }

    FragColor = vec4(mix(sourceColor, hitColor, hit * uSsrScalars[4]), 1.0)
              * SpriteColor;
}
