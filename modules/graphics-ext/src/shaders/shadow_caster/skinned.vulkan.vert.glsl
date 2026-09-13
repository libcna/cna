#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
// Custom ShaderEffect declarations preserve Byte4 as VK_FORMAT_R8G8B8A8_UINT. Stock-effect
// layouts separately use USCALED to feed their float input, so this package must use uvec4.
layout(location = 4) in uvec4 aBoneIndices;
layout(location = 0) out float vDistance;

layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uBones[72];
};

layout(set = 1, binding = 19, std140) uniform ShadowMatrices
{
    mat4 uLightViewProjection;
    mat4 uWorld;
} matrices;

layout(push_constant) uniform PushConstants
{
    vec2 vpSize;
    mat4 uMatrix;
    vec4 uVector;
    float uWeightsPerVertex;
} pc;

void main()
{
    mat4 skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (pc.uWeightsPerVertex >= 2.0)
        skin += uBones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (pc.uWeightsPerVertex >= 4.0)
        skin += uBones[int(aBoneIndices.z)] * aBoneWeights.z
              + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
    vec4 lightSpace = matrices.uLightViewProjection * matrices.uWorld * skin
                    * vec4(aPosition, 1.0);
    gl_Position = lightSpace;
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
