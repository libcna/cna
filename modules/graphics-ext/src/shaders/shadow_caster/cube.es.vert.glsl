#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uFaceViewProjection;
uniform mat4 uWorld;
out vec3 vWorldPos;
void main()
{
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uFaceViewProjection * world;
}
