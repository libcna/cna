#version 450

layout(local_size_x = 1) in;
layout(set = 0, binding = 0) uniform usampler2D uSource;
layout(std430, set = 0, binding = 1) writeonly buffer Output
{
    uvec4 color;
};

void main()
{
    color = texelFetch(uSource, ivec2(0, 0), 0);
}
