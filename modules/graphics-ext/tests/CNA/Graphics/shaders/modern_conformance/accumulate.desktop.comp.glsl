#version 430 core

// Read-modify-write: acc[i] += input[i] * uScale, for i < uCount.
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Input
{
    float inputs[];
};
layout(std430, binding = 1) buffer Accumulator
{
    float acc[];
};

uniform int uCount;
uniform float uScale;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(uCount))
        acc[i] = acc[i] + inputs[i] * uScale;
}
