#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 4) in vec4 inWorld0;
layout(location = 5) in vec4 inWorld1;
layout(location = 6) in vec4 inWorld2;
layout(location = 7) in vec4 inWorld3;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragFog;

layout(set = 1, binding = 0) uniform PC {
    mat4  wvp;
    vec4  diffuseColor;
    vec3  ambientColor;
    float lightingEnabled;
    vec3  light0Dir;
    float textureEnabled;
    vec3  light0Diffuse;
    float vertexColorEnabled;
} pc;

layout(set = 1, binding = 1) uniform FogParams {
    vec4 fogColorEnabled;
    vec4 fogVector;
} fog;

void main() {
    mat4 world = mat4(inWorld0, inWorld1, inWorld2, inWorld3);
    vec4 worldPos = world * vec4(inPos, 1.0);
    gl_Position = pc.wvp * worldPos;
    fragColor = pc.vertexColorEnabled > 0.5
        ? inColor * pc.diffuseColor : pc.diffuseColor;
    float fogKeep = 1.0 - clamp(dot(worldPos, fog.fogVector), 0.0, 1.0);
    fragFog = vec4(fog.fogColorEnabled.xyz, fogKeep);
}
