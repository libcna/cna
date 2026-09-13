#version 450

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(std430, set = 0, binding = 1) writeonly buffer Partials
{
    float partials[];
};
shared float sharedSums[64];

void main()
{
    vec2 grid = vec2(gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / grid;
    vec3 colour = texture(uScene, uv).rgb;
    float luminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    float value = log(max(luminance, 1e-4));

    uint local = gl_LocalInvocationIndex;
    sharedSums[local] = value;
    memoryBarrierShared();
    barrier();
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (local < stride)
            sharedSums[local] += sharedSums[local + stride];
        memoryBarrierShared();
        barrier();
    }
    if (local == 0u)
    {
        uint group = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
        partials[group] = sharedSums[0];
    }
}
