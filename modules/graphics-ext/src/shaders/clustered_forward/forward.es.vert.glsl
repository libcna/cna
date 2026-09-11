#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3  vWorldPosition;
out vec3  vWorldNormal;
out vec4  vClipPosition;
out float vViewDistance;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vec4 view  = uView * world;
    gl_Position = uProjection * view;

    vWorldPosition = world.xyz;
    // The upper 3x3 is the normal matrix only for a uniformly scaled world; the prepass documents
    // the same limitation, and correcting it needs an inverse per draw nothing else here pays for.
    vWorldNormal   = mat3(uWorld) * aNormal;
    vClipPosition  = gl_Position;
    vViewDistance  = -view.z;
}
