#version 310 es

layout(local_size_x = 64) in;

struct CnaInstance
{
    mat4 world;
    vec4 centre;
    vec4 extent;
};

layout(std430, binding = 0) readonly buffer CnaInstances
{
    CnaInstance cnaInstances[];
};
layout(std430, binding = 1) writeonly buffer CnaVisible
{
    mat4 cnaVisible[];
};
layout(std430, binding = 2) readonly buffer CnaPlanes
{
    vec4 cnaPlanes[];
};
layout(std430, binding = 3) buffer CnaCommand
{
    uint cnaCommand[];
};

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= cnaCommand[5])
        return;

    vec3 centre = cnaInstances[index].centre.xyz;
    vec3 extent = cnaInstances[index].extent.xyz;
    for (int i = 0; i < 6; ++i)
    {
        vec4 plane = cnaPlanes[i];
        // XNA frustum plane normals point outward, so even the box's nearest corner being in
        // front of a plane means the complete box is outside it.
        float radius = dot(extent, abs(plane.xyz));
        if (dot(plane.xyz, centre) + plane.w - radius > 0.0)
            return;
    }

    // InstanceCount is both the compaction atomic and the following indirect draw's live count.
    uint slot = atomicAdd(cnaCommand[1], 1u);
    cnaVisible[slot] = cnaInstances[index].world;
}
