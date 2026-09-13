#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in vec4 aBoneIndices;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
out float vDistance;
void main()
{
    mat4 skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2)
        skin += uBones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (uWeightsPerVertex >= 4)
        skin += uBones[int(aBoneIndices.z)] * aBoneWeights.z
              + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 lightSpace = uLightViewProjection * uWorld * skin * vec4(aPosition, 1.0);
    gl_Position = lightSpace;
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
