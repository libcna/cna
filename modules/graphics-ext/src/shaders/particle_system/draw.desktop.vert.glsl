#version 430 core

layout(std430, binding = 7) readonly buffer CnaParticleBuffer
{
    vec4 cnaParticles[];
};

layout(location = 0) in vec3 aPos;
out vec2 vTexCoord;
out vec4 vColor;
out float vViewDepth;

uniform mat4 uParticleMatrices[2];
uniform vec3 uParticleColours[2];
uniform float uParticleScalars[11];

void main()
{
    int base = gl_InstanceID * 3;
    vec3 position = cnaParticles[base].xyz;
    vec4 state = cnaParticles[base + 2];
    float t = clamp(state.x / max(state.y, 1e-4), 0.0, 1.0);
    float size = mix(uParticleScalars[2], uParticleScalars[3], t);
    if (float(gl_InstanceID) >= uParticleScalars[4])
        size = 0.0;

    vec3 viewPosition = (uParticleMatrices[0] * vec4(position, 1.0)).xyz;
    viewPosition.xy += aPos.xy * size;
    gl_Position = uParticleMatrices[1] * vec4(viewPosition, 1.0);
    vTexCoord = aPos.xy + 0.5;
    vColor = vec4(mix(uParticleColours[0], uParticleColours[1], t),
                  mix(uParticleScalars[0], uParticleScalars[1], t));
    vViewDepth = -viewPosition.z;
}
