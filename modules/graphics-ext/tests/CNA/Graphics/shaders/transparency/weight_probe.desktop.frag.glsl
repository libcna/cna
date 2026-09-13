#version 330 core

in float PositionX;
out vec4 FragColor;
uniform vec4 uEffectParams;

void main()
{
    float depth = mix(uEffectParams.y, uEffectParams.z, PositionX);
    float z = clamp(depth / max(uEffectParams.w, 1e-4), 0.0, 1.0);
    float weight = uEffectParams.x * clamp(0.03 / (1e-5 + pow(z, 4.0)), 1e-2, 3e3);
    float encoded = (log(weight) / 2.302585 + 2.0) / 5.5;
    FragColor = vec4(clamp(encoded, 0.0, 1.0), 0.0, 0.0, 1.0);
}
