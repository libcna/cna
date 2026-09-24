// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_opengl4_modern_graphics.md GL4-0008: the one GL stock-effect shader corpus.
//
// Every stock program CNA's OpenGL renderers draw BasicEffect, AlphaTestEffect, DualTextureEffect,
// EnvironmentMapEffect, SkinnedEffect, PbrEffect, SkinnedPbrEffect and SpriteBatch with is
// authored here once, in GLSL ES 3.00, and adapted per context by the renderer that compiles it
// (EasyGL: to GLSL 3.30 core, GLSL ES 1.00 or left as ES 3.00; OpenGL4: to desktop core). The
// text is moved verbatim from EasyGLRenderer.cpp, where it was the most-corrected statement of
// XNA's stock-effect semantics in CNA (null texture, fog, saturation, render-target orientation,
// shadow reception, image-based lighting). Keeping ONE copy is the point: a second renderer that
// hand-maintained its own GLSL drifted from it for two months (plans/plan_opengl4_modern_graphics.md
// GL4-0006), and every correction then had to be discovered twice.
//
// A renderer that compiles these programs owns the uniform/attribute contract they declare; the
// names are part of that contract and must not change without both renderers' binders.

#include "CNA/Internal/Graphics/SrgbTransfer.hpp"

#include <cstddef>
#include <string>

// REMED-GFX-147: the one GLSL declaration every fragment shader that samples a sampler2D shares.
//
// An OpenGL framebuffer's origin is bottom-left, so a render target's colour texture stores the
// logical image BOTTOM-UP: its texel row v=0 is the LAST logical row. Readback already compensates
// (EasyGLRenderTargetRenderer::GetData reads with glReadPixels' bottom-left origin and then reverses
// the rows), which is why the public GetData contract has always been top-down. Sampling did not,
// so a rendered texture arrived vertically mirrored while an uploaded one did not.
//
// uRtFlipV.x/.y/.z/.w carry texture units 0-3 and uRtFlipVHi.x/.y/.z carry units 4-6: 1 when the source
// bound there is a render-target colour attachment, 0 for an ordinary texture (and 0 always on a
// renderer whose framebuffer origin is top-left). Exactly one correction is applied, at sample
// time, per bound source -- nothing is copied, read back or re-uploaded, and no shader variant is
// created, because the flag is a uniform rather than a compile-time switch. Kept as one macro so
// the eleven shaders cannot drift apart; the same per-slot shape bgfx uses for u_rtFlipV
// (REMED-GFX-067/078).
#define CNA_GL_RT_SAMPLE_UV_DECL \
"uniform vec4 uRtFlipV;\n" \
"vec2 cnaSampleUV(vec2 uv,float flip){return vec2(uv.x,mix(uv.y,1.0-uv.y,flip));}\n"

/// REMED-GFX-147: units 4-6, declared only by the shaders that reach that far (PbrEffect).
#define CNA_GL_RT_SAMPLE_UV_HI_DECL \
"uniform vec4 uRtFlipVHi;\n"

/// plans/plan_gltf.md GLTF-210/GLTF-212: the sRGB transfer, pasted into the PbrEffect shaders. The text
/// is not written here -- it comes from CNA/Internal/Graphics/SrgbTransfer.hpp, which is also
/// where the C++ implementation of the same formula lives, so the two cannot drift into two
/// slightly different curves.
#define CNA_GL_SRGB_TRANSFER_DECL CNA_GLSL_SRGB_TRANSFER

// plans/plan_gltf.md GLTF-264: a normal follows the inverse transpose of the blended skin matrix.
// All EasyGL profiles, including GLSL ES 1.00, support cross/dot but ES 1.00 has no inverse() for
// matrices. The three cross products are the columns of det(m)*inverseTranspose(m). Normalisation
// cancels abs(det); multiplying by sign(det) retains the orientation under a mirrored joint. A
// nearly singular blend falls back to the historical direct transform, after which each caller's
// existing zero-length guard prevents a NaN from poisoning the lighting calculation.
#define CNA_GL_SKIN_NORMAL_DECL \
"vec3 cnaSkinNormal(mat3 m,vec3 n){\n" \
"    vec3 c0=m[0],c1=m[1],c2=m[2];\n" \
"    vec3 co0=cross(c1,c2),co1=cross(c2,c0),co2=cross(c0,c1);\n" \
"    float det=dot(c0,co0);\n" \
"    vec3 transformed=mat3(co0,co1,co2)*n;\n" \
"    return (abs(det)>1e-6)?transformed*sign(det):m*n;\n" \
"}\n"

// plans/plan_gltf.md GLTF-176: a tangent frame changes orientation under a negative-determinant
// direction transform. GLSL ES 1.00 has no determinant(mat3), so compute the scalar triple product
// shared by both PBR vertex programs. A singular transform has no meaningful tangent frame; +1 is
// the stable fallback and, unlike sign(0), does not erase an otherwise valid authored sign.
#define CNA_GL_DIRECTION_HANDEDNESS_DECL \
"float cnaDirectionHandedness(mat3 m){\n" \
"    float det=dot(m[0],cross(m[1],m[2]));\n" \
"    return (det<0.0)?-1.0:1.0;\n" \
"}\n"

// REMED-GFX-122: stock EasyGL effects share one optional per-instance world matrix input. Locations
// 12-15 reserve the final four slots of GLES 3's guaranteed 16-attribute floor. That leaves the
// complete XNA profile budget (12 per-vertex elements + 4 matrix columns) available instead of
// colliding with an otherwise-legal extended mesh declaration. uCnaInstanced keeps the same
// programs byte-for-byte equivalent for ordinary draws; only DrawInstancedPrimitivesEx enables
// the transform and binds these four attributes.
#define CNA_GL_INSTANCE_TRANSFORM_DECL \
"layout(location=12) in vec4 cnaInstanceCol0;\n" \
"layout(location=13) in vec4 cnaInstanceCol1;\n" \
"layout(location=14) in vec4 cnaInstanceCol2;\n" \
"layout(location=15) in vec4 cnaInstanceCol3;\n" \
"uniform float uCnaInstanced;\n" \
"mat4 cnaInstanceMatrix(){return mat4(cnaInstanceCol0,cnaInstanceCol1,cnaInstanceCol2,cnaInstanceCol3);}\n" \
"vec4 cnaInstancePosition(vec4 p){return (uCnaInstanced>0.5)?cnaInstanceMatrix()*p:p;}\n" \
"vec3 cnaInstanceDirection(vec3 d){return (uCnaInstanced>0.5)?mat3(cnaInstanceMatrix())*d:d;}\n"

// plans/plan_modern.md MOD-836..MOD-841: shadow reception, shared by every lit fragment shader so the
// four of them cannot drift into four subtly different shadows.
//
// The map holds light-space distance rather than a depth buffer: CNA cannot sample a depth
// attachment as a texture on every renderer, so CNA::Graphics::ShadowMap writes distance into an
// ordinary colour target and this reads it back like any other texture.
//
// uShadowTexel carries 1/size rather than the shader calling textureSize(): that function is GLSL
// ES 3.00 only, and these shaders are also transformed to ES 1.00 for the WebGL1/GLES2 profiles
// (TransformGlslEs300BodyToEs100), which would reject it. The loop bounds are literal for the same
// reason -- ES 1.00 requires a statically countable loop -- so the kernel is always 5x5 and
// uShadowPcfRadius decides how much of it counts. Radius 0 is a single tap.
//
// Returns 1 where the surface is lit and 0 where a caster is fully in front of it, with the
// fraction in between coming from the kernel: a single tap gives a hard, stair-stepped edge at
// every shadow-map resolution.
// Cascades (MOD-905/906/909/910) share the same code path: a single map is simply the case where
// uCascadeCount is 0, and everything below it is skipped. Two shapes of restriction shape the
// code more than taste does -- the ES 1.00 form these shaders are also compiled in forbids
// dynamically indexing a uniform array in a fragment shader, so the cascade's matrix is selected
// by four constant-index comparisons rather than by uCascadeMatrices[index]; and the cascades
// share one atlas, so every lookup is clamped to its own slice or a PCF tap at the seam would
// read the neighbouring cascade's texels.
//
// The cross-fade near a split (MOD-906) is not cosmetic either: without it the two cascades
// disagree about where an edge is, and the disagreement draws a straight line across the ground at
// the split distance -- more obviously wrong than the resolution change it is hiding.
#define CNA_GL_SHADOW_DECL \
"uniform sampler2D uShadowMap;\n" \
"uniform mat4 uLightViewProj;\n" \
"uniform float uShadowsEnabled;\n" \
"uniform float uShadowBias;\n" \
"uniform vec2 uShadowTexel;\n" \
"uniform float uShadowPcfRadius;\n" \
"uniform float uCascadeCount;\n" \
"uniform mat4 uCascadeMatrices[4];\n" \
"uniform vec4 uCascadeSplits;\n" \
"uniform vec4 uCascadeViewZ;\n" \
"uniform float uCascadeBlend;\n" \
"uniform float uCascadeDebug;\n" \
"float cnaShadowTap(vec3 uv,vec2 uvMin,vec2 uvMax){\n" \
"    if(uv.z>1.0) return 1.0;\n" \
"    float lit=0.0;\n" \
"    float taps=0.0;\n" \
"    for(int y=-2;y<=2;++y){\n" \
"        for(int x=-2;x<=2;++x){\n" \
"            float ring=max(abs(float(x)),abs(float(y)));\n" \
"            if(ring>uShadowPcfRadius+0.5) continue;\n" \
"            vec2 at=clamp(uv.xy+vec2(float(x),float(y))*uShadowTexel,uvMin,uvMax);\n" \
"            float occluder=texture(uShadowMap,at).r;\n" \
"            lit+=(uv.z-uShadowBias<=occluder)?1.0:0.0;\n" \
"            taps+=1.0;\n" \
"        }\n" \
"    }\n" \
"    return lit/max(taps,1.0);\n" \
"}\n" \
"mat4 cnaCascadeMatrix(int index){\n" \
"    mat4 m=uCascadeMatrices[0];\n" \
"    if(index==1) m=uCascadeMatrices[1];\n" \
"    if(index==2) m=uCascadeMatrices[2];\n" \
"    if(index==3) m=uCascadeMatrices[3];\n" \
"    return m;\n" \
"}\n" \
"float cnaCascadeSplit(int index){\n" \
"    float s=uCascadeSplits.x;\n" \
"    if(index==1) s=uCascadeSplits.y;\n" \
"    if(index==2) s=uCascadeSplits.z;\n" \
"    if(index==3) s=uCascadeSplits.w;\n" \
"    return s;\n" \
"}\n" \
"float cnaCascadeLookup(vec3 worldPos,int index,float count){\n" \
"    vec4 atlas=cnaCascadeMatrix(index)*vec4(worldPos,1.0);\n" \
"    vec3 uv=atlas.xyz/atlas.w;\n" \
"    float slice=1.0/count;\n" \
"    float x0=float(index)*slice;\n" \
"    if(uv.x<x0||uv.x>x0+slice||uv.y<0.0||uv.y>1.0) return 1.0;\n" \
"    vec2 uvMin=vec2(x0+uShadowTexel.x,uShadowTexel.y);\n" \
"    vec2 uvMax=vec2(x0+slice-uShadowTexel.x,1.0-uShadowTexel.y);\n" \
"    return cnaShadowTap(uv,uvMin,uvMax);\n" \
"}\n" \
"int cnaSelectCascade(float viewDepth,float count){\n" \
"    int chosen=int(count)-1;\n" \
"    for(int i=0;i<4;++i){\n" \
"        if(float(i)>=count) break;\n" \
"        if(viewDepth<=cnaCascadeSplit(i)){ chosen=i; break; }\n" \
"    }\n" \
"    return chosen;\n" \
"}\n" \
"float cnaShadowFactor(vec3 worldPos){\n" \
"    if(uShadowsEnabled<0.5) return 1.0;\n" \
"    if(uCascadeCount<0.5){\n" \
"        vec4 lightSpace=uLightViewProj*vec4(worldPos,1.0);\n" \
"        vec3 uv=lightSpace.xyz/lightSpace.w*0.5+0.5;\n" \
"        if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;\n" \
"        return cnaShadowTap(uv,vec2(0.0),vec2(1.0));\n" \
"    }\n" \
"    float viewDepth=-dot(vec4(worldPos,1.0),uCascadeViewZ);\n" \
"    int index=cnaSelectCascade(viewDepth,uCascadeCount);\n" \
"    float factor=cnaCascadeLookup(worldPos,index,uCascadeCount);\n" \
"    float split=cnaCascadeSplit(index);\n" \
"    if(uCascadeBlend>0.0&&float(index+1)<uCascadeCount&&viewDepth>split-uCascadeBlend){\n" \
"        float t=clamp((viewDepth-(split-uCascadeBlend))/uCascadeBlend,0.0,1.0);\n" \
"        factor=mix(factor,cnaCascadeLookup(worldPos,index+1,uCascadeCount),t);\n" \
"    }\n" \
"    return factor;\n" \
"}\n" \
"vec3 cnaCascadeDebugTint(vec3 worldPos){\n" \
"    if(uCascadeDebug<0.5||uShadowsEnabled<0.5||uCascadeCount<0.5) return vec3(1.0);\n" \
"    float viewDepth=-dot(vec4(worldPos,1.0),uCascadeViewZ);\n" \
"    int index=cnaSelectCascade(viewDepth,uCascadeCount);\n" \
"    if(index==0) return vec3(1.0,0.6,0.6);\n" \
"    if(index==1) return vec3(0.6,1.0,0.6);\n" \
"    if(index==2) return vec3(0.6,0.6,1.0);\n" \
"    return vec3(1.0,1.0,0.6);\n" \
"}\n"

// plans/plan_modern.md MOD-1005/MOD-1006: one punctual light -- point or spot -- with its own shadow.
// Kept beside the directional lookup rather than folded into it because the two shadow different
// lights: one answers "is the sun blocked here", the other "is that lamp blocked here", and
// multiplying one light's contribution by the other's visibility produces a plausible image and no
// clue that anything is wrong.
//
// Both maps store *distance from the light over its range*, so the comparison is the same
// arithmetic for a cube face and a spot map, and the cube is sampled by direction -- which is the
// whole reason distance is stored instead of projected depth.
//
// Three choices inside it are worth stating. A cube face is chosen by the sampled direction alone,
// so a point light needs no per-face bookkeeping in the lookup -- the six faces are one texture
// here. The cube path takes a single tap while the spot path filters 3x3: filtering across a cube
// face's edge needs seamless sampling, which is not available on every profile these shaders
// compile in, and a tap that silently wrapped to the wrong face would draw a stripe of shadow
// along every cube seam. And the inverse-square falloff is windowed to zero at the range, so the
// light ends exactly where its shadow map ends -- an unwindowed one never reaches zero and leaves
// a visible step at the boundary, which is precisely where the shadow stops being available.
#define CNA_GL_PUNCTUAL_DECL \
"uniform float uPunctualKind;\n" \
"uniform vec3 uPunctualPosition;\n" \
"uniform vec3 uPunctualDirection;\n" \
"uniform vec3 uPunctualDiffuse;\n" \
"uniform float uPunctualRange;\n" \
"uniform float uPunctualCosInner;\n" \
"uniform float uPunctualCosOuter;\n" \
"uniform float uPunctualBias;\n" \
"uniform float uPunctualHasShadow;\n" \
"uniform vec2 uPunctualTexel;\n" \
"uniform samplerCube uPunctualCube;\n" \
"uniform sampler2D uPunctualMap;\n" \
"uniform mat4 uPunctualViewProj;\n" \
"float cnaPunctualShadow(vec3 worldPos,vec3 toLight,float distanceToLight){\n" \
"    if(uPunctualHasShadow<0.5) return 1.0;\n" \
"    float here=clamp(distanceToLight/uPunctualRange,0.0,1.0);\n" \
"    if(uPunctualKind<1.5){\n" \
"        float occluder=texture(uPunctualCube,-toLight).r;\n" \
"        return (here-uPunctualBias<=occluder)?1.0:0.0;\n" \
"    }\n" \
"    vec4 clip=uPunctualViewProj*vec4(worldPos,1.0);\n" \
"    if(clip.w<=0.0) return 1.0;\n" \
"    vec3 uv=clip.xyz/clip.w*0.5+0.5;\n" \
"    if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;\n" \
"    float lit=0.0;\n" \
"    for(int y=-1;y<=1;++y){\n" \
"        for(int x=-1;x<=1;++x){\n" \
"            vec2 at=clamp(uv.xy+vec2(float(x),float(y))*uPunctualTexel,vec2(0.0),vec2(1.0));\n" \
"            float occluder=texture(uPunctualMap,at).r;\n" \
"            lit+=(here-uPunctualBias<=occluder)?1.0:0.0;\n" \
"        }\n" \
"    }\n" \
"    return lit/9.0;\n" \
"}\n" \
"vec3 cnaPunctualLight(vec3 worldPos,vec3 normal){\n" \
"    if(uPunctualKind<0.5) return vec3(0.0);\n" \
"    vec3 offset=uPunctualPosition-worldPos;\n" \
"    float distanceToLight=length(offset);\n" \
"    if(distanceToLight>uPunctualRange||distanceToLight<1e-5) return vec3(0.0);\n" \
"    vec3 toLight=offset/distanceToLight;\n" \
"    float t=distanceToLight/uPunctualRange;\n" \
"    float window=clamp(1.0-t*t*t*t,0.0,1.0);\n" \
"    float attenuation=window*window/(1.0+distanceToLight*distanceToLight);\n" \
"    if(uPunctualKind>1.5){\n" \
"        float cosAngle=dot(normalize(uPunctualDirection),-toLight);\n" \
"        float cone=clamp((cosAngle-uPunctualCosOuter)/max(uPunctualCosInner-uPunctualCosOuter,1e-4),0.0,1.0);\n" \
"        attenuation*=cone*cone;\n" \
"    }\n" \
"    float ndotl=max(dot(normal,toLight),0.0);\n" \
"    return uPunctualDiffuse*ndotl*attenuation*cnaPunctualShadow(worldPos,toLight,distanceToLight);\n" \
"}\n"

namespace CNA::Internal::Renderers::GlStockShaders
{
    /** @brief The two sources of one stock program, before any per-context adaptation. */
    struct GlStockProgramSource
    {
        /** @brief GLSL ES 3.00 vertex shader source. */
        std::string vertex;
        /** @brief GLSL ES 3.00 fragment shader source. */
        std::string fragment;
    };

    // plans/plan_modern.md MOD-1225: the split-sum ambient term, replacing the flat uAmbientColor when an
    // environment is bound. Three inputs that were generated together (CNA::Graphics::
    // EnvironmentProcessor): the irradiance cube read by the normal, the prefiltered specular cube
    // whose mip IS the roughness, and the BRDF table that says how much of the reflection survives at
    // this angle and roughness.
    //
    // A function rather than another CNA_GL_*_DECL macro, because one line of it depends on the
    // profile: selecting a mip explicitly needs textureLod, which GLSL ES 1.00 fragment shaders do not
    // have. Those profiles read the cube's base level instead, so a rough surface reflects a sharp
    // environment -- wrong, visibly so on a rough metal, and the honest alternative to silently
    // compiling nothing at all. Every other profile gets the real roughness ramp.
    //
    // The Fresnel term uses max(1-roughness, F0) rather than plain F0: at grazing angles a rough
    // surface must not reflect as hard as a mirror, and the plain Schlick form makes it do exactly
    // that. Diffuse and specular are then weighted so they do not both claim the same energy, and
    // metals get no diffuse at all.
    [[nodiscard]] inline std::string CnaGlIblDecl(const bool explicitLodAvailable)
    {
        const char* const prefilteredSample = explicitLodAvailable
            ? "textureLod(uIblSpecular,R,lod)"
            : "texture(uIblSpecular,R)";
        return std::string(
    "uniform samplerCube uIblIrradiance;\n"
    "uniform samplerCube uIblSpecular;\n"
    "uniform sampler2D uIblBrdfLut;\n"
    "uniform float uIblEnabled;\n"
    "uniform float uIblMipCount;\n"
    "uniform float uIblIntensity;\n"
    "vec3 cnaIblAmbient(vec3 N,vec3 V,vec3 albedo,vec3 F0,float roughness,float metallic,float occlusion){\n"
    "    if(uIblEnabled<0.5) return vec3(0.0);\n"
    "    float NdotV=clamp(dot(N,V),1e-4,1.0);\n"
    "    vec3 kS=F0+(max(vec3(1.0-roughness),F0)-F0)*pow(1.0-NdotV,5.0);\n"
    "    vec3 kD=(1.0-kS)*(1.0-metallic);\n"
    "    vec3 diffuse=texture(uIblIrradiance,N).rgb*albedo*kD;\n"
    "    vec3 R=reflect(-V,N);\n"
    "    float lod=roughness*max(uIblMipCount-1.0,0.0);\n"
    "    vec3 prefiltered=") + prefilteredSample + std::string(".rgb;\n"
    "    vec2 ab=texture(uIblBrdfLut,vec2(NdotV,roughness)).rg;\n"
    "    vec3 specular=prefiltered*(kS*ab.x+ab.y);\n"
    "    return (diffuse+specular)*uIblIntensity*occlusion;\n"
    "}\n");
    }

    /**
     * @brief Converts a stock vertex shader's Direct3D clip depth to OpenGL's and pins gl_PointSize.
     *
     * @param source A stock GLSL ES 3.00 vertex shader.
     * @return The source with the conversion inserted after its gl_Position assignment.
     */
    [[nodiscard]] inline std::string AdaptStockVertexShaderForOpenGL(const char* source)
    {
        std::string result(source);
        const std::size_t position = result.find("gl_Position");
        if (position == std::string::npos)
            return result;

        const std::size_t terminator = result.find(';', position);
        if (terminator != std::string::npos)
        {
            // SOFTWARE-336: every matrix exposed by XNA produces Direct3D clip depth in
            // [0,w], while OpenGL accepts [-w,w]. MojoShader applies this same conversion to
            // classic compiled Effects. Applying it to all renderer-owned 3D programs makes
            // the near plane, viewport depth range, and mixed stock/compiled draws agree.
            std::string additions =
                "\n    gl_Position.z=gl_Position.z*2.0-gl_Position.w;";
            if (result.find("gl_PointSize") == std::string::npos)
                additions += "\n    gl_PointSize=1.0;";
            result.insert(terminator + 1, additions);
        }
        return result;
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- colored3d: BasicEffect untextured, vertex colour gated by VertexColorEnabled.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource Colored3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 vColor;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
// SOFTWARE-153: XNA's stock Common.fxh writes material*vertex colour to D3D9 COLOR0. That
// semantic is saturated at the vertex boundary, before interpolation and texture sampling.
"    vColor=clamp(((uVertexColorEnabled>0.5)?aColor:vec4(1.0))*uDiffuseColor,0.0,1.0);\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in float vFogFactor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    FragColor=vColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- textured3d: BasicEffect/AlphaTestEffect textured.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource Textured3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"uniform vec4 uDiffuseColor;\n"
"out vec2 vUV;\n"
"out vec4 vDiffuse;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"
"    vDiffuse=clamp(uDiffuseColor,0.0,1.0);\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in vec4 vDiffuse;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vDiffuse;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- colored_textured3d: textured with vertex colour.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource ColoredTextured3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vColor=clamp(((uVertexColorEnabled>0.5)?aColor:vec4(1.0))*uDiffuseColor,0.0,1.0);\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- lit_textured3d: per-pixel lit BasicEffect family (PreferPerPixelLighting or shadow reception).
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource Lit3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
// plans/plan_fx.md FX-125: BasicEffect.VertexColorEnabled has a "Vc" variant of every
// lit family in XNA (VSBasicVertexLightingVc and friends), which multiplies the lit
// diffuse by the per-vertex colour. These programs had no colour input at all, so a mesh
// that is BOTH lit and vertex-coloured could not be drawn by them; selection fell through
// to the unlit prog_colored_ and the model rendered flat, with no shading and no specular.
// The location is the element's INDEX in this program's own input table
// (ConfigureDeclarationForStockProgramEXT binds inputs[i] to location i), so kLitColor's
// {aPos, aNormal, aUV, aColor} puts the colour at 3. When a draw has no colour
// element the attribute is simply unbound and uVertexColorEnabled is 0, so vc is white and
// every existing lit draw is byte-identical.
"layout(location=3) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out vec4 vColor;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vNormal=uNormalMatrix*cnaInstanceDirection(aNormal);\n"
"    vUV=aUV;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
// plans/plan_fx.md FX-124: this fragment stage normalizes a WORLD-SPACE vector --
// normalize(uEyePosition - vWorldPos) -- and `mediump` guarantees only fp16 RANGE (~65504).
// dot(v, v) is computed first, so an eye a few thousand units from the geometry overflows,
// inversesqrt returns 0, the view direction collapses to the zero vector, and the specular term
// is wrong everywhere while the frame still reads as a plausible lit render. Measured, not
// assumed: SAMPLE-046 puts its camera 3500 units out, and with one directional light and
// PreferPerPixelLighting the frame agreed with real XNA on 90.62% of pixels within 8 levels at
// mediump and 99.99% at highp, the signed error over the model going from -20 levels to -0.01.
// Mesa does honour the qualifier here. GLSL ES 3.00 requires fragment highp, so it is asked for
// unconditionally; the GLSL ES 1.00 profiles get the GL_FRAGMENT_PRECISION_HIGH guard from
// TransformGlslEs300BodyToEs100, where 1.00 makes highp optional. FX-121 is the same defect in
// MojoShader's compiled-effect path; this is the built-in effect path.
"precision highp float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"in vec4 vColor;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform float uLightingEnabled;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform float uVertexColorEnabled;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
CNA_GL_SHADOW_DECL
CNA_GL_PUNCTUAL_DECL
"void main(){\n"
// XNA uses a separate unlit shader variant. Do not merely zero the light colours and continue
// through the lit math here: an unlit vertex at the default eye position makes normalize(0)
// undefined, and NaN*zero is still NaN, turning the otherwise-correct diffuse result black.
"    vec3 litRGB=uDiffuseColor.rgb;\n"
"    vec3 specularRGB=vec3(0.0);\n"
"    if(uLightingEnabled>0.5){\n"
"        vec3 N=normalize(vNormal);\n"
"        vec3 E=normalize(uEyePosition-vWorldPos);\n"
"        float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"        float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"        float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
// MOD-838: the shadow attenuates direct light only. Ambient is light arriving from every
// direction, and darkening it here would make a shadowed surface black rather than shaded.
"        float shadow=cnaShadowFactor(vWorldPos);\n"
"        vec3 lightSum=uAmbientColor+(uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2)*shadow+cnaPunctualLight(vWorldPos,N);\n"
"        litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"        vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"        vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"        vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"        specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor*shadow;\n"
"    }\n"
// FX-125: XNA's Vc variants multiply the vertex colour into the DIFFUSE result before the
// specular term is added -- `vout.Diffuse *= vin.Color`, then `color.rgb += Specular * color.a`
// -- so the highlight is scaled only through the resulting alpha, exactly as here.
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vec4(litRGB,uDiffuseColor.a)*vc;\n"
"    FragColor.rgb*=cnaCascadeDebugTint(vWorldPos);\n"
"    FragColor.rgb+=specularRGB*FragColor.a;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- lit_textured3d_vertexlit: per-vertex lit BasicEffect family.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource Lit3DVertexLitSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
// plans/plan_fx.md FX-125: BasicEffect.VertexColorEnabled has a "Vc" variant of every
// lit family in XNA (VSBasicVertexLightingVc and friends), which multiplies the lit
// diffuse by the per-vertex colour. These programs had no colour input at all, so a mesh
// that is BOTH lit and vertex-coloured could not be drawn by them; selection fell through
// to the unlit prog_colored_ and the model rendered flat, with no shading and no specular.
// The location is the element's INDEX in this program's own input table
// (ConfigureDeclarationForStockProgramEXT binds inputs[i] to location i), so kLitColor's
// {aPos, aNormal, aUV, aColor} puts the colour at 3. When a draw has no colour
// element the attribute is simply unbound and uVertexColorEnabled is 0, so vc is white and
// every existing lit draw is byte-identical.
"layout(location=3) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
// uDiffuseColor is read by BOTH stages here (vertex needs .rgb for vLitRGB, fragment needs .a) --
// GLSL ES 3.00 requires a uniform shared across stages to have the SAME precision qualification,
// and this shader's own vertex/fragment stages have different DEFAULT float precisions (highp
// here vs. mediump in the fragment stage below), so it must be qualified explicitly and
// identically in both declarations or linking fails ("mismatching precision qualifiers") --
// found via a real link failure, not assumed.
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec3 uEmissiveColor;\n"
// Declared in THIS stage only. The fragment stage defaults to mediump while this one is
// highp, and GLSL ES 3.00 refuses to link a uniform shared across stages with different
// precision -- the same trap uDiffuseColor above documents. Since the multiply moved here,
// the fragment stage does not need it.
"uniform float uVertexColorEnabled;\n"
// Declared in THIS stage only. The fragment stage below defaults to mediump while
// this one is highp, and GLSL ES 3.00 refuses to link a uniform shared across stages
// with different precision -- the same trap uDiffuseColor above documents. After the
// multiply moved here the fragment stage no longer needs it at all.

"out vec2 vUV;\n"
"out float vVertexAlpha;\n"
"out float vFogFactor;\n"
"out vec3 vLitRGB;\n"
"out vec3 vSpecularRGB;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"


// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"    vec3 worldPos=(uWorld*cnaPos).xyz;\n"
"    vec3 N=normalize(uNormalMatrix*cnaInstanceDirection(aNormal));\n"
"    vec3 E=normalize(uEyePosition-worldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// plans/plan_fx.md FX-123: Direct3D 9 clamps a vertex shader's colour output registers
// (oD0/oD1) to [0,1] BEFORE interpolating them, so XNA's own VSBasicVertexLighting /
// VSSkinnedVertexLighting hand a saturated colour to the rasterizer even though the .fx
// source never writes a saturate(). These are plain varyings, which nothing clamps, so an
// unclamped sum interpolates high between vertices and the triangle comes out BRIGHTER than
// D3D9's. It only shows once the lights accumulate past 1: SAMPLE-046 agrees with real XNA
// to 99.99%% with any ONE of its three directional lights on and drops to 90.31%% with all
// three. Saturating here is what oD0/oD1 do, not an approximation of them. FX-122 fixed the
// same D3D9 semantic in MojoShader's compiled-effect path; this is the built-in effect path.
// FX-125 ordering: XNA's Vc variants apply `vout.Diffuse *= vin.Color` BEFORE the value reaches
// oD0, and oD0 is what Direct3D 9 saturates (FX-123). Clamping first and scaling afterwards is a
// DIFFERENT picture, not a rounding difference: with a lit sum of 1.8 and a vertex colour of 0.5
// it yields 0.5 where D3D9 yields 0.9, so the model darkens exactly as the light grows. Measured
// on SAMPLE-047: the sphere agreed on 99.76%% of pixels with the vertex colour switched off and
// 46.94%% with it on, CNA being up to 84 levels too dark.
"    vec4 vcv=(uVertexColorEnabled>0.5)?aColor:vec4(1.0,1.0,1.0,1.0);\n"
"    vLitRGB=clamp((lightSum*uDiffuseColor.rgb+uEmissiveColor)*vcv.rgb,0.0,1.0);\n"
"    vVertexAlpha=vcv.a;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=clamp((spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor,0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vVertexAlpha;\n"
"in float vFogFactor;\n"
"in vec3 vLitRGB;\n"
"in vec3 vSpecularRGB;\n"
"uniform sampler2D uTexture;\n"
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"

"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
// FX-125: XNA's Vc variants multiply the vertex colour into the DIFFUSE result before the
// specular term is added -- `vout.Diffuse *= vin.Color`, then `color.rgb += Specular * color.a`
// -- so the highlight is scaled only through the resulting alpha, exactly as here.
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vec4(vLitRGB,uDiffuseColor.a*vVertexAlpha);\n"
"    FragColor.rgb+=vSpecularRGB*FragColor.a;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- dual_textured3d: DualTextureEffect.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource DualTextured3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
"layout(location=2) in vec2 aUV1;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"uniform vec4 uDiffuseColor;\n"
"out vec2 vUV;\n"
"out vec2 vUV1;\n"
"out vec4 vDiffuse;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"
"    vUV1=aUV1;\n"
"    vDiffuse=clamp(uDiffuseColor,0.0,1.0);\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in vec2 vUV1;\n"
"in vec4 vDiffuse;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 base=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,cnaSampleUV(vUV1,uRtFlipV.y))*vDiffuse;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- dual_textured_colored3d: DualTextureEffect with vertex colour.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource DualTexturedColored3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec2 aUV1;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out vec2 vUV1;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vColor=clamp(((uVertexColorEnabled>0.5)?aColor:vec4(1.0))*uDiffuseColor,0.0,1.0);\n"
"    vUV=aUV;\n"
"    vUV1=aUV1;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in vec2 vUV;\n"
"in vec2 vUV1;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 base=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,cnaSampleUV(vUV1,uRtFlipV.y))*vColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- env_mapped3d: EnvironmentMapEffect.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource EnvMapped3DSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uWorld;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uFogVector;\n"
"uniform float uEnvMapAmount;\n"
"uniform float uFresnelEnabled;\n"
"uniform float uFresnelFactor;\n"
"out vec3 vWorldNormal;\n"
"out vec3 vEyeDir;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out float vFresnel;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vec3 worldPos=(uWorld*cnaPos).xyz;\n"
"    vec3 worldNormal=normalize(uNormalMatrix*cnaInstanceDirection(aNormal));\n"
"    vec3 eyeVector=normalize(uEyePosition-worldPos);\n"
"    vWorldNormal=worldNormal;\n"
"    vEyeDir=eyeVector;\n"
"    vUV=aUV;\n"
// Real XNA (EnvironmentMapEffect.fx's ComputeFresnelFactor) evaluates this per-VERTEX, in the
// vertex shader, from each vertex's own un-interpolated normal/eye vector, then Gouraud-
// interpolates the resulting scalar -- NOT a per-fragment recompute from an interpolated normal
// (Task 1112: the two are not equivalent once vertices carry different normals).
"    float viewAngle=dot(eyeVector,worldNormal);\n"
// The clamp is XNA's, not a safety net. EnvironmentMapEffect.fx carries this scalar to the
// pixel shader in `float4 Specular : COLOR1` (Structures.fxh's VSOutputTxEnvMap), and Direct3D 9
// saturates a vertex shader's COLOR output registers to [0,1] BEFORE interpolating them. The
// value itself is not bounded -- ComputeFresnelFactor multiplies by EnvironmentMapAmount, whose
// XNA range reaches well past 1 -- and it is then used as the weight of
// `lerp(color.rgb, envmap.rgb, ...)`. Without the clamp that lerp EXTRAPOLATES past the
// environment map's own colour and the rim over-brightens: on RimLighting_4_0 at
// EnvironmentMapAmount 5 that turned XNA's orange rim yellow-white and cost 4.5 % of the frame
// (SAMPLE-037). Clamp here, at the vertex, so the interpolation starts from the same values
// D3D9's register file would hold -- clamping per fragment instead would interpolate the
// unclamped value first and give a different gradient (plans/plan_fx.md FX-122 is the same
// distinction for translated effects).
"    vFresnel=clamp((uFresnelEnabled>0.5)\n"
"        ? pow(max(1.0-abs(viewAngle),0.0),uFresnelFactor)*uEnvMapAmount\n"
"        : uEnvMapAmount, 0.0, 1.0);\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vWorldNormal;\n"
"in vec3 vEyeDir;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in float vFresnel;\n"
"uniform sampler2D uTexture;\n"
"uniform samplerCube uEnvMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEnvMapSpecular;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec3 N=normalize(vWorldNormal);\n"
"    vec3 E=normalize(vEyeDir);\n"
"    float NdotL0=max(dot(N,-uLight0Dir),0.0);\n"
"    float NdotL1=max(dot(N,-uLight1Dir),0.0);\n"
"    float NdotL2=max(dot(N,-uLight2Dir),0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// Same FNA-fidelity fix as EnsureSkinnedProgram below - EnvironmentMapEffect.fx routes its own
// lighting through the identical Lighting.fxh ComputeLights() in FNA, so it composes emissive
// exactly the same way (added after the diffuse multiply, not multiplied by it).
"    vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec4 texColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    vec3 reflDir=reflect(-E,N);\n"
"    vec4 envSample=texture(uEnvMap,reflDir);\n"
"    vec3 baseColor=litRGB*texColor.rgb;\n"
"    float combinedAlpha=uDiffuseColor.a*texColor.a;\n"
"    float blendFactor=vFresnel;\n"
"    vec3 rgb=mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)+uEnvMapSpecular*envSample.a*combinedAlpha;\n"
"    FragColor=vec4(rgb,combinedAlpha);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- skinned3d: per-pixel lit SkinnedEffect.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource SkinnedSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aBoneWeights;\n"
"layout(location=4) in vec4 aBoneIndices;\n"
"layout(location=5) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"out vec4 vColor;\n"
CNA_GL_SKIN_NORMAL_DECL
"void main(){\n"
// Task 895: FNA's real Skin(vin, boneCount) only sums the first WeightsPerVertex (1, 2, or 4)
// weight/index pairs -- matches XNA's own validated property range, so >=2/>=4 gating suffices.
"    mat4 skinMat=uBones[int(aBoneIndices.x)]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[int(aBoneIndices.y)]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[int(aBoneIndices.z)]*aBoneWeights.z+uBones[int(aBoneIndices.w)]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    vec4 cnaPos=cnaInstancePosition(skinnedPos);\n"
"    gl_Position=uWVP*cnaPos;\n"
// A vertex blended near-evenly between two bones whose current relative rotation is
// close to 180 degrees (reachable in practice: wide weight-blend joint regions x a
// large-angle animation pose, e.g. Wave) can make the linearly-blended skinMat's
// rotational part nearly cancel out for this particular normal, so its transformed
// length collapses toward zero. normalize() of a near-zero vector is numerically
// unstable (can yield NaN), which then poisons the entire downstream lighting sum --
// observed as solid-black blotches independent of ambient/diffuse light color, not a
// plausible-but-wrong shading direction. Falls back to the untransformed bind-pose
// normal for just that vertex rather than propagating NaN; XNA/FNA's own Skin() was
// never validated against this degenerate case, so this is a numerical-safety guard,
// not a deviation from its intended per-vertex transform.
// FNA/XNA SkinnedEffect.fx transforms normals directly by the weighted bone 3x3. Bone palettes
// are assumed to contain rigid/uniform transforms; only the outer World uses inverse-transpose.
// Keep this classic stock-effect path exact even though the CNAEXT PBR skinning path deliberately
// supports non-uniform glTF joint scale via cnaSkinNormal().
"    vec3 skinnedNormal=mat3(skinMat)*aNormal;\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
// REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
// (uNormalMatrix = transpose(inverse(World3x3)), CPU-precomputed in BindDrawParams() exactly as
// every non-skinned lit program here already receives it). FNA's SkinnedEffect.fx establishes the
// direct-bone then inverse-transpose-World composition order. This shader also used to drop the
// outer world factor entirely (audit Variant A), so any rotated or non-uniformly-scaled skinned
// model was lit as if World were identity. The fragment stage re-normalizes vNormal.
"    vNormal=uNormalMatrix*cnaInstanceDirection(boneNormal);\n"
"    vUV=aUV;\n"
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
// Skinned: dot the POST-skin position (FNA Skin() mutates vin.Position before ComputeFogFactor).
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
// plans/plan_fx.md FX-124: this fragment stage normalizes a WORLD-SPACE vector --
// normalize(uEyePosition - vWorldPos) -- and `mediump` guarantees only fp16 RANGE (~65504).
// dot(v, v) is computed first, so an eye a few thousand units from the geometry overflows,
// inversesqrt returns 0, the view direction collapses to the zero vector, and the specular term
// is wrong everywhere while the frame still reads as a plausible lit render. Measured, not
// assumed: SAMPLE-046 puts its camera 3500 units out, and with one directional light and
// PreferPerPixelLighting the frame agreed with real XNA on 90.62% of pixels within 8 levels at
// mediump and 99.99% at highp, the signed error over the model going from -20 levels to -0.01.
// Mesa does honour the qualifier here. GLSL ES 3.00 requires fragment highp, so it is asked for
// unconditionally; the GLSL ES 1.00 profiles get the GL_FRAGMENT_PRECISION_HIGH guard from
// TransformGlslEs300BodyToEs100, where 1.00 makes highp optional. FX-121 is the same defect in
// MojoShader's compiled-effect path; this is the built-in effect path.
"precision highp float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"in vec4 vColor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
CNA_GL_SHADOW_DECL
CNA_GL_PUNCTUAL_DECL
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 E=normalize(uEyePosition-vWorldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
// MOD-837: direct light only. This shader has no uAmbientColor of its own -- FillGpuDrawParams
// folds ambient into uEmissiveColor, which is added below and therefore already outside the
// attenuated term.
"    float cnaShadow=cnaShadowFactor(vWorldPos);\n"
"    vec3 lightSum=(uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2)*cnaShadow+cnaPunctualLight(vWorldPos,N);\n"
// audit_net.md remediation (2026-07-18, fourth round): EmissiveColor is ADDED after the
// diffuse multiply, never multiplied by it - matches FNA's own Lighting.fxh ComputeLights()
// verbatim (`mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor`), and matches what
// EnsureLit3DProgram/EnsureLit3DVertexLitProgram in this same file already did correctly.
// This shader had `(uEmissiveColor+lightSum)*uDiffuseColor.rgb`, which multiplied the emissive
// term by DiffuseColor a second time. Since FillGpuDrawParams pre-folds ambient into emissive
// (`emissive + ambient*diffuse`, itself correct and FNA-faithful), the old form computed
// ambient*diffuse^2 - a quadratic suppression that crushed DARK materials specifically: the
// avatar's shoes (diffuse 0.14) got an ambient floor of 0.5*0.14*0.14 = 0.0098 -> 2.5/255
// (confirmed by direct pixel sampling: the darkest foot pixels read exactly (3,3,3)) instead
// of the correct 0.5*0.14 = 0.07 -> 18/255. This is the real reason raising ambient kept
// giving diminishing returns on the dark regions.
"    vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor*cnaShadow;\n"
"    vec4 texColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vec4(litRGB*texColor.rgb,uDiffuseColor.a*texColor.a*vc.a);\n"
"    FragColor.rgb*=cnaCascadeDebugTint(vWorldPos);\n"
"    FragColor.rgb+=specularRGB*FragColor.a;\n"
// Vertex color modulates the whole combined diffuse+specular output, not just diffuse -- applied
// after the specular add so VertexColorEnabled=true with a black vertex color genuinely zeroes
// the pixel (a specular highlight added afterward would otherwise leak through unmodulated).
"    FragColor.rgb*=vc.rgb;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- skinned3d_vertexlit: per-vertex lit SkinnedEffect.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource SkinnedVertexLitSource()
    {
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec4 aBoneWeights;\n"
"layout(location=4) in vec4 aBoneIndices;\n"
"layout(location=5) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
// uDiffuseColor is read by BOTH stages here (vertex needs .rgb for vLitRGB, fragment needs .a) --
// same GLSL ES "matching precision qualifier across stages" requirement Task 1102 already found
// and fixed for BasicEffect's own vertex-lit shader -- qualified explicitly and identically in
// both declarations here too, not assumed safe by analogy.
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uLight0Specular;\n"
"uniform vec3 uLight1Specular;\n"
"uniform vec3 uLight2Specular;\n"
"uniform vec3 uSpecularColor;\n"
"uniform float uSpecularPower;\n"
"uniform vec3 uEyePosition;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vLitRGB;\n"
"out vec3 vSpecularRGB;\n"
"out vec4 vColor;\n"
CNA_GL_SKIN_NORMAL_DECL
"void main(){\n"
"    mat4 skinMat=uBones[int(aBoneIndices.x)]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[int(aBoneIndices.y)]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[int(aBoneIndices.z)]*aBoneWeights.z+uBones[int(aBoneIndices.w)]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    vec4 cnaPos=cnaInstancePosition(skinnedPos);\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor*alpha,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"    vec3 worldPos=(uWorld*cnaPos).xyz;\n"
// Same degenerate-blend-normal guard as EnsureSkinnedProgram() above (see its own
// comment for the root cause) -- this vertex-lit sibling does the identical skinning
// and normal transform, just with lighting evaluated per-vertex instead of per-pixel.
// Match FNA/XNA Skin(): normal * weighted bone 3x3. See the per-pixel sibling above.
"    vec3 skinnedNormal=mat3(skinMat)*aNormal;\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
// REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix (uNormalMatrix =
// transpose(inverse(World3x3)), CPU-precomputed in BindDrawParams()). This vertex-lit sibling had
// the identical missing-world-factor defect (audit Variant A) as EnsureSkinnedProgram; unlike that
// per-pixel program (whose fragment stage re-normalizes vNormal), lighting here is evaluated in
// this stage, so the world-transformed normal must be re-normalized before the dot products.
"    vec3 N=normalize(uNormalMatrix*cnaInstanceDirection(boneNormal));\n"
"    vec3 E=normalize(uEyePosition-worldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
// Same FNA-fidelity fix as EnsureSkinnedProgram above - see its own comment for the full
// reasoning; this vertex-lit sibling had the identical emissive-multiplied-twice bug.
// plans/plan_fx.md FX-123: Direct3D 9 clamps a vertex shader's colour output registers
// (oD0/oD1) to [0,1] BEFORE interpolating them, so XNA's own VSBasicVertexLighting /
// VSSkinnedVertexLighting hand a saturated colour to the rasterizer even though the .fx
// source never writes a saturate(). These are plain varyings, which nothing clamps, so an
// unclamped sum interpolates high between vertices and the triangle comes out BRIGHTER than
// D3D9's. It only shows once the lights accumulate past 1: SAMPLE-046 agrees with real XNA
// to 99.99%% with any ONE of its three directional lights on and drops to 90.31%% with all
// three. Saturating here is what oD0/oD1 do, not an approximation of them. FX-122 fixed the
// same D3D9 semantic in MojoShader's compiled-effect path; this is the built-in effect path.
"    vLitRGB=clamp(lightSum*uDiffuseColor.rgb+uEmissiveColor,0.0,1.0);\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=clamp((spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor,0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"in vec3 vLitRGB;\n"
"in vec3 vSpecularRGB;\n"
"in vec4 vColor;\n"
"uniform sampler2D uTexture;\n"
"uniform highp vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 texColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vec4(vLitRGB*texColor.rgb,uDiffuseColor.a*texColor.a*vc.a);\n"
"    FragColor.rgb+=vSpecularRGB*FragColor.a;\n"
// See EnsureSkinnedProgram()'s identical comment: vertex color modulates the whole combined
// diffuse+specular output, applied after the specular add.
"    FragColor.rgb*=vc.rgb;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor*FragColor.a,FragColor.rgb,vFogFactor);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- pbr3d: PbrEffect (glTF metallic-roughness), single- or dual-UV record.
     *
     * @param dualUv True for the record carrying a second UV set and a colour (stride 60 / 76 / 80).
     * @param explicitLodAvailable True when the fragment dialect has textureLod (every profile but GLSL ES 1.00).
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource PbrSource(const bool dualUv, const bool explicitLodAvailable)
    {
        const std::string vsrc =
std::string("#version 300 es\n") +
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
+ (dualUv ? "layout(location=4) in vec2 aUV1;\n"
          // plans/plan_gltf.md GLTF-462: only the stride-60 program declares the colour, for the reason
          // in this function's own opening comment -- an unused varying added to the stride-48
          // program moved thousands of llvmpipe fragments by one RGB unit, and stride 60 is the only
          // rigid PBR record that HAS a colour slot.
            "layout(location=5) in vec4 aColor;\n" : "") +
CNA_GL_INSTANCE_TRANSFORM_DECL
CNA_GL_DIRECTION_HANDEDNESS_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec3 vTangent;\n"
"out float vBitangentSign;\n"
"out vec2 vUV;\n"
+ (dualUv ? "out vec2 vUV1;\nout vec4 vColor;\n" : "") +
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vNormal=uNormalMatrix*cnaInstanceDirection(aNormal);\n"
// Tangent transforms as a plain direction under mat3(uWorld) (not the inverse-transpose
// uNormalMatrix use for the normal) -- correct for uniform-scale World transforms, a documented
// simplification for non-uniform scale shared with most real-time engines lacking a full
// per-tangent inverse-transpose.
"    mat3 worldDirectionMat=mat3(uWorld);\n"
"    vTangent=worldDirectionMat*cnaInstanceDirection(aTangent.xyz);\n"
"    float instanceHandedness=(uCnaInstanced>0.5)?cnaDirectionHandedness(mat3(cnaInstanceMatrix())):1.0;\n"
"    vBitangentSign=aTangent.w*cnaDirectionHandedness(worldDirectionMat)*instanceHandedness;\n"
"    vUV=aUV;\n"
+ (dualUv ? "    vUV1=aUV1;\n    vColor=aColor;\n" : "") +
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";

        const char* const baseUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.x)" : "vUV";
        const char* const normalUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.y)" : "vUV";
        const char* const mrUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.z)" : "vUV";
        const char* const emissiveUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.w)" : "vUV";
        const char* const occlusionUv =
            dualUv ? "cnaPbrUV(uOcclusionTextureCoordinateSet)" : "vUV";
        const char* const specularUv =
            dualUv ? "cnaPbrUV(uSpecularTextureCoordinateSets.x)" : "vUV";
        const char* const specularColorUv =
            dualUv ? "cnaPbrUV(uSpecularTextureCoordinateSets.y)" : "vUV";
        const std::string fsrc =
std::string("#version 300 es\n") +
// plans/plan_fx.md FX-124: this fragment stage normalizes a WORLD-SPACE vector --
// normalize(uEyePosition - vWorldPos) -- and `mediump` guarantees only fp16 RANGE (~65504).
// dot(v, v) is computed first, so an eye a few thousand units from the geometry overflows,
// inversesqrt returns 0, the view direction collapses to the zero vector, and the specular term
// is wrong everywhere while the frame still reads as a plausible lit render. Measured, not
// assumed: SAMPLE-046 puts its camera 3500 units out, and with one directional light and
// PreferPerPixelLighting the frame agreed with real XNA on 90.62% of pixels within 8 levels at
// mediump and 99.99% at highp, the signed error over the model going from -20 levels to -0.01.
// Mesa does honour the qualifier here. GLSL ES 3.00 requires fragment highp, so it is asked for
// unconditionally; the GLSL ES 1.00 profiles get the GL_FRAGMENT_PRECISION_HIGH guard from
// TransformGlslEs300BodyToEs100, where 1.00 makes highp optional. FX-121 is the same defect in
// MojoShader's compiled-effect path; this is the built-in effect path.
"precision highp float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
+ (dualUv ? "in vec2 vUV1;\nin vec4 vColor;\nuniform float uVertexColorEnabled;\n" : "") +
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform sampler2D uSpecularMap;\n"
"uniform sampler2D uSpecularColorMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
// plans/plan_gltf.md GLTF-343/344: factor-only KHR_materials_ior/specular state, already reduced by
// FillGpuDrawParams to the exact shader-ready Fresnel endpoints. xyz is dielectric F0; w is F90.
"uniform vec4 uDielectricFresnel;\n"
// GLTF-344: the colour texture is multiplied before clamping, so this must retain the pre-clamp
// value instead of attempting to reconstruct it from uDielectricFresnel.
"uniform vec4 uSpecularFresnelInputs;\n"
// plans/plan_gltf.md GLTF-210/GLTF-212: x = decode the base-colour sample from sRGB, y = decode the
// emissive sample, z = encode the fragment's RGB back. Each is 0 or 1 and drives a mix() rather
// than a branch, so every fragment costs the same whichever way it is set.
"uniform vec4 uSrgb;\n"
// plans/plan_gltf.md GLTF-224/GLTF-225: normalTexture.scale and occlusionTexture.strength. Two scalar
// uniforms rather than one vec2, to stay on the single-float set_uniform overload this file
// already uses everywhere.
"uniform float uNormalScale;\n"
"uniform float uOcclusionStrength;\n"
+ (dualUv ? "uniform vec4 uTextureCoordinateSets;\n"
          "uniform float uOcclusionTextureCoordinateSet;\n"
          "uniform vec2 uSpecularTextureCoordinateSets;\n" : "") +
// GLTF-184: two precomputed affine rows per texture map. The selected vertex stream is transformed
// before cnaSampleUV applies the storage-origin adjustment for a render-target texture.
"uniform vec4 uTextureTransformRows[10];\n"
"uniform vec4 uSpecularTextureTransformRows[4];\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
// GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting k=(roughness+1)^2/8), and
// Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF (Appendix B.3.3/B.3.4/B.3.2).
CNA_GL_SRGB_TRANSFER_DECL
"vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, vec3 F90, float roughness, float metallic){\n"
"    vec3 H=normalize(V+L);\n"
"    float NdotL=max(dot(N,L),0.0);\n"
"    float NdotV=max(dot(N,V),1e-4);\n"
"    float NdotH=max(dot(N,H),0.0);\n"
"    float VdotH=max(dot(V,H),0.0);\n"
"    float a2=pow(roughness,4.0);\n"
"    float dTerm=(NdotH*NdotH*(a2-1.0)+1.0);\n"
"    float D=a2/(3.14159265*dTerm*dTerm+1e-7);\n"
"    float k=(roughness+1.0); k=k*k/8.0;\n"
"    float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));\n"
"    vec3 F=F0+(F90-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);\n"
"    vec3 specular=(D*G*F)/max(4.0*NdotV*NdotL,1e-4);\n"
"    vec3 diffuseColor=albedo*(1.0-metallic);\n"
"    vec3 kd=vec3(1.0)-F;\n"
"    return (kd*diffuseColor/3.14159265+specular)*lightColor*NdotL;\n"
"}\n"
CNA_GL_RT_SAMPLE_UV_HI_DECL
CNA_GL_RT_SAMPLE_UV_DECL
CNA_GL_SHADOW_DECL
CNA_GL_PUNCTUAL_DECL
+ CnaGlIblDecl(explicitLodAvailable) +
+ (dualUv ? "vec2 cnaPbrUV(float setIndex){return setIndex<0.5?vUV:vUV1;}\n" : "") +
"vec2 cnaPbrTransformUV(vec2 uv,int slot){\n"
"    vec3 value=vec3(uv,1.0);\n"
"    return vec2(dot(value,uTextureTransformRows[slot*2].xyz),dot(value,uTextureTransformRows[slot*2+1].xyz));\n"
"}\n"
"vec2 cnaPbrSpecularTransformUV(vec2 uv,int slot){\n"
"    vec3 value=vec3(uv,1.0);\n"
"    return vec2(dot(value,uSpecularTextureTransformRows[slot*2].xyz),dot(value,uSpecularTextureTransformRows[slot*2+1].xyz));\n"
"}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,cnaSampleUV(cnaPbrTransformUV(" + baseUv + ",0),uRtFlipV.x));\n"
// glTF §3.9.2: the base-colour TEXTURE is sRGB-encoded, the base-colour FACTOR is linear. Only
// the sample is decoded -- transferring both would apply it twice to one of them.
"    vec3 baseRGB=mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x);\n"
// plans/plan_gltf.md GLTF-462. §3.7.2.1: "if a primitive specifies a vertex color using the attribute
// semantic property COLOR_0, then this value acts as an additional linear multiplier to base
// color". LINEAR is the operative word and the reason there is no transfer function here: the
// attribute is a normalized integer already in linear space, unlike the base-colour TEXTURE. Both
// RGB and alpha are multiplied, because §3.9.2's base colour is an RGBA product.
+ (dualUv ? "    vec4 cnaVertexColor=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
          : "    vec4 cnaVertexColor=vec4(1.0,1.0,1.0,1.0);\n") +
"    vec3 albedo=baseRGB*uDiffuseColor.rgb*cnaVertexColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a*cnaVertexColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(" + normalUv + ",1),uRtFlipV.y)).rgb*2.0-1.0;\n"
// glTF §3.9.3: normalTexture.scale scales the tangent-space X and Y only. Scaling Z as well would
// merely rescale the whole vector, which normalization then undoes -- the perturbation would not
// change at all.
"    sampledNormal.xy*=uNormalScale;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrTransformUV(" + mrUv + ",2),uRtFlipV.z));\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    float specularWeight=uSpecularFresnelInputs.w*texture(uSpecularMap,cnaSampleUV(cnaPbrSpecularTransformUV(" + specularUv + ",0),uRtFlipVHi.y)).a;\n"
"    vec3 specularColorTex=texture(uSpecularColorMap,cnaSampleUV(cnaPbrSpecularTransformUV(" + specularColorUv + ",1),uRtFlipVHi.z)).rgb;\n"
"    specularColorTex=mix(specularColorTex,cnaSrgbToLinear(specularColorTex),uSrgb.w);\n"
"    vec3 dielectricF0=min(uSpecularFresnelInputs.xyz*specularColorTex,vec3(1.0))*specularWeight;\n"
"    vec3 F0=mix(dielectricF0,albedo,metallic);\n"
"    vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,F90,roughness,metallic);\n"
// plans/plan_modern.md MOD-838/MOD-839: Lo is the direct-lighting term and the only one a shadow may
// touch. The ambient/occlusion term below stands for light arriving from the rest of the
// environment, which an occluder between the surface and this one light does not block.
"    Lo*=cnaShadowFactor(vWorldPos);\n"
"    Lo+=cnaPunctualLight(vWorldPos,finalNormal)*albedo;\n"
"    float occlusion=texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(" + occlusionUv + ",4),uRtFlipVHi.x)).r;\n"
// §3.9.3's own formula: 1 + strength * (sampled - 1). At strength 0 this is 1 whatever the map
// holds, which is what "no occlusion" has to mean -- multiplying by the strength instead would
// darken everything to black.
"    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n"
// MOD-1226/MOD-1227: the two ambient terms are exclusive, and the map's occlusion multiplies
// whichever one is in force -- never the direct light, which is one light whose visibility the
// shadow map already answers. uAmbientColor arrives zeroed when an environment is bound (see
// PbrEffect::FillGpuDrawParams), so this is a sum of two terms only one of which is ever
// non-zero, rather than a branch that would cost every fragment.
"    vec3 ambient=uAmbientColor*albedo*occlusion\n"
"               +cnaIblAmbient(finalNormal,V,albedo,F0,roughness,metallic,occlusion);\n"
"    vec3 emissiveTex=texture(uEmissiveMap,cnaSampleUV(cnaPbrTransformUV(" + emissiveUv + ",3),uRtFlipV.w)).rgb;\n"
// Same split as the base colour. The factor is additionally allowed above 1 by
// KHR_materials_emissive_strength, which is a second reason never to transfer it.
"    vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y);\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
"    FragColor.rgb*=cnaCascadeDebugTint(vWorldPos);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
// Fog is mixed in LINEAR space, so uFogColor -- an ordinary application-supplied sRGB
// colour -- is decoded first. Mixing an encoded colour into a linear result would tint
// the fade toward the wrong shade as it thickens.
"    vec3 fogLinear=mix(uFogColor,cnaSrgbToLinear(uFogColor),uSrgb.z);\n"
"    FragColor.rgb=mix(fogLinear,FragColor.rgb,vFogFactor);\n"
// GLTF-212: encode last, and RGB only -- §3.9.4 makes alpha coverage, never colour.
"    FragColor.rgb=mix(FragColor.rgb,cnaLinearToSrgb(FragColor.rgb),uSrgb.z);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- pbr_skinned3d: SkinnedPbrEffect, single- or dual-UV record.
     *
     * @param dualUv True for the record carrying a second UV set and a colour (stride 60 / 76 / 80).
     * @param explicitLodAvailable True when the fragment dialect has textureLod (every profile but GLSL ES 1.00).
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource PbrSkinnedSource(const bool dualUv, const bool explicitLodAvailable)
    {
        const std::string vsrc =
std::string("#version 300 es\n") +
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
"layout(location=4) in vec4 aBoneWeights;\n"
"layout(location=5) in vec4 aBoneIndices;\n"
+ (dualUv ? "layout(location=6) in vec2 aUV1;\n"
          // plans/plan_gltf.md GLTF-463: only the stride-76/80 program declares the colour, for the same
          // reason the rigid pair splits -- an unused varying added to the stride-68 program moved
          // llvmpipe fragments by one RGB unit, and stride 80 is the only skinned PBR record that
          // HAS a colour slot.
            "layout(location=7) in vec4 aColor;\n" : "") +
CNA_GL_INSTANCE_TRANSFORM_DECL
CNA_GL_DIRECTION_HANDEDNESS_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uBones[72];\n"
"uniform int uWeightsPerVertex;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec3 vTangent;\n"
"out float vBitangentSign;\n"
"out vec2 vUV;\n"
+ (dualUv ? "out vec2 vUV1;\nout vec4 vColor;\n" : "") +
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
CNA_GL_SKIN_NORMAL_DECL
"void main(){\n"
"    mat4 skinMat=uBones[int(aBoneIndices.x)]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[int(aBoneIndices.y)]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[int(aBoneIndices.z)]*aBoneWeights.z+uBones[int(aBoneIndices.w)]*aBoneWeights.w;\n"
"    vec4 skinnedPos=skinMat*vec4(aPos,1.0);\n"
"    vec4 cnaPos=cnaInstancePosition(skinnedPos);\n"
"    gl_Position=uWVP*cnaPos;\n"
"    mat3 skinDirectionMat=mat3(skinMat);\n"
// REMED-GFX-006 (Variant B): the normal takes the inverse-transpose world matrix (uNormalMatrix),
// not raw mat3(uWorld). Raw World is only correct for rotation and uniform scale and diverges from
// FNA's mul(normal, WorldInverseTranspose) under non-uniform scale; it also contradicted this
// file's own unskinned EnsurePbrProgram, which already uses uNormalMatrix. The tangent stays on
// raw World: tangents transform as directions, not as normals (glTF convention, unchanged).
"    vec3 skinnedNormal=cnaSkinNormal(skinDirectionMat,aNormal);\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
"    vNormal=normalize(uNormalMatrix*cnaInstanceDirection(boneNormal));\n"
"    mat3 worldDirectionMat=mat3(uWorld);\n"
"    vTangent=worldDirectionMat*cnaInstanceDirection(skinDirectionMat*aTangent.xyz);\n"
"    float instanceHandedness=(uCnaInstanced>0.5)?cnaDirectionHandedness(mat3(cnaInstanceMatrix())):1.0;\n"
"    vBitangentSign=aTangent.w*cnaDirectionHandedness(worldDirectionMat)*instanceHandedness*cnaDirectionHandedness(skinDirectionMat);\n"
"    vUV=aUV;\n"
+ (dualUv ? "    vUV1=aUV1;\n    vColor=aColor;\n" : "") +
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";

        const char* const baseUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.x)" : "vUV";
        const char* const normalUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.y)" : "vUV";
        const char* const mrUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.z)" : "vUV";
        const char* const emissiveUv = dualUv ? "cnaPbrUV(uTextureCoordinateSets.w)" : "vUV";
        const char* const occlusionUv =
            dualUv ? "cnaPbrUV(uOcclusionTextureCoordinateSet)" : "vUV";
        const char* const specularUv =
            dualUv ? "cnaPbrUV(uSpecularTextureCoordinateSets.x)" : "vUV";
        const char* const specularColorUv =
            dualUv ? "cnaPbrUV(uSpecularTextureCoordinateSets.y)" : "vUV";
        const std::string fsrc =
std::string("#version 300 es\n") +
// plans/plan_fx.md FX-124: this fragment stage normalizes a WORLD-SPACE vector --
// normalize(uEyePosition - vWorldPos) -- and `mediump` guarantees only fp16 RANGE (~65504).
// dot(v, v) is computed first, so an eye a few thousand units from the geometry overflows,
// inversesqrt returns 0, the view direction collapses to the zero vector, and the specular term
// is wrong everywhere while the frame still reads as a plausible lit render. Measured, not
// assumed: SAMPLE-046 puts its camera 3500 units out, and with one directional light and
// PreferPerPixelLighting the frame agreed with real XNA on 90.62% of pixels within 8 levels at
// mediump and 99.99% at highp, the signed error over the model going from -20 levels to -0.01.
// Mesa does honour the qualifier here. GLSL ES 3.00 requires fragment highp, so it is asked for
// unconditionally; the GLSL ES 1.00 profiles get the GL_FRAGMENT_PRECISION_HIGH guard from
// TransformGlslEs300BodyToEs100, where 1.00 makes highp optional. FX-121 is the same defect in
// MojoShader's compiled-effect path; this is the built-in effect path.
"precision highp float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
+ (dualUv ? "in vec2 vUV1;\nin vec4 vColor;\nuniform float uVertexColorEnabled;\n" : "") +
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform sampler2D uSpecularMap;\n"
"uniform sampler2D uSpecularColorMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
// plans/plan_gltf.md GLTF-343/344: same shader-ready dielectric Fresnel endpoints as unskinned PBR.
"uniform vec4 uDielectricFresnel;\n"
"uniform vec4 uSpecularFresnelInputs;\n"
// plans/plan_gltf.md GLTF-210/GLTF-212: x = decode the base-colour sample from sRGB, y = decode the
// emissive sample, z = encode the fragment's RGB back. Each is 0 or 1 and drives a mix() rather
// than a branch, so every fragment costs the same whichever way it is set.
"uniform vec4 uSrgb;\n"
// plans/plan_gltf.md GLTF-224/GLTF-225: normalTexture.scale and occlusionTexture.strength. Two scalar
// uniforms rather than one vec2, to stay on the single-float set_uniform overload this file
// already uses everywhere.
"uniform float uNormalScale;\n"
"uniform float uOcclusionStrength;\n"
+ (dualUv ? "uniform vec4 uTextureCoordinateSets;\n"
          "uniform float uOcclusionTextureCoordinateSet;\n"
          "uniform vec2 uSpecularTextureCoordinateSets;\n" : "") +
"uniform vec4 uTextureTransformRows[10];\n"
"uniform vec4 uSpecularTextureTransformRows[4];\n"
"uniform vec3 uLight0Dir;\n"
"uniform vec3 uLight0Diffuse;\n"
"uniform vec3 uLight1Dir;\n"
"uniform vec3 uLight1Diffuse;\n"
"uniform vec3 uLight2Dir;\n"
"uniform vec3 uLight2Diffuse;\n"
"uniform vec3 uEyePosition;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_SRGB_TRANSFER_DECL
"vec3 PbrLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 albedo, vec3 F0, vec3 F90, float roughness, float metallic){\n"
"    vec3 H=normalize(V+L);\n"
"    float NdotL=max(dot(N,L),0.0);\n"
"    float NdotV=max(dot(N,V),1e-4);\n"
"    float NdotH=max(dot(N,H),0.0);\n"
"    float VdotH=max(dot(V,H),0.0);\n"
"    float a2=pow(roughness,4.0);\n"
"    float dTerm=(NdotH*NdotH*(a2-1.0)+1.0);\n"
"    float D=a2/(3.14159265*dTerm*dTerm+1e-7);\n"
"    float k=(roughness+1.0); k=k*k/8.0;\n"
"    float G=(NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k));\n"
"    vec3 F=F0+(F90-F0)*pow(clamp(1.0-VdotH,0.0,1.0),5.0);\n"
"    vec3 specular=(D*G*F)/max(4.0*NdotV*NdotL,1e-4);\n"
"    vec3 diffuseColor=albedo*(1.0-metallic);\n"
"    vec3 kd=vec3(1.0)-F;\n"
"    return (kd*diffuseColor/3.14159265+specular)*lightColor*NdotL;\n"
"}\n"
CNA_GL_RT_SAMPLE_UV_HI_DECL
CNA_GL_RT_SAMPLE_UV_DECL
CNA_GL_SHADOW_DECL
CNA_GL_PUNCTUAL_DECL
+ CnaGlIblDecl(explicitLodAvailable) +
+ (dualUv ? "vec2 cnaPbrUV(float setIndex){return setIndex<0.5?vUV:vUV1;}\n" : "") +
"vec2 cnaPbrTransformUV(vec2 uv,int slot){\n"
"    vec3 value=vec3(uv,1.0);\n"
"    return vec2(dot(value,uTextureTransformRows[slot*2].xyz),dot(value,uTextureTransformRows[slot*2+1].xyz));\n"
"}\n"
"vec2 cnaPbrSpecularTransformUV(vec2 uv,int slot){\n"
"    vec3 value=vec3(uv,1.0);\n"
"    return vec2(dot(value,uSpecularTextureTransformRows[slot*2].xyz),dot(value,uSpecularTextureTransformRows[slot*2+1].xyz));\n"
"}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,cnaSampleUV(cnaPbrTransformUV(" + baseUv + ",0),uRtFlipV.x));\n"
// glTF §3.9.2: the base-colour TEXTURE is sRGB-encoded, the base-colour FACTOR is linear. Only
// the sample is decoded -- transferring both would apply it twice to one of them.
"    vec3 baseRGB=mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x);\n"
// plans/plan_gltf.md GLTF-463. §3.7.2.1: COLOR_0 "acts as an additional linear multiplier to base color".
// LINEAR is why there is no transfer function here -- the attribute is a normalized integer already
// in linear space, unlike the base-colour TEXTURE -- and both RGB and alpha are multiplied because
// §3.9.2's base colour is an RGBA product. Identical to the rigid program's own term.
+ (dualUv ? "    vec4 cnaVertexColor=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
          : "    vec4 cnaVertexColor=vec4(1.0,1.0,1.0,1.0);\n") +
"    vec3 albedo=baseRGB*uDiffuseColor.rgb*cnaVertexColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a*cnaVertexColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,cnaSampleUV(cnaPbrTransformUV(" + normalUv + ",1),uRtFlipV.y)).rgb*2.0-1.0;\n"
// glTF §3.9.3: normalTexture.scale scales the tangent-space X and Y only. Scaling Z as well would
// merely rescale the whole vector, which normalization then undoes -- the perturbation would not
// change at all.
"    sampledNormal.xy*=uNormalScale;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrTransformUV(" + mrUv + ",2),uRtFlipV.z));\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    float specularWeight=uSpecularFresnelInputs.w*texture(uSpecularMap,cnaSampleUV(cnaPbrSpecularTransformUV(" + specularUv + ",0),uRtFlipVHi.y)).a;\n"
"    vec3 specularColorTex=texture(uSpecularColorMap,cnaSampleUV(cnaPbrSpecularTransformUV(" + specularColorUv + ",1),uRtFlipVHi.z)).rgb;\n"
"    specularColorTex=mix(specularColorTex,cnaSrgbToLinear(specularColorTex),uSrgb.w);\n"
"    vec3 dielectricF0=min(uSpecularFresnelInputs.xyz*specularColorTex,vec3(1.0))*specularWeight;\n"
"    vec3 F0=mix(dielectricF0,albedo,metallic);\n"
"    vec3 F90=mix(vec3(specularWeight),vec3(1.0),metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,F90,roughness,metallic);\n"
// plans/plan_modern.md MOD-838/MOD-839: Lo is the direct-lighting term and the only one a shadow may
// touch. The ambient/occlusion term below stands for light arriving from the rest of the
// environment, which an occluder between the surface and this one light does not block.
"    Lo*=cnaShadowFactor(vWorldPos);\n"
"    Lo+=cnaPunctualLight(vWorldPos,finalNormal)*albedo;\n"
"    float occlusion=texture(uOcclusionMap,cnaSampleUV(cnaPbrTransformUV(" + occlusionUv + ",4),uRtFlipVHi.x)).r;\n"
// §3.9.3's own formula: 1 + strength * (sampled - 1). At strength 0 this is 1 whatever the map
// holds, which is what "no occlusion" has to mean -- multiplying by the strength instead would
// darken everything to black.
"    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n"
// MOD-1226/MOD-1227: the two ambient terms are exclusive, and the map's occlusion multiplies
// whichever one is in force -- never the direct light, which is one light whose visibility the
// shadow map already answers. uAmbientColor arrives zeroed when an environment is bound (see
// PbrEffect::FillGpuDrawParams), so this is a sum of two terms only one of which is ever
// non-zero, rather than a branch that would cost every fragment.
"    vec3 ambient=uAmbientColor*albedo*occlusion\n"
"               +cnaIblAmbient(finalNormal,V,albedo,F0,roughness,metallic,occlusion);\n"
"    vec3 emissiveTex=texture(uEmissiveMap,cnaSampleUV(cnaPbrTransformUV(" + emissiveUv + ",3),uRtFlipV.w)).rgb;\n"
// Same split as the base colour. The factor is additionally allowed above 1 by
// KHR_materials_emissive_strength, which is a second reason never to transfer it.
"    vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y);\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
"    FragColor.rgb*=cnaCascadeDebugTint(vWorldPos);\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
// Fog is mixed in LINEAR space, so uFogColor -- an ordinary application-supplied sRGB
// colour -- is decoded first. Mixing an encoded colour into a linear result would tint
// the fade toward the wrong shade as it thickens.
"    vec3 fogLinear=mix(uFogColor,cnaSrgbToLinear(uFogColor),uSrgb.z);\n"
"    FragColor.rgb=mix(fogLinear,FragColor.rgb,vFogFactor);\n"
// GLTF-212: encode last, and RGB only -- §3.9.4 makes alpha coverage, never colour.
"    FragColor.rgb=mix(FragColor.rgb,cnaLinearToSrgb(FragColor.rgb),uSrgb.z);\n"
"}\n";
        return {vsrc, fsrc};
    }

    /**
     * @brief Returns the GLSL ES 3.00 sources of one stock program -- SpriteBatch.
     *
     * @return The vertex and fragment sources, before any profile adaptation.
     */
    [[nodiscard]] inline GlStockProgramSource SpriteSource()
    {
        const char* vertexShaderSource = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 1.0);
    gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;
    TexCoord = aTexCoord;
    Color = aColor;
}
)";

        const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D texture1;

// Direct3D 9 expands a texture's missing channels when a shader samples it: a one-channel
// format arrives as (R, 1, 1, 1) and a two-channel one as (R, G, 1, 1). OpenGL expands the
// same storage to (R, 0, 0, 1) and (R, G, 0, 1), so an XNA game that draws a
// SurfaceFormat.Single texture -- a shadow map, a depth visualisation -- gets a red image
// here where it got a white one there. GL_TEXTURE_SWIZZLE_G/B/A = GL_ONE is exactly D3D9's
// rule and is core in ES 3.0 and desktop GL 3.3, but WebGL 2 exposes neither the constants
// nor the parameter (measured: texParameteri raises INVALID_ENUM), so the expansion is done
// here instead, where every profile this renderer targets can do it identically.
uniform vec4 uChannelMask;
uniform vec4 uChannelFill;

void main()
{
    FragColor = (texture(texture1, TexCoord) * uChannelMask + uChannelFill) * Color;
}
)";
        return {vertexShaderSource, fragmentShaderSource};
    }

}
