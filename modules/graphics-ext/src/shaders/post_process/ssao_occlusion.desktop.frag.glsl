#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uNormalSampler;
uniform sampler2D uNoiseSampler;
uniform vec3 uSsaoKernel[64];
uniform vec2 uSsaoVectors[1];
uniform float uSsaoScalars[5];

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uSsaoScalars[4] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

void main()
{
    float centerDepth = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    if (centerDepth <= 0.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 rawNormal = texture(uNormalSampler, TexCoord).xyz * 2.0 - 1.0;
    vec3 normal = length(rawNormal) > 1e-4 ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);

    vec3 rawRandom = vec3(texture(uNoiseSampler, TexCoord * uSsaoVectors[0]).xy * 2.0 - 1.0,
                          0.0);
    vec3 randomVector = length(rawRandom) > 1e-4
                      ? normalize(rawRandom)
                      : vec3(1.0, 0.0, 0.0);

    vec3 rawTangent = randomVector - normal * dot(randomVector, normal);
    vec3 tangent = length(rawTangent) > 1e-4
                 ? normalize(rawTangent)
                 : normalize(cross(normal, vec3(0.0, 1.0, 0.0)) + vec3(1e-3, 0.0, 0.0));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int count = int(uSsaoScalars[3] + 0.5);
    for (int i = 0; i < 64; ++i)
    {
        if (i >= count)
            break;

        vec3 samplePosition = tbn * uSsaoKernel[i];
        vec2 sampleUv = TexCoord + samplePosition.xy * uSsaoScalars[0];
        float sampleDepth = cnaDecodeLinearDepth(textureLod(texture1, sampleUv, 0.0));
        if (sampleDepth <= 0.0)
            continue;

        if (sampleDepth < centerDepth - uSsaoScalars[1])
        {
            float rangeCheck = smoothstep(
                0.0, 1.0,
                uSsaoScalars[2] / max(abs(centerDepth - sampleDepth), 1e-5));
            occlusion += rangeCheck;
        }
    }

    float visibility = 1.0 - occlusion / float(count);
    FragColor = vec4(visibility, visibility, visibility, 1.0) * SpriteColor;
}
