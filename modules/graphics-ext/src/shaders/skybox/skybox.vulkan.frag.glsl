#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 5) uniform samplerCube uEnvironment;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants
{
    vec2 viewportSize;
    mat4 uInvViewProj;
    vec4 uTintIntensity;
    float uYaw;
} pc;

void main()
{
    vec2 ndc = TexCoord * 2.0 - 1.0;
    vec4 farPoint = pc.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / max(abs(farPoint.w), 1e-6) * sign(farPoint.w));
    float yawSin = sin(pc.uYaw);
    float yawCos = cos(pc.uYaw);
    vec3 rotated = vec3(direction.x * yawCos + direction.z * yawSin,
                        direction.y,
                        -direction.x * yawSin + direction.z * yawCos);
    vec3 sky = texture(uEnvironment, rotated).rgb;
    FragColor = vec4(sky * pc.uTintIntensity.rgb, 1.0) * SpriteColor;
    FragColor.a = 1.0 + texture(texture1, TexCoord).a * 0.0;
}
