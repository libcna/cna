$input v_texcoord0, v_normal, v_color0, v_fogFactor, v_worldPos, v_vertexColor0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_emissiveColor;
uniform vec4 u_fogColor;
uniform vec4 u_eyePos;
uniform vec4 u_light0Specular;
uniform vec4 u_light1Specular;
uniform vec4 u_light2Specular;
uniform vec4 u_specularColorPower; // xyz = material SpecularColor, w = SpecularPower
// CNB-67 (Phase 13C) Bgfx port: reuses the existing shared u_vertexColorEnabled3D uniform
// (already set by BasicEffect's no-texture/dual-texture/alpha-test-colored paths) rather than a
// new one -- bgfx uniforms are shared by name across every program (Task 888 precedent).
uniform vec4 u_vertexColorEnabled3D;

void main()
{
    vec4 tex  = texture2D(s_texColor, v_texcoord0);
    vec3 N    = normalize(v_normal);
    vec3 E    = normalize(u_eyePos.xyz - v_worldPos);
    vec3 nL0 = normalize(u_light0Dir.xyz);
    vec3 nL1 = normalize(u_light1Dir.xyz);
    vec3 nL2 = normalize(u_light2Dir.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdL2 = max(dotL2, 0.0);
    vec3 lightSum = NdL0 * u_light0Diffuse.xyz + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    // Half-vector Blinn-Phong specular (FNA's Lighting.fxh ComputeLights), gated by the same
    // zeroL "does this light face the surface" term used for diffuse. Material SpecularColor is
    // applied once to the summed per-light contribution, not per-light.
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, u_specularColorPower.w);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, u_specularColorPower.w);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, u_specularColorPower.w);
    vec3 specularRGB = (spec0 * u_light0Specular.xyz + spec1 * u_light1Specular.xyz
                        + spec2 * u_light2Specular.xyz) * u_specularColorPower.xyz;
    // Task 899: EmissiveColor was a total GPU no-op on Bgfx (u_emissiveColor was never even
    // declared here) -- found while writing this task's fog test, which (matching EasyGL's Task
    // 900 test) uses EmissiveColor as the sole non-fog material-color signal to isolate fog
    // cleanly. C++'s FillGpuDrawParams() already pre-combines AmbientLightColor*DiffuseColor into
    // emissiveColor (u_ambientColor is never separately populated for SkinnedEffect, always 0),
    // matching EasyGL's already-working (uEmissiveColor+uLight0Diffuse*NdotL)*uDiffuseColor.rgb.
    vec3 lit  = u_emissiveColor.xyz + u_ambientColor.xyz + lightSum;
    vec3 finalLight = mix(vec3(1.0, 1.0, 1.0), lit, u_lightingEnabled.x);
    // CNB-67 (Phase 13C) Bgfx port: vc is (1,1,1,1) unless VertexColorEnabled is set, matching
    // EasyGLGraphicsBackend::EnsureSkinnedProgram()'s vc gate exactly.
    vec4 vc = mix(vec4(1.0, 1.0, 1.0, 1.0), v_vertexColor0, u_vertexColorEnabled3D.x);
    gl_FragColor = tex * v_color0 * vec4(finalLight, 1.0);
    gl_FragColor.a *= vc.a;
    gl_FragColor.rgb += specularRGB * gl_FragColor.a;
    // Vertex color modulates the whole combined diffuse+specular output, not just diffuse --
    // applied after the specular add so VertexColorEnabled=true with a black vertex color
    // genuinely zeroes the pixel (a specular highlight added afterward would otherwise leak
    // through unmodulated). Mirrors EnsureSkinnedProgram()'s identical ordering/comment.
    gl_FragColor.rgb *= vc.rgb;
    // Task 899: mix toward FogColor as v_fogFactor -> 0 (matches Task 888's established formula).
    gl_FragColor.rgb = mix(u_fogColor.xyz, gl_FragColor.rgb, v_fogFactor);
}
