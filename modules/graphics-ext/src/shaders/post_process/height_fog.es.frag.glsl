#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform mat4 uFogMatrices[2];
uniform vec3 uFogVectors[1];
uniform float uFogScalars[5];

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uFogScalars[4] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

vec3 cnaViewPositionFromDepth(vec2 uv, float linearDepth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray = uFogMatrices[0] * clip;
    vec3 direction = ray.xyz / ray.w;
    return direction * (linearDepth / max(-direction.z, 1e-6));
}

float cnaOpticalDepth(float cameraHeight, float rayHeightStep, float distance)
{
    float atCamera = uFogScalars[1]
                   * exp(-uFogScalars[2] * (cameraHeight - uFogScalars[3]));
    float climb = uFogScalars[2] * rayHeightStep;
    if (abs(climb) < 1e-5)
        return max(atCamera * distance, 0.0);
    return max(atCamera * (1.0 - exp(-climb * distance)) / climb, 0.0);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
    float farPlane = uFogScalars[0];
    float travelled = (depth <= 0.0 || depth >= 0.999) ? farPlane : depth * farPlane;
    vec3 viewPosition = cnaViewPositionFromDepth(TexCoord, max(depth, 1e-4)) * farPlane;
    vec4 world = uFogMatrices[1] * vec4(viewPosition, 1.0);
    vec4 cameraWorld = uFogMatrices[1] * vec4(0.0, 0.0, 0.0, 1.0);
    vec3 alongRay = world.xyz - cameraWorld.xyz;
    float rayLength = max(length(alongRay), 1e-4);
    float optical = cnaOpticalDepth(cameraWorld.y, alongRay.y / rayLength, travelled);
    float fog = 1.0 - exp(-optical);
    FragColor = vec4(mix(source.rgb, uFogVectors[0], clamp(fog, 0.0, 1.0)), source.a)
              * SpriteColor;
}
