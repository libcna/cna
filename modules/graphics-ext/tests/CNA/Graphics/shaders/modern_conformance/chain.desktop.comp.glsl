#version 430 core

// One link of a chain: dst[i] = src[i] * 2 + 1. Chained dispatches read what the previous one wrote.
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Source
{
    uint src[];
};
layout(std430, binding = 1) writeonly buffer Destination
{
    uint dst[];
};

uniform int uCount;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(uCount))
        dst[i] = src[i] * 2u + 1u;
}
