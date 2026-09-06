#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out vec3 vEyeDir;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vFogFactor;
// plan_vulkan.md VULKAN-260: the Fresnel blend factor is a PER-VERTEX scalar that the
// rasterizer Gouraud-interpolates, not something the fragment shader recomputes. See below.
layout(location = 4) out float vFresnel;

layout(push_constant) uniform PC {
    mat4 mvp;
    mat4 world;
} pc;

layout(set = 0, binding = 2) uniform EnvMapParams {
    vec4 eyePos_pad;
    vec4 diffuseColor;
    vec4 emissive_em;
    vec4 light0Dir_pad;
    vec4 light0Diff_pad;
    vec4 envMapSpec_pad;
    // Task 899's noted cheap leftover: fog packed into this UBO's spare tail bytes.
    vec4 fogColorEnabled;  // xyz = FogColor, w = fogEnabled
    vec4 fogVector;      // REMED-GFX-010: FNA fog vector (dot with object/skin pos)
    // Task 890: DirectionalLight1/DirectionalLight2 diffuse forwarding.
    vec4 light1Dir_pad;
    vec4 light1Diff_pad;
    vec4 light2Dir_pad;
    vec4 light2Diff_pad;
} ep;

void main() {
    gl_Position = pc.mvp * vec4(aPos, 1.0);
    // REMED-GFX-011: renderer-wide Vulkan NDC Y-flip -- see pbr3d.vert.glsl.
    gl_Position.y = -gl_Position.y;
    gl_PointSize = 1.0;
    vec3 worldPos    = (pc.world * vec4(aPos, 1.0)).xyz;
    mat3 nm          = transpose(inverse(mat3(pc.world)));
    vWorldNormal     = normalize(nm * aNormal);
    vEyeDir          = ep.eyePos_pad.xyz - worldPos;
    vUV              = aUV;
    // Fog factor from raw object-space Z. REMED-GFX-005: corrected to FNA/EasyGL Task-1111
    // form (z+FogEnd)/(FogEnd-FogStart); the prior Task 888/899 (FogEnd-z) formula was the
    // mirror image and wrong. Zero-length range -> fully fogged, matching FNA SetFogVector.
    vFogFactor = 1.0 - clamp(dot(vec4(aPos, 1.0), ep.fogVector), 0.0, 1.0); // REMED-GFX-010: FNA view-space fog vector

    // plan_vulkan.md VULKAN-260. XNA's EnvironmentMapEffect.fx computes ComputeFresnelFactor in
    // the VERTEX shader, from each vertex's OWN un-interpolated normal and eye vector, and writes
    // the resulting scalar to vout.Diffuse.a -- which the rasterizer then linearly interpolates.
    // It never re-derives dot()/pow() from an interpolated-and-renormalized normal per fragment.
    //
    // The two are not equivalent the moment a triangle's vertices carry different normals, and on
    // the quad tools/xna-oracle/scenes/envmap_fresnel_quad.scene describes -- top edge normal
    // (0,0,1), bottom edge (1,0,0), eye at the origin -- the per-fragment form collapses to
    // fresnel = 1 along the whole vertical centre line, because the interpolated normal there has
    // no component along the interpolated eye vector. That is exactly what this renderer produced
    // before this row: a flat (200,100,50) where a gradient belongs.
    //
    // EnvironmentMapAmount (emissive_em.w) is folded in here, not in the fragment shader, because
    // FNA folds it in the same place: VSEnvMap writes ComputeFresnelFactor(...) * EnvironmentMapAmount
    // and VSEnvMapNoFresnel writes EnvironmentMapAmount alone.
    vec3  fresnelEye  = normalize(vEyeDir);
    vec3  fresnelNorm = normalize(vWorldNormal);
    float viewAngle   = dot(fresnelEye, fresnelNorm);
    vFresnel = (ep.light0Diff_pad.w > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), ep.envMapSpec_pad.w) * ep.emissive_em.w
        : ep.emissive_em.w;
}
