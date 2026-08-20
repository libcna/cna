// SPDX-License-Identifier: MS-PL
//
// plans/plan_sokol.md SOKOL-34: the Sokol renderer's environment-mapped 3D program --
// EnvironmentMapEffect (reflection cube mapping), always textured (a real base Texture2D, or the
// renderer's 1x1 white fallback, same convention lit3d.glsl already uses) and always lit (ambient
// folded into `emissiveColor` CPU-side -- EnvironmentMapEffect::FillGpuDrawParams's own
// "emissive + ambient*diffuse, pre-combined and pre-multiplied by alpha" comment -- plus up to 3
// directional lights). No vertex-colour support: real XNA's EnvironmentMapEffect has no
// VertexColorEnabled property at all (`FillGpuDrawParams` always sets `vertexColorEnabled=false`),
// matching EasyGLRenderer::EnsureEnvMapped3DProgram()'s own 3-attribute (position/normal/
// texcoord0 only, no color0) vertex layout -- this shader mirrors that shape exactly rather than
// reusing lit3d.glsl's optional-color0 pattern.
//
// GpuDrawParams::diffuseColor/emissiveColor both arrive PRE-multiplied by alpha from the CPU side
// for this effect specifically (unlike BasicEffect/lit3d.glsl, which keep diffuse.rgb unmultiplied
// and apply alpha only via the alpha channel) -- ported verbatim from
// EasyGLRenderer::EnsureEnvMapped3DProgram()'s own `litRGB=lightSum*uDiffuseColor.rgb+
// uEmissiveColor` / `combinedAlpha=uDiffuseColor.a*texColor.a` shape, itself matching FNA's
// EnvironmentMapEffect.fx (PSEnvMap) exactly -- see docs/environmentmapeffect-support.md for the
// additive-vs-lerp / unscaled-vs-alpha-scaled-specular bug history this formula fixes.
//
// The reflection vector and the Fresnel blend factor are both computed per-VERTEX from each
// vertex's own un-interpolated world-space normal/eye vector, then Gouraud-interpolated -- NOT a
// per-fragment recompute from an interpolated normal, matching real XNA's own vertex-stage
// ComputeFresnelFactor/EnvCoord (a per-fragment recompute is not equivalent once vertices carry
// different normals). The diffuse lighting sum (NdotL against each directional light), by
// contrast, IS computed per-fragment from the interpolated world normal, matching
// EasyGLRenderer's identical split.

@module cna

@vs envmap3d_vs
layout(binding = 0) uniform envmap3d_vs_params {
    mat4 mvp;
    mat4 world;
    // Transpose(Invert(world)), computed CPU-side so a non-uniform scale still transforms the
    // normal correctly; only the upper-left 3x3 is used.
    mat4 normalMatrix;
    vec4 eyePosition;  // xyz used -- world-space camera position.
    // x=FresnelEnabled, y=FresnelFactor, z=EnvironmentMapAmount; w unused.
    vec4 flags;
    // REMED-GFX-010: FNA stock-effect fog vector, dotted with the OBJECT-space vertex position.
    vec4 fogVector;
};

in vec3 position;
in vec3 normal;
in vec2 texcoord0;

out vec3 worldNormal;
out vec2 uv;
out vec3 reflectDir;
out float fresnelBlend;
out float fogFactor;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    vec3 worldPos = (world * vec4(position, 1.0)).xyz;
    vec3 n = normalize(mat3(normalMatrix) * normal);
    vec3 eyeVector = normalize(eyePosition.xyz - worldPos);

    float viewAngle = dot(eyeVector, n);
    fresnelBlend = (flags.x > 0.5)
        ? pow(max(1.0 - abs(viewAngle), 0.0), flags.y) * flags.z
        : flags.z;
    reflectDir = reflect(-eyeVector, n);

    worldNormal = n;
    uv = texcoord0;
    fogFactor = 1.0 - clamp(dot(vec4(position, 1.0), fogVector), 0.0, 1.0);
}
@end

@fs envmap3d_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(binding = 1) uniform textureCube envTex;
layout(binding = 1) uniform sampler envSmp;

layout(binding = 2) uniform envmap3d_fs_params {
    vec4 diffuse;             // pre-multiplied by alpha; w = alpha itself
    vec4 emissiveColor;       // xyz used -- pre-combined emissive+ambient*diffuse, pre-multiplied
    vec4 light0Dir;           // xyz used
    vec4 light0Diffuse;       // xyz used
    vec4 light1Dir;
    vec4 light1Diffuse;
    vec4 light2Dir;
    vec4 light2Diffuse;
    vec4 envMapSpecular;      // xyz used
    // x=refVal, y=tolerance, z=passWeight, w=failWeight -- see GpuDrawParams::alphaTest's doc.
    vec4 alphaTest;
    vec4 fogColor;            // xyz used
    // REMED-GFX-147: 1 when `tex` is a RenderTarget2D's colour attachment. x used, yzw unused.
    // Only applies to the base 2D texture -- the cube map is never sampled Y-flipped.
    vec4 rtFlipV;
};

in vec3 worldNormal;
in vec2 uv;
in vec3 reflectDir;
in float fresnelBlend;
in float fogFactor;

out vec4 frag_color;

void main() {
    vec3 n = normalize(worldNormal);
    float ndotL0 = max(dot(n, -light0Dir.xyz), 0.0);
    float ndotL1 = max(dot(n, -light1Dir.xyz), 0.0);
    float ndotL2 = max(dot(n, -light2Dir.xyz), 0.0);
    vec3 lightSum = light0Diffuse.xyz * ndotL0 + light1Diffuse.xyz * ndotL1 + light2Diffuse.xyz * ndotL2;
    vec3 litRGB = lightSum * diffuse.xyz + emissiveColor.xyz;

    vec2 sampleUv = vec2(uv.x, mix(uv.y, 1.0 - uv.y, rtFlipV.x));
    vec4 texColor = texture(sampler2D(tex, smp), sampleUv);
    vec4 envSample = texture(samplerCube(envTex, envSmp), reflectDir);

    vec3 baseColor = litRGB * texColor.rgb;
    float combinedAlpha = diffuse.a * texColor.a;
    vec3 rgb = mix(baseColor, envSample.rgb * combinedAlpha, fresnelBlend)
             + envMapSpecular.xyz * envSample.a * combinedAlpha;
    frag_color = vec4(rgb, combinedAlpha);

    float pass = (alphaTest.y > 0.0)
        ? ((abs(frag_color.a - alphaTest.x) < alphaTest.y) ? alphaTest.z : alphaTest.w)
        : ((frag_color.a < alphaTest.x) ? alphaTest.z : alphaTest.w);
    if (pass < 0.0) discard;

    frag_color.rgb = mix(fogColor.xyz, frag_color.rgb, fogFactor);
}
@end

@program envmap3d envmap3d_vs envmap3d_fs
