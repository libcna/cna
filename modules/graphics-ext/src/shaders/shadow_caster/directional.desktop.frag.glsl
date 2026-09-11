#version 330 core
in float vDistance;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vDistance, vDistance, vDistance, 1.0);
}
