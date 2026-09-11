#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
out float PositionX;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    PositionX = aPos.x * 0.5 + 0.5;
}
