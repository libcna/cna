#version 450

layout(location = 0) in float vDistance;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(vDistance, vDistance, vDistance, 1.0);
}
