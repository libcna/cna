#version 300 es
precision highp float;
in vec3 vWorldPos;
uniform vec3 uLightPosition;
uniform float uLightRange;
out vec4 FragColor;
void main()
{
    float distance = clamp(length(vWorldPos - uLightPosition) / uLightRange, 0.0, 1.0);
    FragColor = vec4(distance, distance, distance, 1.0);
}
