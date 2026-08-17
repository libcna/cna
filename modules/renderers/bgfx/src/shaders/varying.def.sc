vec4 v_color0    : COLOR0    = vec4(0.0, 0.0, 0.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_normal    : NORMAL    = vec3(0.0, 0.0, 1.0);
vec3 v_eyeDir    : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
float v_fogFactor : TEXCOORD2 = 1.0;
vec3 v_worldPos  : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec3 v_litRGB      : TEXCOORD4 = vec3(0.0, 0.0, 0.0);
vec3 v_specularRGB : TEXCOORD5 = vec3(0.0, 0.0, 0.0);
// PBR (CNB-58 Bgfx port): tangent xyz + glTF bitangent-handedness sign in w, matching
// EnsurePbrProgram()'s own vTangent/vBitangentSign pair packed into one varying slot.
vec4 v_tangent     : TEXCOORD6 = vec4(1.0, 0.0, 0.0, 1.0);
// GLTF-344: second sampled UV channel for PBR stride-60/76 layouts. Standard stride-48/68
// layouts omit a_texcoord1 and keep the seven-bit selector mask zero, so its default is unused.
vec2 v_texcoord1   : TEXCOORD7 = vec2(0.0, 0.0);
// Skinned vertex color (stride-56 SkinnedEffect+Color): kept in its own varying slot, distinct
// from v_color0 (which fs_skinned3d.sc/fs_skinned3d_vertexlit.sc already use to carry
// u_diffuseColor), so it can be gated by u_vertexColorEnabled3D and multiplied into the final
// combined diffuse+specular output separately -- mirrors EasyGLRenderer::
// EnsureSkinnedProgram()'s vColor.
vec4 v_vertexColor0 : COLOR1 = vec4(1.0, 1.0, 1.0, 1.0);

vec3 a_position  : POSITION;
vec4 a_color0    : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec2 a_texcoord1 : TEXCOORD1;
vec3 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec4 a_weight    : BLENDWEIGHT;
vec4 a_indices   : BLENDINDICES;
vec4 i_data0     : TEXCOORD7;
vec4 i_data1     : TEXCOORD6;
vec4 i_data2     : TEXCOORD5;
vec4 i_data3     : TEXCOORD4;
