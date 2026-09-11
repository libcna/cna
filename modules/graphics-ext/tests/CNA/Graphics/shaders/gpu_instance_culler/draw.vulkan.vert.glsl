#version 450

layout(std430, set = 2, binding = 6) readonly buffer CnaVisibleInstances
{
    mat4 cnaVisibleInstances[];
};

layout(location = 0) in vec3 aPos;

void main()
{
    vec4 world = cnaVisibleInstances[gl_InstanceIndex] * vec4(aPos, 1.0);
    gl_Position = vec4(world.x / 4.0, world.y / 5.0, 0.5, 1.0);
}
