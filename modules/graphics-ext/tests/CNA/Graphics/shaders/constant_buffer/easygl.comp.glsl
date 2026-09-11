#version 310 es
precision highp float;

layout(local_size_x = 1) in;
layout(std140, binding = 0) uniform Parameters
{
    vec4 value;
};
layout(std430, binding = 1) writeonly buffer Output
{
    vec4 result;
};

void main()
{
    result = value;
}
