#version 330 core

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform sampler2D uVolumeSampler;
uniform float uVolumetricResolveScalars[5];
uniform vec4 uRtFlipV;

vec2 cnaLogicalUv(vec2 primaryUv)
{
    return vec2(primaryUv.x, mix(primaryUv.y, 1.0 - primaryUv.y, uRtFlipV.x));
}

vec2 cnaSampleUv(vec2 logicalUv, float flip)
{
    return vec2(logicalUv.x, mix(logicalUv.y, 1.0 - logicalUv.y, flip));
}

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uVolumetricResolveScalars[4] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec2 cnaAtlasJoin(float slice, vec2 inside)
{
    float sliceWidth = 1.0 / uVolumetricResolveScalars[0];
    float texelWidth = sliceWidth / uVolumetricResolveScalars[1];
    float u = slice * sliceWidth + 0.5 * texelWidth
            + inside.x * texelWidth * (uVolumetricResolveScalars[1] - 1.0);
    return vec2(u, inside.y);
}

float cnaSliceDepth(float slice)
{
    float t = (slice + 0.5) / uVolumetricResolveScalars[0];
    return uVolumetricResolveScalars[3] * t * t;
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    vec2 logicalUv = cnaLogicalUv(TexCoord);
    float depth = cnaDecodeLinearDepth(
        texture(uDepthSampler, cnaSampleUv(logicalUv, uRtFlipV.y)));
    float travelled = (depth <= 0.0 || depth >= 0.999)
                    ? uVolumetricResolveScalars[3]
                    : min(depth * uVolumetricResolveScalars[2],
                          uVolumetricResolveScalars[3]);

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float previousDepth = 0.0;
    for (int i = 0; i < 64; ++i)
    {
        if (float(i) >= uVolumetricResolveScalars[0])
            break;
        float sliceDepth = cnaSliceDepth(float(i));
        if (sliceDepth > travelled)
            break;
        float thickness = sliceDepth - previousDepth;
        previousDepth = sliceDepth;

        vec2 volumeUv = cnaAtlasJoin(float(i), logicalUv);
        vec4 froxel = texture(uVolumeSampler, cnaSampleUv(volumeUv, uRtFlipV.z));
        float extinction = froxel.a * thickness;
        scattered += froxel.rgb * thickness * transmittance;
        transmittance *= exp(-extinction);
    }

    FragColor = vec4(source.rgb * transmittance + scattered, source.a) * SpriteColor;
}
