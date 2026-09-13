#version 300 es
precision highp float;
const int kCnaMaxLightsPerFragment = 128;

uniform sampler2D uCnaLightData;
uniform sampler2D uCnaClusterTable;
uniform sampler2D uCnaLightIndices;
uniform int   uCnaTilesX;
uniform int   uCnaTilesY;
uniform int   uCnaSliceCount;
uniform int   uCnaLightCount;
uniform float uCnaGridNear;
uniform float uCnaGridFar;

const int kCnaFloatsPerLight = 16;
const int kCnaTableWidth     = 256;

struct CnaClusteredLight {
    vec3  position;
    float range;
    vec3  colour;      // the light's colour already multiplied by its intensity
    float isSpot;      // 1.0 for a spot light, 0.0 for a point light
    vec3  direction;
    float cosOuter;
    float cosInner;
};

/// The four bytes of a texel as the 32-bit value they were written from. Exact because the texture
/// is read with texelFetch, which does no filtering and no coordinate rounding.
uint cnaUnpackUint(vec4 texel) {
    return uint(texel.r * 255.0 + 0.5)
         | (uint(texel.g * 255.0 + 0.5) << 8)
         | (uint(texel.b * 255.0 + 0.5) << 16)
         | (uint(texel.a * 255.0 + 0.5) << 24);
}

float cnaUnpackFloat(vec4 texel) { return uintBitsToFloat(cnaUnpackUint(texel)); }

CnaClusteredLight cnaLoadLight(int index) {
    CnaClusteredLight light;
    light.position  = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(0, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(1, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(2, index), 0)));
    light.range     = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(3, index), 0));
    light.colour    = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(4, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(5, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(6, index), 0)));
    light.isSpot    = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(7, index), 0));
    light.direction = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(8, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(9, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(10, index), 0)));
    light.cosOuter  = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(11, index), 0));
    light.cosInner  = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(12, index), 0));
    return light;
}

/// The cluster a fragment belongs to. The depth argument is a *view distance* -- positive, in world
/// units -- and not a depth-buffer value, because the slice spacing is a ratio of world distances
/// and converting one to the other is the caller's business, not this table's.
int cnaClusterFromNdc(vec2 ndc, float viewDistance) {
    int tx = clamp(int((ndc.x * 0.5 + 0.5) * float(uCnaTilesX)), 0, uCnaTilesX - 1);
    int ty = clamp(int((ndc.y * 0.5 + 0.5) * float(uCnaTilesY)), 0, uCnaTilesY - 1);
    float span = log(uCnaGridFar / uCnaGridNear);
    float t = log(max(viewDistance, uCnaGridNear) / uCnaGridNear) / max(span, 1e-6);
    int tz = clamp(int(floor(t * float(uCnaSliceCount))), 0, uCnaSliceCount - 1);
    return (tz * uCnaTilesY + ty) * uCnaTilesX + tx;
}

int cnaClusterLightCount(int cluster) {
    int texel = cluster * 2 + 1;
    return int(cnaUnpackUint(texelFetch(uCnaClusterTable,
                                        ivec2(texel % kCnaTableWidth, texel / kCnaTableWidth), 0)));
}

int cnaClusterLightIndex(int cluster, int i) {
    int start = int(cnaUnpackUint(texelFetch(uCnaClusterTable,
                                             ivec2((cluster * 2) % kCnaTableWidth,
                                                   (cluster * 2) / kCnaTableWidth), 0)));
    int texel = start + i;
    return int(cnaUnpackUint(texelFetch(uCnaLightIndices,
                                        ivec2(texel % kCnaTableWidth, texel / kCnaTableWidth), 0)));
}

const float kCnaShC1 = 0.429043;
const float kCnaShC2 = 0.511664;
const float kCnaShC3 = 0.743125;
const float kCnaShC4 = 0.886227;
const float kCnaShC5 = 0.247708;

/// Irradiance, not outgoing radiance: a Lambertian surface reflects albedo/pi of this, and the
/// caller applies that. Baking the albedo in here would put a surface's colour into a probe that
/// has nothing to do with any surface.
vec3 cnaProbeIrradiance(vec3 coefficients[9], vec3 normal) {
    vec3 n = normalize(normal);
    vec3 result =
          kCnaShC4 * coefficients[0]
        + 2.0 * kCnaShC2 * (coefficients[1] * n.y + coefficients[2] * n.z + coefficients[3] * n.x)
        + 2.0 * kCnaShC1 * (coefficients[4] * n.x * n.y + coefficients[5] * n.y * n.z
                            + coefficients[7] * n.x * n.z)
        + kCnaShC3 * coefficients[6] * n.z * n.z - kCnaShC5 * coefficients[6]
        + kCnaShC1 * coefficients[8] * (n.x * n.x - n.y * n.y);
    return max(result, vec3(0.0));
}

const float kCnaFilmPi = 3.14159265359;

float cnaFilmSquare(float v) { return v * v; }
vec3  cnaFilmSquare(vec3 v) { return v * v; }

float cnaFilmSchlick(float f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 cnaFilmSchlick(vec3 f0, float cosTheta) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float cnaFilmIorToFresnel0(float transmitted, float incident) {
    return cnaFilmSquare((transmitted - incident) / (transmitted + incident));
}

vec3 cnaFilmIorToFresnel0(vec3 transmitted, float incident) {
    return cnaFilmSquare((transmitted - vec3(incident)) / (transmitted + vec3(incident)));
}

vec3 cnaFilmFresnel0ToIor(vec3 fresnel0) {
    vec3 root = sqrt(clamp(fresnel0, 0.0, 0.9999));
    return (vec3(1.0) + root) / (vec3(1.0) - root);
}

/// The eye's response to an optical path difference, as Gaussians fitted to the CIE curves and
/// converted to Rec. 709. This is what turns interference over wavelength into a colour.
vec3 cnaFilmSensitivity(float opd, vec3 shift) {
    float phase = 2.0 * kCnaFilmPi * opd * 1.0e-9;
    vec3 value = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 position = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 variance = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);

    vec3 xyz = value * sqrt(2.0 * kCnaFilmPi * variance) * cos(position * phase + shift)
             * exp(-cnaFilmSquare(phase) * variance);
    xyz.x += 9.7470e-14 * sqrt(2.0 * kCnaFilmPi * 4.5282e+09)
           * cos(2.2399e+06 * phase + shift.x) * exp(-4.5282e+09 * cnaFilmSquare(phase));
    xyz /= 1.0685e-7;

    return vec3(3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
               -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
                0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z);
}

vec3 cnaThinFilmIridescence(float outsideIor, float filmIor, float cosTheta, float thicknessNm,
                            vec3 baseF0) {
    float cosTheta1 = clamp(cosTheta, 0.0, 1.0);

    // A film of no thickness returns the base's own Schlick reflectance exactly -- see the C++
    // mirror for why the reference implementation does not, and why "off" should mean off.
    if (thicknessNm <= 0.0) return cnaFilmSchlick(baseF0, cosTheta1);

    // A film of no thickness is not a film: the index fades back to the surrounding medium over the
    // first few nanometres, so the extension present with a zero thickness is the material without.
    float iridescenceIor = mix(outsideIor, filmIor, smoothstep(0.0, 0.03, thicknessNm));

    float sinTheta2Squared = cnaFilmSquare(outsideIor / iridescenceIor)
                           * (1.0 - cnaFilmSquare(cosTheta1));
    float cosTheta2Squared = 1.0 - sinTheta2Squared;
    if (cosTheta2Squared < 0.0) return vec3(1.0);   // total internal reflection
    float cosTheta2 = sqrt(cosTheta2Squared);

    float r0 = cnaFilmIorToFresnel0(iridescenceIor, outsideIor);
    float r12 = cnaFilmSchlick(r0, cosTheta1);
    float t121 = 1.0 - r12;
    float phi12 = iridescenceIor < outsideIor ? kCnaFilmPi : 0.0;
    float phi21 = kCnaFilmPi - phi12;

    vec3 baseIor = cnaFilmFresnel0ToIor(baseF0);
    vec3 r23 = cnaFilmSchlick(cnaFilmIorToFresnel0(baseIor, iridescenceIor), cosTheta2);
    vec3 phi23 = vec3(baseIor.x < iridescenceIor ? kCnaFilmPi : 0.0,
                      baseIor.y < iridescenceIor ? kCnaFilmPi : 0.0,
                      baseIor.z < iridescenceIor ? kCnaFilmPi : 0.0);

    float opd = 2.0 * iridescenceIor * thicknessNm * cosTheta2;
    vec3 phi = vec3(phi21) + phi23;

    vec3 r123 = clamp(vec3(r12) * r23, 1e-5, 0.9999);
    vec3 rs = cnaFilmSquare(vec3(t121)) * r23 / (vec3(1.0) - r123);

    vec3 result = vec3(r12) + rs;
    vec3 cm = rs - vec3(t121);
    for (int order = 1; order <= 2; ++order) {
        cm *= sqrt(r123);
        result += cm * 2.0 * cnaFilmSensitivity(float(order) * opd, float(order) * phi);
    }
    return max(result, vec3(0.0));
}

uniform sampler2D uCnaAreaBrdf;
uniform float uCnaAreaBrdfSize;

/// x = directional albedo, y = Fresnel weight, z/w = the average reflection direction in the plane
/// the view and the normal span.
///
/// The coordinate is remapped onto the first and last texel *centres* rather than onto the edges
/// of the texture, and that is not tidiness. A texture bound through ShaderEffect::SetTexture keeps
/// the default wrap mode, and per-unit sampler state does not reach it (measured, MOD-2029) -- so a
/// lookup at N.V = 1 lands exactly on the seam and the filter averages the last column with the
/// *first*, which is the grazing end of the table. The result was a mirror looked at head-on being
/// told its reflection leans thirty degrees off the normal, which pushed a corner of every area
/// light below the horizon and left the highlight black.
vec4 cnaAreaBrdfTerms(float nDotV, float roughness) {
    vec2 index = clamp(vec2(nDotV, roughness), 0.0, 1.0);
    vec2 uv = (index * (uCnaAreaBrdfSize - 1.0) + 0.5) / max(uCnaAreaBrdfSize, 1.0);
    return texture(uCnaAreaBrdf, uv);
}

uniform int   uAreaShape;        // -1 none, 0 rectangle, 1 disc, 2 tube
uniform vec3  uAreaPosition;
uniform vec3  uAreaRight;
uniform vec3  uAreaUp;
uniform vec3  uAreaColour;       // already multiplied by the light's intensity
uniform float uAreaRange;
uniform float uAreaTwoSided;

const float kCnaDiscAxisScale = 0.88622692545;   // sqrt(pi) / 2

vec3 cnaFrameTangent(vec3 axis) {
    vec3 guess = abs(axis.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(guess, axis));
}

void cnaAreaQuad(vec3 surface, out vec3 quad[4]) {
    vec3 right = uAreaRight;
    vec3 up = uAreaUp;
    if (uAreaShape == 1) {
        right *= kCnaDiscAxisScale;
        up *= kCnaDiscAxisScale;
    } else if (uAreaShape == 2) {
        float radius = length(up);
        vec3 axis = normalize(right);
        vec3 toSurface = surface - uAreaPosition;
        // Perpendicular to both the axis and the direction to the surface, so the quad's normal
        // points at the surface rather than lying in its plane.
        vec3 facing = cross(toSurface, axis);
        facing = dot(facing, facing) > 1e-12 ? normalize(facing) : cnaFrameTangent(axis);
        up = facing * radius;
    }
    quad[0] = uAreaPosition - right - up;
    quad[1] = uAreaPosition + right - up;
    quad[2] = uAreaPosition + right + up;
    quad[3] = uAreaPosition - right + up;
}

float cnaIntegrateEdge(vec3 a, vec3 b) {
    float cosine = clamp(dot(a, b), -0.9999, 0.9999);
    float angle = acos(cosine);
    return cross(a, b).z * angle / max(sin(angle), 1e-4);
}

/// The fraction of a clamped-cosine lobe the quad covers. With the lobe on the surface normal and a
/// scale of 1 this is the diffuse form factor, and it is exact rather than approximate.
float cnaAreaCoverage(vec3 quad[4], vec3 surface, vec3 lobeAxis, float lobeScale, bool twoSided) {
    vec3 axis = normalize(lobeAxis);
    vec3 tangent = cnaFrameTangent(axis);
    vec3 bitangent = cross(axis, tangent);
    float inverseScale = 1.0 / max(lobeScale, 1e-4);

    vec3 p[5];
    for (int i = 0; i < 4; ++i) {
        vec3 relative = quad[i] - surface;
        p[i] = vec3(dot(relative, tangent) * inverseScale,
                    dot(relative, bitangent) * inverseScale,
                    dot(relative, axis));
    }
    p[4] = p[0];

    // The same loop ClipToHorizon runs on the CPU, rather than the reference implementation's
    // sixteen-case switch: one place to be wrong instead of sixteen, and the two paths cannot
    // disagree about a case one of them forgot.
    vec3 clipped[8];
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        vec3 current = p[i];
        vec3 next = p[i + 1];
        bool currentIn = current.z > 0.0;
        bool nextIn = next.z > 0.0;
        if (currentIn) { clipped[count] = current; ++count; }
        if (currentIn != nextIn) {
            float t = current.z / (current.z - next.z);
            clipped[count] = current + (next - current) * t;
            ++count;
        }
    }
    if (count < 3) return 0.0;

    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        clipped[i] = normalize(clipped[i]);
    }

    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        int next = (i + 1 == count) ? 0 : i + 1;
        sum += cnaIntegrateEdge(clipped[i], clipped[next]);
    }

    sum = twoSided ? abs(sum) : max(-sum, 0.0);
    return clamp(sum / 6.28318530718, 0.0, 1.0);
}

vec3 cnaAreaContribution(vec3 surface, vec3 normal, vec3 viewDirection, vec3 baseColor,
                         float metallic, float roughness) {
    if (uAreaShape < 0) return vec3(0.0);
    vec3 toLight = uAreaPosition - surface;
    if (dot(toLight, toLight) >= uAreaRange * uAreaRange) return vec3(0.0);

    vec3 quad[4];
    cnaAreaQuad(surface, quad);
    bool twoSided = uAreaTwoSided > 0.5;

    float diffuseCoverage = cnaAreaCoverage(quad, surface, normal, 1.0, twoSided);

    float nDotV = clamp(dot(normal, viewDirection), 1e-3, 1.0);
    vec4 terms = cnaAreaBrdfTerms(nDotV, roughness);
    // Guarded, because at exactly normal incidence `normal * nDotV - viewDirection` is the zero
    // vector: normalizing it gives NaN, the NaN reaches the lobe axis, and the coverage clamps to
    // zero -- so a mirror looked at head-on, which is the case a highlight test aims for, comes
    // back black. The CPU path always had this fallback; the shader did not.
    vec3 tangentBase = normal * nDotV - viewDirection;
    vec3 tangent = dot(tangentBase, tangentBase) > 1e-12 ? normalize(tangentBase)
                                                         : cnaFrameTangent(normal);
    vec3 lobeAxis = normalize(tangent * terms.b + normal * terms.a);
    float lobeScale = max(roughness * roughness, 0.02);
    float specularCoverage = cnaAreaCoverage(quad, surface, lobeAxis, lobeScale, twoSided);

    float scale = terms.r - terms.g;
    float bias = terms.g;
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specularCoverage * (f0 * scale + bias);
    vec3 diffuse = diffuseCoverage * baseColor * (1.0 - metallic);
    return (diffuse + specular) * uAreaColour;
}

const float kCnaPi = 3.14159265359;

float cnaDistribution(float NoH, float roughness) {
    float a = roughness * roughness;
    float aa = a * a;
    float d = NoH * NoH * (aa - 1.0) + 1.0;
    return aa / max(kCnaPi * d * d, 1e-7);
}

float cnaGeometry(float NoV, float NoL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = NoV / max(NoV * (1.0 - k) + k, 1e-7);
    float gl = NoL / max(NoL * (1.0 - k) + k, 1e-7);
    return gv * gl;
}

vec3 cnaFresnel(float VoH, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
}

float cnaFalloff(float distance, float range) {
    float ratio = distance / max(range, 1e-4);
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / max(distance * distance, 1e-4);
}

uniform float uClearcoat;
uniform float uClearcoatRoughness;
uniform vec3  uSheenColor;
uniform float uSheenRoughness;
uniform vec3  uSubsurfaceColor;
uniform float uSubsurfaceWrap;
uniform float uIridescence;
uniform float uIridescenceIor;
uniform float uIridescenceThickness;
uniform float uTransmission;
uniform float uThickness;
uniform float uAttenuationDistance;
uniform vec3  uAttenuationColor;
uniform float uIor;
uniform mat4  uViewProjection;
uniform sampler2D uOpaqueFrame;

// KHR_materials_sheen: the Charlie distribution with Ashikhmin's visibility. Its peak is where the
// half-vector is *perpendicular* to the normal, which is the opposite of a specular lobe -- that is
// why sheen appears as a rim at grazing angles and why no roughness on the base material produces
// it. The alpha floor is not cosmetic: the exponent is 1/alpha, so a small roughness gives a rim
// too tight to survive any sensible resolution.
float cnaSheenDistribution(float NoH, float roughness) {
    float alpha = max(roughness * roughness, 0.07);
    float inverseAlpha = 1.0 / alpha;
    float sinSquared = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + inverseAlpha) * pow(sinSquared, inverseAlpha * 0.5) / 6.28318530718;
}

float cnaSheenVisibility(float NoV, float NoL) {
    return 1.0 / max(4.0 * (NoL + NoV - NoL * NoV), 1e-7);
}

vec3 cnaShade(CnaClusteredLight light, vec3 surface, vec3 normal, vec3 viewDirection,
              vec3 baseColor, float metallic, float roughness, out vec3 diffuseOut) {
    diffuseOut = vec3(0.0);
    vec3 toLight = light.position - surface;
    float distance = length(toLight);
    if (distance >= light.range || distance <= 0.0) return vec3(0.0);
    vec3 L = toLight / distance;

    float attenuation = cnaFalloff(distance, light.range);
    if (light.isSpot > 0.5) {
        // The cone, measured from the light outwards, so -L is the direction the light travels.
        float cosAngle = dot(-L, light.direction);
        attenuation *= clamp((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 1e-4),
                             0.0, 1.0);
    }
    if (attenuation <= 0.0) return vec3(0.0);

    // Guarded: with the light exactly behind the surface, L is -V and their sum is the zero
    // vector, so normalizing it is a NaN -- which then survives being multiplied by a zero N.L and
    // paints the pixel black. That configuration is not exotic, it is precisely the one the
    // subsurface back-scatter exists for.
    vec3 halfSum = L + viewDirection;
    vec3 H = dot(halfSum, halfSum) > 1e-8 ? normalize(halfSum) : normal;
    float rawNoL = dot(normal, L);
    float NoL = max(rawNoL, 0.0);
    // Wrapped diffuse: light reaches a little past the terminator, which is what a surface light
    // travels *inside* looks like. An approximation, and one that cannot know how thick the object
    // is -- see PbrMaterialExtensions::setSubsurfaceColor for what that costs.
    float subsurface = uSubsurfaceColor.r + uSubsurfaceColor.g + uSubsurfaceColor.b;
    float wrappedNoL = NoL;
    if (subsurface > 0.0) {
        float w = uSubsurfaceWrap;
        wrappedNoL = clamp((rawNoL + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
    }
    float NoV = max(dot(normal, viewDirection), 1e-4);
    float NoH = max(dot(normal, H), 0.0);
    float VoH = max(dot(viewDirection, H), 0.0);
    float backScatter = 0.0;
    if (subsurface > 0.0) backScatter = pow(clamp(dot(viewDirection, -L), 0.0, 1.0), 4.0);
    if (NoL <= 0.0 && wrappedNoL <= 0.0 && backScatter <= 0.0) return vec3(0.0);

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 fresnel = cnaFresnel(VoH, f0);
    // KHR_materials_iridescence replaces the Fresnel term itself rather than adding a lobe: what a
    // thin film changes is *which wavelengths* the surface reflects, not how much it reflects.
    if (uIridescence > 0.0) {
        vec3 film = cnaThinFilmIridescence(1.0, uIridescenceIor, NoV, uIridescenceThickness, f0);
        fresnel = mix(fresnel, film, uIridescence);
    }
    vec3 specular = fresnel * cnaDistribution(NoH, roughness) * cnaGeometry(NoV, NoL, roughness)
                  / max(4.0 * NoV * NoL, 1e-7);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor / kCnaPi;
    // The diffuse term leaves separately, because KHR_materials_transmission replaces *it* with
    // what is behind the surface and leaves the highlights alone -- glass with no highlight is the
    // thing that stops looking like glass.
    diffuseOut = diffuse * light.colour * attenuation * wrappedNoL;
    // What comes through from behind: strongest when looking straight into the light through the
    // surface, which is the second half of what makes a leaf or an ear read as translucent.
    if (subsurface > 0.0) diffuseOut += uSubsurfaceColor * backScatter * light.colour * attenuation;
    vec3 layered = specular;

    if (uSheenColor.r + uSheenColor.g + uSheenColor.b > 0.0) {
        layered += uSheenColor * cnaSheenDistribution(NoH, uSheenRoughness)
                 * cnaSheenVisibility(NoV, NoL);
    }

    // KHR_materials_clearcoat: a second, thin specular layer over the whole material, with its own
    // roughness. Not a brighter highlight -- a *second* one. What it takes from the base layer is
    // exactly what it reflects, so a coat brightens the surface where it catches the light and
    // darkens it everywhere else, which is what makes lacquer look like lacquer.
    if (uClearcoat > 0.0) {
        float ccRoughness = max(uClearcoatRoughness, 0.04);
        float ccFresnel = 0.04 + 0.96 * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
        float ccSpecular = ccFresnel * cnaDistribution(NoH, ccRoughness)
                         * cnaGeometry(NoV, NoL, ccRoughness) / max(4.0 * NoV * NoL, 1e-7);
        layered = layered * (1.0 - uClearcoat * ccFresnel) + vec3(uClearcoat * ccSpecular);
    }

    return layered * light.colour * attenuation * NoL;
}

in vec3  vWorldPosition;
in vec3  vWorldNormal;
in vec4  vClipPosition;
in float vViewDistance;
out vec4 FragColor;

uniform vec3  uCameraPosition;
uniform vec3  uBaseColor;
uniform vec3  uAmbient;
uniform vec3  uProbeCoefficients[9];
uniform float uHasProbe;
uniform float uMetallic;
uniform float uRoughness;

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec2 ndc = vClipPosition.xy / max(abs(vClipPosition.w), 1e-6) * sign(vClipPosition.w);

    int cluster = cnaClusterFromNdc(ndc, vViewDistance);
    int count = cnaClusterLightCount(cluster);

    // The ambient: a probe where one was given, the flat term where none was. A probe carries
    // irradiance, so the Lambertian surface reflects albedo/pi of it -- the flat term is the colour
    // a surface *shows*, and the two are not interchangeable without that division.
    vec3 ambient = uAmbient * uBaseColor;
    if (uHasProbe > 0.5) {
        ambient = cnaProbeIrradiance(uProbeCoefficients, normal) * uBaseColor / kCnaPi;
    }
    vec3 diffuseSum = ambient;
    diffuseSum += cnaAreaContribution(vWorldPosition, normal, viewDirection, uBaseColor, uMetallic,
                                      uRoughness);
    vec3 otherSum = vec3(0.0);
    for (int i = 0; i < kCnaMaxLightsPerFragment; ++i) {
        if (i >= count) break;
        CnaClusteredLight light = cnaLoadLight(cnaClusterLightIndex(cluster, i));
        vec3 lightDiffuse;
        otherSum += cnaShade(light, vWorldPosition, normal, viewDirection, uBaseColor, uMetallic,
                             uRoughness, lightDiffuse);
        diffuseSum += lightDiffuse;
    }

    if (uTransmission > 0.0) {
        // Refraction, not transparency: the ray bends entering the surface, travels the volume's
        // thickness, and leaves somewhere else -- so what shows through is *displaced*, which is
        // the whole visual difference from alpha blending. The exit point is projected back to
        // screen space to find it in the copy of the opaque frame.
        vec3 refracted = refract(-viewDirection, normal, 1.0 / max(uIor, 1.0));
        vec3 exitPoint = vWorldPosition + refracted * uThickness;
        vec4 exitClip = uViewProjection * vec4(exitPoint, 1.0);
        vec2 uv = exitClip.xy / max(abs(exitClip.w), 1e-4) * sign(exitClip.w) * 0.5 + 0.5;
        vec3 behind = texture(uOpaqueFrame, clamp(uv, 0.0, 1.0)).rgb;

        vec3 absorbed = vec3(1.0);
        if (uAttenuationDistance > 0.0 && uThickness > 0.0) {
            vec3 sigma = -log(clamp(uAttenuationColor, 1e-4, 1.0)) / uAttenuationDistance;
            absorbed = exp(-sigma * uThickness);
        }
        diffuseSum = mix(diffuseSum, behind * absorbed, uTransmission);
    }

    FragColor = vec4(diffuseSum + otherSum, 1.0);
}
