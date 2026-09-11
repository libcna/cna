#version 450

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 1, binding = 1) uniform sampler2D uDepthSampler;
layout(set = 1, binding = 12, std140) uniform FloatArray
{
    float uAerialScalars[72];
};
layout(set = 1, binding = 14, std140) uniform Vec3Array
{
    vec3 uAerialVectors[72];
};
layout(set = 1, binding = 15, std140) uniform Mat4Array
{
    mat4 uAerialMatrices[72];
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 SpriteColor;
layout(location = 0) out vec4 FragColor;

const vec3 kRayleigh = vec3(0.0464, 0.1085, 0.2650);
const float kMiePerTurbidity = 0.021;
const float kMieG = 0.76;
const float kSkyScale = 24.0;

float cnaDecodeLinearDepth(vec4 channels)
{
    if (uAerialScalars[4] < 0.5)
        return channels.r;
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}

float cnaRayleighPhase(float cosAngle)
{
    return 0.05968310365 * (1.0 + cosAngle * cosAngle);
}

float cnaMiePhase(float cosAngle)
{
    float gg = kMieG * kMieG;
    float d = 1.0 + gg - 2.0 * kMieG * cosAngle;
    return 0.07957747155 * (1.0 - gg)
         / max(pow(max(d, 1e-4), 1.5) * (2.0 + gg), 1e-4);
}

float cnaAirMass(float upwards)
{
    float up = clamp(upwards, 0.0, 1.0);
    float zenithDegrees = degrees(acos(up));
    return 1.0
         / max(up + 0.50572 * pow(max(96.07995 - zenithDegrees, 1e-3), -1.6364), 1e-4);
}

vec3 cnaScatteringAlongPath(vec3 viewDirection, vec3 sunDirection, float turbidity,
                            float viewMass)
{
    vec3 view = normalize(viewDirection);
    vec3 toSun = -normalize(sunDirection);
    float cosAngle = dot(view, toSun);
    float sunMass = cnaAirMass(toSun.y);
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    vec3 total = kRayleigh + vec3(mie);
    vec3 scattered = kRayleigh * cnaRayleighPhase(cosAngle)
                   + vec3(mie * cnaMiePhase(cosAngle));
    vec3 alongView = vec3(1.0) - exp(-total * viewMass);
    vec3 sunlight = exp(-total * sunMass);
    return scattered / total * alongView * sunlight * kSkyScale;
}

vec3 cnaAtmosphereTransmittance(float turbidity, float viewMass)
{
    float mie = kMiePerTurbidity * max(turbidity - 1.0, 0.0);
    return exp(-(kRayleigh + vec3(mie)) * viewMass);
}

float cnaAerialAirMass(vec3 viewDirection, float distance, float scaleHeight)
{
    float full = cnaAirMass(normalize(viewDirection).y);
    return min(max(distance, 0.0) / max(scaleHeight, 1e-3), full);
}

void main()
{
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
    if (depth <= 0.0 || depth >= 0.999)
    {
        FragColor = source * SpriteColor;
        return;
    }

    vec2 cameraUv = vec2(TexCoord.x, 1.0 - TexCoord.y);
    vec2 ndc = cameraUv * 2.0 - 1.0;
    vec4 world = uAerialMatrices[0] * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(world.xyz / world.w);
    vec4 viewRay = uAerialMatrices[1] * vec4(ndc, 1.0, 1.0);
    vec3 view = viewRay.xyz / viewRay.w;
    float alongRay = depth * uAerialScalars[3]
                   * (length(view) / max(-view.z, 1e-4));
    float airMass = cnaAerialAirMass(direction, alongRay, uAerialScalars[2]);
    vec3 graded = source.rgb * cnaAtmosphereTransmittance(uAerialScalars[0], airMass)
                + cnaScatteringAlongPath(direction, uAerialVectors[0],
                                         uAerialScalars[0], airMass)
                  * uAerialScalars[1];
    FragColor = vec4(graded, source.a) * SpriteColor;
}
