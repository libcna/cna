#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uDecalSampler;
layout(set = 1, binding = 2) uniform sampler2D uNormalSampler;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uDecalScalars[72];
};
layout(set = 1, binding = 14, std140) uniform Vec3Array
{
    vec3 uDecalVectors[72];
};
layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uDecalMatrices[72];
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uDecalScalars[4] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec3 cnaViewPositionFromDepth(vec2 uv, float linearDepth)
{
    vec2 cameraUv = vec2(uv.x, 1.0 - uv.y);
    vec4 clip = vec4(cameraUv * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray = uDecalMatrices[0] * clip;
    vec3 direction = ray.xyz / ray.w;
    return direction * (linearDepth / max(-direction.z, 1e-6));
}

void main()
{
    float depth = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    if (depth >= 0.999)
        discard;

    vec3 viewPosition = cnaViewPositionFromDepth(TexCoord, depth)
                      * uDecalScalars[0];
    vec3 local = (uDecalMatrices[1] * vec4(viewPosition, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3(0.5))))
        discard;

    if (uDecalScalars[3] > 0.5)
    {
        vec3 normal = normalize(texture(uNormalSampler, TexCoord).xyz * 2.0 - 1.0);
        if (dot(normal, -uDecalVectors[0]) < uDecalScalars[2])
            discard;
    }

    vec4 decal = texture(uDecalSampler, local.xy + 0.5);
    FragColor = vec4(decal.rgb * uDecalVectors[1], decal.a * uDecalScalars[1])
              * SpriteColor;
}
