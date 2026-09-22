#version 450

// One link of a chain: dst[i] = src[i] * 2 + 1. Chained dispatches read what the previous one wrote.
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 0) readonly buffer Source
{
    uint src[];
};
layout(std430, set = 0, binding = 1) writeonly buffer Destination
{
    uint dst[];
};

layout(push_constant) uniform Params
{
    int uCount;
} params;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(params.uCount))
        dst[i] = src[i] * 2u + 1u;
}
