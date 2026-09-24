#version 430 core

layout(local_size_x = 1) in;
layout(std430, binding = 1) buffer Output
{
    vec4 color;
};

uniform sampler2D uSource;

void main()
{
    color = texelFetch(uSource, ivec2(0, 0), 0);
}
