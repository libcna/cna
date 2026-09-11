#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
// Custom ShaderEffect declarations preserve Byte4 as VK_FORMAT_R8G8B8A8_UINT.
layout(location = 4) in uvec4 aBoneIndices;

layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uPrepassScalars[72];
};
layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uBones[72];
};
layout(set = 1, binding = 19, std140) uniform EngineMatrices
{
    mat4 uReservedLightViewProjection;
    mat4 uWorld;
    mat4 uView;
    mat4 uProjection;
    mat4 uPreviousWorld;
    mat4 uPreviousViewProjection;
};
layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uMatrix;
    vec4 uVector;
    float uWeightsPerVertex;
} pc;

layout(location = 0) out vec3 vViewNormal;
layout(location = 1) out float vViewDepth;
layout(location = 2) out vec4 vCurrentClip;
layout(location = 3) out vec4 vPreviousClip;

void main()
{
    mat4 skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x;
    if (pc.uWeightsPerVertex >= 2.0)
        skin += uBones[int(aBoneIndices.y)] * aBoneWeights.y;
    if (pc.uWeightsPerVertex >= 4.0)
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
