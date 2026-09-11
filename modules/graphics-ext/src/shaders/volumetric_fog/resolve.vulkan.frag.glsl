#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uDepthSampler;
layout(set = 1, binding = 2) uniform sampler2D uVolumeSampler;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uVolumetricResolveScalars[72];
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

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
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
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

        vec4 froxel = texture(uVolumeSampler, cnaAtlasJoin(float(i), TexCoord));
        float extinction = froxel.a * thickness;
        scattered += froxel.rgb * thickness * transmittance;
        transmittance *= exp(-extinction);
    }

    FragColor = vec4(source.rgb * transmittance + scattered, source.a) * SpriteColor;
}
