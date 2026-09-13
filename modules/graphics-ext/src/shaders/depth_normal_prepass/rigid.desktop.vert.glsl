#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uPreviousWorld;
uniform mat4 uPreviousViewProjection;
uniform float uPrepassScalars[4];

out vec3 vViewNormal;
out float vViewDepth;
out vec4 vCurrentClip;
out vec4 vPreviousClip;

void main()
{
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vec4 view = uView * world;
    gl_Position = uProjection * view;
    vCurrentClip = gl_Position;
    vPreviousClip = uPreviousViewProjection * (uPreviousWorld * vec4(aPosition, 1.0));
    vViewNormal = normalize(mat3(uView) * mat3(uWorld) * aNormal);
    vViewDepth = clamp(-view.z / uPrepassScalars[0], 0.0, 1.0);
}
