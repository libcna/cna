#version 300 es
precision highp float;

in vec2 TexCoord;
in vec4 SpriteColor;
out vec4 FragColor;

uniform sampler2D texture1;
uniform sampler2D uShadowSampler;
uniform float uVolumetricBuildScalars[6];
uniform vec3 uVolumetricBuildVectors[2];
uniform mat4 uVolumetricBuildMatrices[3];
uniform vec4 uRtFlipV;

vec2 cnaLogicalUv(vec2 primaryUv)
{
    return vec2(primaryUv.x, mix(primaryUv.y, 1.0 - primaryUv.y, uRtFlipV.x));
}

void cnaAtlasSplit(vec2 atlasUv, out float slice, out vec2 inside)
{
    float scaled = atlasUv.x * uVolumetricBuildScalars[0];
    slice = min(floor(scaled), uVolumetricBuildScalars[0] - 1.0);
    inside = vec2(scaled - slice, atlasUv.y);
}

float cnaSliceDepth(float slice)
{
    float t = (slice + 0.5) / uVolumetricBuildScalars[0];
    return uVolumetricBuildScalars[4] * t * t;
}

float cnaPhase(float cosAngle)
{
    float g = uVolumetricBuildScalars[3];
    float gg = g * g;
    float d = 1.0 + gg - 2.0 * g * cosAngle;
    return (1.0 - gg) / (12.566370614 * max(pow(max(d, 1e-4), 1.5), 1e-4));
}

float cnaLitFraction(vec3 world)
{
    if (uVolumetricBuildScalars[5] < 0.5)
        return 1.0;
    vec4 lightClip = uVolumetricBuildMatrices[2] * vec4(world, 1.0);
    if (lightClip.w <= 0.0)
        return 1.0;
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 lightUv = lightNdc.xy * 0.5 + 0.5;
    if (lightUv.x < 0.0 || lightUv.x > 1.0 ||
        lightUv.y < 0.0 || lightUv.y > 1.0)
        return 1.0;
    float stored = texture(uShadowSampler, lightUv).r;
    float here = lightNdc.z * 0.5 + 0.5;
    return here - 0.002 > stored ? 0.0 : 1.0;
}

void main()
{
    float slice;
    vec2 inside;
    cnaAtlasSplit(cnaLogicalUv(TexCoord), slice, inside);

    float depth = cnaSliceDepth(slice);
    vec2 cameraUv = vec2(inside.x, 1.0 - inside.y);
    vec4 ray = uVolumetricBuildMatrices[0]
             * vec4(cameraUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = ray.xyz / ray.w;
    vec3 viewPosition = direction * (depth / max(-direction.z, 1e-6));
    vec4 world = uVolumetricBuildMatrices[1] * vec4(viewPosition, 1.0);
    vec4 cameraWorld = uVolumetricBuildMatrices[1] * vec4(0.0, 0.0, 0.0, 1.0);

    vec3 toCamera = normalize(cameraWorld.xyz - world.xyz);
    float phase = cnaPhase(dot(normalize(uVolumetricBuildVectors[0]), toCamera));
    float lit = cnaLitFraction(world.xyz);
    vec3 scattered = uVolumetricBuildVectors[1] * lit * phase
                   * uVolumetricBuildScalars[2];

    FragColor = vec4(scattered, uVolumetricBuildScalars[2]) * SpriteColor;
    FragColor.a += texture(texture1, TexCoord).a * 0.0;
}
