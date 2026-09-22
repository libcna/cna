#version 310 es
precision highp float;
precision highp int;

// Every invocation of a 3D dispatch writes its own global id, packed, at its own linear index.
layout(local_size_x = 4, local_size_y = 2, local_size_z = 2) in;
layout(std430, binding = 0) writeonly buffer Output
{
    uint values[];
};

uniform int uWidth;
uniform int uHeight;

void main()
{
    uvec3 g = gl_GlobalInvocationID;
    uint index = g.x + g.y * uint(uWidth) + g.z * uint(uWidth) * uint(uHeight);
    values[index] = (g.x << 20u) | (g.y << 10u) | g.z;
}
