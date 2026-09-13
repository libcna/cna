#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uDepthSampler;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uContactScalars[72];
};
layout(set = 1, binding = 13, std140) uniform Vec2Array
{
    vec2 uContactVectors[72];
};
layout(set = 1, binding = 14, std140) uniform Vec3Array
{
    vec3 uContactDirections[72];
};
layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uContactMatrices[72];
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

const float kCnaSkyDepth = 0.999;

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uContactScalars[6] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec3 cnaViewPositionFromDepth(vec2 uv, float linearDepth)
{
    vec2 cameraUv = vec2(uv.x, 1.0 - uv.y);
    vec4 clip = vec4(cameraUv * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray = uContactMatrices[1] * clip;
    vec3 direction = ray.xyz / ray.w;
    return direction * (linearDepth / max(-direction.z, 1e-6));
}

vec2 cnaTextureUvFromClip(vec4 clip)
{
    vec2 ndc = clip.xy / clip.w;
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

vec2 cnaSnapToTexel(vec2 uv)
{
    return (floor(uv * uContactVectors[0]) + 0.5) / uContactVectors[0];
}

bool cnaContactOccluded(float rayViewDepth, float sceneViewDepth,
                        float bias, float thickness)
{
    float difference = rayViewDepth - sceneViewDepth;
    return difference > bias && difference < thickness;
}

void main()
{
    vec4 scene = texture(texture1, TexCoord);
    float centerDepth = cnaDecodeLinearDepth(
        texture(uDepthSampler, cnaSnapToTexel(TexCoord)));
    if (centerDepth <= 0.0 || centerDepth >= kCnaSkyDepth)
    {
        FragColor = scene * SpriteColor;
        return;
    }

    vec3 position = cnaViewPositionFromDepth(TexCoord, centerDepth)
                  * uContactScalars[0];
    float stepLength = uContactScalars[1] / uContactScalars[5];
    float occluded = 0.0;

    for (int i = 1; i <= 64; ++i)
    {
        if (float(i) > uContactScalars[5])
            break;
        vec3 samplePosition = position
                            + uContactDirections[0] * (stepLength * float(i));
        if (samplePosition.z >= -1e-6)
            break;

        vec4 clip = uContactMatrices[0] * vec4(samplePosition, 1.0);
        if (clip.w <= 0.0)
            break;
        vec2 sampleUv = cnaTextureUvFromClip(clip);
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
            sampleUv.y < 0.0 || sampleUv.y > 1.0)
            break;

        float sceneDepth = cnaDecodeLinearDepth(
            textureLod(uDepthSampler, cnaSnapToTexel(sampleUv), 0.0));
        if (sceneDepth <= 0.0 || sceneDepth >= kCnaSkyDepth)
            continue;

        if (cnaContactOccluded(-samplePosition.z, sceneDepth * uContactScalars[0],
                               uContactScalars[3], uContactScalars[2]))
        {
            occluded = 1.0;
            break;
        }
    }

    float visibility = 1.0 - occluded * clamp(uContactScalars[4], 0.0, 1.0);
    FragColor = vec4(scene.rgb * visibility, scene.a) * SpriteColor;
}
