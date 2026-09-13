#version 300 es
precision mediump float;

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D texture1;
uniform vec4 uColor;

void main()
{
    FragColor = texture(texture1, TexCoord) * Color * uColor;
}
