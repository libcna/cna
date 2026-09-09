#version 450

layout(local_size_x = 1) in;
layout(std140, set = 0, binding = 0) uniform Parameters
{
    vec4 value;
};
layout(std430, set = 0, binding = 1) writeonly buffer Output
{
    vec4 result;
};

void main()
{
    result = value;
}
