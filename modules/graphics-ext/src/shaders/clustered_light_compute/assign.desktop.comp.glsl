#version 430 core

layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer CnaLights { vec4 uLights[]; };
layout(std430, binding = 1) readonly buffer CnaMatrix { float uInverseProjection[]; };
layout(std430, binding = 2) buffer CnaCounts { int uCounts[]; };
layout(std430, binding = 3) buffer CnaIndices { int uIndices[]; };
layout(std140, binding = 4) uniform ClusteredLightParameters
{
    ivec4 uGrid;
    ivec4 uOutput;
    vec4 uDepth;
};

vec3 cnaUnproject(mat4 inverseProjection, float x, float y, float z)
{
    vec4 p = inverseProjection * vec4(x, y, z, 1.0);
    if (abs(p.w) <= 1e-9)
        return p.xyz;
    return p.xyz / p.w;
}

vec3 cnaAtDistance(vec3 atNear, vec3 atFar, float distance)
{
    float span = atNear.z - atFar.z;
    if (abs(span) <= 1e-9)
        return atNear;
    float t = (atNear.z + distance) / span;
    return vec3(atNear.x + (atFar.x - atNear.x) * t,
                atNear.y + (atFar.y - atNear.y) * t,
                -distance);
}

float cnaSliceDistance(int slice)
{
    if (slice == 0)
        return uDepth.x;
    if (slice == uGrid.z)
        return uDepth.y;
    return uDepth.x * pow(uDepth.y / uDepth.x, float(slice) / float(uGrid.z));
}

void main()
{
    int cluster = int(gl_GlobalInvocationID.x);
    if (cluster >= uOutput.y)
        return;

    int x = cluster % uGrid.x;
    int y = (cluster / uGrid.x) % uGrid.y;
    int slice = cluster / (uGrid.x * uGrid.y);

    mat4 inverseProjection = mat4(
        uInverseProjection[0],  uInverseProjection[1],  uInverseProjection[2],  uInverseProjection[3],
        uInverseProjection[4],  uInverseProjection[5],  uInverseProjection[6],  uInverseProjection[7],
        uInverseProjection[8],  uInverseProjection[9],  uInverseProjection[10], uInverseProjection[11],
        uInverseProjection[12], uInverseProjection[13], uInverseProjection[14], uInverseProjection[15]);

    float u0 = 2.0 * float(x) / float(uGrid.x) - 1.0;
    float u1 = 2.0 * float(x + 1) / float(uGrid.x) - 1.0;
    float v0 = 2.0 * float(y) / float(uGrid.y) - 1.0;
    float v1 = 2.0 * float(y + 1) / float(uGrid.y) - 1.0;
    float d0 = cnaSliceDistance(slice);
    float d1 = cnaSliceDistance(slice + 1);

    vec3 minimum = vec3(3.4028235e38);
    vec3 maximum = vec3(-3.4028235e38);
    for (int i = 0; i < 2; ++i)
    {
        float u = i == 0 ? u0 : u1;
        for (int j = 0; j < 2; ++j)
        {
            float v = j == 0 ? v0 : v1;
            vec3 atNear = cnaUnproject(inverseProjection, u, v, 0.0);
            vec3 atFar = cnaUnproject(inverseProjection, u, v, 1.0);
            for (int k = 0; k < 2; ++k)
            {
                vec3 p = cnaAtDistance(atNear, atFar, k == 0 ? d0 : d1);
                minimum = min(minimum, p);
                maximum = max(maximum, p);
            }
        }
    }

    int found = 0;
    for (int light = 0; light < uGrid.w; ++light)
    {
        vec4 sphere = uLights[light];
        if (sphere.w <= 0.0)
            continue;
        vec3 nearest = clamp(sphere.xyz, minimum, maximum);
        vec3 delta = sphere.xyz - nearest;
        if (dot(delta, delta) > sphere.w * sphere.w)
            continue;
        if (found < uOutput.x)
            uIndices[cluster * uOutput.x + found] = light;
        ++found;
    }

    uCounts[cluster] = min(found, uOutput.x);
    if (found > uOutput.x)
        uCounts[uOutput.y] = 1;
}
