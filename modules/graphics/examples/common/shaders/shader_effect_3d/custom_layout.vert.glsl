#version 450

// Five inputs, one per declaration element, at locations 0..4 in declaration order. Each feeds
// exactly one output channel, so a wrong offset is one wrong channel.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inColor;

layout(location = 0) out vec4 vProbe;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uColor;
    float uFloat0;
} pc;

void main()
{
    gl_Position = pc.uMatrix * vec4(inPosition, 1.0);
    vProbe = vec4(inNormal.x, inTangent.y, inColor.r, inTexCoord.x);
}
