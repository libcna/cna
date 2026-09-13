#version 330 core

out vec4 FragColor;
uniform vec4 uEffectParams;

void main()
{
    FragColor = uEffectParams;
}
