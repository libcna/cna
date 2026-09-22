#version 450

// Read-modify-write: acc[i] += input[i] * uScale, for i < uCount.
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 0) readonly buffer Input
{
    float inputs[];
};
layout(std430, set = 0, binding = 1) buffer Accumulator
{
    float acc[];
};

layout(push_constant) uniform Params
{
    int uCount;
    float uScale;
} params;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(params.uCount))
        acc[i] = acc[i] + inputs[i] * params.uScale;
}
