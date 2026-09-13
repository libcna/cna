#version 300 es
precision highp float;
in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform samplerCube uEnvironment;
uniform mat4 uInvViewProj;
uniform vec3 uTintIntensity;
uniform float uYaw;
void main()
{
    vec2 ndc = TexCoord * 2.0 - 1.0;
    vec4 farPoint = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / max(abs(farPoint.w), 1e-6) * sign(farPoint.w));
    float yawSin = sin(uYaw);
    float yawCos = cos(uYaw);
    vec3 rotated = vec3(direction.x * yawCos + direction.z * yawSin,
                        direction.y,
                        -direction.x * yawSin + direction.z * yawCos);
    vec3 sky = texture(uEnvironment, rotated).rgb;
    FragColor = vec4(sky * uTintIntensity, 1.0) * SpriteColor;
    FragColor.a = 1.0 + texture(texture1, TexCoord).a * 0.0;
}
