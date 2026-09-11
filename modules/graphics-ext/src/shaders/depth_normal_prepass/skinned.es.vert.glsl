#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in vec4 aBoneIndices;

uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
uniform mat4 uPreviousWorld;
uniform mat4 uPreviousViewProjection;
uniform float uPrepassScalars[4];

out vec3 vViewNormal;
out float vViewDepth;
out vec4 vCurrentClip;
out vec4 vPreviousClip;

void main()
{
    mat4 skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2)
        skin += uBones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (uWeightsPerVertex >= 4)
        skin += uBones[int(aBoneIndices.z)] * aBoneWeights.z
              + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 world = uWorld * skin * vec4(aPosition, 1.0);
    vec4 view = uView * world;
    gl_Position = uProjection * view;
    vCurrentClip = gl_Position;
    vPreviousClip = uPreviousViewProjection
                  * (uPreviousWorld * skin * vec4(aPosition, 1.0));
    vViewNormal = normalize(mat3(uView) * mat3(uWorld) * mat3(skin) * aNormal);
    vViewDepth = clamp(-view.z / uPrepassScalars[0], 0.0, 1.0);
}
