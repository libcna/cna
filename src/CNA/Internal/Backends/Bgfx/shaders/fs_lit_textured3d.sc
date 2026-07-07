$input v_texcoord0, v_normal, v_color0, v_eyeDir

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

uniform vec4 u_ambientColor;
uniform vec4 u_light0Dir;
uniform vec4 u_light0Diffuse;
uniform vec4 u_lightingEnabled;
uniform vec4 u_light1Dir;
uniform vec4 u_light1Diffuse;
uniform vec4 u_light2Dir;
uniform vec4 u_light2Diffuse;
uniform vec4 u_emissiveColor;
uniform vec4 u_light0Specular;
uniform vec4 u_light1Specular;
uniform vec4 u_light2Specular;
uniform vec4 u_specularColorPower; // xyz = material SpecularColor, w = SpecularPower

void main()
{
    vec4 tex   = texture2D(s_texColor, v_texcoord0);
    vec3 N     = normalize(v_normal);
    vec3 E     = normalize(v_eyeDir);
    vec3 nL0 = normalize(u_light0Dir.xyz);
    vec3 nL1 = normalize(u_light1Dir.xyz);
    vec3 nL2 = normalize(u_light2Dir.xyz);
    float dotL0 = dot(N, -nL0); float zeroL0 = step(0.0, dotL0); float NdL0 = max(dotL0, 0.0);
    float dotL1 = dot(N, -nL1); float zeroL1 = step(0.0, dotL1); float NdL1 = max(dotL1, 0.0);
    float dotL2 = dot(N, -nL2); float zeroL2 = step(0.0, dotL2); float NdL2 = max(dotL2, 0.0);
    vec3 lightSum = u_ambientColor.xyz + NdL0 * u_light0Diffuse.xyz
                    + NdL1 * u_light1Diffuse.xyz + NdL2 * u_light2Diffuse.xyz;
    vec3 finalLight = mix(vec3(1.0, 1.0, 1.0), lightSum, u_lightingEnabled.x);
    // Half-vector Blinn-Phong specular (FNA's Lighting.fxh ComputeLights), gated by the same
    // zeroL "does this light face the surface" term used for diffuse. Material SpecularColor is
    // applied once to the summed per-light contribution, not per-light.
    vec3 h0 = normalize(E - nL0); float spec0 = pow(max(dot(h0, N), 0.0) * zeroL0, u_specularColorPower.w);
    vec3 h1 = normalize(E - nL1); float spec1 = pow(max(dot(h1, N), 0.0) * zeroL1, u_specularColorPower.w);
    vec3 h2 = normalize(E - nL2); float spec2 = pow(max(dot(h2, N), 0.0) * zeroL2, u_specularColorPower.w);
    vec3 specularRGB = (spec0 * u_light0Specular.xyz + spec1 * u_light1Specular.xyz
                        + spec2 * u_light2Specular.xyz) * u_specularColorPower.xyz;
    // EmissiveColor is added after the light sum is multiplied by DiffuseColor (v_color0),
    // not scaled by it, then the whole result is modulated by the texture sample (matches
    // FNA's PSBasicTexture: tex2D(...) * pin.Diffuse, where pin.Diffuse already has
    // EmissiveColor baked in additively by ComputeLights()). Specular is added after that
    // multiply too, scaled by the resulting alpha (FNA's AddSpecular macro), never by the texture.
    vec3 litColor = v_color0.rgb * finalLight + u_emissiveColor.xyz;
    gl_FragColor = tex * vec4(litColor, v_color0.a);
    gl_FragColor.rgb += specularRGB * gl_FragColor.a;
}
