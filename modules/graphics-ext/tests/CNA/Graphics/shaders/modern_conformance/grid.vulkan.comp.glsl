#version 450

// Every invocation of a 3D dispatch writes its own global id, packed, at its own linear index.
layout(local_size_x = 4, local_size_y = 2, local_size_z = 2) in;
layout(std430, set = 0, binding = 0) writeonly buffer Output
{
    uint values[];
};

layout(push_constant) uniform Params
{
    int uWidth;
    int uHeight;
} params;

void main()
{
    uvec3 g = gl_GlobalInvocationID;
    uint index = g.x + g.y * uint(params.uWidth) + g.z * uint(params.uWidth) * uint(params.uHeight);
    values[index] = (g.x << 20u) | (g.y << 10u) | g.z;
}
