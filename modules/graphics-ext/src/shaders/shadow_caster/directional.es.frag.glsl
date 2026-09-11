#version 300 es
precision highp float;
in float vDistance;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vDistance, vDistance, vDistance, 1.0);
}
