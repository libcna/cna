#version 450

layout(location = 0) in vec3 aPos;

void main()
{
    gl_Position = vec4(aPos.xy, aPos.z * 0.5 + 0.5, 1.0);
}
