// MOD-2236: EasyGL-equivalent directional, cascaded, point and spot shadow reception.
// Included into the per-pixel BasicEffect, SkinnedEffect and PBR fragment stages by
// compile_shaders.py. Set 1 is deliberately identical for every family; set 0 remains the
// family's material/lighting bundle.

layout(set = 1, binding = 0) uniform sampler2D uCnaShadowMap;
layout(set = 1, binding = 1) uniform samplerCube uCnaPunctualCube;
layout(set = 1, binding = 2) uniform sampler2D uCnaPunctualMap;

layout(set = 1, binding = 3) uniform CnaShadowParams {
    mat4 lightViewProj;
    mat4 cascadeMatrices[4];
    mat4 punctualViewProj;
    // x = enabled, y = depth bias, z = PCF radius, w = cascade count.
    vec4 directional;
    // xy = directional texel size, z = cascade blend band, w = debug tint enabled.
    vec4 shadowTexelBlendDebug;
    vec4 cascadeSplits;
    vec4 cascadeViewZ;
    // xyz = position, w = range.
    vec4 punctualPositionRange;
    // xyz = direction, w = kind (0 none, 1 point, 2 spot).
    vec4 punctualDirectionKind;
    // xyz = diffuse colour, w = a matching shadow texture is attached.
    vec4 punctualDiffuseHasShadow;
    // x/y = inner/outer cone cosine, z = shadow bias, w = spot texel X.
    vec4 punctualConeBiasTexelX;
    // x = spot texel Y.
    vec4 punctualTexelY_pad;
} cnaShadow;

float CnaShadowTap(vec3 uv, vec2 uvMin, vec2 uvMax) {
    if (uv.z > 1.0) return 1.0;
    float lit = 0.0;
    float taps = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float ring = max(abs(float(x)), abs(float(y)));
            if (ring > cnaShadow.directional.z + 0.5) continue;
            vec2 at = clamp(
                uv.xy + vec2(float(x), float(y)) * cnaShadow.shadowTexelBlendDebug.xy,
                uvMin, uvMax);
            float occluder = texture(uCnaShadowMap, at).r;
            lit += (uv.z - cnaShadow.directional.y <= occluder) ? 1.0 : 0.0;
            taps += 1.0;
        }
    }
    return lit / max(taps, 1.0);
}

mat4 CnaCascadeMatrix(int index) {
    mat4 m = cnaShadow.cascadeMatrices[0];
    if (index == 1) m = cnaShadow.cascadeMatrices[1];
    if (index == 2) m = cnaShadow.cascadeMatrices[2];
    if (index == 3) m = cnaShadow.cascadeMatrices[3];
    return m;
}

float CnaCascadeSplit(int index) {
    float split = cnaShadow.cascadeSplits.x;
    if (index == 1) split = cnaShadow.cascadeSplits.y;
    if (index == 2) split = cnaShadow.cascadeSplits.z;
    if (index == 3) split = cnaShadow.cascadeSplits.w;
    return split;
}

float CnaCascadeLookup(vec3 worldPos, int index, float count) {
    vec4 atlas = CnaCascadeMatrix(index) * vec4(worldPos, 1.0);
    vec3 uv = atlas.xyz / atlas.w;
    float slice = 1.0 / count;
    float x0 = float(index) * slice;
    if (uv.x < x0 || uv.x > x0 + slice || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    vec2 uvMin = vec2(x0 + cnaShadow.shadowTexelBlendDebug.x,
                      cnaShadow.shadowTexelBlendDebug.y);
    vec2 uvMax = vec2(x0 + slice - cnaShadow.shadowTexelBlendDebug.x,
                      1.0 - cnaShadow.shadowTexelBlendDebug.y);
    return CnaShadowTap(uv, uvMin, uvMax);
}

int CnaSelectCascade(float viewDepth, float count) {
    int chosen = int(count) - 1;
    for (int i = 0; i < 4; ++i) {
        if (float(i) >= count) break;
        if (viewDepth <= CnaCascadeSplit(i)) { chosen = i; break; }
    }
    return chosen;
}

float CnaShadowFactor(vec3 worldPos) {
    if (cnaShadow.directional.x < 0.5) return 1.0;
    if (cnaShadow.directional.w < 0.5) {
        vec4 lightSpace = cnaShadow.lightViewProj * vec4(worldPos, 1.0);
        vec3 uv = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
        return CnaShadowTap(uv, vec2(0.0), vec2(1.0));
    }
    float viewDepth = -dot(vec4(worldPos, 1.0), cnaShadow.cascadeViewZ);
    int index = CnaSelectCascade(viewDepth, cnaShadow.directional.w);
    float factor = CnaCascadeLookup(worldPos, index, cnaShadow.directional.w);
    float split = CnaCascadeSplit(index);
    if (cnaShadow.shadowTexelBlendDebug.z > 0.0 &&
        float(index + 1) < cnaShadow.directional.w &&
        viewDepth > split - cnaShadow.shadowTexelBlendDebug.z) {
        float t = clamp(
            (viewDepth - (split - cnaShadow.shadowTexelBlendDebug.z)) /
                cnaShadow.shadowTexelBlendDebug.z,
            0.0, 1.0);
        factor = mix(factor,
                     CnaCascadeLookup(worldPos, index + 1, cnaShadow.directional.w), t);
    }
    return factor;
}

vec3 CnaCascadeDebugTint(vec3 worldPos) {
    if (cnaShadow.shadowTexelBlendDebug.w < 0.5 ||
        cnaShadow.directional.x < 0.5 || cnaShadow.directional.w < 0.5)
        return vec3(1.0);
    float viewDepth = -dot(vec4(worldPos, 1.0), cnaShadow.cascadeViewZ);
    int index = CnaSelectCascade(viewDepth, cnaShadow.directional.w);
    if (index == 0) return vec3(1.0, 0.6, 0.6);
    if (index == 1) return vec3(0.6, 1.0, 0.6);
    if (index == 2) return vec3(0.6, 0.6, 1.0);
    return vec3(1.0, 1.0, 0.6);
}

float CnaPunctualShadow(vec3 worldPos, vec3 toLight, float distanceToLight) {
    if (cnaShadow.punctualDiffuseHasShadow.w < 0.5) return 1.0;
    float here = clamp(distanceToLight / cnaShadow.punctualPositionRange.w, 0.0, 1.0);
    if (cnaShadow.punctualDirectionKind.w < 1.5) {
        float occluder = texture(uCnaPunctualCube, -toLight).r;
        return (here - cnaShadow.punctualConeBiasTexelX.z <= occluder) ? 1.0 : 0.0;
    }
    vec4 clip = cnaShadow.punctualViewProj * vec4(worldPos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 uv = clip.xyz / clip.w * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float lit = 0.0;
    vec2 texel = vec2(cnaShadow.punctualConeBiasTexelX.w,
                      cnaShadow.punctualTexelY_pad.x);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 at = clamp(uv.xy + vec2(float(x), float(y)) * texel,
                            vec2(0.0), vec2(1.0));
            float occluder = texture(uCnaPunctualMap, at).r;
            lit += (here - cnaShadow.punctualConeBiasTexelX.z <= occluder) ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

vec3 CnaPunctualLight(vec3 worldPos, vec3 normal) {
    if (cnaShadow.punctualDirectionKind.w < 0.5) return vec3(0.0);
    vec3 offset = cnaShadow.punctualPositionRange.xyz - worldPos;
    float distanceToLight = length(offset);
    if (distanceToLight > cnaShadow.punctualPositionRange.w || distanceToLight < 1e-5)
        return vec3(0.0);
    vec3 toLight = offset / distanceToLight;
    float t = distanceToLight / cnaShadow.punctualPositionRange.w;
    float window = clamp(1.0 - t * t * t * t, 0.0, 1.0);
    float attenuation = window * window / (1.0 + distanceToLight * distanceToLight);
    if (cnaShadow.punctualDirectionKind.w > 1.5) {
        float cosAngle = dot(normalize(cnaShadow.punctualDirectionKind.xyz), -toLight);
        float cone = clamp(
            (cosAngle - cnaShadow.punctualConeBiasTexelX.y) /
                max(cnaShadow.punctualConeBiasTexelX.x -
                    cnaShadow.punctualConeBiasTexelX.y, 1e-4),
            0.0, 1.0);
        attenuation *= cone * cone;
    }
    float ndotl = max(dot(normal, toLight), 0.0);
    return cnaShadow.punctualDiffuseHasShadow.xyz * ndotl * attenuation *
           CnaPunctualShadow(worldPos, toLight, distanceToLight);
}
