#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/Internal/Graphics/SrgbTransfer.hpp"
#include "CNA/Internal/Renderers/EasyGL/GlProfile.hpp"
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/EasyGL/EasyGLCompiledEffect.hpp"
#include "Fna3dStockEffectBlobs.hpp"
#endif

namespace CNA::Internal::Renderers::EasyGL
{
    namespace
    {
        // plans/plan_runtimerenderer.md phase P11: the runtime replacements for this file's former
        // CNA_GL_PROFILE_* preprocessor guards. Argument-less on purpose -- they read the active
        // profile themselves, so a guard converts to a one-line condition with no plumbing through
        // the many free helpers this file is built from.
        //
        // The runtime-renderer audit found that ProfileIsEs2ApiGeneration() must cover OPENGLES2
        // AND WEBGL1.
        //
        // It used to reproduce `#if defined(CNA_GL_PROFILE_OPENGLES2)` exactly, excluding WEBGL1 --
        // faithful to the compile-time guards, and wrong. Every remaining use guards an ES 2.0
        // API-GENERATION limitation, not a shading-language difference where the two profiles could
        // legitimately diverge, and WebGL 1 is an ES 2.0-class API with the same limitation in each
        // case. Excluding it meant a WEBGL1 build took the ES 3.0 path and called entry points its
        // context does not have.
        [[nodiscard]] inline bool ProfileIsDesktopCore()  { return IsDesktopCoreProfile(ActiveGlProfile()); }
        [[nodiscard]] inline bool ProfileUsesGlslEs100()  { return UsesGlslEs100(ActiveGlProfile()); }
        [[nodiscard]] inline bool ProfileIsEs2ApiGeneration() { return UsesEs2ApiGeneration(ActiveGlProfile()); }
        [[nodiscard]] inline bool ProfileRequiresBaseVertexPointerRebase()
        {
            return RequiresBaseVertexPointerRebase(ActiveGlProfile());
        }
        [[nodiscard]] inline bool ProfileIs(GlProfile expected) { return ActiveGlProfile() == expected; }
    }
}

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/NotSupportedException.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <span>

#include "CNA/TargetPlatform.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <metagl/Emscripten.hpp>
#endif
#include <metagl/Capabilities.hpp>
#include <metagl/Context.hpp>
#include <metagl/ContextEvents.hpp>
#include <metagl/EnumNames.hpp>
#include <metagl/Functions.hpp>

// Verbose 3D rendering trace. Define `CNA_DEBUG_RENDERING` (e.g. via
// -DCNA_DEBUG_RENDERING) to enable. By default these logs are silent so the
// 3D pipeline does not spam the console every frame.
#if defined(CNA_DEBUG_RENDERING)
#define CNA_RENDER_LOG(msg) do { std::cerr << "[CNA EasyGL 3D] " << msg << std::endl; } while (0)
#else
#define CNA_RENDER_LOG(msg) do { } while (0)
#endif
#include <stdexcept>
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <string>
#include <sstream>
#include <set>
#include <unordered_map>
#include <utility>
#include <cctype>
#include "Microsoft/Xna/Framework/Color.hpp"

#if defined(__EMSCRIPTEN__)
EM_JS(void, CNA_DebugLoseWebGLContext, (), {
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] loseContext: canvas not found'); return; }
    const gl = Module['ctx'] || canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) { console.error('[CNA] loseContext: WebGL context not found'); return; }
    const ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) { console.error('[CNA] WEBGL_lose_context extension not available'); return; }
    console.warn('[CNA] Simulating WebGL context loss');
    ext.loseContext();
});

EM_JS(void, CNA_DebugRestoreWebGLContext, (), {
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] restoreContext: canvas not found'); return; }
    const gl = Module['ctx'] || canvas.getContext('webgl2') || canvas.getContext('webgl');
    if (!gl) { console.error('[CNA] restoreContext: WebGL context not found'); return; }
    const ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) { console.error('[CNA] WEBGL_lose_context extension not available'); return; }
    console.warn('[CNA] Simulating WebGL context restore');
    ext.restoreContext();
});
#endif

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
inline std::string CnaGlIblDecl(const bool explicitLodAvailable)
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

namespace CNA::Internal::Renderers::EasyGL
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;

    namespace
    {
        // MERGE (plans/plan_platform.md PLAT-* x plans/plan_runtimerenderer.md P11): the version/profile the
        // platform is asked for is DATA, so it is computed from the runtime profile rather than
        // from `#if defined(CNA_GL_PROFILE_*)`. The compile-time form could only ever describe one
        // profile, which is exactly what P11 removed: a single binary may hold up to five, and the
        // one in force is known only when the renderer is constructed.
        //
        // The profile is a parameter rather than a read of ActiveGlProfile() on purpose: this is
        // called from the constructor's member-initializer list, which runs before the constructor
        // body could publish the active profile.
        CNA::Platform::GlContextDescription RequestedGlContext(GlProfile profile)
        {
            CNA::Platform::GlContextDescription description;
            if (IsDesktopCoreProfile(profile))
            {
                description.majorVersion = 3;
                description.minorVersion = 3;
                description.profile = CNA::Platform::GlProfile::Core;
            }
            else if (UsesGlslEs100(profile))
            {
                description.majorVersion = 2;
                description.minorVersion = 0;
                description.profile = CNA::Platform::GlProfile::Es;
            }
            else
            {
                description.majorVersion = 3;
                description.minorVersion = 0;
                description.profile = CNA::Platform::GlProfile::Es;
            }
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            return description;
        }

        CNA::Platform::WindowId RequireEasyGlWindowId(const RendererSurfaceInfo& surface)
        {
            if (surface.windowId == 0)
            {
                throw CNA::Platform::PlatformException(
                    "EasyGLRenderer::CreateContext", "surface has no platform window id");
            }
            return surface.windowId;
        }

        CNA::Platform::GlProcAddressLoader glProcAddressLoader = nullptr;

        void* LoadEasyGlProcAddress(const char* name)
        {
            return glProcAddressLoader != nullptr ? glProcAddressLoader(name) : nullptr;
        }
    }

    class EasyGLPlatformContext
    {
    public:
        EasyGLPlatformContext(CNA::Platform::IPlatformGlContext& service,
                              const CNA::Platform::WindowId window,
                              CNA::Platform::GlContextDescription description)
            : service_(service), window_(window), description_(std::move(description))
        {
            context_ = service_.CreateContext(window_, description_);
            try
            {
                service_.MakeCurrent(window_, context_);
            }
            catch (...)
            {
                service_.DestroyContext(context_);
                context_ = nullptr;
                throw;
            }
        }

        ~EasyGLPlatformContext()
        {
            service_.DestroyContext(context_);
        }

        EasyGLPlatformContext(const EasyGLPlatformContext&) = delete;
        EasyGLPlatformContext& operator=(const EasyGLPlatformContext&) = delete;

        void Recreate()
        {
            if (context_ != nullptr)
            {
                service_.MakeCurrent(window_, nullptr);
                service_.DestroyContext(context_);
                context_ = nullptr;
            }

            context_ = service_.CreateContext(window_, description_);
            try
            {
                service_.MakeCurrent(window_, context_);
            }
            catch (...)
            {
                service_.DestroyContext(context_);
                context_ = nullptr;
                throw;
            }
        }

        void SwapBuffers() { service_.SwapBuffers(window_); }
        bool SetSwapInterval(const int interval) { return service_.SetSwapInterval(interval); }
        [[nodiscard]] CNA::Platform::GlProcAddressLoader GetLoader() const
        {
            return service_.GetProcAddressLoader();
        }

    private:
        CNA::Platform::IPlatformGlContext& service_;
        CNA::Platform::WindowId window_ = 0;
        CNA::Platform::GlContextDescription description_;
        CNA::Platform::GlContextHandle context_ = nullptr;
    };

    EasyGLSurfaceState::EasyGLSurfaceState(
        const RendererSurfaceInfo& surface, const int virtualWidth, const int virtualHeight,
        const CnaPresentationMode presentationMode)
        : surface_(surface), virtualWidth_(virtualWidth), virtualHeight_(virtualHeight),
          presentationMode_(presentationMode)
    {
        Update(surface);
    }

    void EasyGLSurfaceState::Update(const RendererSurfaceInfo& surface)
    {
        surface_ = surface;
        if (!(surface_.displayScale > 0.0f))
        {
            surface_.displayScale = 1.0f;
        }
    }

    void EasyGLSurfaceState::SetVirtualResolution(const int width, const int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void EasyGLSurfaceState::SetPresentationMode(const CnaPresentationMode mode)
    {
        presentationMode_ = mode;
    }

    void EasyGLSurfaceState::GetDrawableSize(int& width, int& height) const
    {
        width = surface_.drawableSize.width;
        height = surface_.drawableSize.height;
    }

    void EasyGLSurfaceState::GetClientSize(int& width, int& height) const
    {
        width = static_cast<int>(std::lround(
            static_cast<double>(surface_.drawableSize.width) / surface_.displayScale));
        height = static_cast<int>(std::lround(
            static_cast<double>(surface_.drawableSize.height) / surface_.displayScale));
    }

    void EasyGLSurfaceState::GetLogicalSize(int& width, int& height) const
    {
        int clientWidth = 0;
        int clientHeight = 0;
        GetClientSize(clientWidth, clientHeight);
        if (virtualHeight_ <= 0)
        {
            width = clientWidth;
            height = clientHeight;
            return;
        }

        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && clientHeight > 0)
        {
            width = static_cast<int>(
                static_cast<double>(clientWidth) * virtualHeight_ / clientHeight + 0.5);
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : clientWidth;
        }
    }

    // The physical counterpart of GetLogicalSize() above, and the reason this renderer needs an
    // IGraphicsRenderer::GetDefaultViewportRect() override at all.
    //
    // GraphicsDevice::UpdateViewportFromWindow() pushes THIS rectangle to glViewport, while
    // GetLogicalSize() only feeds GraphicsDevice.Viewport.Width/Height. The base-class default
    // returns (0, 0, GetViewportSize()) -- the LOGICAL size used as if it were physical pixels.
    // That is correct only for a renderer with no virtual resolution, and EasyGL always has one:
    // GraphicsDevice::Reset() sets it to the backbuffer size on every device creation, and the
    // default presentation mode is FixedHeightDynamicWidth.
    //
    // So the default was actively wrong here. Resize an 800x480 window to 1200x800 and the logical
    // size became 720x480 (height pinned, width following the aspect) -- which then got applied as
    // a 720x480 PHYSICAL viewport inside a 1200x800 drawable. The game rendered into a corner and
    // the rest of the window kept the clear colour, while glClear (viewport-independent) covered
    // all of it. Reported against galaxy-eggbert 2026-08-21: resizing the window or going
    // fullscreen with F11 did not enlarge the game.
    //
    // Mirrors OpenGL2Renderer::ComputeLogicalViewport()/SdlGpuRenderer's algorithm, the
    // established reference for real Letterbox/Overscan/Stretch semantics in this codebase.
    void EasyGLSurfaceState::GetDefaultViewportRect(int& x, int& y, int& width, int& height) const
    {
        int physWidth = 0;
        int physHeight = 0;
        GetDrawableSize(physWidth, physHeight);

        x = 0;
        y = 0;
        width = std::max(0, physWidth);
        height = std::max(0, physHeight);

        if (physWidth <= 0 || physHeight <= 0)
        {
            return;
        }

        // Full drawable, no scaling: nothing to centre, and a degenerate virtual resolution has
        // no aspect to preserve.
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth ||
            presentationMode_ == CnaPresentationMode::Stretch ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
        {
            return;
        }

        // Letterbox/Overscan: scale the virtual resolution uniformly -- min to fit inside the
        // drawable (bars), max to cover it (cropping) -- then centre the result.
        const double logicalWidth = static_cast<double>(virtualWidth_);
        const double logicalHeight = static_cast<double>(virtualHeight_);
        const double scaleX = static_cast<double>(physWidth) / logicalWidth;
        const double scaleY = static_cast<double>(physHeight) / logicalHeight;
        const double scale = (presentationMode_ == CnaPresentationMode::Overscan)
                                 ? std::max(scaleX, scaleY)
                                 : std::min(scaleX, scaleY);

        width = static_cast<int>(std::lround(logicalWidth * scale));
        height = static_cast<int>(std::lround(logicalHeight * scale));
        x = static_cast<int>(std::lround((static_cast<double>(physWidth) - logicalWidth * scale) * 0.5));
        y = static_cast<int>(std::lround((static_cast<double>(physHeight) - logicalHeight * scale) * 0.5));
    }

    bool EasyGLSurfaceState::WindowToLogical(const float windowX, const float windowY,
                                             float& logicalX, float& logicalY) const
    {
        if (virtualHeight_ <= 0) return false;
        int clientWidth = 0;
        int clientHeight = 0;
        GetClientSize(clientWidth, clientHeight);
        if (clientHeight <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) /
                            static_cast<float>(clientHeight);
        logicalX = windowX * scale;
        logicalY = windowY * scale;
        return true;
    }

    bool EasyGLSurfaceState::LogicalToWindow(const float logicalX, const float logicalY,
                                             float& windowX, float& windowY) const
    {
        if (virtualHeight_ <= 0) return false;
        int clientWidth = 0;
        int clientHeight = 0;
        GetClientSize(clientWidth, clientHeight);
        if (clientHeight <= 0) return false;
        const float scale = static_cast<float>(clientHeight) /
                            static_cast<float>(virtualHeight_);
        windowX = logicalX * scale;
        windowY = logicalY * scale;
        return true;
    }

    // REMED-GFX-147. See the declaration in EasyGLRenderer.hpp.
    bool SampledRowOrderIsBottomUp(const ITextureRenderer* texture)
    {
        return dynamic_cast<const IRenderTargetRenderer*>(texture) != nullptr;
    }

    // --- REMED-GFX-168: render-target ownership trace -------------------------------------------
    //
    // Set CNA_EASYGL_TARGET_TRACE=1 to have every render-target ownership transition print one line
    // to stderr. This exists because the question REMED-GFX-168 had to answer -- is the object gone,
    // are the GL names gone, and which of the two does the next transition touch -- cannot be
    // answered from a crash address alone. Off by default and costs one already-cached bool read
    // per event.
    //
    // Deliberately prints POINTER VALUES for anything that may already be destroyed and reads
    // fields only from the object whose own method is running, so enabling the trace can never turn
    // a survivable state into a dereference.
    namespace
    {
        bool TargetTraceEnabled()
        {
            static const bool enabled = [] {
                const char* v = std::getenv("CNA_EASYGL_TARGET_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
            return enabled;
        }

        /// Monotonic event counter, so the ORDER of two events is readable without timestamps.
        unsigned long NextTraceSeq()
        {
            static unsigned long seq = 0;
            return ++seq;
        }

        /**
         * @brief Every GL error pending at this point, drained; "none" when there are none.
         *
         * Attached to each trace line so the GL-validation question -- does a transition past a
         * destroyed target produce an invalid-framebuffer-operation, use a deleted name, or leave an
         * incomplete framebuffer -- is answered at the transition that would raise it, not by a
         * summary at the end of a run. Only reached when the trace is enabled, so a normal run does
         * not pay for a glGetError round trip.
         */
        std::string TraceGlErrors()
        {
            std::ostringstream os;
            for (int i = 0; i < 16; ++i)
            {
                const auto error = ::metagl::glGetError();
                if (error == ::metagl::ErrorCode::NoError) break;
                if (os.tellp() > 0) os << '+';
                os << ::metagl::to_string(error);
            }
            return os.tellp() > 0 ? os.str() : std::string("none");
        }

        /// One trace line. @p detail carries whatever the call site can safely read.
        void TargetTrace(const char* event, const void* object, const std::string& detail)
        {
            if (!TargetTraceEnabled()) return;
            std::cerr << "[GFX168] seq=" << NextTraceSeq() << " ev=" << event
                      << " obj=" << object << ' ' << detail
                      << " glerr=" << TraceGlErrors() << std::endl;
        }

        // --- REMED-GFX-174: sampler / texture-completeness trace --------------------------------
        //
        // Set CNA_EASYGL_SAMPLER_TRACE=1 to print one line per public sampler application and one
        // per stock/custom 3D texture bind. The question REMED-GFX-174 has to answer -- is a wrong
        // pixel true bilinear interpolation, an incomplete-texture fetch, a stale sampler object or
        // the wrong texture unit -- cannot be answered from pixels alone, because all four can
        // produce a colour that is in no texel of the source. Every field below is READ BACK FROM
        // GL rather than echoed from the value CNA just wrote, so a parameter that never reached
        // the driver is visible as a disagreement. Off by default; costs one cached bool read.
        bool SamplerTraceEnabled()
        {
            static const bool enabled = [] {
                const char* v = std::getenv("CNA_EASYGL_SAMPLER_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
            return enabled;
        }

        /// The public TextureFilter ordinal's XNA name, so a trace line names what the game asked
        /// for rather than only what GL was told.
        const char* FilterOrdinalName(int filter)
        {
            switch (filter)
            {
            case 0:  return "Linear";
            case 1:  return "Point";
            case 2:  return "Anisotropic";
            case 3:  return "LinearMipPoint";
            case 4:  return "PointMipLinear";
            case 5:  return "MinLinearMagPointMipLinear";
            case 6:  return "MinLinearMagPointMipPoint";
            case 7:  return "MinPointMagLinearMipLinear";
            case 8:  return "MinPointMagLinearMipPoint";
            default: return "<out-of-range>";
            }
        }

        /// True when @p minFilter is one of the four GL minification filters that SAMPLE A MIP
        /// CHAIN. Those are exactly the filters for which mipmap completeness is evaluated.
        bool GlMinFilterUsesMipChain(int minFilter)
        {
            return minFilter == static_cast<int>(::easygl::TextureMinFilter::NearestMipmapNearest)
                || minFilter == static_cast<int>(::easygl::TextureMinFilter::LinearMipmapNearest)
                || minFilter == static_cast<int>(::easygl::TextureMinFilter::NearestMipmapLinear)
                || minFilter == static_cast<int>(::easygl::TextureMinFilter::LinearMipmapLinear);
        }

        /// One sampler/texture trace line.
        void SamplerTrace(const char* event, const std::string& detail)
        {
            if (!SamplerTraceEnabled()) return;
            std::cerr << "[GFX174] seq=" << NextTraceSeq() << " ev=" << event << ' ' << detail
                      << " glerr=" << TraceGlErrors() << std::endl;
        }

        /**
         * @brief Traces the COMPLETE effective sampling state of the currently active texture unit.
         *
         * Must be called while the unit of interest is the active one and the texture is bound.
         * Reports the texture name, its base/max level, the sampler object bound to the same unit,
         * and -- the point of the whole exercise -- WHICH of the two supplies the effective min/mag
         * filter. A bound sampler object overrides the texture's own parameters; a unit with sampler
         * 0 falls back to them, which is exactly how a correctly-configured sampler can still fail
         * to reach a draw. `complete` applies the GL mipmap-completeness rule to the effective min
         * filter and the real level range, so an incomplete-texture fetch is distinguishable from
         * genuine interpolation.
         */
        void TraceBoundTextureUnit(const char* event, int unit)
        {
            if (!SamplerTraceEnabled()) return;

            int activeUnit = 0, texName = 0, samplerName = 0;
            ::metagl::glGetIntegerv(::metagl::GetParameter::ActiveTexture, &activeUnit);
            ::metagl::glGetIntegerv(::metagl::GetParameter::TextureBinding2D, &texName);
            ::metagl::glGetIntegerv(::metagl::GetParameter::SamplerBinding, &samplerName);

            int texMin = 0, texMag = 0, baseLevel = 0, maxLevel = 0;
            ::metagl::glGetTexParameteriv(::metagl::TextureTarget::Texture2D,
                                          ::metagl::TextureParameterQuery::MinFilter, &texMin);
            ::metagl::glGetTexParameteriv(::metagl::TextureTarget::Texture2D,
                                          ::metagl::TextureParameterQuery::MagFilter, &texMag);
            ::metagl::glGetTexParameteriv(::metagl::TextureTarget::Texture2D,
                                          ::metagl::TextureParameterQuery::BaseLevel, &baseLevel);
            ::metagl::glGetTexParameteriv(::metagl::TextureTarget::Texture2D,
                                          ::metagl::TextureParameterQuery::MaxLevel, &maxLevel);

            int effMin = texMin, effMag = texMag;
            if (samplerName != 0)
            {
                ::metagl::glGetSamplerParameteriv(::metagl::SamplerId{static_cast<unsigned int>(samplerName)},
                                                  ::metagl::SamplerParameter::MinFilter, &effMin);
                ::metagl::glGetSamplerParameteriv(::metagl::SamplerId{static_cast<unsigned int>(samplerName)},
                                                  ::metagl::SamplerParameter::MagFilter, &effMag);
            }

            // GL evaluates mipmap completeness over levels [baseLevel, maxLevel]; a chain of one
            // level is complete, so MAX_LEVEL clamping alone already satisfies the rule.
            const bool needsChain = GlMinFilterUsesMipChain(effMin);
            const bool complete   = !needsChain || maxLevel >= baseLevel;

            std::ostringstream os;
            os << "unit=" << unit
               << " activeUnit=0x" << std::hex << activeUnit
               << " tex=" << std::dec << texName
               << " sampler=" << samplerName
               << " src=" << (samplerName != 0 ? "sampler-object" : "texture-object")
               << " texMin=0x" << std::hex << texMin << " texMag=0x" << texMag
               << " effMin=0x" << effMin << " effMag=0x" << effMag << std::dec
               << " base=" << baseLevel << " max=" << maxLevel
               << " needsMipChain=" << (needsChain ? 1 : 0)
               << " complete=" << (complete ? 1 : 0);
            SamplerTrace(event, os.str());
        }

        /// The native identities of one render target, read from the object that owns them.
        std::string NativeDetail(unsigned int fbo, unsigned int color, unsigned int depth,
                                 unsigned int msaaRbo, unsigned int resolveFbo,
                                 int w, int h, int samples, int levels)
        {
            std::ostringstream os;
            os << "fbo=" << fbo << " color=" << color << " depth=" << depth
               << " msaaRbo=" << msaaRbo << " resolveFbo=" << resolveFbo
               << " dim=" << w << 'x' << h << " msaa=" << samples << " levels=" << levels;
            return os.str();
        }
    }

    // plans/plan_glbackends.md Phase B (GLB-10/11): every embedded shader in this file is authored
    // once, against GLSL ES 3.00 (the OPENGLES3/WEBGL2 profiles' native syntax). Body syntax
    // (in/out, texture(), no varying/attribute) is shared with desktop GLSL 3.30 core -- only
    // the "#version ...\nprecision ... float;\n" header two lines differ, so OPENGL33 does not
    // need a second copy of every shader, just a header rewrite performed here at first-use time.
    enum class GlShaderStageKind { Vertex, Fragment };

    namespace
    {
        // plans/plan_glbackends.md GLB-36 helper: true whole-word replace (identifiers only), used for
        // rewriting FragColor -> gl_FragColor in fragment shader bodies without touching
        // substrings inside longer identifiers.
        std::string ReplaceWholeWord(std::string text, const std::string& word, const std::string& replacement)
        {
            size_t pos = 0;
            while ((pos = text.find(word, pos)) != std::string::npos)
            {
                const bool leftOk = (pos == 0) ||
                    !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
                const size_t after = pos + word.size();
                const bool rightOk = (after >= text.size()) ||
                    !(std::isalnum(static_cast<unsigned char>(text[after])) || text[after] == '_');
                if (leftOk && rightOk)
                {
                    text.replace(pos, word.size(), replacement);
                    pos += replacement.size();
                }
                else
                {
                    pos += word.size();
                }
            }
            return text;
        }

        // plans/plan_glbackends.md GLB-36 helper: GLSL ES 1.00 has no unified texture() overload set --
        // callers must use texture2D()/textureCube() depending on the sampler's declared type.
        // Scans the ORIGINAL ES 3.00 source for "uniform samplerCube NAME;" declarations first
        // (the only non-sampler2D case any shader in this file uses, confirmed by a full survey
        // during GLB-36), then rewrites every texture(NAME, ...) call using that set.
        std::string RewriteTextureCallsForEs100(std::string line, const std::set<std::string>& cubeSamplerNames)
        {
            size_t pos = 0;
            while ((pos = line.find("texture(", pos)) != std::string::npos)
            {
                const size_t argStart = pos + 8;
                const size_t argEnd = line.find(',', argStart);
                if (argEnd == std::string::npos) { pos += 8; continue; }
                std::string samplerName = line.substr(argStart, argEnd - argStart);
                while (!samplerName.empty() && std::isspace(static_cast<unsigned char>(samplerName.front())))
                    samplerName.erase(samplerName.begin());
                while (!samplerName.empty() && std::isspace(static_cast<unsigned char>(samplerName.back())))
                    samplerName.pop_back();
                const bool isCube = cubeSamplerNames.count(samplerName) > 0;
                const std::string replacement = isCube ? "textureCube(" : "texture2D(";
                line.replace(pos, 8, replacement);
                pos += replacement.size();
            }
            return line;
        }

        // plans/plan_glbackends.md GLB-36: rewrites a GLSL ES 3.00 shader body to GLSL ES 1.00
        // (WebGL 1). Real syntax differences handled, confirmed exhaustive by a full survey of
        // every shader in this file during GLB-36 (no texelFetch/textureSize/derivatives/
        // gl_FragDepth/flat/#extension/MRT usage anywhere):
        //   - "layout(location=N) in TYPE NAME;" (vertex attributes) -> "attribute TYPE NAME;"
        //     (the layout qualifier itself is not valid GLSL ES 1.00 -- the caller is expected to
        //     have already extracted the (location, name) pairs via ExtractVertexAttribLocations()
        //     from the ORIGINAL source and rebind them with Program::bind_attrib_location() before
        //     linking, since ES 1.00 has no way to request a specific location from shader text).
        //   - "out TYPE NAME;" varyings (vertex) / "in TYPE NAME;" varyings (fragment) ->
        //     "varying TYPE NAME;" (the same varying keyword serves both directions in ES 1.00).
        //   - "out vec4 FragColor;" (the single fragment color output every shader in this file
        //     uses -- no MRT) is dropped entirely and every reference to the identifier
        //     "FragColor" in the body is replaced with the ES 1.00 built-in "gl_FragColor".
        //   - "texture(sampler, ...)" -> "texture2D(...)"/"textureCube(...)" depending on the
        //     sampler's declared type (see RewriteTextureCallsForEs100 above).
        //   - "#version 300 es" -> "#version 100"; the following "precision ... float;" line is
        //     kept as-is (valid, and required, GLSL ES 1.00 syntax too).
        std::string TransformGlslEs300BodyToEs100(const std::string& es300Body, GlShaderStageKind stage)
        {
            std::set<std::string> cubeSamplerNames;
            {
                const std::string marker = "uniform samplerCube ";
                size_t pos = 0;
                while ((pos = es300Body.find(marker, pos)) != std::string::npos)
                {
                    const size_t nameStart = pos + marker.size();
                    const size_t nameEnd = es300Body.find(';', nameStart);
                    if (nameEnd == std::string::npos) break;
                    cubeSamplerNames.insert(es300Body.substr(nameStart, nameEnd - nameStart));
                    pos = nameEnd;
                }
            }

            std::istringstream iss(es300Body);
            std::string line;
            std::string out;
            while (std::getline(iss, line))
            {
                size_t firstNonSpace = line.find_first_not_of(" \t");
                const std::string trimmed = (firstNonSpace == std::string::npos)
                    ? std::string() : line.substr(firstNonSpace);

                // plans/plan_fx.md FX-124: GLSL ES 3.00 REQUIRES fragment highp, so the shaders in
                // this file ask for it unconditionally. GLSL ES 1.00 makes it optional, and an
                // implementation that lacks it fails to compile a shader that demands it -- so
                // down here the request becomes a guarded one, exactly as FX-121 did for
                // MojoShader's own GLSL ES 1.00 output. A fragment shader that only ever handles
                // [0,1] colours is left at mediump by its own source and is untouched by this.
                if (stage == GlShaderStageKind::Fragment && trimmed == "precision highp float;")
                {
                    out += "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                           "precision highp float;\n"
                           "#else\n"
                           "precision mediump float;\n"
                           "#endif\n";
                    continue;
                }
                if (trimmed.rfind("layout(location", 0) == 0)
                {
                    // "layout(location=N) in TYPE NAME;" or "layout(location = N) in TYPE NAME;"
                    const size_t inPos = trimmed.find(" in ");
                    if (inPos != std::string::npos)
                    {
                        out += "attribute " + trimmed.substr(inPos + 4) + "\n";
                        continue;
                    }
                }
                if (stage == GlShaderStageKind::Vertex && trimmed.rfind("in ", 0) == 0)
                {
                    out += "attribute " + trimmed.substr(3) + "\n";
                    continue;
                }
                if (stage == GlShaderStageKind::Vertex && trimmed.rfind("out ", 0) == 0)
                {
                    out += "varying " + trimmed.substr(4) + "\n";
                    continue;
                }
                if (stage == GlShaderStageKind::Fragment && trimmed.rfind("in ", 0) == 0)
                {
                    out += "varying " + trimmed.substr(3) + "\n";
                    continue;
                }
                if (stage == GlShaderStageKind::Fragment && trimmed == "out vec4 FragColor;")
                {
                    // No declaration needed -- gl_FragColor is an ES 1.00 built-in.
                    continue;
                }

                std::string rewritten = RewriteTextureCallsForEs100(line, cubeSamplerNames);
                if (stage == GlShaderStageKind::Fragment)
                {
                    rewritten = ReplaceWholeWord(rewritten, "FragColor", "gl_FragColor");
                }
                out += rewritten + "\n";
            }
            // Applied to the whole assembled output, not per-line, since it must also see the
            // "attribute vec4 aBoneIndices;" declaration line produced by the
            // layout(location=N) branch above (which `continue`s past the per-line pipeline).
            return out;
        }
    }

    // plans/plan_glbackends.md GLB-36: extracts (location, name) pairs from
    // "layout(location=N) in TYPE NAME;" declarations in the ORIGINAL (unmodified) ES 3.00 vertex
    // shader source, so the caller can rebind the same numeric locations via
    // Program::bind_attrib_location() before linking on WEBGL1/OPENGLES2 (where the layout
    // qualifier itself is stripped out of the shader text -- see TransformGlslEs300BodyToEs100).
    // This is what lets every existing VertexArray/VAO attribute-binding call site in this file
    // (all of which use hardcoded numeric indices matching these same layout(location=N) values)
    // keep working completely unmodified regardless of which of the 5 GL profiles is active.
    static std::vector<std::pair<int, std::string>> ExtractVertexAttribLocations(const std::string& es300VertexSource)
    {
        std::vector<std::pair<int, std::string>> result;
        const std::string marker = "layout(location";
        size_t pos = 0;
        while ((pos = es300VertexSource.find(marker, pos)) != std::string::npos)
        {
            const size_t eq = es300VertexSource.find('=', pos);
            const size_t closeParen = es300VertexSource.find(')', pos);
            if (eq == std::string::npos || closeParen == std::string::npos || eq > closeParen) break;
            const int location = std::stoi(es300VertexSource.substr(eq + 1, closeParen - eq - 1));

            const size_t inPos = es300VertexSource.find(" in ", closeParen);
            if (inPos == std::string::npos) break;
            const size_t typeStart = inPos + 4;
            const size_t typeEnd = es300VertexSource.find(' ', typeStart);
            if (typeEnd == std::string::npos) break;
            const size_t nameStart = typeEnd + 1;
            const size_t nameEnd = es300VertexSource.find(';', nameStart);
            if (nameEnd == std::string::npos) break;
            result.emplace_back(location, es300VertexSource.substr(nameStart, nameEnd - nameStart));
            pos = nameEnd;
        }
        return result;
    }

    // WEBGL1 and OPENGLES2 (both GLSL ES 1.00): see TransformGlslEs300BodyToEs100 above for the
    // real syntax differences handled, and the one known-unconverted gap (integer vertex
    // attributes). WEBGL1 reaches GLSL ES 1.00 through a browser WebGL 1 context; OPENGLES2
    // reaches the exact same dialect through a native OpenGL ES 2.0 context -- the shader text is
    // identical, so the two profiles share one transform branch.
    static std::string AdaptGlslEs300ForActiveProfile(const char* es300Source, GlShaderStageKind stage)
    {
if (ProfileIsDesktopCore())
{
        std::string src(es300Source);
        const std::string versionLine = "#version 300 es\n";
        const auto versionPos = src.find(versionLine);
        if (versionPos == std::string::npos)
        {
            // Not one of the standard ES3-header shaders this function expects -- leave untouched
            // rather than corrupt something it doesn't recognize.
            return src;
        }
        src.replace(versionPos, versionLine.size(), "#version 330 core\n");

        // Desktop GLSL 3.30 core does not accept "precision ... float;" (that syntax is only
        // valid GLSL ES / with GL_ARB_ES2_compatibility) -- drop the line immediately following
        // the version pragma if it is a precision qualifier.
        const auto afterVersion = versionPos + std::string("#version 330 core\n").size();
        if (src.compare(afterVersion, 10, "precision ") == 0)
        {
            const auto lineEnd = src.find('\n', afterVersion);
            if (lineEnd != std::string::npos)
            {
                src.erase(afterVersion, lineEnd + 1 - afterVersion);
            }
        }
        return src;
        }
        if (ProfileUsesGlslEs100())
        {
        std::string src(es300Source);
        const std::string versionLine = "#version 300 es\n";
        const auto versionPos = src.find(versionLine);
        if (versionPos == std::string::npos) return src;
        src.replace(versionPos, versionLine.size(), "#version 100\n");
        std::string transformed = TransformGlslEs300BodyToEs100(src, stage);
        // No shader in this file declares an integer vertex attribute any more: the skinned
        // programs' bone indices are a float vec4 on every profile (FX-127). This check is a
        // defensive safety net for any FUTURE shader that introduces one the transform cannot
        // handle --
        // fail loudly rather than let an invalid shader reach the driver with only an opaque
        // compile-error log as the symptom.
        if (transformed.find("uvec4") != std::string::npos || transformed.find("ivec") != std::string::npos)
        {
            std::cerr << "[CNA EasyGL GLSL ES 1.00] shader uses an integer vertex attribute type "
                          "(uvec4/ivecN) that TransformGlslEs300BodyToEs100 doesn't know how to "
                          "convert, which has no GLSL ES 1.00 equivalent -- this shader is not "
                          "supported under the WEBGL1/OPENGLES2 profiles (see EasyGLRenderer.cpp's "
                          "GLSL ES 1.00 shader adaptation code)."
                       << std::endl;
        }
        return transformed;
        }

        // OPENGLES3 / WEBGL2: the stored source is already GLSL ES 3.00.
        (void)stage;
        return std::string(es300Source);
    }

    // plans/plan_runtimerenderer.md P11: always compiled now; the call site is runtime-gated on the
    // active profile instead. Loading the entry point is harmless on a non-core profile because it
    // is only ever invoked when IsDesktopCoreProfile() holds.
    namespace
    {
        // plans/plan_glbackends.md GLB-40: desktop GL core profile -- unlike GLES/WebGL, which always
        // honor a vertex shader's gl_PointSize output automatically for GL_POINTS primitives --
        // requires this capability explicitly enabled, or gl_PointSize is silently ignored and
        // every point renders at the fixed 1.0-pixel default size. Found investigating why
        // easygl_shipgame_particle_shader_test (a GL_POINTS/gl_PointSize/gl_PointCoord particle
        // shader) rendered nothing under OPENGL33 while passing under OPENGLES3/WEBGL2 -- NOT a
        // shader compile failure (Mesa's desktop compiler accepts "#version 300 es" leniently
        // even under a core-profile context, confirmed empirically), a real missing GL state
        // toggle. Not exposed by meta-gl's typed Capability enum (GLES/WebGL have no equivalent
        // constant at all, so meta-gl never needed to expose it), so loaded and called directly
        // via a runtime function pointer, matching this project's own "no static libGL linkage"
        // convention (meta-gl itself loads every GL entry point the same way). Every stock vertex
        // shader below therefore writes gl_PointSize explicitly: once this state is enabled, its
        // value is undefined when a shader omits the write, and real desktop drivers may rasterize
        // no point at all while llvmpipe happens to retain the 1-pixel default.
        void EnableVertexProgramPointSize()
        {
            using GlEnableFn = void (*)(unsigned int);
            static const auto glEnableFn =
                reinterpret_cast<GlEnableFn>(LoadEasyGlProcAddress("glEnable"));
            constexpr unsigned int kGlVertexProgramPointSize = 0x8642;
            if (glEnableFn) glEnableFn(kGlVertexProgramPointSize);
        }
    }

    // --- EasyGLTexture3DRenderer ---

    static constexpr int kTexLinear       = static_cast<int>(::metagl::TextureMagFilter::Linear);
    static constexpr int kTexClampToEdge  = static_cast<int>(::metagl::TextureWrapMode::ClampToEdge);

    static ::easygl::TextureUnit ToTextureUnit(int unit)
    {
        return static_cast<::easygl::TextureUnit>(
            static_cast<GLenum>(::easygl::TextureUnit::Texture0) + unit);
    }

    // =============================================================================================
    // OPENGLES2 profile support (plans/plan_opengles2.md)
    //
    // The OPENGLES2 public profile drives this same renderer through a NATIVE OpenGL ES 2.0
    // context request combined with WEBGL1's GLSL ES 1.00 shader dialect (see
    // AdaptGlslEs300ForActiveProfile above). OpenGL ES 2.0 predates several entry points the
    // ES 3.0-class profiles use freely, so the profile-gated helpers below supply genuine
    // ES 2.0 mechanics instead of calling functions the API level does not define:
    //   - sampler objects do not exist (glGenSamplers is ES 3.0): sampling state is written onto
    //     the texture objects themselves (the Es2* sampler helpers);
    //   - GL_TEXTURE_MAX_LEVEL does not exist: mipmap completeness is kept by demoting the mip
    //     term of the requested min filter for textures without a full chain (the level-count
    //     registry below tracks which textures allocated complete chains);
    //   - glDrawElementsBaseVertex does not exist (it is ES 3.2): baseVertex draws re-offset every
    //     enabled attribute pointer by baseVertex elements of its own stride instead. The helper
    //     is shared with WebGL and the ES 3.0 floor for the same API limitation;
    //   - GL_READ_FRAMEBUFFER does not exist (ES 3.0): readbacks bind GL_FRAMEBUFFER, whose single
    //     color attachment is the implicit read source (ReadbackFramebufferTarget() below).
    // =============================================================================================

    /// Framebuffer binding point used by the non-MSAA texture/render-target readback paths.
    /// GLES 2.0 has only the combined GL_FRAMEBUFFER target (GL_READ_FRAMEBUFFER is ES 3.0);
    /// reads then come from the bound framebuffer's single color attachment implicitly. The MSAA
    /// resolve paths keep their separate READ/DRAW targets -- they are unreachable under
    /// OPENGLES2, which forces every sample count to 1.
    /// plans/plan_runtimerenderer.md P11: was a profile-selected constant, now a profile-selected value.
    [[nodiscard]] inline ::easygl::FramebufferTarget ReadbackFramebufferTarget()
    {
        // GL_READ_FRAMEBUFFER is ES 3.0; WebGL 1, like ES 2.0, has only the combined target.
        return UsesEs2ApiGeneration(ActiveGlProfile())
            ? ::easygl::FramebufferTarget::Framebuffer
            : ::easygl::FramebufferTarget::ReadFramebuffer;
    }

    /// Internal format for the explicit-internal-format RGBA8 texture allocations in this file
    /// (render-target color storage and cube-map faces). GLES 2.0's glTexImage2D accepts only
    /// UNSIZED internal formats (sized RGBA8 arrived with ES 3.0 / GL_OES_required_internalformat),
    /// so the OPENGLES2 profile allocates GL_RGBA; every other profile keeps the sized RGBA8
    /// allocation unchanged.
    /// plans/plan_runtimerenderer.md P11: was a profile-selected constant, now a profile-selected value.
    [[nodiscard]] inline ::metagl::InternalFormat RgbaTexImageInternalFormat()
    {
        // The sized internal format RGBA8 is ES 3.0; WebGL 1, like ES 2.0, needs the unsized one.
        return UsesEs2ApiGeneration(ActiveGlProfile())
            ? ::metagl::InternalFormat::Rgba
            : ::metagl::InternalFormat::Rgba8;
    }

    /// Attaches a render target's depth (or packed depth+stencil) renderbuffer to the bound FBO.
    /// GLES 2.0 has no GL_DEPTH_STENCIL_ATTACHMENT (the combined point is ES 3.0) -- a packed
    /// GL_DEPTH24_STENCIL8 renderbuffer (GL_OES_packed_depth_stencil) is attached to the DEPTH
    /// and STENCIL points separately there; every other profile keeps the single combined attach.
    static void AttachDepthRenderbufferToBoundFbo(::easygl::Framebuffer& fbo,
                                                  ::metagl::FramebufferAttachment attachment,
                                                  ::easygl::Renderbuffer& rbo)
    {
if (ProfileIsEs2ApiGeneration())
{
        if (attachment == ::metagl::FramebufferAttachment::DepthStencil)
        {
            fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferAttachment::Depth, rbo);
            fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferAttachment::Stencil, rbo);
            return;
        }
}
        fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer, attachment, rbo);
    }

    // plans/plan_runtimerenderer.md P11: always compiled now. Every entry point below is called only
    // from a runtime-gated path, so an ES 3.0 profile simply never reaches this bookkeeping.
    namespace
    {
        /// One recorded XNA sampler-state request (raw ordinals, exactly as ApplySamplerState
        /// receives them).
        struct Es2SamplerDesc
        {
            int filter        = 0;  ///< TextureFilter::Linear
            int addressU      = 1;  ///< TextureAddressMode::Clamp
            int addressV      = 1;  ///< TextureAddressMode::Clamp
            int maxAnisotropy = 4;  ///< SamplerState default MaxAnisotropy
        };

        constexpr int kEs2MaxSamplerSlots = 16;  ///< mirrors EasyGLRenderer::kMaxSamplerSlots

        /// Last sampler state requested per slot. GL texture-unit count and sampler slots share
        /// the same indexing here, exactly like the sampler-object path's samplers_[slot].
        Es2SamplerDesc (&Es2PendingSamplers())[kEs2MaxSamplerSlots]
        {
            static Es2SamplerDesc pending[kEs2MaxSamplerSlots];
            return pending;
        }

        /// GL texture name -> allocated mip level count. GLES 2.0 has no GL_TEXTURE_MAX_LEVEL, so
        /// completeness under a mip-carrying min filter demands a FULL chain -- this registry is
        /// what lets Es2ApplyPendingSamplerToUnit keep the mip term for textures that allocated
        /// one and demote it for single-level textures (which would otherwise sample as opaque
        /// black, the exact REMED-GFX-174/Task 924 failure MAX_LEVEL clamping prevents on ES 3.0).
        /// A name not present is treated as single-level (demote -- the safe direction).
        std::unordered_map<unsigned int, int>& Es2TextureLevelCounts()
        {
            static std::unordered_map<unsigned int, int> levels;
            return levels;
        }

        void Es2RegisterTextureLevels(unsigned int glName, int levelCount)
        {
            if (glName != 0) Es2TextureLevelCounts()[glName] = levelCount;
        }

        void Es2UnregisterTexture(unsigned int glName)
        {
            if (glName != 0) Es2TextureLevelCounts().erase(glName);
        }

        [[nodiscard]] bool Es2TextureHasFullMipChain(unsigned int glName)
        {
            const auto& levels = Es2TextureLevelCounts();
            const auto it = levels.find(glName);
            return it != levels.end() && it->second > 1;
        }

        /// GL_TEXTURE_MAX_ANISOTROPY_EXT is an extension constant meta-gl exposes only for
        /// SAMPLER objects (SamplerParameter::MaxAnisotropy) -- ES 2.0 has no sampler objects, so
        /// the per-TEXTURE parameter is written through a runtime-loaded glTexParameterf,
        /// following EnableVertexProgramPointSize's identical meta-gl-gap precedent (OPENGL33).
        /// Callers gate on GL_EXT_texture_filter_anisotropic being genuinely advertised.
        void Es2SetTextureMaxAnisotropy(::easygl::TextureTarget target, float value)
        {
            using GlTexParameterfFn = void (*)(unsigned int, unsigned int, float);
            static const auto glTexParameterfFn =
                reinterpret_cast<GlTexParameterfFn>(LoadEasyGlProcAddress("glTexParameterf"));
            constexpr unsigned int kGlTextureMaxAnisotropyExt = 0x84FE;  // GL_TEXTURE_MAX_ANISOTROPY_EXT
            if (glTexParameterfFn)
                glTexParameterfFn(static_cast<unsigned int>(target), kGlTextureMaxAnisotropyExt, value);
        }

        /// Writes @p desc onto whatever texture object(s) are bound to @p unit right now.
        ///
        /// Keeps the SAME ordinal -> min/mag/mip decomposition table as ApplySamplerState's
        /// sampler-object path (REMED-GFX-175) -- the two switches must stay in sync, mirroring
        /// how the stride-52 layout is deliberately duplicated per renderer (Task 11.10 note in
        /// ApplyLayout). The only ES 2.0 delta: a texture without a full mip chain gets the mip
        /// term of its min filter dropped (Linear*Mipmap* -> Linear, Nearest*Mipmap* -> Nearest),
        /// which is exactly the effective filtering a complete single-level chain produces on the
        /// ES 3.0 profiles via MAX_LEVEL clamping.
        void Es2ApplyPendingSamplerToUnit(int unit)
        {
            if (unit < 0 || unit >= kEs2MaxSamplerSlots) return;
            const Es2SamplerDesc& desc = Es2PendingSamplers()[unit];

            ::easygl::TextureMinFilter minF;
            ::easygl::TextureMagFilter magF;
            switch (desc.filter)
            {
            case 1: // Point
                minF = ::easygl::TextureMinFilter::NearestMipmapNearest;
                magF = ::easygl::TextureMagFilter::Nearest;
                break;
            case 2: // Anisotropic
                minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
                magF = ::easygl::TextureMagFilter::Linear;
                break;
            case 3: // LinearMipPoint
                minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
                magF = ::easygl::TextureMagFilter::Linear;
                break;
            case 4: // PointMipLinear
                minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
                magF = ::easygl::TextureMagFilter::Nearest;
                break;
            case 5: // MinLinearMagPointMipLinear
                minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
                magF = ::easygl::TextureMagFilter::Nearest;
                break;
            case 6: // MinLinearMagPointMipPoint
                minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
                magF = ::easygl::TextureMagFilter::Nearest;
                break;
            case 7: // MinPointMagLinearMipLinear
                minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
                magF = ::easygl::TextureMagFilter::Linear;
                break;
            case 8: // MinPointMagLinearMipPoint
                minF = ::easygl::TextureMinFilter::NearestMipmapNearest;
                magF = ::easygl::TextureMagFilter::Linear;
                break;
            default: // Linear
                minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
                magF = ::easygl::TextureMagFilter::Linear;
                break;
            }

            const auto demote = [](::easygl::TextureMinFilter f) -> int {
                switch (f)
                {
                case ::easygl::TextureMinFilter::NearestMipmapNearest:
                case ::easygl::TextureMinFilter::NearestMipmapLinear:
                    return static_cast<int>(::easygl::TextureMinFilter::Nearest);
                case ::easygl::TextureMinFilter::LinearMipmapNearest:
                case ::easygl::TextureMinFilter::LinearMipmapLinear:
                    return static_cast<int>(::easygl::TextureMinFilter::Linear);
                default:
                    return static_cast<int>(f);
                }
            };

            const auto toWrap = [](int mode) -> int {
                switch (mode)
                {
                case 1:  return static_cast<int>(::easygl::TextureWrapMode::ClampToEdge);
                case 2:  return static_cast<int>(::easygl::TextureWrapMode::MirroredRepeat);
                default: return static_cast<int>(::easygl::TextureWrapMode::Repeat);
                }
            };

            const bool hasAniso = metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            float anisoValue = 1.0f;
            if (hasAniso && desc.filter == 2)
            {
                GLfloat maxAnisoCap = 1.0f;
                metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
                const float requested = static_cast<float>(desc.maxAnisotropy);
                anisoValue = (maxAnisoCap > 0.0f && requested > maxAnisoCap) ? maxAnisoCap : requested;
                if (anisoValue < 1.0f) anisoValue = 1.0f;
            }

            ::metagl::glActiveTexture(ToTextureUnit(unit));

            const struct
            {
                ::metagl::GetParameter binding;
                ::easygl::TextureTarget target;
            } kTargets[] = {
                { ::metagl::GetParameter::TextureBinding2D,      ::easygl::TextureTarget::Texture2D },
                { ::metagl::GetParameter::TextureBindingCubeMap, ::easygl::TextureTarget::TextureCubeMap },
            };
            for (const auto& entry : kTargets)
            {
                GLint boundName = 0;
                ::metagl::glGetIntegerv(entry.binding, &boundName);
                if (boundName == 0) continue;

                const bool fullChain =
                    Es2TextureHasFullMipChain(static_cast<unsigned int>(boundName));
                const int effectiveMin =
                    fullChain ? static_cast<int>(minF) : demote(minF);
                ::metagl::glTexParameteri(entry.target,
                                          ::easygl::TextureParameterSetter::MinFilter, effectiveMin);
                ::metagl::glTexParameteri(entry.target,
                                          ::easygl::TextureParameterSetter::MagFilter,
                                          static_cast<int>(magF));
                ::metagl::glTexParameteri(entry.target,
                                          ::easygl::TextureParameterSetter::WrapS, toWrap(desc.addressU));
                ::metagl::glTexParameteri(entry.target,
                                          ::easygl::TextureParameterSetter::WrapT, toWrap(desc.addressV));
                // REMED-GFX-174: anisotropy is a component of the ordinal, written on every
                // application (see the sampler-object path's identical reasoning) -- here onto the
                // texture object, the only per-sampling state ES 2.0 offers.
                if (hasAniso)
                    Es2SetTextureMaxAnisotropy(entry.target, anisoValue);
            }

            // Leave unit 0 active, matching every texture-binding site in this file.
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        /// Bytes per component for the vertex-attribute types this renderer binds. Used only to
        /// resolve the effective stride of a tightly-packed (stride 0) attribute.
        [[nodiscard]] int Es2AttribComponentBytes(GLenum type)
        {
            switch (type)
            {
            case GL_BYTE:
            case GL_UNSIGNED_BYTE:  return 1;
            case GL_SHORT:
            case GL_UNSIGNED_SHORT:
            case GL_HALF_FLOAT:     return 2;
            default:                return 4;  // GL_FLOAT / GL_FIXED / GL_INT / GL_UNSIGNED_INT
            }
        }

        /// Re-offsets every enabled per-vertex attribute pointer by @p baseVertex elements of its
        /// own stride, in @p direction (+1 applies the shift, -1 restores it). Must run while the
        /// draw's VAO is bound. This is FNA3D's no-base-vertex fallback shape and serves every CNA
        /// profile whose guaranteed API floor lacks glDrawElementsBaseVertex: GLES 2/3 and both
        /// WebGL generations. ES 3-class profiles can also have per-instance attributes enabled;
        /// those are identified by a nonzero divisor and deliberately left unchanged.
        void ShiftEnabledPerVertexAttribPointers(int baseVertex, int direction)
        {
            if (baseVertex == 0) return;

            GLint maxAttribs = 0;
            ::metagl::glGetIntegerv(::metagl::GetParameter::MaxVertexAttribs, &maxAttribs);
            if (maxAttribs > 16) maxAttribs = 16;  // XNA profile budget; nothing above 15 is used

            GLint previousArrayBuffer = 0;
            ::metagl::glGetIntegerv(::metagl::GetParameter::ArrayBufferBinding, &previousArrayBuffer);

            for (GLint i = 0; i < maxAttribs; ++i)
            {
                const ::metagl::AttribLocation location{static_cast<GLuint>(i)};
                GLint enabled = 0;
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayEnabled, &enabled);
                if (enabled == 0) continue;

                if (!ProfileIsEs2ApiGeneration())
                {
                    GLint divisor = 0;
                    ::metagl::glGetVertexAttribiv(
                        location, ::metagl::VertexAttribParameter::ArrayDivisor, &divisor);
                    if (divisor != 0) continue;
                }

                GLint size = 4, type = GL_FLOAT, normalized = 0, integer = 0;
                GLint stride = 0, bufferBinding = 0;
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArraySize, &size);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayType, &type);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayNormalized, &normalized);
                if (!ProfileIsEs2ApiGeneration())
                    ::metagl::glGetVertexAttribiv(
                        location, ::metagl::VertexAttribParameter::ArrayInteger, &integer);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayStride, &stride);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayBufferBinding, &bufferBinding);
                if (bufferBinding == 0) continue;  // no client-side arrays are used in this file

                void* pointer = nullptr;
                ::metagl::glGetVertexAttribPointerv(location,
                                                    ::metagl::VertexAttribParameter::ArrayPointer, &pointer);

                const std::intptr_t effectiveStride =
                    stride != 0 ? stride
                                : static_cast<std::intptr_t>(size) *
                                      Es2AttribComponentBytes(static_cast<GLenum>(type));
                const std::intptr_t delta =
                    static_cast<std::intptr_t>(baseVertex) * effectiveStride * direction;

                ::metagl::glBindBuffer(::metagl::BufferTarget::Array,
                                       ::metagl::BufferId{static_cast<GLuint>(bufferBinding)});
                if (integer != 0)
                {
                    ::metagl::glVertexAttribIPointer(
                        location, size, static_cast<::metagl::DataType>(type), stride,
                        static_cast<const std::uint8_t*>(pointer) + delta);
                }
                else
                {
                    ::metagl::glVertexAttribPointer(
                        location, size, static_cast<::metagl::DataType>(type),
                        normalized != 0 ? 1 : 0, stride,
                        static_cast<const std::uint8_t*>(pointer) + delta);
                }
            }

            ::metagl::glBindBuffer(::metagl::BufferTarget::Array,
                                   ::metagl::BufferId{static_cast<GLuint>(previousArrayBuffer)});
        }
    }

    // Mirrors Texture3D.cpp's CalculateMipLevels(w,h) — depth does not participate in the level
    // count, matching FNA's Texture3D constructor, but each level's own GPU storage still halves
    // in all 3 dimensions (standard volume-mip behavior).
    static int CalculateTexture3DMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    EasyGLTexture3DRenderer::EasyGLTexture3DRenderer(int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
        : width_(w), height_(h), depth_(depth)
        , levelCount_(mipMap ? CalculateTexture3DMipLevels(w, h) : 1)
    {
        tex_.create();
        tex_.bind(::easygl::TextureTarget::Texture3D);
        // Pre-allocate GPU storage for every mip level (not just level 0): SetData's box writes
        // use glTexSubImage3D, which requires the target level to already have a defined image —
        // without this loop, SetData(level>0,...) would silently fail (same bug shape as Task
        // 276's TextureCube finding).
        const int levelCount = levelCount_;
        int levelW = w, levelH = h, levelD = depth;
        for (int level = 0; level < levelCount; ++level)
        {
            tex_.set_image_3d(::easygl::TextureTarget::Texture3D, level,
                              ::metagl::InternalFormat::Rgba8,
                              levelW, levelH, levelD,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              nullptr);
            levelW = std::max(1, levelW / 2);
            levelH = std::max(1, levelH / 2);
            levelD = std::max(1, levelD / 2);
        }
        // REMED-GFX-174: see EasyGLRenderTargetRenderer's identical clamp. The MinFilter written
        // below is overridden by whatever sampler object ApplySamplerState binds to this unit, so
        // the level range is what has to make the texture complete.
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::easygl::TextureParameterSetter::MaxLevel,
                           levelCount_ - 1);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::easygl::TextureParameterSetter::MinFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::easygl::TextureParameterSetter::MagFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::easygl::TextureParameterSetter::WrapS, kTexClampToEdge);
        tex_.set_parameter(::easygl::TextureTarget::Texture3D, ::easygl::TextureParameterSetter::WrapT, kTexClampToEdge);
    }

    // REMED-GFX-135: glTexSubImage2D/3D have no return value, so GL's error queue is the only
    // completion signal it offers. Drained BEFORE the upload so a stale error left by unrelated code
    // cannot be misread as this call failing, and read AFTER it so an upload the driver rejected is
    // reported as such instead of being returned as a completed write.
    static void DrainGlErrors()
    {
        for (int i = 0; i < 8; ++i)
            if (::metagl::glGetError() == ::metagl::ErrorCode::NoError) return;
    }

    static bool GlUploadSucceeded()
    {
        return ::metagl::glGetError() == ::metagl::ErrorCode::NoError;
    }

    bool EasyGLTexture3DRenderer::SetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          const void* data, int dataLength)
    {
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (dataLength < w * h * depth * 4) return false;

        DrainGlErrors();
        tex_.bind(::easygl::TextureTarget::Texture3D);
        tex_.set_sub_image_3d(::easygl::TextureTarget::Texture3D, level,
                              x, y, z, w, h, depth,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              data);
        return GlUploadSucceeded();
    }

    void EasyGLTexture3DRenderer::BindGL(int unit) const
    {
        tex_.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::Texture3D);
    }

    // --- EasyGLTextureCubeRenderer ---

    static const ::easygl::TextureTarget kCubeFaceTargets[6] = {
        ::easygl::TextureTarget::TextureCubeMapPositiveX,
        ::easygl::TextureTarget::TextureCubeMapNegativeX,
        ::easygl::TextureTarget::TextureCubeMapPositiveY,
        ::easygl::TextureTarget::TextureCubeMapNegativeY,
        ::easygl::TextureTarget::TextureCubeMapPositiveZ,
        ::easygl::TextureTarget::TextureCubeMapNegativeZ,
    };

    // Mirrors TextureCube.cpp's CalculateMipLevels(size,size) — cube faces are square.
    static int CalculateCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    EasyGLTextureCubeRenderer::EasyGLTextureCubeRenderer(int size, bool mipMap, int /*surfaceFormat*/)
        : size_(size)
        , levelCount_(mipMap ? CalculateCubeMipLevels(size) : 1)
    {
        tex_.create();
        tex_.bind(::easygl::TextureTarget::TextureCubeMap);
        // Pre-allocate GPU storage for every mip level (not just level 0): SetData's box writes
        // use glTexSubImage2D, which requires the target level to already have a defined image —
        // without this loop, SetData(level>0,...) would silently fail (Task 276 finding).
        const int levelCount = levelCount_;
        for (auto faceTarget : kCubeFaceTargets)
        {
            int levelSize = size;
            for (int level = 0; level < levelCount; ++level)
            {
                tex_.set_image_2d(faceTarget, level,
                                  RgbaTexImageInternalFormat(),
                                  levelSize, levelSize,
                                  ::metagl::PixelFormat::Rgba,
                                  ::metagl::PixelType::UnsignedByte,
                                  nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- completeness is instead handled by
        // Es2ApplyPendingSamplerToUnit's mip-term demotion, driven by this registration.
        Es2RegisterTextureLevels(tex_.native_handle(), levelCount_);
}
else
{
        // REMED-GFX-174: see EasyGLRenderTargetRenderer's identical clamp -- a cube sampled through
        // EnvironmentMapEffect's slot-1 sampler faces exactly the same completeness rule.
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::MaxLevel,
                           levelCount_ - 1);
}
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::MinFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::MagFilter, kTexLinear);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::WrapS, kTexClampToEdge);
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::WrapT, kTexClampToEdge);
    }

    bool EasyGLTexture3DRenderer::GetData(int level, int x, int y, int z,
                                          int w, int h, int depth,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: every early-out below used to be impossible to express -- this method
        // returned void, so the shared layer converted its own zeroed scratch buffer into a
        // complete transparent-black volume whenever nothing was actually read.
        if (data == nullptr || level < 0 || w <= 0 || h <= 0 || depth <= 0) return false;
        if (dataLength < w * h * depth * 4) return false;

        // GLES3 does not have glGetTexImage. Use a temporary FBO per Z-slice
        // with glReadPixels to read back the pixel data.
        const int bytesPerPixel = 4; // RGBA8
        auto* dest = static_cast<uint8_t*>(data);

        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(::easygl::FramebufferTarget::Framebuffer);
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));

        bool complete = true;
        for (int slice = z; slice < z + depth; ++slice)
        {
            fbo.attach_texture_layer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     tex_, level, slice);
            // A slice whose attachment is not framebuffer-complete reads back nothing at all, so
            // the requested box would only be partly written -- report that, never half-succeed.
            if (!fbo.is_complete(::easygl::FramebufferTarget::Framebuffer)) { complete = false; break; }
            ::metagl::glReadPixels(x, y, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   dest);
            dest += w * h * bytesPerPixel;
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
        return complete;
    }

    EasyGLTextureCubeRenderer::~EasyGLTextureCubeRenderer()
    {
if (ProfileIsEs2ApiGeneration())
{
        // GL reuses deleted names; drop the level registration before tex_'s destructor frees it.
        Es2UnregisterTexture(tex_.native_handle());
}
    }

    void EasyGLTextureCubeRenderer::BindGL(int unit) const
    {
        tex_.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::TextureCubeMap);
    }

    bool EasyGLTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                            const void* data, int dataLength)
    {
        // REMED-GFX-135: the face guard used to be a silent `return`, which the shared layer could
        // not tell apart from a completed upload.
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        DrainGlErrors();
        tex_.bind(::easygl::TextureTarget::TextureCubeMap);
        tex_.set_sub_image_2d(kCubeFaceTargets[face], level, x, y, w, h,
                              ::metagl::PixelFormat::Rgba,
                              ::metagl::PixelType::UnsignedByte,
                              data);
        return GlUploadSucceeded();
    }

    bool EasyGLTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        // REMED-GFX-130: the face guard used to be a silent `return`, which the shared layer turned
        // into a complete transparent-black face rather than a refusal.
        if (face < 0 || face >= 6 || data == nullptr || level < 0 || w <= 0 || h <= 0) return false;
        if (dataLength < w * h * 4) return false;

        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(::easygl::FramebufferTarget::Framebuffer);
        fbo.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                              ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                              kCubeFaceTargets[face],
                              tex_, level);
if (!ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there, so the explicit selection exists only for the ES 3.0 profiles.
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
}

        const bool complete = fbo.is_complete(::easygl::FramebufferTarget::Framebuffer);
        if (complete)
        {
            ::metagl::glReadPixels(x, y, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   data);
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
        return complete;
    }

    // --- EasyGLEffectRenderer ---

    bool EasyGLEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        ::easygl::Shader vs(::easygl::ShaderType::Vertex);
        vs.create();
        vs.compile_from_source(vertSrc.c_str());
        if (!vs.is_compiled())
        {
            compileError_ = "VS: " + vs.info_log();
            return false;
        }
        ::easygl::Shader fs(::easygl::ShaderType::Fragment);
        fs.create();
        fs.compile_from_source(fragSrc.c_str());
        if (!fs.is_compiled())
        {
            compileError_ = "FS: " + fs.info_log();
            return false;
        }
        program_.create();
        program_.attach(vs);
        program_.attach(fs);
        program_.link();
        if (!program_.is_linked())
        {
            compileError_ = "Link: " + program_.info_log();
            return false;
        }
        return true;
    }

    void EasyGLEffectRenderer::Bind()
    {
        if (program_.is_linked())
            program_.use();
    }

    void EasyGLEffectRenderer::Unbind()
    {
        // No easygl::Program::unuse() — the next bind or sprite-batch flush will override.
    }

    bool EasyGLEffectRenderer::IsValid() const
    {
        return program_.is_linked();
    }

    std::string EasyGLEffectRenderer::GetCompileError() const
    {
        return compileError_;
    }

    void EasyGLEffectRenderer::SetUniformFloat(const char* name, float value)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, value);
    }

    void EasyGLEffectRenderer::SetUniformInt(const char* name, int value)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, value);
    }

    void EasyGLEffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y);
    }

    void EasyGLEffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y, z);
    }

    void EasyGLEffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform(loc, x, y, z, w);
    }

    void EasyGLEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_matrix4(loc, matrix);
    }

    int EasyGLEffectRenderer::ArrayUniformLocation(const char* name)
    {
        // GLSL names an array uniform by its first element, and whether a driver also accepts the
        // bare array name is a driver decision rather than a specified one. Looking for both is
        // the difference between an SSAO kernel that occludes and one that silently stays at the
        // origin -- there is no error either way, only a black-and-white image where an ambient
        // occlusion pass should be.
        const int direct = program_.uniform_location(name);
        if (direct >= 0)
            return direct;
        return program_.uniform_location((std::string(name) + "[0]").c_str());
    }

    void EasyGLEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count)), 1);
    }

    void EasyGLEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count) * 2), 2);
    }

    void EasyGLEffectRenderer::SetUniformVec3Array(const char* name, const float* values, int count)
    {
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count) * 3), 3);
    }

    void EasyGLEffectRenderer::SetUniformMat4Array(const char* name, const float* matrices,
                                                  int count)
    {
        const int loc = ArrayUniformLocation(name);
        if (loc >= 0 && count > 0)
            ::metagl::glUniformMatrix4fv(::metagl::UniformLocation{loc}, count, 0, matrices);
    }

    // ---------------------------------------------------------------------------------------
    // plans/plan_modern.md MOD-1511..MOD-1515: compute shaders and shader storage buffers.

    EasyGLStorageBufferRenderer::EasyGLStorageBufferRenderer(const std::size_t byteSize)
        : byteSize_(byteSize)
    {
        buffer_.create();
        // Allocated once with no initial data; DynamicDraw because the whole point of a storage
        // buffer is that something writes it repeatedly -- usually the GPU itself.
        buffer_.set_data(::easygl::BufferTarget::ShaderStorage, nullptr, byteSize_,
                         ::easygl::BufferUsage::DynamicDraw);
    }

    EasyGLStorageBufferRenderer::~EasyGLStorageBufferRenderer() = default;

    void EasyGLStorageBufferRenderer::SetData(const void* data, const std::size_t byteSize)
    {
        if (data == nullptr || byteSize == 0) return;
        buffer_.set_sub_data(::easygl::BufferTarget::ShaderStorage, data,
                             byteSize > byteSize_ ? byteSize_ : byteSize, 0);
    }

    void EasyGLStorageBufferRenderer::GetData(void* out, const std::size_t byteSize) const
    {
        if (out == nullptr || byteSize == 0) return;
        const std::size_t bytes = byteSize > byteSize_ ? byteSize_ : byteSize;
        // glGetBufferSubData is desktop-only; mapping for read is the portable form and is what
        // the GL ES 3.1 contexts this renderer usually holds actually provide.
        void* mapped = buffer_.map_range(::easygl::BufferTarget::ShaderStorage, 0,
                                         static_cast<std::ptrdiff_t>(bytes),
                                         ::metagl::MapBufferAccessMask::Read);
        if (mapped == nullptr) return;
        std::memcpy(out, mapped, bytes);
        buffer_.unmap(::easygl::BufferTarget::ShaderStorage);
    }

    void EasyGLStorageBufferRenderer::BindBase(const int binding) const
    {
        buffer_.bind_base(::easygl::BufferTarget::ShaderStorage,
                          static_cast<unsigned int>(binding));
    }

    void EasyGLStorageBufferRenderer::BindAsDrawIndirect() const
    {
        buffer_.bind(::easygl::BufferTarget::DrawIndirect);
    }

    void EasyGLComputeShaderRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        if (texture == nullptr) return;
        texture->BindGL(unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
    }

    bool EasyGLComputeShaderRenderer::CompileProgram(const std::string& computeSrc)
    {
        compileError_.clear();
        valid_ = false;
        ::easygl::Shader cs(::easygl::ShaderType::Compute);
        cs.create();
        cs.compile_from_source(computeSrc.c_str());
        if (!cs.is_compiled())
        {
            compileError_ = "CS: " + cs.info_log();
            return false;
        }
        program_.create();
        program_.attach(cs);
        program_.link();
        if (!program_.is_linked())
        {
            compileError_ = "Link: " + program_.info_log();
            return false;
        }
        valid_ = true;
        return true;
    }

    void EasyGLComputeShaderRenderer::Bind()
    {
        if (valid_) program_.use();
    }

    void EasyGLComputeShaderRenderer::SetUniformInt(const char* name, const int value)
    {
        if (!valid_) return;
        const int location = program_.uniform_location(name);
        if (location >= 0) program_.set_uniform(location, value);
    }

    void EasyGLComputeShaderRenderer::SetUniformFloat(const char* name, const float value)
    {
        if (!valid_) return;
        const int location = program_.uniform_location(name);
        if (location >= 0) program_.set_uniform(location, value);
    }

    void EasyGLComputeShaderRenderer::BindStorageBuffer(const int binding,
                                                        IStorageBufferRenderer* buffer)
    {
        if (buffer == nullptr) return;
        static_cast<EasyGLStorageBufferRenderer*>(buffer)->BindBase(binding);
    }

    void EasyGLComputeShaderRenderer::BindImageTexture(const int unit, ITextureRenderer* texture,
                                                        const int accessMode)
    {
        if (texture == nullptr) return;
        auto access = ::metagl::ImageAccess::ReadWrite;
        if (accessMode == static_cast<int>(CNA::GraphicsImageAccess::ReadOnly))
            access = ::metagl::ImageAccess::ReadOnly;
        else if (accessMode == static_cast<int>(CNA::GraphicsImageAccess::WriteOnly))
            access = ::metagl::ImageAccess::WriteOnly;

        // RGBA8 because every CNA texture is SurfaceFormat::Color (Texture::ValidateFormat admits
        // nothing else), so the image format is not a choice the caller could make differently.
        ::metagl::glBindImageTexture(
            static_cast<::metagl::ImageUnit>(unit),
            ::metagl::TextureId{static_cast<EasyGLTextureRenderer*>(texture)->texture.native_handle()},
            0, GL_FALSE, 0, access, ::metagl::InternalFormat::Rgba8);
    }

    void EasyGLEffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        TraceBoundTextureUnit("bind-texture-3d", unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
if (ProfileIsEs2ApiGeneration())
{
        // ES 2.0 keeps sampling state on the texture object, so a texture bound AFTER the
        // GraphicsDevice applied this slot's SamplerState must receive that state now.
        Es2ApplyPendingSamplerToUnit(unit);
}

        // REMED-GFX-147: a custom ShaderEffect's GLSL belongs to the game, so this renderer cannot
        // rewrite its sampling for it -- but it can tell it what it is sampling. A user shader that
        // declares `uniform vec4 uRtFlipV;` and maps its V through 1-v where the flag is 1 gets the
        // same render-target correction the stock effects get; one that does not declare it pays
        // nothing (uniform_location returns -1). Sprite draws need no opt-in at all: SpriteBatch
        // corrects its own CPU-side quad UVs, which a custom sprite effect reads unchanged.
        if (unit >= 0 && unit < 4)
        {
            const float flip = SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
            if (rtFlipV_[unit] != flip || !rtFlipVUploaded_)
            {
                rtFlipV_[unit] = flip;
                const int loc = program_.uniform_location("uRtFlipV");
                if (loc >= 0)
                {
                    program_.set_uniform(loc, rtFlipV_[0], rtFlipV_[1], rtFlipV_[2], rtFlipV_[3]);
                    rtFlipVUploaded_ = true;
                }
            }
        }
    }

    // Task 1081: same shape as BindTexture(), but for a samplerCube -- ITextureCubeRenderer is
    // its own interface (not a subtype of ITextureRenderer), so this can't just overload/reuse
    // BindTexture() at the call site.
    void EasyGLEffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
if (ProfileIsEs2ApiGeneration())
{
        // See BindTexture just above -- same ES 2.0 texture-object sampling-state rule.
        Es2ApplyPendingSamplerToUnit(unit);
}
    }

    // plans/plan_graphics.md Task 863: same shape as BindTextureCube(), but for a sampler3D --
    // ITexture3DRenderer is its own interface (not a subtype of ITextureRenderer), so this can't
    // just overload/reuse BindTexture() at the call site.
    void EasyGLEffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
    }

    // --- EasyGLOcclusionQueryRenderer ---

    EasyGLOcclusionQueryRenderer::EasyGLOcclusionQueryRenderer(std::shared_ptr<::easygl::ResourceRegistry> registry)
        : registry_(registry)
    {
if (!ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no query objects (glGenQueries is ES 3.0), and
        // SupportsCapability(OcclusionQuery) reports false under that profile -- never creating
        // the GL query there makes every method below take its existing !is_created() no-op
        // path, honest and crash-free on any ES 2.0 driver (IsComplete stays false,
        // PixelCount stays 0).
        query_.create();
}
        if (auto reg = registry_.lock()) reg->add(this);
    }

    EasyGLOcclusionQueryRenderer::~EasyGLOcclusionQueryRenderer()
    {
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    namespace {
        // GL_SAMPLES_PASSED. Not in metagl's QueryTarget, which is written to the ES 3.0 core set
        // where the only occlusion target is the BOOLEAN GL_ANY_SAMPLES_PASSED. Desktop GL has
        // had the real per-fragment tally since 1.5, and that is what XNA's PixelCount means, so
        // ask for it and keep it when the driver agrees. Cast once, here -- the same shape the
        // GL_TIME_ELAPSED timer query below already uses.
        constexpr ::metagl::QueryTarget kSamplesPassed =
            static_cast<::metagl::QueryTarget>(0x8914);

        // Resolved once per process, on the first query that runs: begin the precise target and
        // ask GL whether it took it. A driver that does not know the enum raises GL_INVALID_ENUM
        // and has begun nothing, so there is nothing to end on that path.
        bool ResolvePreciseTarget(const ::easygl::Query& query)
        {
            while (::metagl::glGetError() != ::metagl::ErrorCode::NoError) {}
            query.begin(kSamplesPassed);
            const bool accepted = ::metagl::glGetError() == ::metagl::ErrorCode::NoError;
            if (accepted) query.end(kSamplesPassed);
            return accepted;
        }

        // -1 until the first Begin() resolves it.
        int g_preciseOcclusionTarget = -1;

        ::metagl::QueryTarget OcclusionTarget()
        {
            return g_preciseOcclusionTarget == 1
                       ? kSamplesPassed
                       : ::easygl::QueryTarget::AnySamplesPassed;
        }
    }

    void EasyGLOcclusionQueryRenderer::Begin()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        if (g_preciseOcclusionTarget < 0)
            g_preciseOcclusionTarget = ResolvePreciseTarget(query_) ? 1 : 0;
        query_.begin(OcclusionTarget());
    }

    void EasyGLOcclusionQueryRenderer::End()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        query_.end(OcclusionTarget());
    }

    bool EasyGLOcclusionQueryRenderer::IsComplete() const
    {
        if (metagl::IsContextLost() || !query_.is_created()) return false;
        return query_.is_result_available();
    }

    int EasyGLOcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete()) return 0;
        // With GL_SAMPLES_PASSED this is the real fragment tally, as XNA's own query is. With the
        // ES/WebGL fallback, GL_ANY_SAMPLES_PASSED, it is 0 (none) or 1 (any) -- ask
        // PixelCountIsPreciseEXT() rather than inferring a coverage ratio from it.
        return static_cast<int>(query_.result());
    }

    bool EasyGLOcclusionQueryRenderer::PixelCountIsPreciseEXT() const noexcept
    {
        return g_preciseOcclusionTarget == 1;
    }

    void EasyGLOcclusionQueryRenderer::release_gl_handle_only()
    {
        query_.reset_handle_no_gl();
    }

    void EasyGLOcclusionQueryRenderer::recreate_gl_resource()
    {
if (!ProfileIsEs2ApiGeneration())
{
        // See the constructor -- no GL query objects exist under the OPENGLES2 profile.
        query_.create();
}
    }

    // --- EasyGLGpuTimerRenderer ---

    namespace {
        // GL_TIME_ELAPSED. Not in metagl's QueryTarget, which is written to the ES 3.0 core set
        // where the timer query is an extension rather than core. Cast once, here.
        constexpr ::metagl::QueryTarget kTimeElapsed = static_cast<::metagl::QueryTarget>(0x88BF);
    }

    EasyGLGpuTimerRenderer::EasyGLGpuTimerRenderer(std::shared_ptr<::easygl::ResourceRegistry> registry)
        : registry_(registry)
    {
        create();
        if (auto reg = registry_.lock()) reg->add(this);
    }

    EasyGLGpuTimerRenderer::~EasyGLGpuTimerRenderer()
    {
        if (auto reg = registry_.lock()) reg->remove(this);
        if (created_ && !metagl::IsContextLost())
        {
            ::metagl::glDeleteQueries(1, &id_);
        }
    }

    void EasyGLGpuTimerRenderer::create()
    {
        if (metagl::IsContextLost()) return;
        ::metagl::glGenQueries(1, &id_);
        created_ = id_.value != 0;
    }

    void EasyGLGpuTimerRenderer::Begin()
    {
        if (metagl::IsContextLost() || !created_ || open_) return;
        ::metagl::glBeginQuery(kTimeElapsed, id_);
        open_ = true;
    }

    void EasyGLGpuTimerRenderer::End()
    {
        if (metagl::IsContextLost() || !created_ || !open_) return;
        ::metagl::glEndQuery(kTimeElapsed);
        open_ = false;
    }

    bool EasyGLGpuTimerRenderer::IsResultAvailable() const
    {
        if (metagl::IsContextLost() || !created_ || open_) return false;
        ::metagl::GLuint available = 0;
        ::metagl::glGetQueryObjectuiv(id_, ::metagl::QueryObjectParameter::ResultAvailable,
                                      &available);
        return available != 0;
    }

    std::uint64_t EasyGLGpuTimerRenderer::ElapsedNanoseconds() const
    {
        if (!IsResultAvailable()) return 0;
        ::metagl::GLuint nanoseconds = 0;
        // 32-bit, because that is what metagl exposes: it saturates a little over 4.29 seconds.
        // No pass this layer measures is within three orders of magnitude of that, and a caller
        // that hits it has a hang rather than a measurement.
        ::metagl::glGetQueryObjectuiv(id_, ::metagl::QueryObjectParameter::Result, &nanoseconds);
        return static_cast<std::uint64_t>(nanoseconds);
    }

    void EasyGLGpuTimerRenderer::release_gl_handle_only()
    {
        id_ = ::metagl::QueryId{};
        created_ = false;
        open_ = false;
    }

    void EasyGLGpuTimerRenderer::recreate_gl_resource()
    {
        create();
    }

    // --- EasyGLTextureRenderer ---

    EasyGLTextureRenderer::EasyGLTextureRenderer(const ImageData& data, std::shared_ptr<::easygl::ResourceRegistry> registry)
        : registry_(registry), surfaceFormat_(data.surfaceFormat),
          mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        width = data.width;
        height = data.height;
        texture.create();
        UploadLevel(0, width, height, data.pixels.data());
        AllocateDeclaredLevels();
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL, so Task 924's clamp cannot exist there --
        // completeness under mip-carrying filters is instead handled by
        // Es2ApplyPendingSamplerToUnit's mip-term demotion, driven by this registration.
        Es2RegisterTextureLevels(texture.native_handle(), mipLevels_);
}
else
{
        // Task 924: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- otherwise a mipmap-
        // requiring TextureFilter (e.g. Anisotropic) treats this as an incomplete mipmap chain
        // (GL's own default max level is 1000) and renders solid black, even for an ordinary
        // single-level (mipLevels_==1) texture that never uploads any level beyond 0.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::easygl::TextureParameterSetter::MaxLevel,
                               mipLevels_ - 1);
}
        if (auto reg = registry_.lock()) reg->add(this);
    }

    void EasyGLTextureRenderer::UploadLevel(int level, int levelWidth, int levelHeight,
                                            const void* pixels)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const SurfaceFormat uploadFormat = static_cast<SurfaceFormat>(surfaceFormat_);
        // The two signed-normalized byte formats differ only in channel count. NormalizedByte2 is
        // what a content pipeline picks for a 2D displacement map, where a third and fourth
        // channel would carry nothing (SAMPLE-032's DisplacementMapProcessor ends with exactly
        // that conversion).
        if (uploadFormat == SurfaceFormat::NormalizedByte4 ||
            uploadFormat == SurfaceFormat::NormalizedByte2)
        {
            const bool twoChannel = uploadFormat == SurfaceFormat::NormalizedByte2;
            texture.bind(::easygl::TextureTarget::Texture2D);
            ::metagl::glPixelStorei(::metagl::PixelStoreParam::UnpackAlignment, 1);
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, level,
                                 twoChannel ? ::easygl::InternalFormat::Rg8Snorm
                                            : ::easygl::InternalFormat::Rgba8Snorm,
                                 levelWidth, levelHeight,
                                 twoChannel ? ::easygl::PixelFormat::Rg
                                            : ::easygl::PixelFormat::Rgba,
                                 ::easygl::PixelType::Byte, pixels);
            texture.set_parameter(::easygl::TextureTarget::Texture2D,
                                  ::easygl::TextureParameterSetter::MinFilter,
                                  static_cast<int>(::easygl::TextureMinFilter::Linear));
            texture.set_parameter(::easygl::TextureTarget::Texture2D,
                                  ::easygl::TextureParameterSetter::MagFilter,
                                  static_cast<int>(::easygl::TextureMagFilter::Linear));
            texture.set_parameter(::easygl::TextureTarget::Texture2D,
                                  ::easygl::TextureParameterSetter::WrapS,
                                  static_cast<int>(::easygl::TextureWrapMode::ClampToEdge));
            texture.set_parameter(::easygl::TextureTarget::Texture2D,
                                  ::easygl::TextureParameterSetter::WrapT,
                                  static_cast<int>(::easygl::TextureWrapMode::ClampToEdge));
            return;
        }
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, level,
                             levelWidth, levelHeight, pixels);
    }

    // REMED-GFX-175: a Texture2D created with mipMap=true DECLARES a chain, and Task 924 widens
    // GL_TEXTURE_MAX_LEVEL to match that declaration. Only level 0 was ever given storage here,
    // though, so until the game happened to call SetData for every remaining level the texture was
    // mipmap-INCOMPLETE -- and an incomplete texture samples as opaque black over its whole surface,
    // magnification included, under every ordinal that carries a mipmap term. That was already true
    // for ordinals 2..8 before this task; giving ordinals 0 and 1 their mipmap term would have
    // extended it to the DEFAULT filter, so the declared chain has to be real.
    //
    // Every declared level is therefore given storage at creation with no pixel data, exactly as
    // this renderer's render-target, cube and volume constructors already do and exactly as the
    // reference GL driver's own CreateTexture2D does. This ALLOCATES the levels the caller asked
    // for; it does not GENERATE them -- no level is downsampled from another, and a texture created
    // without mipMap keeps its single level untouched (the loop body never runs).
    void EasyGLTextureRenderer::AllocateDeclaredLevels()
    {
        for (int level = 1; level < mipLevels_; ++level)
        {
            const int levelW = std::max(1, width >> level);
            const int levelH = std::max(1, height >> level);
            UploadLevel(level, levelW, levelH, nullptr);
        }
    }

    EasyGLTextureRenderer::~EasyGLTextureRenderer()
    {
if (ProfileIsEs2ApiGeneration())
{
        // GL reuses deleted names; drop the level registration before texture's destructor frees it.
        Es2UnregisterTexture(texture.native_handle());
}
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    void EasyGLTextureRenderer::release_gl_handle_only()
    {
if (ProfileIsEs2ApiGeneration())
{
        // Context loss: the name dies with the old context (and the new one may re-issue it), so
        // the registration must go BEFORE the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(texture.native_handle());
}
        texture.reset_handle_no_gl();
    }

    void EasyGLTextureRenderer::recreate_gl_resource()
    {
        texture.create();
        if (pixels_ && !pixels_->empty())
        {
            UploadLevel(0, width, height, pixels_->data());
        }
        else
        {
            const std::vector<uint8_t> blank(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
            UploadLevel(0, width, height, blank.data());
        }
        // REMED-GFX-175: the fresh GL texture object has storage for level 0 only, so a declared
        // chain has to be re-allocated here too or the texture comes back from a context loss
        // mipmap-incomplete and samples black under every mip-filtering ordinal.
        AllocateDeclaredLevels();
if (ProfileIsEs2ApiGeneration())
{
        // See the constructor: the fresh name replaces whatever release_gl_handle_only dropped.
        Es2RegisterTextureLevels(texture.native_handle(), mipLevels_);
}
else
{
        // Task 924: the fresh GL texture object defaults GL_TEXTURE_MAX_LEVEL back to 1000 --
        // reapply the same clamp the constructor set, matching this texture's real level count.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::easygl::TextureParameterSetter::MaxLevel,
                               mipLevels_ - 1);
}
    }

    void EasyGLTextureRenderer::BindGL(int unit) const
    {
        texture.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::Texture2D);
    }

    void EasyGLTextureRenderer::ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels)
    {
        if (!registry_.expired()) pixels_ = std::move(pixels);
    }

    void EasyGLTextureRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        // pixels_ (shared with Texture2D::cpuPixels_) is already updated by the caller
        // before this method is invoked — no need to update it here.
        UploadLevel(0, width, height, rgba);
    }

    void EasyGLTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        UploadLevel(level, levelW, levelH, rgba);
    }

    // --- EasyGLRenderTargetRenderer ---

    // Mirrors Texture2D.cpp's/TextureCube.cpp's CalculateMipLevels.
    static int CalculateRenderTargetMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    // Task 877: maps a Microsoft::Xna::Framework::Graphics::DepthFormat ordinal to the GL
    // renderbuffer internal format and framebuffer attachment point a render target's
    // depth/stencil buffer should use. Returns false for DepthFormat::None, meaning no
    // depth/stencil attachment should be created at all.
    static bool MapDepthFormat(int depthFormat, ::metagl::InternalFormat& outFormat,
                                ::metagl::FramebufferAttachment& outAttachment)
    {
        using ::Microsoft::Xna::Framework::Graphics::DepthFormat;
        switch (static_cast<DepthFormat>(depthFormat))
        {
        case DepthFormat::Depth16:
            outFormat = ::metagl::InternalFormat::DepthComponent16;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24:
            outFormat = ::metagl::InternalFormat::DepthComponent24;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24Stencil8:
            outFormat = ::metagl::InternalFormat::Depth24Stencil8;
            outAttachment = ::metagl::FramebufferAttachment::DepthStencil;
            return true;
        case DepthFormat::None:
        default:
            return false;
        }
    }

    // plans/plan_modern.md MOD-116: maps a Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal to
    // the GL colour storage a render target of that format needs. Data, not a switch chain buried in
    // the allocation code, because the identical triple is needed in three places: the texture's
    // per-level storage, the multisample colour renderbuffer, and the probe that decides whether
    // this GL context can render to the format at all.
    //
    // Only the formats CNA's HDR pipeline actually allocates are listed. Everything else is
    // deliberately absent and refused rather than silently substituted -- a caller who asks for
    // Rgba64 and is handed 8-bit Color has no way to find out, which is the exact failure mode
    // MOD-100 exists to end.
    struct RenderTargetColorStorage
    {
        ::metagl::InternalFormat internalFormat;
        ::metagl::PixelFormat    pixelFormat;
        ::metagl::PixelType      pixelType;
        bool                     isFloat;      ///< Needs a float-renderable colour buffer.
        bool                     isFullFloat;  ///< 32-bit per channel (vs 16-bit half float).
        int                      bytesPerPixel;///< Readback stride per texel, for GetData.
    };

    static bool MapRenderTargetColorFormat(int surfaceFormat, RenderTargetColorStorage& out)
    {
        using ::Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
            out = {RgbaTexImageInternalFormat(), ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::UnsignedByte, false, false, 4};
            return true;
        case SurfaceFormat::Single:
            out = {::metagl::InternalFormat::R32F, ::metagl::PixelFormat::Red,
                   ::metagl::PixelType::Float, true, true, 4};
            return true;
        case SurfaceFormat::Vector2:
            out = {::metagl::InternalFormat::Rg32F, ::metagl::PixelFormat::Rg,
                   ::metagl::PixelType::Float, true, true, 8};
            return true;
        case SurfaceFormat::Vector4:
            out = {::metagl::InternalFormat::Rgba32F, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::Float, true, true, 16};
            return true;
        case SurfaceFormat::HalfSingle:
            out = {::metagl::InternalFormat::R16F, ::metagl::PixelFormat::Red,
                   ::metagl::PixelType::HalfFloat, true, false, 2};
            return true;
        case SurfaceFormat::HalfVector2:
            out = {::metagl::InternalFormat::Rg16F, ::metagl::PixelFormat::Rg,
                   ::metagl::PixelType::HalfFloat, true, false, 4};
            return true;
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            // HdrBlendable is XNA's "float format for HDR data"; on Windows it was RGBA16F, and CNA
            // makes that equivalence explicit rather than inventing a third meaning for it.
            out = {::metagl::InternalFormat::Rgba16F, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::HalfFloat, true, false, 8};
            return true;
        default:
            return false;
        }
    }

    EasyGLRenderTargetRenderer::EasyGLRenderTargetRenderer(int w, int h, int depthFormat,
                                                          std::shared_ptr<::easygl::ResourceRegistry> registry,
                                                          std::weak_ptr<EasyGLBoundTargetEXT> binding,
                                                          bool mipMap, int multiSampleCount,
                                                          int surfaceFormat)
        : width_(w), height_(h), depthFormat_(depthFormat), surfaceFormat_(surfaceFormat),
          mipMap_(mipMap), multiSampleCount_(multiSampleCount), registry_(registry),
          binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(w, h) : 1;
        CreateResources();
        if (auto reg = registry_.lock()) reg->add(this);
        TargetTrace("rt2d.create", this, TraceNativeDetailEXT());
    }

    EasyGLRenderTargetRenderer::~EasyGLRenderTargetRenderer()
    {
        TargetTrace("rt2d.destroy", this, TraceNativeDetailEXT());
        DetachFromBindingEXT();
if (ProfileIsEs2ApiGeneration())
{
        // GL reuses deleted names; drop the level registration before colorTex_ is freed.
        Es2UnregisterTexture(colorTex_.native_handle());
}
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    /**
     * @brief REMED-GFX-168: leaves the shared binding record naming nothing that is about to be freed.
     *
     * Runs FIRST in the destructor, before any GL handle is released, so there is no instant at which
     * the record names this object while its storage is already partly gone.
     *
     * Deliberately does NOT run this target's pending finalization on the way out. `UnbindAsRenderTarget`
     * resolves `msaaColorRbo_` into `colorTex_` and regenerates `colorTex_`'s mip chain -- both write
     * into members this destructor is about to destroy, and the only public routes to that content
     * (`RenderTarget2D::GetData`, and sampling the target as a `Texture2D`) both need the live
     * wrapper. So the work has no reachable observer and skipping it loses nothing; doing it here
     * would instead issue GL calls from a destructor that may run during context teardown.
     *
     * The multi-target case is different and is handled as such: the OTHER slots of a bound set are
     * live targets whose finalization is still observable, so only this target's own slot is cleared
     * and `FinalizeCurrentMRT` still resolves the survivors. The set's extent is left alone, because
     * every member of a set is required to share it.
     */
    void EasyGLRenderTargetRenderer::DetachFromBindingEXT()
    {
        const auto binding = binding_.lock();
        if (!binding) return;   // the graphics renderer went first; there is nothing to detach from
        if (binding->rt2D == this)
        {
            TargetTrace("rt2d.detach", this, "was the bound single target");
            binding->rt2D = nullptr;
            binding->width = 0;
            binding->height = 0;
        }
        bool anySlotLeft = false;
        for (int i = 0; i < binding->mrtCount; ++i)
        {
            if (binding->mrt[static_cast<std::size_t>(i)] == this)
            {
                TargetTrace("mrt.detach", this, "slot " + std::to_string(i));
                binding->mrt[static_cast<std::size_t>(i)] = nullptr;
            }
            else if (binding->mrt[static_cast<std::size_t>(i)] != nullptr)
            {
                anySlotLeft = true;
            }
        }
        // Every slot of the set has now died. Keeping a positive count would leave
        // GetCurrentRenderTarget2DSize reporting an extent no live destination has.
        if (binding->mrtCount > 0 && !anySlotLeft)
        {
            binding->mrtCount = 0;
            binding->mrtFramebuffer = 0;
            binding->width = 0;
            binding->height = 0;
        }
    }

    std::string EasyGLRenderTargetRenderer::TraceNativeDetailEXT() const
    {
        return NativeDetail(fbo_.native_handle(), colorTex_.native_handle(),
                            depthRbo_.native_handle(), msaaColorRbo_.native_handle(),
                            resolveFbo_.native_handle(), width_, height_, multiSampleCount_,
                            levelCount_);
    }

    void EasyGLRenderTargetRenderer::CreateResources()
    {
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no multisample renderbuffers and no blit to resolve them
        // (glRenderbufferStorageMultisample/glBlitFramebuffer are ES 3.0), so the requested
        // preference degrades to single-sample -- the same silent clamp the GL_MAX_SAMPLES limit
        // applies on the ES 3.0 profiles, taken to this profile's real ceiling of 1.
        // GetMultiSampleCount() then reports 0, keeping the public applied count truthful.
        multiSampleCount_ = 0;
}
        // plans/plan_modern.md MOD-115: the colour storage this target's SurfaceFormat calls for. The
        // request is validated before construction (EasyGLRenderer::CreateRenderTarget2DEXT refuses
        // a format this context cannot render to), so an unmapped ordinal here would be a caller
        // bypassing that route; fall back to Color rather than leaving the storage undefined.
        RenderTargetColorStorage colorStorage{};
        if (!MapRenderTargetColorFormat(surfaceFormat_, colorStorage))
        {
            MapRenderTargetColorFormat(0, colorStorage);
            surfaceFormat_ = 0;
        }

        colorTex_.create();
        // The 6-parameter set_image_2d overload does not call glBindTexture first;
        // bind the texture explicitly so glTexImage2D targets our handle.
        colorTex_.bind(::easygl::TextureTarget::Texture2D);
        // Pre-allocate GPU storage for every mip level (not just level 0): the mip chain is
        // regenerated from level 0 via generate_mipmap() when the target is unbound (see
        // UnbindAsRenderTarget), mirroring FNA3D's OPENGL_ResolveTarget behavior — without this
        // loop, levels 1+ would have no defined image and glGenerateMipmap's writes would target
        // GL-incomplete storage (Task 336 finding, same root cause as Task 276's TextureCube fix).
        {
            int levelW = width_, levelH = height_;
            for (int level = 0; level < levelCount_; ++level)
            {
                colorTex_.set_image_2d(::easygl::TextureTarget::Texture2D, level,
                                       colorStorage.internalFormat,
                                       levelW, levelH,
                                       colorStorage.pixelFormat,
                                       colorStorage.pixelType,
                                       nullptr);
                levelW = std::max(1, levelW / 2);
                levelH = std::max(1, levelH / 2);
            }
        }
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- the same completeness problem REMED-GFX-174
        // describes is handled by Es2ApplyPendingSamplerToUnit's mip-term demotion instead,
        // driven by this registration.
        Es2RegisterTextureLevels(colorTex_.native_handle(), levelCount_);
}
else
{
        // REMED-GFX-174: clamp GL_TEXTURE_MAX_LEVEL to the real level count, exactly as Task 924
        // already does for an ordinary Texture2D. GL evaluates mipmap completeness over
        // [BASE_LEVEL, MAX_LEVEL] and GL's own default MAX_LEVEL is 1000, so a render target with
        // one level was INCOMPLETE under any of the seven ordinals (2..8) whose minification filter
        // samples a mip chain, and sampled as solid black.
        //
        // The MinFilter write below is NOT sufficient on its own and had become dead cover: a
        // sampler object bound to the texture unit OVERRIDES the texture object's own filters, and
        // ApplySamplerState binds one for every slot on every draw, so the public TextureFilter --
        // not this line -- decides the effective min filter. Clamping the level range is what makes
        // the texture complete whatever that filter turns out to be, and it allocates nothing: the
        // storage loop above already created exactly levelCount_ levels.
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::easygl::TextureParameterSetter::MaxLevel,
                                levelCount_ - 1);
}
        // REMED-GFX-175: this is the texture object's OWN default min filter, which every draw's
        // sampler object overrides, so it decides nothing about how a game's TextureFilter samples
        // this target -- the level-range clamp above is what keeps it complete under any of them.
        // It is left as a defined starting state for a bind that happens before any sampler is
        // applied; it is no longer, and never really was, the completeness guard its old comment
        // claimed ("the RT has no mipmaps so a mip filter would be incomplete -- use LINEAR").
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::easygl::TextureParameterSetter::MinFilter,
                                static_cast<int>(::metagl::TextureMagFilter::Linear));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::easygl::TextureParameterSetter::MagFilter,
                                static_cast<int>(::metagl::TextureMagFilter::Linear));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::easygl::TextureParameterSetter::WrapS,
                                static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));
        colorTex_.set_parameter(::easygl::TextureTarget::Texture2D,
                                ::easygl::TextureParameterSetter::WrapT,
                                static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));

        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors, mirroring
        // EasyGLRenderer::CreateMsaaBuffers / FNA3D's OPENGL_GetMaxMultiSampleCount.
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        fbo_.create();
        // glFramebufferTexture2D/glFramebufferRenderbuffer operate on the currently bound FBO;
        // bind ours first.
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        if (multiSampleCount_ > 0)
        {
            // Render into a multisampled color renderbuffer (matching FNA3D's
            // FNA3D_GenColorRenderbuffer); colorTex_ is only ever the single-sample resolve
            // target, written by UnbindAsRenderTarget()'s blit, never rendered into directly.
            msaaColorRbo_.create();
            msaaColorRbo_.bind();
            // MOD-115: the multisample buffer must carry the same colour format as the resolve
            // texture -- glBlitFramebuffer requires compatible formats, and a float target whose
            // multisample side stayed Rgba8 would clamp exactly the values HDR exists to keep.
            msaaColorRbo_.set_storage_multisample(multiSampleCount_,
                                                   colorStorage.internalFormat,
                                                   width_, height_);
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     msaaColorRbo_);

            resolveFbo_.create();
            resolveFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            resolveFbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                          ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                          ::easygl::TextureTarget::Texture2D,
                                          colorTex_, 0);
            fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }
        else
        {
            fbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                   ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                   ::easygl::TextureTarget::Texture2D,
                                   colorTex_, 0);
        }

        {
            ::metagl::InternalFormat depthGlFormat;
            ::metagl::FramebufferAttachment depthAttachment;
            if (MapDepthFormat(depthFormat_, depthGlFormat, depthAttachment))
            {
                depthRbo_.create();
                depthRbo_.bind();
                if (multiSampleCount_ > 0)
                    depthRbo_.set_storage_multisample(multiSampleCount_, depthGlFormat, width_, height_);
                else
                    depthRbo_.set_storage(depthGlFormat, width_, height_);
                AttachDepthRenderbufferToBoundFbo(fbo_, depthAttachment, depthRbo_);
            }
        }

        // plans/plan_modern.md MOD-119: ask GL whether the combination it was just handed is actually
        // renderable, and say so if it is not. A driver can accept every individual call above and
        // still refuse the assembled framebuffer -- a colour format that is sampleable but not
        // renderable, a sample count the depth attachment cannot match, a size beyond a limit. All
        // of that used to surface as a target that silently rendered nowhere, which is the hardest
        // shape of bug to trace back to its cause; the format and the GL status make it a
        // one-glance diagnosis instead.
        const ::metagl::FramebufferStatus status =
            fbo_.check_status(::easygl::FramebufferTarget::Framebuffer);
        if (status != ::metagl::FramebufferStatus::Complete)
        {
            ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
            throw std::runtime_error(
                "EasyGL: render target " + std::to_string(width_) + "x" + std::to_string(height_) +
                " (SurfaceFormat ordinal " + std::to_string(surfaceFormat_) +
                ", DepthFormat ordinal " + std::to_string(depthFormat_) +
                ", samples " + std::to_string(multiSampleCount_) +
                ") is not framebuffer-complete: " + std::string(::metagl::to_string(status)));
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetRenderer::BindAsRenderTarget()
    {
        TargetTrace("rt2d.bind", this, TraceNativeDetailEXT());
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetRenderer::ResolveColorEXT(const char* traceEvent) const
    {
        if (multiSampleCount_ <= 0) return;
        TargetTrace(traceEvent, this, TraceNativeDetailEXT());
        fbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
        resolveFbo_.bind(::easygl::FramebufferTarget::DrawFramebuffer);
        ::easygl::Framebuffer::blit(0, 0, width_, height_,
                                    0, 0, width_, height_,
                                    ::metagl::ClearBufferBit::Color,
                                    ::metagl::BlitFilter::Nearest);
    }

    void EasyGLRenderTargetRenderer::UnbindAsRenderTarget()
    {
        TargetTrace("rt2d.unbind", this, TraceNativeDetailEXT());
        // Resolve the multisampled color renderbuffer into colorTex_ before mips (if any) are
        // regenerated from it, matching FNA3D's OPENGL_ResolveTarget resolve-then-mipmap order.
        if (multiSampleCount_ > 0)
            ResolveColorEXT("rt2d.resolve");
        // Regenerate the mip chain from level 0's just-rendered (and possibly just-resolved)
        // content, matching FNA3D's OPENGL_ResolveTarget: "if (target->levelCount > 1) { ...
        // glGenerateMipmap... }".
        if (levelCount_ > 1)
        {
            TargetTrace("rt2d.mipgen", this, TraceNativeDetailEXT());
            colorTex_.bind(::easygl::TextureTarget::Texture2D);
            colorTex_.generate_mipmap(::easygl::TextureTarget::Texture2D);
        }
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    bool EasyGLRenderTargetRenderer::GetData(
        int level, int x, int y, int w, int h, void* data, int dataLength) const
    {
        // plans/plan_modern.md MOD-108: a float target's texels are 2, 4, 8 or 16 bytes wide, not always
        // 4. Reading one back as RGBA8 would silently clamp exactly the above-1.0 values the format
        // was chosen to keep -- the readback has to speak the target's own format.
        RenderTargetColorStorage colorStorage{};
        if (!MapRenderTargetColorFormat(surfaceFormat_, colorStorage))
            MapRenderTargetColorFormat(0, colorStorage);

        if (!data || level < 0 || w <= 0 || h <= 0
            || static_cast<std::int64_t>(dataLength)
                < static_cast<std::int64_t>(w) * h * colorStorage.bytesPerPixel)
            throw std::invalid_argument(
                "EasyGLRenderTargetRenderer::GetData: invalid destination or range.");
        if (level >= levelCount_)
            throw std::out_of_range(
                "EasyGLRenderTargetRenderer::GetData: mip level out of bounds.");

        const int levelWidth = std::max(1, width_ >> level);
        const int levelHeight = std::max(1, height_ >> level);
        if (x < 0 || y < 0 || x + w > levelWidth || y + h > levelHeight)
            throw std::out_of_range(
                "EasyGLRenderTargetRenderer::GetData: rectangle out of bounds.");

        // REMED-GFX-164: GetData is legal while this target remains the active producer.  Its
        // public texture is the single-sample resolve destination, so reading that texture before
        // the bind changes would otherwise return the preceding resolve (fresh targets commonly
        // expose transparent-black storage).  Resolve exactly when this multisample attachment is
        // active, then restore the same DRAW framebuffer so rendering may continue after the read.
        // An idle target was already resolved by the producer/consumer bind transition and incurs
        // no second blit.  READ framebuffer selection below remains the one native pixel transfer.
        if (multiSampleCount_ > 0)
        {
            const auto binding = binding_.lock();
            const bool activeSingle = binding && binding->rt2D == this;
            bool activeMrt = false;
            if (binding)
                for (int i = 0; i < binding->mrtCount; ++i)
                    activeMrt = activeMrt || binding->mrt[static_cast<std::size_t>(i)] == this;
            if (activeSingle || activeMrt)
            {
                ResolveColorEXT("rt2d.resolve.readback");
                if (activeMrt)
                    ::metagl::glBindFramebuffer(
                        ::metagl::FramebufferTarget::DrawFramebuffer,
                        ::metagl::FramebufferId{binding->mrtFramebuffer});
                else
                    fbo_.bind(::easygl::FramebufferTarget::DrawFramebuffer);
            }
        }

GLint previousFramebuffer = 0;  // plans/plan_runtimerenderer.md P11: hoisted -- read by a separate runtime-gated block below
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has only the combined GL_FRAMEBUFFER binding (ReadbackFramebufferTarget()), so
        // selecting a read source below also redirects draws; remember the current binding and
        // restore it afterwards, preserving the ES 3.0 paths' read-only semantics.
        ::metagl::glGetIntegerv(::metagl::GetParameter::FramebufferBinding, &previousFramebuffer);
}
        ::easygl::Framebuffer mipFbo;
        if (level == 0)
        {
            if (multiSampleCount_ > 0)
                resolveFbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
            else
                fbo_.bind(ReadbackFramebufferTarget());
        }
        else
        {
            // Attaching a level above 0 needs GL_OES_fbo_render_mipmap on a real ES 2.0 context
            // (core ES 2.0 restricts glFramebufferTexture2D to level 0) -- universally shipped
            // wherever mip chains exist at all, and advertised by Mesa.
            mipFbo.create();
            mipFbo.bind(ReadbackFramebufferTarget());
            mipFbo.attach_texture_2d(
                ReadbackFramebufferTarget(),
                ::metagl::to_framebuffer_attachment(
                    ::metagl::ColorAttachment::Color0),
                ::easygl::TextureTarget::Texture2D, colorTex_, level);
        }
if (!ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there.
        ::metagl::glReadBuffer(
            ::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
}
        ::metagl::glReadPixels(
            x, levelHeight - y - h, w, h,
            colorStorage.pixelFormat,
            colorStorage.pixelType, data);

        const int rowBytes = w * colorStorage.bytesPerPixel;
        auto* pixels = static_cast<std::uint8_t*>(data);
        std::vector<std::uint8_t> row(static_cast<std::size_t>(rowBytes));
        for (int topRow = 0; topRow < h / 2; ++topRow)
        {
            auto* top = pixels + topRow * rowBytes;
            auto* bottom = pixels + (h - 1 - topRow) * rowBytes;
            std::copy(top, top + rowBytes, row.data());
            std::copy(bottom, bottom + rowBytes, top);
            std::copy(row.begin(), row.end(), bottom);
        }
if (ProfileIsEs2ApiGeneration())
{
        ::metagl::glBindFramebuffer(::metagl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferId{static_cast<GLuint>(previousFramebuffer)});
}
else
{
        ::easygl::Framebuffer::unbind(
            ::easygl::FramebufferTarget::ReadFramebuffer);
}
        return true;
    }

    void EasyGLRenderTargetRenderer::AttachColorToMRT(
        ::easygl::Framebuffer& framebuffer,
        ::metagl::FramebufferAttachment attachment) const
    {
        if (multiSampleCount_ > 0)
        {
            framebuffer.attach_renderbuffer(
                ::easygl::FramebufferTarget::Framebuffer,
                attachment, msaaColorRbo_);
        }
        else
        {
            framebuffer.attach_texture_2d(
                ::easygl::FramebufferTarget::Framebuffer,
                attachment, ::easygl::TextureTarget::Texture2D,
                colorTex_, 0);
        }
    }

    void EasyGLRenderTargetRenderer::AttachDepthToMRT(
        ::easygl::Framebuffer& framebuffer) const
    {
        ::metagl::InternalFormat ignoredFormat;
        ::metagl::FramebufferAttachment attachment;
        if (MapDepthFormat(depthFormat_, ignoredFormat, attachment))
        {
            framebuffer.attach_renderbuffer(
                ::easygl::FramebufferTarget::Framebuffer,
                attachment, depthRbo_);
        }
    }

    void EasyGLRenderTargetRenderer::BindGL(int unit) const
    {
        colorTex_.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::Texture2D);
    }

    unsigned int EasyGLRenderTargetRenderer::GetColorGLHandle() const
    {
        return colorTex_.native_handle();
    }

    void EasyGLRenderTargetRenderer::release_gl_handle_only()
    {
if (ProfileIsEs2ApiGeneration())
{
        // Context loss: unregister before the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(colorTex_.native_handle());
}
        fbo_.reset_handle_no_gl();
        resolveFbo_.reset_handle_no_gl();
        colorTex_.reset_handle_no_gl();
        depthRbo_.reset_handle_no_gl();
        msaaColorRbo_.reset_handle_no_gl();
    }

    void EasyGLRenderTargetRenderer::recreate_gl_resource()
    {
        CreateResources();
    }

    // --- EasyGLRenderTargetCubeRenderer ---

    EasyGLRenderTargetCubeRenderer::EasyGLRenderTargetCubeRenderer(
        int size, int depthFormat, std::shared_ptr<::easygl::ResourceRegistry> registry,
        std::weak_ptr<EasyGLBoundTargetEXT> binding, bool mipMap, int multiSampleCount,
        int surfaceFormat)
        : size_(size), depthFormat_(depthFormat), surfaceFormat_(surfaceFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount), registry_(registry), binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(size, size) : 1;
        CreateResources();
        if (auto reg = registry_.lock()) reg->add(this);
        TargetTrace("cube.create", this, TraceNativeDetailEXT());
    }

    EasyGLRenderTargetCubeRenderer::~EasyGLRenderTargetCubeRenderer()
    {
        TargetTrace("cube.destroy", this, TraceNativeDetailEXT());
        DetachFromBindingEXT();
if (ProfileIsEs2ApiGeneration())
{
        // GL reuses deleted names; drop the level registration before cubeTex_ is freed.
        Es2UnregisterTexture(cubeTex_.native_handle());
}
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    /**
     * @brief REMED-GFX-168: the cube counterpart of `EasyGLRenderTargetRenderer::DetachFromBindingEXT`.
     *
     * A cube is recorded as ONE binding whatever face is active -- the face index lives in the cube's
     * own `lastFace_` -- so detaching the resource detaches every face of it at once, and a face
     * binding cannot outlive its parent. A cube is never a member of a multi-target set (EasyGL's
     * `SetRenderTargets` refuses cube faces in one), so there are no slots to walk.
     *
     * Its pending finalization -- resolving `msaaColorRbos_[lastFace_]` into `cubeTex_` and
     * regenerating that face's mip chain -- writes into members this destructor destroys, exactly as
     * for a 2D target, so there is nothing observable left to owe.
     */
    void EasyGLRenderTargetCubeRenderer::DetachFromBindingEXT()
    {
        const auto binding = binding_.lock();
        if (!binding) return;
        if (binding->cube == this)
        {
            TargetTrace("cube.detach", this, "was the bound cube, face " + std::to_string(lastFace_));
            binding->cube = nullptr;
            binding->width = 0;
            binding->height = 0;
        }
    }

    std::string EasyGLRenderTargetCubeRenderer::TraceNativeDetailEXT() const
    {
        return NativeDetail(fbo_.native_handle(), cubeTex_.native_handle(),
                            depthRbo_.native_handle(),
                            msaaColorRbos_[static_cast<std::size_t>(lastFace_)].native_handle(),
                            resolveFbo_.native_handle(), size_, size_, multiSampleCount_,
                            levelCount_) + " face=" + std::to_string(lastFace_);
    }

    void EasyGLRenderTargetCubeRenderer::CreateResources()
    {
if (ProfileIsEs2ApiGeneration())
{
        // See EasyGLRenderTargetRenderer::CreateResources -- GLES 2.0 has no multisample
        // renderbuffers/blit, so the requested preference degrades to single-sample.
        multiSampleCount_ = 0;
}
        // plans/plan_modern.md MOD-107: the same storage description the 2D targets use, so a float cube
        // face and a float 2D target cannot end up with different GL formats.
        RenderTargetColorStorage cubeStorage{};
        if (!MapRenderTargetColorFormat(surfaceFormat_, cubeStorage))
        {
            MapRenderTargetColorFormat(0, cubeStorage);
            surfaceFormat_ = 0;
        }

        cubeTex_.create();
        cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
        // Allocate storage for all 6 faces, all mip levels (see EasyGLRenderTargetRenderer's
        // CreateResources for why — same Task 336 finding, applied to cube render targets).
        static const ::easygl::TextureTarget kFaceTargets[6] = {
            ::easygl::TextureTarget::TextureCubeMapPositiveX,
            ::easygl::TextureTarget::TextureCubeMapNegativeX,
            ::easygl::TextureTarget::TextureCubeMapPositiveY,
            ::easygl::TextureTarget::TextureCubeMapNegativeY,
            ::easygl::TextureTarget::TextureCubeMapPositiveZ,
            ::easygl::TextureTarget::TextureCubeMapNegativeZ,
        };
        for (auto faceTarget : kFaceTargets)
        {
            int levelSize = size_;
            for (int level = 0; level < levelCount_; ++level)
            {
                cubeTex_.set_image_2d(faceTarget, level,
                                       cubeStorage.internalFormat,
                                       levelSize, levelSize,
                                       cubeStorage.pixelFormat,
                                       cubeStorage.pixelType,
                                       nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- see EasyGLRenderTargetRenderer::CreateResources.
        Es2RegisterTextureLevels(cubeTex_.native_handle(), levelCount_);
}
else
{
        // REMED-GFX-174: see EasyGLRenderTargetRenderer's identical clamp.
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::MaxLevel,
                               levelCount_ - 1);
}
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::MinFilter,
                               static_cast<int>(::metagl::TextureMagFilter::Linear));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::MagFilter,
                               static_cast<int>(::metagl::TextureMagFilter::Linear));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::WrapS,
                               static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::WrapT,
                               static_cast<int>(::metagl::TextureWrapMode::ClampToEdge));

        // Clamp to GL_MAX_SAMPLES, same as EasyGLRenderTargetRenderer.
        if (multiSampleCount_ > 0)
        {
            GLint maxSamples = 0;
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
            if (maxSamples > 0 && multiSampleCount_ > static_cast<int>(maxSamples))
                multiSampleCount_ = static_cast<int>(maxSamples);
        }

        fbo_.create();
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        if (multiSampleCount_ > 0)
        {
            // REMED-GFX-141: one multisample color renderbuffer PER FACE. This used to allocate a
            // single one for the whole cube, on the reasoning that only one face is ever rendered
            // into at a time -- true for producing a face, but it left a PreserveContents face
            // nothing of its own to load back: a renderbuffer carries no face identity, so rebinding
            // face A found whichever face was rendered last. Face 0 of an interleaved A -> B -> A
            // sequence came back holding 60 of its 64 texels from face B. resolveFbo_ is still
            // re-attached to whichever face is being unbound, so the blit resolves into the correct
            // cube image; what changed is that the blit SOURCE is now that face's own storage.
            for (auto& rbo : msaaColorRbos_)
            {
                rbo.create();
                rbo.bind();
                // MOD-107: same reason as the 2D target's multisample storage -- the resolve blit
                // needs compatible formats, and an RGBA8 multisample side would clamp a float face.
                rbo.set_storage_multisample(multiSampleCount_,
                                             cubeStorage.internalFormat,
                                             size_, size_);
            }
            // Face 0 is attached here only so this FBO is complete the moment it exists;
            // BindAsRenderTargetFace re-attaches the face actually being bound.
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     msaaColorRbos_[0]);
            resolveFbo_.create();
        }

        ::metagl::InternalFormat depthGlFormat;
        ::metagl::FramebufferAttachment depthAttachment;
        if (MapDepthFormat(depthFormat_, depthGlFormat, depthAttachment))
        {
            depthRbo_.create();
            depthRbo_.bind();
            if (multiSampleCount_ > 0)
                depthRbo_.set_storage_multisample(multiSampleCount_, depthGlFormat, size_, size_);
            else
                depthRbo_.set_storage(depthGlFormat, size_, size_);
            fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            AttachDepthRenderbufferToBoundFbo(fbo_, depthAttachment, depthRbo_);
        }

        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        lastFace_ = face;
        TargetTrace("cube.bindface", this, TraceNativeDetailEXT());
        fbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        const auto faceTarget = static_cast<::easygl::TextureTarget>(
            static_cast<unsigned int>(::easygl::TextureTarget::TextureCubeMapPositiveX) + face);
        if (multiSampleCount_ == 0)
        {
            // Non-MSAA: fbo_'s color attachment IS cubeTex_ — re-attach the requested face
            // (0=+X .. 5=-Z) directly, since all faces share this one FBO/texture.
            fbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                    faceTarget,
                                    cubeTex_, 0);
        }
        else
        {
            // REMED-GFX-141: MSAA re-attaches this face's OWN multisample renderbuffer, the exact
            // counterpart of the non-MSAA branch above. GL has no load action, so a face's samples
            // simply survive until something draws over them -- which is all PreserveContents ever
            // needed. A DiscardContents face is still wiped by the Clear()
            // GraphicsDevice::SetRenderTargets issues on every bind, so nothing about discard
            // semantics changes here.
            fbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                     ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                     msaaColorRbos_[static_cast<std::size_t>(face)]);
        }
    }

    void EasyGLRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        TargetTrace("cube.unbind", this, TraceNativeDetailEXT());
        if (multiSampleCount_ > 0)
        {
            TargetTrace("cube.resolve", this, TraceNativeDetailEXT());
            const auto faceTarget = static_cast<::easygl::TextureTarget>(
                static_cast<unsigned int>(::easygl::TextureTarget::TextureCubeMapPositiveX) + lastFace_);
            resolveFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
            resolveFbo_.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                          ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                          faceTarget,
                                          cubeTex_, 0);
            fbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
            resolveFbo_.bind(::easygl::FramebufferTarget::DrawFramebuffer);
            ::easygl::Framebuffer::blit(0, 0, size_, size_,
                                        0, 0, size_, size_,
                                        ::metagl::ClearBufferBit::Color,
                                        ::metagl::BlitFilter::Linear);
        }
        // Regenerate the mip chain for all 6 faces from their just-rendered (and possibly
        // just-resolved) level-0 content.
        if (levelCount_ > 1)
        {
            cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
            cubeTex_.generate_mipmap(::easygl::TextureTarget::TextureCubeMap);
        }
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
    }

    unsigned int EasyGLRenderTargetCubeRenderer::GetGLHandle() const
    {
        return cubeTex_.native_handle();
    }

    void EasyGLRenderTargetCubeRenderer::BindGL(int unit) const
    {
        cubeTex_.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::TextureCubeMap);
    }

    bool EasyGLRenderTargetCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                                 const void* data, int dataLength)
    {
        // REMED-GFX-135: same completion contract as EasyGLTextureCubeRenderer::SetData -- this is
        // the one render-target cube that really stores CPU pixels rather than inheriting
        // IRenderTargetCubeRenderer::SetData's refusal.
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        DrainGlErrors();
        cubeTex_.bind(::easygl::TextureTarget::TextureCubeMap);
        cubeTex_.set_sub_image_2d(kCubeFaceTargets[face], level, x, y, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   data);
        return GlUploadSucceeded();
    }

    bool EasyGLRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                 void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from
        // IRenderTargetCubeRenderer/ITextureCubeRenderer. Same temporary-FBO mechanism
        // EasyGLTextureCubeRenderer::GetData already uses, plus the bottom-up correction a
        // RENDERED attachment needs (EasyGLRenderTargetRenderer::GetData's own).
        if (face < 0 || face >= 6 || data == nullptr || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

GLint previousFramebuffer = 0;  // plans/plan_runtimerenderer.md P11: hoisted -- read by a separate runtime-gated block below
if (ProfileIsEs2ApiGeneration())
{
        // See EasyGLRenderTargetRenderer::GetData -- the combined GL_FRAMEBUFFER binding must be
        // restored so a read here cannot redirect subsequent draws.
        ::metagl::glGetIntegerv(::metagl::GetParameter::FramebufferBinding, &previousFramebuffer);
}
        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(ReadbackFramebufferTarget());
        fbo.attach_texture_2d(ReadbackFramebufferTarget(),
                              ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                              kCubeFaceTargets[face],
                              cubeTex_, level);
if (!ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there.
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
}

        const bool complete = fbo.is_complete(ReadbackFramebufferTarget());
        if (complete)
        {
            ::metagl::glReadPixels(x, levelSize - y - h, w, h,
                                   ::metagl::PixelFormat::Rgba,
                                   ::metagl::PixelType::UnsignedByte,
                                   data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
            auto* pixels = static_cast<std::uint8_t*>(data);
            std::vector<std::uint8_t> row(rowBytes);
            for (int topRow = 0; topRow < h / 2; ++topRow)
            {
                auto* top    = pixels + static_cast<std::size_t>(topRow) * rowBytes;
                auto* bottom = pixels + static_cast<std::size_t>(h - 1 - topRow) * rowBytes;
                std::copy(top, top + rowBytes, row.data());
                std::copy(bottom, bottom + rowBytes, top);
                std::copy(row.begin(), row.end(), bottom);
            }
        }

if (ProfileIsEs2ApiGeneration())
{
        ::metagl::glBindFramebuffer(::metagl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferId{static_cast<GLuint>(previousFramebuffer)});
}
else
{
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::ReadFramebuffer);
}
        return complete;
    }

    void EasyGLRenderTargetCubeRenderer::release_gl_handle_only()
    {
if (ProfileIsEs2ApiGeneration())
{
        // Context loss: unregister before the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(cubeTex_.native_handle());
}
        fbo_.reset_handle_no_gl();
        resolveFbo_.reset_handle_no_gl();
        cubeTex_.reset_handle_no_gl();
        depthRbo_.reset_handle_no_gl();
        // REMED-GFX-141: all six per-face multisample renderbuffers, not one.
        for (auto& rbo : msaaColorRbos_) rbo.reset_handle_no_gl();
    }

    void EasyGLRenderTargetCubeRenderer::recreate_gl_resource()
    {
        CreateResources();
    }

    // --- EasyGLSpriteBatchRenderer ---

    EasyGLSpriteBatchRenderer::EasyGLSpriteBatchRenderer(::easygl::Device& device, std::shared_ptr<::easygl::ResourceRegistry> registry,
                                                       EasyGLRenderer* renderer)
        : device_(device)
        , registry_(registry)
        , graphicsRenderer_(renderer)
    {
        InitializeResources();
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        if (graphicsRenderer_ != nullptr)
        {
            const auto& bytes =
                CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
            spriteCompiledEffect_ = std::make_unique<EasyGLCompiledEffect>(
                *graphicsRenderer_, bytes, sizeof(bytes));
            const auto& parameters = spriteCompiledEffect_->GetDescription().parameters;
            const auto matrix = std::find_if(
                parameters.begin(), parameters.end(),
                [](const CompiledEffectParameterDescription& parameter)
                {
                    return parameter.name == "MatrixTransform";
                });
            if (matrix == parameters.end())
            {
                throw std::runtime_error(
                    "CNA EasyGL: embedded XNA SpriteEffect has no MatrixTransform parameter.");
            }
            spriteMatrixParameterIndex_ = matrix->runtimeIndex;
        }
#endif
        if (auto reg = registry_.lock()) reg->add(this);
    }

    EasyGLSpriteBatchRenderer::~EasyGLSpriteBatchRenderer()
    {
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    void EasyGLSpriteBatchRenderer::release_gl_handle_only()
    {
        program_.reset_handle_no_gl();
        vao_.reset_handle_no_gl();
        vbo_.reset_handle_no_gl();
        ibo_.reset_handle_no_gl();
    }

    void EasyGLSpriteBatchRenderer::recreate_gl_resource()
    {
        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
        transform_ = Matrix::getIdentityProperty();
        InitializeResources();
    }

    void EasyGLSpriteBatchRenderer::ApplyChannelExpansion(::easygl::Program* prog,
                                                          int surfaceFormat) const
    {
        if (prog == nullptr) return;
        const int maskLocation = prog->uniform_location("uChannelMask");
        const int fillLocation = prog->uniform_location("uChannelFill");
        if (maskLocation < 0 || fillLocation < 0) return;

        using ::Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        // D3D9's own expansion rule, per channel count of the stored format. Everything not
        // listed stores all four channels and expands identically under GL, so it takes the
        // identity pair.
        float mask[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float fill[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Single:
        case SurfaceFormat::HalfSingle:
            mask[1] = mask[2] = mask[3] = 0.0f;
            fill[1] = fill[2] = fill[3] = 1.0f;
            break;
        case SurfaceFormat::Vector2:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::NormalizedByte2:
            mask[2] = mask[3] = 0.0f;
            fill[2] = fill[3] = 1.0f;
            break;
        default:
            break;
        }
        prog->set_uniform(maskLocation, mask[0], mask[1], mask[2], mask[3]);
        prog->set_uniform(fillLocation, fill[0], fill[1], fill[2], fill[3]);
    }

    void EasyGLSpriteBatchRenderer::InitializeResources()
    {
        const char* vertexShaderSource = R"(#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
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

        const std::string adaptedVertexSource =
            AdaptGlslEs300ForActiveProfile(vertexShaderSource, GlShaderStageKind::Vertex);
        const std::string adaptedFragmentSource =
            AdaptGlslEs300ForActiveProfile(fragmentShaderSource, GlShaderStageKind::Fragment);

        ::easygl::Shader vertexShader(::easygl::ShaderType::Vertex);
        vertexShader.create();
        vertexShader.compile_from_source(adaptedVertexSource.c_str());

        if (!vertexShader.is_compiled())
        {
            std::cerr << "Vertex shader compilation failed:\n" << vertexShader.info_log() << std::endl;
        }

        ::easygl::Shader fragmentShader(::easygl::ShaderType::Fragment);
        fragmentShader.create();
        fragmentShader.compile_from_source(adaptedFragmentSource.c_str());

        if (!fragmentShader.is_compiled())
        {
            std::cerr << "Fragment shader compilation failed:\n" << fragmentShader.info_log() << std::endl;
        }

        program_.create();
        program_.attach(vertexShader);
        program_.attach(fragmentShader);
if (ProfileUsesGlslEs100())
{
        // plans/plan_glbackends.md GLB-36: see CompileAndLink's identical comment -- rebind the same
        // numeric attribute locations the ES 3.00 source's layout(location=N) qualifiers
        // specified, since the GLSL ES 1.00 shader text these profiles compile has no
        // layout(location=N) at all.
        for (const auto& [location, name] : ExtractVertexAttribLocations(vertexShaderSource))
        {
            program_.bind_attrib_location(static_cast<unsigned int>(location), name);
        }
}
        program_.link();

        if (!program_.is_linked())
        {
            std::cerr << "Shader program linking failed:\n" << program_.info_log() << std::endl;
        }

        program_.use();
        const int textureLocation = program_.uniform_location("texture1");
        if (textureLocation >= 0)
        {
            program_.set_uniform(textureLocation, 0);
        }

        vbo_.create();
        ibo_.create();
        if (!ProfileIsEs2ApiGeneration())
        {
            vao_.create();
            vao_.bind();
        }
        vbo_.bind(::easygl::BufferTarget::Array);

        if (!ProfileIsEs2ApiGeneration())
        {
            // Position (0), TexCoord (1), Color (2). WebGL 1 / GLES 2 has no core VAO;
            // that path reapplies the same default-array attributes immediately before drawing.
            vao_.enable_attribute(0);
            vao_.set_attribute_pointer(0, 2, ::easygl::DataType::Float, false,
                                       8 * sizeof(float), (void*)0);

            vao_.enable_attribute(1);
            vao_.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false,
                                       8 * sizeof(float), (void*)(2 * sizeof(float)));

            vao_.enable_attribute(2);
            vao_.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false,
                                       8 * sizeof(float), (void*)(4 * sizeof(float)));

            ibo_.bind(::easygl::BufferTarget::ElementArray);
            vao_.unbind();
        }
    }

    void EasyGLSpriteBatchRenderer::Begin()
    {
        // Task 956 fix: this previously hardcoded set_blend_enabled(true) +
        // SrcAlpha/OneMinusSrcAlpha unconditionally, clobbering whatever
        // EasyGLRenderer::ApplyBlendState had just set via
        // GraphicsDevice::setBlendStateProperty(blendState) -- called by SpriteBatch::Begin()
        // immediately before renderer_->Begin() runs (see SpriteBatch.cpp). This both silently
        // ignored any non-AlphaBlend BlendState passed to SpriteBatch::Begin() (e.g. Opaque or
        // NonPremultiplied always rendered as if AlphaBlend-with-straight-alpha-factors had been
        // requested) and left the real GL blend state permanently stuck at that hardcoded value
        // after End() -- any 3D draw issued afterward without the game explicitly reassigning
        // BlendState inherited SpriteBatch's leftover raw GL state instead of whatever
        // GraphicsDevice.BlendState still claimed was active. Matches the same bug shape the
        // toolkit-backed 2D renderer already fixed (Task 695).
        begun = true;
    }

    void EasyGLSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    bool EasyGLSpriteBatchRenderer::BatchFlushesThroughCompiledEffect() const
    {
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        return customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr;
#else
        return false;
#endif
    }

    void EasyGLSpriteBatchRenderer::ResolveCurrentTextureRowOrder()
    {
        // plans/plan_fx.md FX-118: the compiled route corrects a bottom-up source per sampler slot
        // (AcquireCompiledEffectFlippedSourceEXT), so the sprite's own V must be left alone there.
        // Doing both mirrors the image -- and only the compiled route can correct a slot the
        // sprite quad does not own, such as the base image a bloom combine reads from slot 1.
        current_texture_bottom_up_ = current_texture_ != nullptr &&
                                     !BatchFlushesThroughCompiledEffect() &&
                                     SampledRowOrderIsBottomUp(current_texture_);
    }

    void EasyGLSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
            // FlushBatch() is a no-op for an empty batch and then leaves current_texture_ in
            // place, so a Begin() that only changes the effect would otherwise keep the previous
            // batch's answer.
            ResolveCurrentTextureRowOrder();
        }
    }

    void EasyGLSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void EasyGLSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void EasyGLSpriteBatchRenderer::End()
    {
        FlushBatch();
        begun = false;
    }

    void EasyGLSpriteBatchRenderer::FlushBatch()
    {
        if (pending_vertices_.empty()) return;

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-080: a compiled XNA Effect gets its own route. Before this branch existed
        // the code below silently kept the stock sprite program for one -- GetEffectRendererPtr()
        // returns null for a compiled effect, so `prog` stayed `&program_` -- and rendered the
        // batch with a shader the game never asked for, reporting nothing.
        if (customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr)
        {
            FlushBatchWithCompiledEffect();
            return;
        }
#endif

        // Determine which GL program to use: built-in or custom Effect.
        // Task 1077 fix: bind the SAME compiled program the Effect itself owns
        // (Effect::GetEffectRendererPtr(), overridden by ShaderEffect) instead of recompiling a
        // second, independent copy from GLSL source text -- the old recompiled-copy approach
        // meant any ShaderEffect::SetUniformXxx() call (which writes to the effect's OWN
        // program) had no way to ever reach the program actually bound for the real draw.
        ::easygl::Program* prog = &program_;
        if (customEffect_)
        {
            auto* renderer = dynamic_cast<EasyGLEffectRenderer*>(customEffect_->GetEffectRendererPtr());
            if (renderer && renderer->IsValid())
                prog = &renderer->GetProgram();
            customEffect_->Apply();
        }

        prog->use();

        int logW = 0, logH = 0;
        int rtW = 0, rtH = 0;
        const bool haveRt = graphicsRenderer_ && graphicsRenderer_->GetCurrentRenderTarget2DSize(rtW, rtH)
                            && rtW > 0 && rtH > 0;

        // REMED-GFX-072: honor a custom GraphicsDevice.Viewport. XNA/FNA build the SpriteBatch ortho
        // from Viewport.Width/Height (CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height,
        // 0)), so a custom sub-Viewport makes sprite coordinates VIEWPORT-LOCAL. The GL viewport that
        // GraphicsDevice.Viewport set via SetViewport() survives Clear() (Task 880), so read it here:
        // if it is a genuine sub-region of the full target, size the projection to it AND leave it in
        // place as the rasterizer viewport (its X/Y position the [-1,1] result at Viewport.X/Y).
        // Previously FlushBatch unconditionally reset the GL viewport to the full target and built the
        // ortho from the full target/logical size, so a custom Viewport was ignored for sprites. The
        // default full-target viewport keeps the exact prior behavior (reset + full-target/logical ortho).
        int curVx = 0, curVy = 0, curVw = 0, curVh = 0;
        device_.get_viewport(curVx, curVy, curVw, curVh);
        int fullW = 0, fullH = 0;
        if (haveRt) { fullW = rtW; fullH = rtH; }
        else if (graphicsRenderer_) graphicsRenderer_->getPhysicalSize(fullW, fullH);
        const bool customVp = curVw > 0 && curVh > 0 && fullW > 0 && fullH > 0
                              && (curVx != 0 || curVy != 0 || curVw != fullW || curVh != fullH);

        // Task 1078: a custom-effect draw into a bound RenderTarget2D must size its viewport
        // and orthographic projection to that RT, not the window -- getPhysicalSize()/
        // getLogicalSize() are always window-sized, which only happened to work in every prior
        // test because those tests' RTs all coincidentally matched the window size.
        if (customVp)
        {
            // Keep the custom GL viewport (do NOT reset to the full target); project by Viewport.W/H.
            logW = curVw;
            logH = curVh;
        }
        else if (haveRt)
        {
            device_.set_viewport(0, 0, rtW, rtH);
            logW = rtW;
            logH = rtH;
        }
        else if (graphicsRenderer_)
        {
            int physW = 0, physH = 0;
            graphicsRenderer_->getPhysicalSize(physW, physH);
            if (physW > 0 && physH > 0)
                device_.set_viewport(0, 0, physW, physH);
            graphicsRenderer_->getLogicalSize(logW, logH);
        }
        if (logW <= 0 || logH <= 0)
        {
            int vx, vy, vw, vh;
            device_.get_viewport(vx, vy, vw, vh);
            logW = vw;
            logH = vh;
        }

        const Matrix orthoM = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logW),
            static_cast<float>(logH), 0.0f,
            -1.0f, 1.0f);
        const Matrix combined = transform_ * orthoM;
        float ortho[16];
        combined.ToColumnMajor(ortho);
        const int projLoc = prog->uniform_location("projection");
        if (projLoc >= 0)
            prog->set_uniform_matrix4(projLoc, ortho);

        current_texture_->BindGL();
        ApplyChannelExpansion(prog, current_texture_->GetSurfaceFormatEXT());
        if (graphicsRenderer_)
            graphicsRenderer_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        vbo_.bind(::easygl::BufferTarget::Array);
        vbo_.set_data(::easygl::BufferTarget::Array,
                      pending_vertices_.data(),
                      pending_vertices_.size() * sizeof(Vertex));

        if (ProfileIsEs2ApiGeneration())
        {
            // Vertex attribute state is global in the GLES 2 / WebGL 1 core API. Other draws may
            // have changed it since the previous batch, so restore SpriteBatch's complete layout
            // after binding this batch's VBO instead of pretending a core VAO exists.
            metagl::glEnableVertexAttribArray(metagl::AttribLocation{0});
            metagl::glVertexAttribPointer(metagl::AttribLocation{0}, 2,
                                          metagl::DataType::Float, 0,
                                          static_cast<metagl::GLsizei>(8 * sizeof(float)), (void*)0);
            metagl::glEnableVertexAttribArray(metagl::AttribLocation{1});
            metagl::glVertexAttribPointer(metagl::AttribLocation{1}, 2,
                                          metagl::DataType::Float, 0,
                                          static_cast<metagl::GLsizei>(8 * sizeof(float)),
                                          (void*)(2 * sizeof(float)));
            metagl::glEnableVertexAttribArray(metagl::AttribLocation{2});
            metagl::glVertexAttribPointer(metagl::AttribLocation{2}, 4,
                                          metagl::DataType::Float, 0,
                                          static_cast<metagl::GLsizei>(8 * sizeof(float)),
                                          (void*)(4 * sizeof(float)));
        }
        else
        {
            vao_.bind();
        }

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        ibo_.set_data(::easygl::BufferTarget::ElementArray,
                      pending_indices_.data(),
                      pending_indices_.size() * sizeof(uint16_t));

        device_.draw_elements(
            ::easygl::PrimitiveType::Triangles,
            static_cast<int>(pending_indices_.size()),
            ::easygl::DataType::UnsignedShort,
            nullptr
        );

        if (!ProfileIsEs2ApiGeneration())
            vao_.unbind();

        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
    }

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
    void EasyGLSpriteBatchRenderer::ApplyCompiledSpriteVertexShader(
        int logicalWidth, int logicalHeight)
    {
        if (spriteCompiledEffect_ == nullptr || customEffect_ == nullptr ||
            logicalWidth <= 0 || logicalHeight <= 0)
        {
            throw std::runtime_error(
                "CNA EasyGL: the XNA SpriteBatch vertex effect is unavailable.");
        }

        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth),
            static_cast<float>(logicalHeight), 0.0f, 0.0f, -1.0f);
        const Matrix combined = transform_ * projection;
        const float values[16] = {
            combined.M11, combined.M21, combined.M31, combined.M41,
            combined.M12, combined.M22, combined.M32, combined.M42,
            combined.M13, combined.M23, combined.M33, combined.M43,
            combined.M14, combined.M24, combined.M34, combined.M44,
        };
        spriteCompiledEffect_->SetParameterValue(
            spriteMatrixParameterIndex_, values, sizeof(values));
        spriteCompiledEffect_->SetTechnique(0);

        GraphicsDevice& graphicsDevice = customEffect_->getGraphicsDeviceInternal();
        CompiledEffectDeviceState deviceState;
        deviceState.blend = &graphicsDevice.getBlendStateProperty();
        deviceState.depthStencil = &graphicsDevice.getDepthStencilStateProperty();
        deviceState.rasterizer = &graphicsDevice.getRasterizerStateProperty();
        deviceState.samplerStates = &graphicsDevice.getSamplerStatesProperty();
        deviceState.vertexSamplerStates = &graphicsDevice.getVertexSamplerStatesProperty();
        CompiledEffectPassStateChanges ignoredChanges;
        spriteCompiledEffect_->ApplyPass(0, deviceState, ignoredChanges);
    }

    void EasyGLSpriteBatchRenderer::FlushBatchWithCompiledEffect()
    {
        // FNA applies its stock SpriteEffect before every custom-effect batch. A custom pass may
        // assign only a pixel shader, as Microsoft's SpriteEffects sample does; Direct3D then
        // retains the stock vertex shader and its MatrixTransform. EasyGL reproduces that shader
        // inheritance through the same compiled XNA SpriteEffect before applying the custom pass.
        ICompiledEffectRuntime* runtime = customEffect_->GetCompiledRuntimePtr();
        if (graphicsRenderer_ == nullptr || runtime == nullptr || current_texture_ == nullptr)
        {
            pending_vertices_.clear();
            pending_indices_.clear();
            current_texture_ = nullptr;
            return;
        }

        // The sprite vertex is the one this renderer builds above: two floats of position, two of
        // texture coordinate and four of colour, tightly packed.
        static const VertexDeclaration kSpriteDeclaration(
            static_cast<int>(sizeof(Vertex)),
            {
                VertexElement(static_cast<int>(offsetof(Vertex, x)),
                              VertexElementFormat::Vector2, VertexElementUsage::Position, 0),
                VertexElement(static_cast<int>(offsetof(Vertex, u)),
                              VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(static_cast<int>(offsetof(Vertex, r)),
                              VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
            });

        const int vertexCount = static_cast<int>(pending_vertices_.size());
        const int indexCount = static_cast<int>(pending_indices_.size());

        // plans/plan_fx.md FX-120: these two buffers are RETAINED, not created per flush.
        //
        // The compiled route records them in a single long-lived vertex array object
        // (EnsureCompiledEffectVaoEXT), so a buffer created and destroyed inside one flush
        // leaves that array object holding a deleted name -- for the element buffer, which is
        // part of a VAO's own state, that name is what the next flush's draw reads. Desktop GL
        // tolerates it and draws; WebGL 2 validates the binding and refuses the whole draw with
        // "glDrawElements: Insufficient buffer size", so on WEBGL2 the first flush of a batch
        // drew and every later one silently produced nothing.
        //
        // Keeping them alive also removes two buffer creations and two deletions from every
        // flush of every compiled-effect sprite batch.
        if (compiledSpriteVertexBuffer_ == nullptr)
        {
            compiledSpriteVertexBuffer_ = graphicsRenderer_->CreateVertexBuffer(vertexCount);
            compiledSpriteVertexBuffer_->SetVertexDeclaration(kSpriteDeclaration);
        }
        if (compiledSpriteIndexBuffer_ == nullptr)
        {
            compiledSpriteIndexBuffer_ = graphicsRenderer_->CreateIndexBuffer16(indexCount);
        }
        compiledSpriteVertexBuffer_->SetData(pending_vertices_.data(), vertexCount,
                                             sizeof(Vertex));
        compiledSpriteIndexBuffer_->SetData16(pending_indices_.data(), indexCount);
        auto* easyVertexBuffer =
            static_cast<EasyGLVertexBufferRenderer*>(compiledSpriteVertexBuffer_.get());
        auto* easyIndexBuffer =
            static_cast<EasyGLIndexBufferRenderer*>(compiledSpriteIndexBuffer_.get());

        // The viewport is still this renderer's own business: a batch drawn into a RenderTarget2D
        // rasterizes at the target's size, not the window's, whatever shader runs.
        int logicalWidth = 0;
        int logicalHeight = 0;
        int rtW = 0, rtH = 0;
        if (graphicsRenderer_->GetCurrentRenderTarget2DSize(rtW, rtH) && rtW > 0 && rtH > 0)
        {
            device_.set_viewport(0, 0, rtW, rtH);
            logicalWidth = rtW;
            logicalHeight = rtH;
        }
        else
        {
            int physW = 0, physH = 0;
            graphicsRenderer_->getPhysicalSize(physW, physH);
            if (physW > 0 && physH > 0) device_.set_viewport(0, 0, physW, physH);
            graphicsRenderer_->getLogicalSize(logicalWidth, logicalHeight);
        }
        graphicsRenderer_->ApplySamplerState(0, pendingFilter_, pendingAddressU_,
                                             pendingAddressV_, 1);

        EasyGLRenderer::CompiledEffectStreamEXT stream;
        stream.buffer = easyVertexBuffer;
        stream.stride = sizeof(Vertex);

        // FNA draws the batch once per pass of the effect's current technique, applying each pass
        // and then overwriting Textures[0] with the drawn texture. Both are reproduced here.
        EffectTechnique* technique = customEffect_->getCurrentTechniqueProperty();
        const int passCount =
            technique != nullptr ? technique->getPassesProperty().getCountProperty() : 0;
        if (passCount == 0)
        {
            throw System::InvalidOperationException(
                "CNA EasyGL: a compiled Effect used with SpriteBatch must have a current "
                "technique with at least one pass.");
        }
        ApplyCompiledSpriteVertexShader(logicalWidth, logicalHeight);
        ::easygl::VertexArray& vao = graphicsRenderer_->EnsureCompiledEffectVaoEXT();
        const TextureCollection& deviceTextures =
            customEffect_->getGraphicsDeviceInternal().getTexturesProperty();
        for (int pass = 0; pass < passCount; ++pass)
        {
            technique->getPassesProperty()[pass].Apply();
            vao.bind();
            graphicsRenderer_->BindCompiledEffectForDrawEXT(&stream, 1, *runtime,
                                                            current_texture_, &deviceTextures);
            easyIndexBuffer->ibo.bind(::easygl::BufferTarget::ElementArray);
            device_.draw_elements(::easygl::PrimitiveType::Triangles, indexCount,
                                  ::easygl::DataType::UnsignedShort, nullptr);
            vao.unbind();
        }

        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
    }
#endif  // CNA_EASYGL_COMPILED_EFFECTS

    void EasyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle((int)x, (int)y, w, h), Rectangle(0, 0, w, h),
             Microsoft::Xna::Framework::Color::White);
    }

    void EasyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void EasyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        Draw(texture,
             static_cast<float>(destinationRectangle.X),
             static_cast<float>(destinationRectangle.Y),
             static_cast<float>(destinationRectangle.Width),
             static_cast<float>(destinationRectangle.Height),
             sourceRectangle, color, rotation, origin, effects, layerDepth);
    }

    // The sub-pixel destination overload is the one that actually builds the quad: XNA and FNA
    // keep a sprite's destination unrounded, so a sprite drawn at a fractional position lands
    // between pixels and the active sampler filters its edges. Rounding here would flatten that
    // to whole pixels and change what every 2D sample looks like.
    void EasyGLSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                        float destinationX,
                                        float destinationY,
                                        float destinationWidth,
                                        float destinationHeight,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        if (!begun) throw std::runtime_error("Draw called before Begin()");

        // Flush pending batch if texture changes
        if (current_texture_ != &texture)
        {
            if (current_texture_ != nullptr) FlushBatch();
            current_texture_ = &texture;
            // REMED-GFX-147: resolved once per bound source rather than once per sprite -- a
            // batch is by construction one texture, so this is a binding-time decision.
            ResolveCurrentTextureRowOrder();
        }

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp here — matches FNA, which divides straight through with no clamping
        // (SpriteBatch.cs, e.g. the Draw(..., Rectangle? sourceRectangle, ...) overloads).
        // A sourceRectangle that extends past the texture bounds intentionally produces UVs
        // outside [0,1], letting the bound SamplerState's TextureAddressMode (Wrap/Mirror/Clamp)
        // govern edge sampling — the classic XNA scrolling/tiling-background technique.
        float u1 = (float)sourceRectangle.X / texW;
        float v1 = (float)sourceRectangle.Y / texH;
        float u2 = (float)(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = (float)(sourceRectangle.Y + sourceRectangle.Height) / texH;

        if ((int)effects & (int)SpriteEffects::FlipHorizontally) std::swap(u1, u2);
        if ((int)effects & (int)SpriteEffects::FlipVertically) std::swap(v1, v2);

        // REMED-GFX-147: a render target's GL texel memory is bottom-up (see
        // SampledRowOrderIsBottomUp), so map each V to its mirror. Applied to the sprite's own
        // CPU-generated quad rather than in the sprite shader, because SpriteBatch::Begin may
        // substitute an arbitrary user ShaderEffect whose GLSL this renderer does not own -- the
        // vertex data is the only place both the built-in and the custom program read from.
        //
        // This composes with SpriteEffects rather than cancelling it: the mapping is per-component,
        // so it commutes with FlipVertically's swap and the two remain separate transforms. It also
        // survives a sourceRectangle that runs past the texture (FNA deliberately leaves those UVs
        // unclamped for Wrap/Mirror tiling), because 1-v is the correct inverse in the periodic
        // domain too.
        if (current_texture_bottom_up_)
        {
            v1 = 1.0f - v1;
            v2 = 1.0f - v2;
        }

        float r = (float)color.getRProperty() / 255.0f;
        float g = (float)color.getGProperty() / 255.0f;
        float b = (float)color.getBProperty() / 255.0f;
        float a = (float)color.getAProperty() / 255.0f;

        float dx = destinationX;
        float dy = destinationY;
        float dw = destinationWidth;
        float dh = destinationHeight;

        float sw = (float)sourceRectangle.Width;
        float sh = (float)sourceRectangle.Height;

        float ox = origin.X;
        float oy = origin.Y;

        float scaleX = dw / sw;
        float scaleY = dh / sh;

        float p0x = (0.0f - ox) * scaleX,  p0y = (0.0f - oy) * scaleY;
        float p1x = (sw   - ox) * scaleX,  p1y = (0.0f - oy) * scaleY;
        float p2x = (sw   - ox) * scaleX,  p2y = (sh   - oy) * scaleY;
        float p3x = (0.0f - ox) * scaleX,  p3y = (sh   - oy) * scaleY;

        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        const auto base = static_cast<uint16_t>(pending_vertices_.size());

        pending_vertices_.push_back({v0x, v0y, u1, v1, r, g, b, a});
        pending_vertices_.push_back({v1x, v1y, u2, v1, r, g, b, a});
        pending_vertices_.push_back({v2x, v2y, u2, v2, r, g, b, a});
        pending_vertices_.push_back({v3x, v3y, u1, v2, r, g, b, a});

        pending_indices_.push_back(base + 0);
        pending_indices_.push_back(base + 1);
        pending_indices_.push_back(base + 2);
        pending_indices_.push_back(base + 2);
        pending_indices_.push_back(base + 3);
        pending_indices_.push_back(base + 0);
    }

    // --- EasyGLRenderer ---

    // MERGE: next's platform-based construction, carrying P11's runtime profile. The profile is
    // threaded as a parameter because RequestedGlContext() runs in the initializer list, before the
    // body can publish it.
    EasyGLRenderer::EasyGLRenderer(
        const RendererSurfaceInfo& surface, CNA::Platform::IPlatformGlContext& glContext,
        const int virtualWidth, const int virtualHeight, const CnaPresentationMode mode,
        const bool contextRecoveryEnabled, const int multiSampleCount, const int swapInterval,
        const GlProfile profile)
        : platformContext_(std::make_unique<EasyGLPlatformContext>(
              glContext, RequireEasyGlWindowId(surface), RequestedGlContext(profile)))
        , surfaceState_(surface, virtualWidth, virtualHeight, mode)
        , contextRecoveryEnabled_(contextRecoveryEnabled)
        , sampleCount_(multiSampleCount > 1 ? multiSampleCount : 1)
    {
        // plans/plan_runtimerenderer.md P11: publish the profile before anything else runs -- the context
        // attributes, the shader adaptation and the API-generation checks all read it, and they run
        // from free helpers that have no other way to reach this instance.
        profile_ = profile;
        ActiveGlProfile() = profile;

        // MERGE: next guarded the blocks below with #if defined(CNA_GL_PROFILE_<X>). P11 made the
        // profile a RUNTIME value so all five identities can be compiled in at once, so each guard
        // becomes the equivalent runtime question about the profile in hand.
        if (ProfileIsEs2ApiGeneration())
        {
        // GLES 2.0 has no multisample renderbuffers and no blit to resolve them
        // (glRenderbufferStorageMultisample/glBlitFramebuffer are ES 3.0), so the requested
        // backbuffer MultiSampleCount preference degrades to single-sample -- the profile's real
        // ceiling. GetMultiSampleCount() then reports 0, keeping the applied count truthful.
            sampleCount_ = 1;
        }

        // plans/plan_glbackends.md GLB-8: context attributes depend on which of the 5 public GL
        // profiles this translation unit was compiled for (see cmake/RendererSelection.cmake).
        // OPENGLES3/WEBGL2 request GLES 3.0 (today's original, unchanged behavior); WEBGL1
        // requests GLES 2.0 (Emscripten maps this to a real WebGL 1 context); OPENGLES2 requests
        // the same GLES 2.0 attributes through the NATIVE (EGL/GLX) path -- the driver may
        // legally return any ES context backward-compatible with 2.0, which is the same
        // version-floor semantic every other profile's request already has; OPENGL33 requests a
        // desktop GL 3.3 core profile context instead of an ES profile.
        // RequestedGlContext() carries those version/profile requirements together with the
        // depth/stencil/double-buffer attributes to the platform before context creation.
        glProcAddressLoader = platformContext_->GetLoader();
        if (glProcAddressLoader == nullptr)
        {
            throw CNA::Platform::PlatformException(
                "EasyGLRenderer::LoadGl", "platform returned a null GL loader");
        }

        device.initialize(glProcAddressLoader);
        // WebGL commonly exposes only four rasterizer subpixel bits. Wine's usual 63/128-pixel
        // displacement rounds back to exactly half a pixel at that precision, putting XNA's 1x1
        // right triangles on an excluded fill edge again. Use the closest representable value
        // below half a pixel, capped at Wine's established correction on higher-precision GL.
        GLint subpixelBits = 0;
        metagl::glGetIntegerv(::metagl::GetParameter::SubpixelBits, &subpixelBits);
        if (subpixelBits > 1 && subpixelBits < 24)
        {
            const float representableBelowHalf =
                1.0f - std::ldexp(1.0f, 1 - subpixelBits);
            xnaPixelCenterScale_ = std::min(xnaPixelCenterScale_, representableBelowHalf);
        }
        if (ProfileIsDesktopCore())
            EnableVertexProgramPointSize();
        // Same reason as the capability dump below: a startup diagnostic goes to the logger (and
        // therefore stderr), never to the program's own stdout.
        CNA::Logger::Info(std::string("EasyGLRenderer initialized with OpenGL ")
                              + device.capabilities().context_info().version_string,
                          CNA::LogCategory::RENDER);

        // Task 456: one-time startup capability dump. Task 918 wired up real
        // GL_EXT_texture_filter_anisotropic support in ApplySamplerState(); report the real,
        // runtime-detected status here instead of a hardcoded claim.
        {
            GLint maxSamplesCap = 0;
            GLint maxDrawBuffers = 1;
            GLint maxColorAttachments = 1;
            if (ProfileIsEs2ApiGeneration())
            {
            // GLES 2.0 defines none of GL_MAX_SAMPLES / GL_MAX_DRAW_BUFFERS /
            // GL_MAX_COLOR_ATTACHMENTS (all ES 3.0) -- querying them on a strict ES 2.0 context
            // raises GL_INVALID_ENUM, and a driver that generously returned a
            // backward-compatible higher-version context would report ES 3.0 numbers this
            // profile must not act on. Pin the ES 2.0 truth instead: one sample, one color
            // attachment, no indexed color masks -- regardless of what the runtime context
            // could additionally do.
            maxSamplesCap = 1;
            maxMrtTargets_ = 1;
            supportsIndexedColorMasks_ = false;
            }
            else
            {
            metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamplesCap);
            metagl::glGetIntegerv(
                ::metagl::GetParameter::MaxDrawBuffers, &maxDrawBuffers);
            metagl::glGetIntegerv(
                ::metagl::GetParameter::MaxColorAttachments,
                &maxColorAttachments);
            maxMrtTargets_ = std::max(
                1, std::min({4, static_cast<int>(maxDrawBuffers),
                             static_cast<int>(maxColorAttachments)}));
            const auto& capabilities = device.capabilities();
            // WebGL 2 identifies as an ES 3.0-shaped API, but indexed colour masks are not part
            // of its core surface. The old two-way GLES/desktop test sent WebGL through the
            // desktop >= 3.0 branch and then called a null glColorMaski entry point during
            // GraphicsDevice construction. Require both the correct API/version contract and
            // the function that meta-gl actually loaded. Extension-backed WebGL support can be
            // enabled later when meta-gl maps the browser extension entry point explicitly.
            supportsIndexedColorMasks_ =
                !capabilities.is_webgl()
                && (capabilities.is_opengles()
                        ? capabilities.is_at_least(3, 2)
                        : capabilities.is_opengl() && capabilities.is_at_least(3, 0))
                && metagl::IsFunctionAvailable("glColorMaski");
            }
            const bool hasAniso = metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            GLfloat maxAnisoCap = 1.0f;
            if (hasAniso)
                metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
            // A startup diagnostic belongs on stderr, through the logger that honours log levels
            // -- stdout is the program's own output channel, and a library writing to it corrupts
            // anything that pipes a game's output. GraphicsDeviceRendererTest::
            // StartupDiagnosticNeverWritesToStdout pins that for the renderer-name line; this one
            // had been left on std::cout and broke it.
            std::ostringstream capabilityMessage;
            capabilityMessage
                      << "CNA: EasyGL capabilities -- MSAA up to " << maxSamplesCap
                      << "x; MRT up to " << maxMrtTargets_
                      << " targets (GL draw buffers=" << maxDrawBuffers
                      << ", color attachments=" << maxColorAttachments
                      << ", CNA/FNA cap=4); indexed color masks: "
                      << (supportsIndexedColorMasks_ ? "supported" : "not supported")
                      << "; "
                         "anisotropic filtering: "
                      << (hasAniso ? ("supported (Task 918, up to " + std::to_string(static_cast<int>(maxAnisoCap)) + "x)")
                                   : std::string("NOT supported (falls back to trilinear)"))
                      << "; texture SurfaceFormat: Color"
                      << (ProfileIsEs2ApiGeneration()
                              ? " only"
                              : " + NormalizedByte4 (RGBA8_SNORM) + NormalizedByte2 (RG8_SNORM)")
                      // plans/plan_modern.md MOD-117: render targets are no longer Color-only, and the
                      // answer is driver-dependent, so it is probed rather than asserted.
                      << "; render-target SurfaceFormat: Color"
                      << (ProbeFloatRenderTargetSupportEXT(false) ? " + half-float (RGBA16F)" : "")
                      << (ProbeFloatRenderTargetSupportEXT(true) ? " + float (RGBA32F)" : "");
            CNA::Logger::Info(capabilityMessage.str(), CNA::LogCategory::RENDER);
        }

        platformContext_->SetSwapInterval(swapInterval);

        registry_->register_with_meta_gl();

        if (sampleCount_ > 1)
        {
            int physW, physH;
            surfaceState_.GetDrawableSize(physW, physH);
            CreateMsaaBuffers(physW, physH);
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }

#if defined(__EMSCRIPTEN__)
        metagl::InstallEmscriptenContextLossCallbacks();
#endif

        // Register last, after every fallible step above has succeeded (matches
        // WebGPU/Canvas/SdlGpu) -- a constructor that throws never runs its destructor, so
        // registering earlier would leave a dangling entry in IGraphicsRenderer's static window
        // registry, later dereferenced unconditionally by SdlInputBridge.cpp/Mouse.cpp.
        IGraphicsRenderer::RegisterForWindow(surfaceState_.GetWindowId(), this);
    }

    void EasyGLRenderer::CreateMsaaBuffers(int w, int h)
    {
        // Clamp to GL_MAX_SAMPLES so glRenderbufferStorageMultisample never errors.
        GLint maxSamples = 0;
        metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamples);
        if (maxSamples > 0 && sampleCount_ > static_cast<int>(maxSamples))
            sampleCount_ = static_cast<int>(maxSamples);

        msaaW_ = w; msaaH_ = h;
        if (!msaaFbo_.is_created()) msaaFbo_.create();
        if (!msaaColorRbo_.is_created()) msaaColorRbo_.create();
        if (!msaaDepthRbo_.is_created()) msaaDepthRbo_.create();

        msaaColorRbo_.bind();
        msaaColorRbo_.set_storage_multisample(sampleCount_,
                                               ::metagl::InternalFormat::Rgba8, w, h);
        msaaDepthRbo_.bind();
        msaaDepthRbo_.set_storage_multisample(sampleCount_,
                                               ::metagl::InternalFormat::DepthComponent24, w, h);

        msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        msaaFbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                      msaaColorRbo_);
        msaaFbo_.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                      ::metagl::FramebufferAttachment::Depth,
                                      msaaDepthRbo_);
    }

    void EasyGLRenderer::BindDefaultFramebuffer()
    {
        if (sampleCount_ > 1)
        {
            // Recreate MSAA FBO if the window was resized.
            int physW, physH;
            surfaceState_.GetDrawableSize(physW, physH);
            if (physW != msaaW_ || physH != msaaH_)
                CreateMsaaBuffers(physW, physH);

            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
        }
        else
        {
            ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::Framebuffer);
        }
    }

    void EasyGLRenderer::ResolveMsaa()
    {
        if (sampleCount_ <= 1) return;
        // Blit colour attachment from MSAA FBO to default framebuffer (FBO 0).
        msaaFbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::DrawFramebuffer);
        ::easygl::Framebuffer::blit(0, 0, msaaW_, msaaH_,
                                     0, 0, msaaW_, msaaH_,
                                     ::metagl::ClearBufferBit::Color,
                                     ::metagl::BlitFilter::Nearest);
    }

    EasyGLRenderer::~EasyGLRenderer()
    {
        // REMED-GFX-168: nothing to unwind for the binding record here. `bound_` is the record's only
        // owner, so its own member destruction is what expires every surviving render target's
        // weak_ptr -- a target that outlives this renderer then detaches from nothing instead of
        // writing into freed storage. Deliberately not reset early either: the record staying valid
        // for the whole of this destructor keeps a target destroyed DURING teardown on the ordinary
        // detach path. (Neither order is reachable through CNA's own Game harness, where
        // GraphicsDevice_ is a Game base member destroyed after every subclass member; a globally
        // held render target reaches the first one, which is why the ownership is weak at all.)
        IGraphicsRenderer::UnregisterForWindow(surfaceState_.GetWindowId());
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // Must run here, in the destructor body, rather than relying on member destruction order:
        // mojoShaderContext_ is a raw pointer (no destructor of its own) and needs the GL context
        // still current, which platformContext_ (destroyed after this body returns) still owns.
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_glMakeContextCurrent(nullptr);
            MOJOSHADER_glDestroyContext(mojoShaderContext_);
            mojoShaderContext_ = nullptr;
        }
#endif
        // platformContext_ is the first-declared member and therefore dies last, after every GL
        // resource member has released while the context is still current.
    }

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
    CNA::Platform::GlProcAddressLoader EasyGLRenderer::GetProcAddressLoaderEXT() const
    {
        return platformContext_->GetLoader();
    }
#endif

    bool EasyGLRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            {
if (ProfileIsEs2ApiGeneration())
{
                // GLES 2.0 has no multisample renderbuffers/blit (both ES 3.0), and GL_MAX_SAMPLES
                // itself is undefined there -- reported false regardless of what a generously
                // higher-versioned runtime context could do, matching the profile's forced
                // single-sample surfaces.
                return false;
}
else
{
                GLint maxSamplesCap = 0;
                metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamplesCap);
                return maxSamplesCap > 1;
}
            }
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            case CNA::GraphicsCapability::WireFrame:
                // REMED-GFX-219 resolved: this renderer's GL_LINES re-expansion renders a genuinely
                // correct wireframe (shared pixel oracle: interior 0/1089, all three triangle edges
                // present), so the previous `false` under-stated the implementation. The emulation
                // draws line primitives and depends on no polygon-mode API, so it holds for every
                // GL profile (OPENGLES3/OPENGL33/WEBGL1/WEBGL2) alike.
if (ProfileIsEs2ApiGeneration())
{
                // ...with one ES 2.0 nuance: the re-expanded line indices are 32-bit, and
                // GL_UNSIGNED_INT element indices are an extension there (core in ES 3.0), so the
                // report is conditional on the runtime genuinely providing it.
                return metagl::HasExtension("GL_OES_element_index_uint");
}
else
{
                return true;
}
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: implemented -- Draw*PrimitivesEx binds every per-vertex stream
                // into the VAO at locations continuing after the previous stream's, each with its
                // own VBO, stride and byte offset, and restores the single-stream layout after.
if (ProfileUsesGlslEs100())
{
                // WebGL 1 / GLES 2.0 lack the attrib-divisor entry points the Ex routes bind
                // through; claiming support would fail inside GL instead of being refused up front.
                return false;
}
else
{
                return true;
}
            case CNA::GraphicsCapability::MultipleRenderTargets:
if (ProfileUsesGlslEs100())
{
                // WebGL 1 / GLES 2.0 core have no draw-buffers MRT.
                return false;
}
else
{
                return true;
}
            case CNA::GraphicsCapability::OcclusionQuery:
if (ProfileUsesGlslEs100())
{
                // WebGL 1 / GLES 2.0 have no query objects.
                return false;
}
else
{
                return true;
}
            case CNA::GraphicsCapability::Texture3D:
if (ProfileUsesGlslEs100())
{
                // WebGL 1 / GLES 2.0 have no 3D textures at all.
                return false;
}
else
{
                return true;
}
            case CNA::GraphicsCapability::Instancing:
if (ProfileUsesGlslEs100())
{
                // WebGL 1 / GLES 2.0 core have no glDrawElementsInstanced/glVertexAttribDivisor.
                return false;
}
else
{
                return true;
}
            default:
                return true;
        }
    }

    void EasyGLRenderer::DebugSimulateContextLoss()
    {
#if defined(__EMSCRIPTEN__)
        CNA_DebugLoseWebGLContext();
        // The webglcontextlost canvas event fires asynchronously and triggers
        // metagl::NotifyContextLost() via InstallEmscriptenContextLossCallbacks().
#else
        std::cerr << "[CNA] Simulating desktop GL context loss + immediate recreate" << std::endl;

        // 1. Notify listeners that context is lost. ResourceRegistry calls
        //    release_gl_handle_only() on every tracked resource (zeros handles,
        //    no GL calls made). Context is still valid here for proper cleanup.
        metagl::NotifyContextLost();

if (ProfileIsEs2ApiGeneration())
{
        // Textures outside the recovery registry (e.g. plain cube maps) leave stale entries in
        // the ES 2.0 level registry on context loss, and the fresh context may re-issue their GL
        // names -- start the registry empty; recreate_gl_resource() below re-registers every
        // recoverable texture with its fresh name.
        Es2TextureLevelCounts().clear();
}

        // 3D programs are recreated lazily by their Ensure* helpers.
        // Reset all handles so create() allocates fresh programs.
        prog_colored_.reset_no_gl();
        prog_textured_.reset_no_gl();
        prog_col_textured_.reset_no_gl();
        prog_lit_textured_.reset_no_gl();
        prog_lit_textured_vertexlit_.reset_no_gl();
        prog_dual_textured_.reset_no_gl();
        prog_dual_textured_colored_.reset_no_gl();
        prog_env_mapped_.reset_no_gl();
        prog_skinned_.reset_no_gl();
        prog_skinned_vertexlit_.reset_no_gl();
        prog_pbr_.reset_no_gl();
        prog_pbr_dual_uv_.reset_no_gl();
        prog_pbr_skinned_.reset_no_gl();
        prog_pbr_skinned_dual_uv_.reset_no_gl();
        default_white_texture_.reset_handle_no_gl();
        default_white_texture_ready_ = false;
        default_flat_normal_texture_.reset_handle_no_gl();
        default_flat_normal_texture_ready_ = false;
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-108: the compiled-effect route's own GL objects live on this renderer
        // rather than in the recovery registry (they are not `RecoverableResource`s, because they
        // hold nothing to recreate CONTENT from -- a fresh empty array object and a fresh scratch
        // copy are as good as the originals). They still have to be forgotten here, or their
        // pre-loss names would be bound into the new context: `compiledEffectVaoCreated_` stayed
        // true across a recreation, so `EnsureCompiledEffectVaoEXT` handed back a dead name and
        // every compiled draw bound array object 0.
        ReleaseCompiledEffectGlObjectsForContextLossEXT();
#endif

        // 2. Recreate the native context through the platform-owned transaction.
        platformContext_->Recreate();

        // 3. Reload GL function pointers and increment context generation. The fresh context
        // brings a fresh loader, so re-take it from the platform rather than reusing the one
        // that belonged to the destroyed context.
        glProcAddressLoader = platformContext_->GetLoader();
        if (glProcAddressLoader == nullptr)
        {
            throw CNA::Platform::PlatformException(
                "EasyGLRenderer::LoadGl", "platform returned a null GL loader");
        }
        // `easygl::Device` is already initialized and its initialize() method is deliberately
        // one-shot, so calling it here returns without touching meta-gl. Context loss invalidated
        // meta-gl's function table above; the next GL wrapper would consequently call
        // std::terminate(). Reload the context-facing table directly while retaining Device's
        // context-independent facade.
        if (!metagl::LoadCurrentContext(
                reinterpret_cast<metagl::GlGetProcAddressFn>(glProcAddressLoader)))
        {
            throw std::runtime_error(
                "meta-gl failed to reload GL entry points after debug context loss");
        }
        if (ProfileIsDesktopCore())
            EnableVertexProgramPointSize();

        // 4. Notify listeners that context is restored. ResourceRegistry calls
        //    recreate_gl_resource() on every tracked resource (shaders, textures, buffers, VAOs).
        metagl::NotifyContextRestored();

        std::cerr << "[CNA] Desktop GL context recreated and all resources restored" << std::endl;
#endif
    }

    void EasyGLRenderer::DebugRestoreContext()
    {
#if defined(__EMSCRIPTEN__)
        CNA_DebugRestoreWebGLContext();
        // The webglcontextrestored canvas event fires asynchronously and triggers
        // metagl::NotifyContextRestored() via InstallEmscriptenContextLossCallbacks().
#else
        // On desktop, loss+restore is a single atomic operation.
        DebugSimulateContextLoss();
#endif
    }

    void EasyGLRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (metagl::IsContextLost())
            throw std::runtime_error("ReadBackbuffer: GL context is lost");

        // On the default framebuffer (no render target bound), explicitly select
        // GL_BACK as the read source.  EGL/GLES3 contexts do not guarantee that
        // the read buffer defaults to GL_BACK, so skipping this call can leave
        // the read buffer pointing at GL_NONE and glReadPixels returns zeros.
        // When a render-target FBO is bound, the read buffer is already
        // GL_COLOR_ATTACHMENT0, so no explicit call is needed there.
        if (bound_->height == 0)
        {
            if (sampleCount_ > 1)
            {
                // Resolve MSAA FBO to FBO 0 so glReadPixels can sample the single-sample copy.
                ResolveMsaa();
                // Bind FBO 0 as the read source and select GL_BACK.
                ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::ReadFramebuffer);
            }
if (!ProfileIsEs2ApiGeneration())
{
            // GLES 2.0 has no glReadBuffer at all -- there the default framebuffer's color buffer
            // is the one and only read source, so the explicit GL_BACK selection this comment
            // block describes for EGL/GLES3 contexts neither exists nor is needed.
            device.set_read_buffer(::easygl::ReadBuffer::Back);
}
        }

        // Use the render-target's own height for the Y-flip when an RT is bound;
        // fall back to the window/viewport height for the default framebuffer.
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int vpW;
            GetViewportSize(vpW, fbH);
        }

        // OpenGL origin is bottom-left; flip y so caller gets top-left origin.
        const int glY = fbH - y - h;

        device.read_pixels(x, glY, w, h, ::metagl::PixelFormat::Rgba,
                           ::metagl::PixelType::UnsignedByte, pixels);

        // Flip rows vertically (GL returned bottom-up, XNA expects top-down).
        const int rowBytes = w * 4;
        std::vector<uint8_t> tmp(rowBytes);
        for (int i = 0; i < h / 2; ++i)
        {
            uint8_t* top = pixels + i * rowBytes;
            uint8_t* bot = pixels + (h - 1 - i) * rowBytes;
            std::copy(top, top + rowBytes, tmp.data());
            std::copy(bot, bot + rowBytes, top);
            std::copy(tmp.begin(), tmp.end(), bot);
        }

        // After reading from FBO 0, restore the MSAA FBO as the draw target.
        if (sampleCount_ > 1 && bound_->height == 0)
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderer::Clear(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        // Task 880: glClear() is viewport-independent (only the scissor rect, if enabled, can
        // narrow it) -- no longer hardcodes the viewport to the full window here, so a
        // previously-set custom Viewport (Task 880) survives across Clear() instead of being
        // silently reset to full size as a side effect.
        device.set_clear_color(r, g, b, a);
        // REMED-GFX-077: XNA Clear() clears all channels regardless of BlendState.ColorWriteChannels,
        // but glClear respects glColorMask — so neutralise a non-default mask across the clear, then
        // restore it. No-op fast path when the mask is the default All.
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        // REMED-GFX-142: COLOUR ONLY. This is the renderer entry point GraphicsDevice::Clear routes
        // a bare ClearOptions::Target to, and it used to add ClearFlags::Depth -- so asking XNA to
        // clear only the colour target silently wiped the depth buffer with it, on the one renderer
        // that did this. Every clear that is meant to include depth has its own entry point
        // (ClearColorAndDepth, ClearColorDepthAndStencil, ...), including the single-argument
        // public overload, which GraphicsDevice::Clear(const Color&) expands to
        // Target | DepthBuffer | Stencil precisely so this one does not have to guess.
        device.clear(::easygl::ClearFlags::Color);
        if (maskActive) ApplyCurrentColorWriteMasks();
    }

    void EasyGLRenderer::Present()
    {
        if (metagl::IsContextLost()) return;
        if (sampleCount_ > 1)
            ResolveMsaa();
        platformContext_->SwapBuffers();
        if (sampleCount_ > 1)
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderer::SetVirtualResolution(int width, int height)
    {
        surfaceState_.SetVirtualResolution(width, height);
    }

    void EasyGLRenderer::SetPresentationMode(int mode)
    {
        surfaceState_.SetPresentationMode(static_cast<CnaPresentationMode>(mode));
    }

    void EasyGLRenderer::SetSwapInterval(int interval)
    {
        // Recorded as well as forwarded. Whether the driver HONOURS an interval is the driver's
        // business and a headless GL context routinely refuses to; whether CNA asked for it is
        // this renderer's, and is the half a test can check anywhere. REMED-GFX-243.
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    void EasyGLRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != surfaceState_.GetWindowId())
        {
            throw CNA::Platform::PlatformException(
                "EasyGLRenderer::OnSurfaceChanged",
                "a renderer's platform window identity cannot change");
        }
        surfaceState_.Update(surface);
    }

    void EasyGLRenderer::getLogicalSize(int& width, int& height) const
    {
        surfaceState_.GetLogicalSize(width, height);
    }

    void EasyGLRenderer::getPhysicalSize(int& width, int& height) const
    {
        surfaceState_.GetDrawableSize(width, height);
    }

    bool EasyGLRenderer::GetCurrentRenderTarget2DSize(int& width, int& height) const
    {
        if (!bound_->rt2D && bound_->mrtCount == 0) return false;
        width = bound_->width;
        height = bound_->height;
        return true;
    }

    bool EasyGLRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                          float& logX, float& logY) const
    {
        return surfaceState_.WindowToLogical(windowX, windowY, logX, logY);
    }

    bool EasyGLRenderer::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        return surfaceState_.LogicalToWindow(logX, logY, windowX, windowY);
    }

    void EasyGLRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    // See EasyGLSurfaceState::GetDefaultViewportRect() for why this override exists: the base
    // default would apply the LOGICAL size as the physical GL viewport, which for this renderer's
    // always-on virtual resolution is the wrong rectangle on any window that does not match the
    // virtual aspect.
    void EasyGLRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        surfaceState_.GetDefaultViewportRect(x, y, width, height);
    }

    std::unique_ptr<ITextureRenderer> EasyGLRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<EasyGLTextureRenderer>(data, RegistryPtr());
    }

    std::unique_ptr<ISpriteBatchRenderer> EasyGLRenderer::CreateSpriteBatch()
    {
        return std::make_unique<EasyGLSpriteBatchRenderer>(device, RegistryPtr(), this);
    }

    std::unique_ptr<IOcclusionQueryRenderer> EasyGLRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<EasyGLOcclusionQueryRenderer>(RegistryPtr());
    }

    std::unique_ptr<IRenderTargetRenderer> EasyGLRenderer::CreateRenderTarget2D(int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-168: `bound_` is handed over as a weak_ptr so the target can clear its own slot
        // when it is destroyed while still bound. Weak, not shared: a target must never keep this
        // renderer's binding record alive past the renderer itself.
        return std::make_unique<EasyGLRenderTargetRenderer>(w, h, depthFormat, RegistryPtr(), bound_,
                                                           mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetRenderer> EasyGLRenderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        // plans/plan_modern.md MOD-115: refuse rather than substitute. The shared default of this factory
        // drops the format and hands back a Color target, which is invisible to the caller; a
        // renderer that has genuinely implemented formats owes an honest answer instead, and
        // GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() is the way to ask in advance.
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) == RendererFormatVerdict::Unsupported)
        {
            throw std::runtime_error(
                "EasyGL: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as a render target on this GL context. Query "
                "GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() first.");
        }
        return std::make_unique<EasyGLRenderTargetRenderer>(w, h, depthFormat, RegistryPtr(), bound_,
                                                           mipMap, multiSampleCount, surfaceFormat);
    }

    RendererFormatVerdict EasyGLRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format == SurfaceFormat::Color)
            return RendererFormatVerdict::Supported;
        // Both signed-normalized byte formats need the ES 3 sized-internal-format set.
        if (format == SurfaceFormat::NormalizedByte4 || format == SurfaceFormat::NormalizedByte2)
        {
            return ProfileIsEs2ApiGeneration()
                ? RendererFormatVerdict::Unsupported
                : RendererFormatVerdict::Supported;
        }
        return RendererFormatVerdict::Defer;
    }

    RendererFormatVerdict EasyGLRenderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const SurfaceFormat format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format == SurfaceFormat::NormalizedByte4 || format == SurfaceFormat::NormalizedByte2)
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Defer;
    }

    RendererFormatVerdict EasyGLRenderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        // plans/plan_modern.md MOD-104/MOD-117. Color and the float formats are this renderer's own
        // answer; everything else defers to the framework rule, exactly as before this change --
        // widening the verdict beyond what CreateResources can actually allocate would put the
        // caller back in the "asked for one format, silently got another" position.
        RenderTargetColorStorage storage{};
        if (!MapRenderTargetColorFormat(surfaceFormat, storage))
            return RendererFormatVerdict::Defer;
        if (!storage.isFloat)
            return RendererFormatVerdict::Supported;   // Color: every profile, always.
        return ProbeFloatRenderTargetSupportEXT(storage.isFullFloat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    bool EasyGLRenderer::SupportsComputeShadersEXT() const
    {
        // The runtime context decides, not the compile-time profile: this renderer asks for ES 3.0
        // and Mesa routinely hands it 3.2. WebGL has no compute in any version, so it is excluded
        // by name rather than by version comparison.
        const auto& capabilities = device.capabilities();
        if (capabilities.is_webgl()) return false;
        if (capabilities.is_opengles()) return capabilities.is_at_least(3, 1);
        if (capabilities.is_opengl()) return capabilities.is_at_least(4, 3);
        return false;
    }

    bool EasyGLRenderer::SupportsIndirectDrawEXT() const
    {
        // Same API generation as compute, asked as its own question: ES 3.1 and desktop GL 4.0 both
        // have glDrawArraysIndirect, and WebGL has it in no version -- WebGL 2 is ES 3.0 and
        // WebGL 2 compute never shipped. The desktop floor is 4.0 rather than compute's 4.3,
        // because indirect draws arrived three versions earlier than compute shaders did; a context
        // between the two really can draw indirectly and not dispatch, and reporting the truth
        // there costs nothing.
        const auto& capabilities = device.capabilities();
        if (capabilities.is_webgl()) return false;
        if (capabilities.is_opengles()) return capabilities.is_at_least(3, 1);
        if (capabilities.is_opengl()) return capabilities.is_at_least(4, 0);
        return false;
    }

    bool EasyGLRenderer::SupportsGpuTimerEXT() const
    {
        // Two different answers for two different APIs. Desktop GL has GL_TIME_ELAPSED in core from
        // 3.3 (ARB_timer_query); ES has it only through GL_EXT_disjoint_timer_query, which many
        // drivers -- software rasterisers in particular -- simply do not ship. Where it is absent
        // the answer is false, and CNA::Graphics::GpuTimer refuses rather than handing back a CPU
        // clock reading: the whole reason to measure on the GPU is that the CPU number is the time
        // the driver took to *accept* the work.
        const auto& capabilities = device.capabilities();
        if (capabilities.is_webgl()) return false;
        if (capabilities.is_opengles())
            return ::metagl::HasExtension("GL_EXT_disjoint_timer_query");
        if (capabilities.is_opengl())
            return capabilities.is_at_least(3, 3)
                || ::metagl::HasExtension("GL_ARB_timer_query")
                || ::metagl::HasExtension("GL_EXT_timer_query");
        return false;
    }

    std::unique_ptr<IGpuTimerRenderer> EasyGLRenderer::CreateGpuTimerEXT()
    {
        if (!SupportsGpuTimerEXT()) return nullptr;
        return std::make_unique<EasyGLGpuTimerRenderer>(RegistryPtr());
    }

    bool EasyGLRenderer::SupportsComputeImageBindingEXT() const
    {
        // Desktop GL only, and not because ES lacks the entry point: ES 3.1 requires the texture
        // bound as an image to be immutable (glTexStorage2D), and every CNA texture is allocated
        // with glTexImage2D. Changing that is a change to the texture path every draw in the engine
        // goes through, so this reports the truth instead -- and CNA::Graphics::ComputeShader
        // refuses the binding rather than issuing one the driver will reject silently.
        return SupportsComputeShadersEXT() && device.capabilities().is_opengl();
    }

    int EasyGLRenderer::GetMaxComputeWorkGroupCountEXT(const int axis) const
    {
        if (!SupportsComputeShadersEXT() || axis < 0 || axis > 2) return 0;
        GLint value = 0;
        ::metagl::glGetIntegeri_v(::metagl::GetParameter::MaxComputeWorkGroupCount,
                                  static_cast<GLuint>(axis), &value);
        return static_cast<int>(value);
    }

    int EasyGLRenderer::GetMaxComputeWorkGroupSizeEXT(const int axis) const
    {
        if (!SupportsComputeShadersEXT() || axis < 0 || axis > 2) return 0;
        GLint value = 0;
        ::metagl::glGetIntegeri_v(::metagl::GetParameter::MaxComputeWorkGroupSize,
                                  static_cast<GLuint>(axis), &value);
        return static_cast<int>(value);
    }

    int EasyGLRenderer::GetMaxComputeWorkGroupInvocationsEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        GLint value = 0;
        ::metagl::glGetIntegerv(::metagl::GetParameter::MaxComputeWorkGroupInvocations, &value);
        return static_cast<int>(value);
    }

    int EasyGLRenderer::GetMaxVertexShaderStorageBlocksEXT() const
    {
        // Asked of the driver rather than inferred: ES 3.1's own minimum for this limit is zero, so
        // a context can implement compute in full and still be unable to read a storage buffer from
        // a vertex shader. Desktop GL 4.3 guarantees at least 8, but the query costs nothing and
        // answering it the same way everywhere is one fewer profile branch.
        if (!SupportsComputeShadersEXT()) return 0;
        GLint value = 0;
        ::metagl::glGetIntegerv(::metagl::GetParameter::MaxVertexShaderStorageBlocks, &value);
        return static_cast<int>(value);
    }

    void EasyGLRenderer::BindStorageBufferForDrawEXT(const int binding,
                                                     const IStorageBufferRenderer& buffer)
    {
        if (!SupportsComputeShadersEXT() || binding < 0) return;
        // glBindBufferBase's shader-storage binding points are context state shared by every stage,
        // so the same call that feeds a dispatch feeds a draw; nothing about the buffer changes.
        static_cast<const EasyGLStorageBufferRenderer&>(buffer).BindBase(binding);
    }

    std::unique_ptr<IComputeShaderRenderer> EasyGLRenderer::CreateComputeShader(
        const std::string& computeSrc)
    {
        if (!SupportsComputeShadersEXT()) return nullptr;
        auto shader = std::make_unique<EasyGLComputeShaderRenderer>();
        // A failed compile still returns the object: its GetCompileError() is the diagnostic the
        // caller needs, and returning null here would throw that log away.
        (void)shader->CompileProgram(computeSrc);
        return shader;
    }

    std::unique_ptr<IStorageBufferRenderer> EasyGLRenderer::CreateStorageBuffer(
        const std::size_t byteSize)
    {
        if (!SupportsComputeShadersEXT() || byteSize == 0) return nullptr;
        return std::make_unique<EasyGLStorageBufferRenderer>(byteSize);
    }

    void EasyGLRenderer::DispatchCompute(IComputeShaderRenderer* shader, const int groupsX,
                                         const int groupsY, const int groupsZ)
    {
        if (shader == nullptr || !shader->IsValid()) return;
        if (groupsX <= 0 || groupsY <= 0 || groupsZ <= 0) return;
        shader->Bind();
        device.dispatch_compute(static_cast<unsigned int>(groupsX),
                                static_cast<unsigned int>(groupsY),
                                static_cast<unsigned int>(groupsZ));
    }

    void EasyGLRenderer::MemoryBarrierEXT(const int barrierBits)
    {
        if (!SupportsComputeShadersEXT() || barrierBits == 0) return;
        using CNA::GraphicsMemoryBarrier;
        const auto requested = static_cast<GraphicsMemoryBarrier>(barrierBits);
        // Translated bit by bit rather than passed through: the ordinals are CNA's own, and a
        // renderer that forwarded them raw would be depending on them happening to match GL's.
        GLbitfield native = 0;
        const auto add = [&](const GraphicsMemoryBarrier bit, const ::metagl::MemoryBarrierMask gl) {
            if (CNA::HasBarrier(requested, bit)) native |= static_cast<GLbitfield>(gl);
        };
        add(GraphicsMemoryBarrier::VertexAttribArray, ::metagl::MemoryBarrierMask::VertexAttribArray);
        add(GraphicsMemoryBarrier::ElementArray, ::metagl::MemoryBarrierMask::ElementArray);
        add(GraphicsMemoryBarrier::Uniform, ::metagl::MemoryBarrierMask::Uniform);
        add(GraphicsMemoryBarrier::TextureFetch, ::metagl::MemoryBarrierMask::TextureFetch);
        add(GraphicsMemoryBarrier::ShaderImageAccess, ::metagl::MemoryBarrierMask::ShaderImageAccess);
        add(GraphicsMemoryBarrier::ShaderStorage, ::metagl::MemoryBarrierMask::ShaderStorage);
        add(GraphicsMemoryBarrier::BufferUpdate, ::metagl::MemoryBarrierMask::BufferUpdate);
        add(GraphicsMemoryBarrier::Framebuffer, ::metagl::MemoryBarrierMask::Framebuffer);
        add(GraphicsMemoryBarrier::IndirectCommand, ::metagl::MemoryBarrierMask::Command);
        if (native == 0) return;
        device.memory_barrier(static_cast<::metagl::MemoryBarrierMask>(native));
    }

    bool EasyGLRenderer::SupportsHalfFloatTextureLinearFilteringEXT() const
    {
        // Half-float texture filtering is core in the ES 3.0 API generation and in desktop GL 3.0+,
        // which is every profile this renderer builds for except the ES 2.0 generation. There it
        // would need GL_OES_texture_half_float_linear, and nothing in CNA asks for it, so the
        // honest answer is no rather than a probe for a path that is never taken.
        if (ProfileIsEs2ApiGeneration())
            return metagl::HasExtension("GL_OES_texture_half_float_linear");
        return true;
    }

    bool EasyGLRenderer::ProbeFloatRenderTargetSupportEXT(bool fullFloat) const
    {
        // MOD-117: probed, not inferred. Whether a float colour buffer is renderable depends on the
        // runtime context, not on the compile-time profile: ES 3.0 needs GL_EXT_color_buffer_float
        // (or _half_float for the 16-bit case), ES 3.2 has half-float in core, desktop GL has had
        // both since 3.0, and WebGL 2 gates them behind an extension the browser may not expose.
        // Rather than encode that matrix -- and be wrong on the driver that disagrees with it --
        // this creates a 1x1 attachment of the real format and asks GL whether the framebuffer is
        // complete. That is the same question CreateResources will ask for real, so the probe cannot
        // be optimistic about something that then fails.
        //
        // Cached because SupportsRenderTargetFormat() is a query a caller may make per frame.
        auto& cache = fullFloat ? probedFullFloatRenderable_ : probedHalfFloatRenderable_;
        if (cache.has_value())
            return *cache;

        if (ProfileIsEs2ApiGeneration())
        {
            // The float internal formats used here (R/RG/RGBA 16F/32F) are sized formats that do
            // not exist in the ES 2.0 / WebGL 1 API generation at all.
            cache = false;
            return false;
        }

        using ::Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        RenderTargetColorStorage storage{};
        MapRenderTargetColorFormat(static_cast<int>(fullFloat ? SurfaceFormat::Vector4
                                                              : SurfaceFormat::HdrBlendable),
                                   storage);

        int previousFbo = 0;
        metagl::glGetIntegerv(::metagl::GetParameter::FramebufferBinding, &previousFbo);

        ::easygl::Texture     probeTex;
        ::easygl::Framebuffer probeFbo;
        probeTex.create();
        probeTex.bind(::easygl::TextureTarget::Texture2D);
        probeTex.set_image_2d(::easygl::TextureTarget::Texture2D, 0, storage.internalFormat,
                              1, 1, storage.pixelFormat, storage.pixelType, nullptr);
        probeFbo.create();
        probeFbo.bind(::easygl::FramebufferTarget::Framebuffer);
        probeFbo.attach_texture_2d(::easygl::FramebufferTarget::Framebuffer,
                                   ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                                   ::easygl::TextureTarget::Texture2D, probeTex, 0);
        const bool complete =
            probeFbo.check_status(::easygl::FramebufferTarget::Framebuffer) ==
            ::metagl::FramebufferStatus::Complete;

        // Leave GL exactly as the probe found it -- this runs on demand, possibly mid-frame with a
        // render target bound, so restoring the previous binding is not optional.
        metagl::glBindFramebuffer(::metagl::FramebufferTarget::Framebuffer,
                                  ::metagl::FramebufferId{static_cast<unsigned int>(previousFbo)});
        // A refused format leaves a GL error queued behind it; drain it so the next real call is not
        // blamed for this one.
        DrainGlErrors();

        cache = complete;
        return complete;
    }

    std::unique_ptr<IRenderTargetCubeRenderer> EasyGLRenderer::CreateRenderTargetCube(int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // REMED-GFX-136: consumed by being deliberately unused, exactly like this renderer's own
        // CreateRenderTarget2D above. A GL framebuffer object's colour attachment IS the cube
        // texture and binding an FBO never touches its contents, so a single-sample face is
        // preserved by construction; the ONLY thing that clears one is an explicit glClear, which
        // is what GraphicsDevice::SetRenderTargets issues (and only issues) for a DiscardContents
        // target. REMED-GFX-141 makes that true of a MULTISAMPLED cube face too: each face now owns
        // its own multisample colour renderbuffer (EasyGLRenderTargetCubeRenderer::msaaColorRbos_),
        // so binding one still touches nothing and there is still no load action to carry a usage
        // decision.
        (void) preserveContents;
        // REMED-GFX-168: see CreateRenderTarget2D above for why the binding record is passed weakly.
        return std::make_unique<EasyGLRenderTargetCubeRenderer>(size, depthFormat, RegistryPtr(),
                                                               bound_, mipMap, multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> EasyGLRenderer::CreateRenderTargetCubeEXT(
        int size, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        (void)preserveContents;   // REMED-GFX-136: deliberately unused, see CreateRenderTargetCube.
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) == RendererFormatVerdict::Unsupported)
        {
            throw std::runtime_error(
                "EasyGL: SurfaceFormat ordinal " + std::to_string(surfaceFormat) +
                " is not supported as a cube render target on this GL context. Query "
                "GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT() first.");
        }
        return std::make_unique<EasyGLRenderTargetCubeRenderer>(
            size, depthFormat, RegistryPtr(), bound_, mipMap, multiSampleCount, surfaceFormat);
    }

    std::unique_ptr<ITexture3DRenderer> EasyGLRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<EasyGLTexture3DRenderer>(w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeRenderer> EasyGLRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<EasyGLTextureCubeRenderer>(size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IEffectRenderer> EasyGLRenderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<EasyGLEffectRenderer>();
        renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    /**
     * @brief REMED-GFX-168: the binding record, as pointer VALUES only.
     *
     * Never dereferences any of them -- the whole point of the trace is that one of these may already
     * be destroyed storage, which is exactly what the pre-fix `set2d.enter` line records.
     */
    std::string EasyGLRenderer::TraceBindingDetailEXT() const
    {
        std::ostringstream os;
        os << "cur2d=" << static_cast<const void*>(bound_->rt2D)
           << " curCube=" << static_cast<const void*>(bound_->cube)
           << " mrtCount=" << bound_->mrtCount
           << " curDim=" << bound_->width << 'x' << bound_->height;
        for (int i = 0; i < bound_->mrtCount; ++i)
            os << " mrt" << i << '=' << static_cast<const void*>(bound_->mrt[i]);
        return os.str();
    }

    void EasyGLRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        TargetTrace("set2d.enter", rt, TraceBindingDetailEXT());
        FinalizeCurrentMRT();
        // Regenerate mips (if requested) for whatever single RT/cube-face was previously
        // active, before switching away from it — see UnbindAsRenderTarget's Task 336 comment.
        if (bound_->rt2D && bound_->rt2D != rt) bound_->rt2D->UnbindAsRenderTarget();
        if (bound_->cube) bound_->cube->UnbindAsRenderTarget();
        bound_->cube = nullptr;
        bound_->rt2D   = rt;
        if (rt)
        {
            bound_->width = rt->GetWidth();
            bound_->height = rt->GetHeight();
            rt->BindAsRenderTarget();
        }
        else
        {
            bound_->width = 0;
            bound_->height = 0;
            BindDefaultFramebuffer();
        }
        TargetTrace("set2d.exit", rt, TraceBindingDetailEXT());
    }

    void EasyGLRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        TargetTrace("setcube.enter", rt, TraceBindingDetailEXT() + " reqFace=" + std::to_string(face));
        if (!rt) { SetRenderTarget2D(nullptr); return; }
        FinalizeCurrentMRT();
        if (bound_->rt2D) bound_->rt2D->UnbindAsRenderTarget();
        if (bound_->cube && bound_->cube != rt) bound_->cube->UnbindAsRenderTarget();
        bound_->rt2D   = nullptr;
        bound_->cube = rt;
        bound_->width = rt->GetSize();
        bound_->height = rt->GetSize();
        rt->BindAsRenderTargetFace(face);
        TargetTrace("setcube.exit", rt, TraceBindingDetailEXT());
    }

    void EasyGLRenderer::FinalizeCurrentMRT()
    {
        if (bound_->mrtCount <= 0) return;
        TargetTrace("mrt.finalize", this, TraceBindingDetailEXT());
        const int count = bound_->mrtCount;
        bound_->mrtCount = 0;
        bound_->mrtFramebuffer = 0;
        bound_->width = 0;
        bound_->height = 0;
        for (int i = 0; i < count; ++i)
        {
            EasyGLRenderTargetRenderer* target = bound_->mrt[i];
            bound_->mrt[i] = nullptr;
            if (target) target->UnbindAsRenderTarget();
        }
    }

    namespace
    {
        std::string DrainNativeGlErrors()
        {
            std::ostringstream result;
            for (int i = 0; i < 64; ++i)
            {
                const auto error = ::metagl::glGetError();
                if (error == ::metagl::ErrorCode::NoError) break;
                if (result.tellp() > 0) result << ", ";
                result << ::metagl::to_string(error) << "(0x"
                       << std::hex << std::uppercase
                       << static_cast<unsigned int>(error)
                       << std::dec << ")";
            }
            return result.str();
        }
    }

    void EasyGLRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (!renderTargets)
            throw std::invalid_argument(
                "EasyGL SetRenderTargets: nonzero count requires a binding array.");
        if (count == 1)
        {
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(
                    renderTargets[0].GetRenderTargetCube(),
                    renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }
        // Under the OPENGLES2 profile maxMrtTargets_ is pinned to 1 at construction (core ES 2.0
        // has no glDrawBuffers), so every multi-target set is refused right here through the
        // family's own established over-the-ceiling refusal -- the boundary this renderer's
        // lifecycle/diagnostic tests already record and catch (std::runtime_error).
        if (count > maxMrtTargets_)
            throw std::runtime_error(
                "EasyGL SetRenderTargets: requested " + std::to_string(count)
                + " targets, but the active GL profile supports "
                + std::to_string(maxMrtTargets_) + ".");

        std::array<EasyGLRenderTargetRenderer*, 4> targets{};
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "EasyGL SetRenderTargets: cube faces in a multi-target set are not "
                    "implemented by this CNA renderer.");
            targets[i] = dynamic_cast<EasyGLRenderTargetRenderer*>(
                renderTargets[i].GetRenderTarget2D());
            if (!targets[i])
                throw std::runtime_error(
                    "EasyGL SetRenderTargets: binding " + std::to_string(i)
                    + " is not an EasyGL RenderTarget2D.");
            if (targets[i]->GetWidth() != renderTargets[0].GetWidth()
                || targets[i]->GetHeight() != renderTargets[0].GetHeight())
                throw std::runtime_error(
                    "EasyGL SetRenderTargets: render targets must have matching dimensions.");
            if (targets[i]->GetMultiSampleCount()
                != renderTargets[0].GetAppliedMultiSampleCount())
                throw std::runtime_error(
                    "EasyGL SetRenderTargets: render targets must have matching applied "
                    "sample counts.");
            for (int previous = 0; previous < i; ++previous)
                if (targets[i] == targets[previous])
                    throw std::runtime_error(
                        "EasyGL SetRenderTargets: the same render target cannot occupy "
                        "multiple slots.");
        }
        if (!supportsIndexedColorMasks_)
        {
            for (int i = 1; i < count; ++i)
                if (currentColorWriteMasks_[i] != currentColorWriteMasks_[0])
                    throw std::runtime_error(
                        "EasyGL SetRenderTargets: this GL profile cannot express distinct "
                        "ColorWriteChannels values for MRT slots.");
        }

        const std::string errorsBeforeSetup = DrainNativeGlErrors();
        if (!errorsBeforeSetup.empty())
            throw std::runtime_error(
                "EasyGL SetRenderTargets: native GL errors were pending before MRT setup: "
                + errorsBeforeSetup);

        // Finalize every old destination before replacing the ordered set. This resolves each
        // multisample color buffer and regenerates each requested mip chain.
        FinalizeCurrentMRT();
        if (bound_->rt2D)   bound_->rt2D->UnbindAsRenderTarget();
        if (bound_->cube) bound_->cube->UnbindAsRenderTarget();
        bound_->rt2D   = nullptr;
        bound_->cube = nullptr;

        mrtFbo_.create();
        mrtFbo_.bind(::easygl::FramebufferTarget::Framebuffer);

        // Replace every possible attachment, including slots/depth owned by the previous set.
        // The FBO is reused, but its identity never stands in for ordered target-set identity.
        for (int i = 0; i < 4; ++i)
        {
            const auto color = static_cast<::metagl::ColorAttachment>(
                static_cast<GLenum>(::metagl::ColorAttachment::Color0)
                + static_cast<GLenum>(i));
            ::metagl::glFramebufferRenderbuffer(
                ::metagl::FramebufferTarget::Framebuffer,
                ::metagl::to_framebuffer_attachment(color),
                ::metagl::RenderbufferTarget::Renderbuffer,
                ::metagl::RenderbufferId{0});
        }
        for (const auto attachment : {
                 ::metagl::FramebufferAttachment::Depth,
                 ::metagl::FramebufferAttachment::Stencil,
                 ::metagl::FramebufferAttachment::DepthStencil})
        {
            ::metagl::glFramebufferRenderbuffer(
                ::metagl::FramebufferTarget::Framebuffer, attachment,
                ::metagl::RenderbufferTarget::Renderbuffer,
                ::metagl::RenderbufferId{0});
        }

        std::array<::easygl::DrawBuffer, 4> drawBuffers{};
        for (int i = 0; i < count; ++i)
        {
            const auto color = static_cast<::metagl::ColorAttachment>(
                static_cast<GLenum>(::metagl::ColorAttachment::Color0)
                + static_cast<GLenum>(i));
            const auto attachment = ::metagl::to_framebuffer_attachment(color);
            targets[i]->AttachColorToMRT(mrtFbo_, attachment);
            drawBuffers[i] = ::metagl::to_draw_buffer(color);
        }
        targets[0]->AttachDepthToMRT(mrtFbo_);
        mrtFbo_.set_draw_buffers(
            std::span<const ::easygl::DrawBuffer>(
                drawBuffers.data(), static_cast<std::size_t>(count)));
        mrtFbo_.set_read_buffer(
            ::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));

        const auto status = mrtFbo_.check_status();
        const std::string setupErrors = DrainNativeGlErrors();
        if (status != ::metagl::FramebufferStatus::Complete
            || !setupErrors.empty())
        {
            BindDefaultFramebuffer();
            bound_->width = 0;
            bound_->height = 0;
            throw std::runtime_error(
                "EasyGL SetRenderTargets: MRT framebuffer setup failed for "
                + std::to_string(count) + " targets; status="
                + std::string(::metagl::to_string(status))
                + "; GL errors="
                + (setupErrors.empty() ? std::string("none") : setupErrors));
        }

        bound_->mrt = targets;
        bound_->mrtCount = count;
        bound_->mrtFramebuffer = mrtFbo_.native_handle();
        bound_->width = renderTargets[0].GetWidth();
        bound_->height = renderTargets[0].GetHeight();
        ApplyCurrentColorWriteMasks();
        TargetTrace("mrt.set", this,
                    TraceBindingDetailEXT() + " mrtFbo=" + std::to_string(mrtFbo_.native_handle()));
    }

    namespace
    {
        // XNA Blend enum → easygl BlendFactor
        // Blend: One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
        //        InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
        //        DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
        //        InverseBlendFactor=11, SourceAlphaSaturation=12
        ::easygl::BlendFactor ToEasyGLBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
            case  1: return ::easygl::BlendFactor::Zero;
            case  2: return ::easygl::BlendFactor::SrcColor;
            case  3: return ::easygl::BlendFactor::OneMinusSrcColor;
            case  4: return ::easygl::BlendFactor::SrcAlpha;
            case  5: return ::easygl::BlendFactor::OneMinusSrcAlpha;
            case  6: return ::easygl::BlendFactor::DstColor;
            case  7: return ::easygl::BlendFactor::OneMinusDstColor;
            case  8: return ::easygl::BlendFactor::DstAlpha;
            case  9: return ::easygl::BlendFactor::OneMinusDstAlpha;
            case 10: return ::easygl::BlendFactor::ConstantColor;
            case 11: return ::easygl::BlendFactor::OneMinusConstantColor;
            case 12: return ::easygl::BlendFactor::SrcAlphaSaturate;
            default: return ::easygl::BlendFactor::One;  // Blend::One = 0
            }
        }

        // XNA BlendFunction enum → easygl BlendEquation
        // BlendFunction: Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4
        ::easygl::BlendEquation ToEasyGLBlendEquation(int xnaBlendFunc)
        {
            switch (xnaBlendFunc)
            {
            case 1: return ::easygl::BlendEquation::FuncSubtract;
            case 2: return ::easygl::BlendEquation::FuncReverseSubtract;
            case 3: return ::easygl::BlendEquation::Max;
            case 4: return ::easygl::BlendEquation::Min;
            default: return ::easygl::BlendEquation::FuncAdd;  // BlendFunction::Add = 0
            }
        }

        // XNA CompareFunction enum → easygl CompareFunc
        // CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
        //                  GreaterEqual=5, Greater=6, NotEqual=7
        ::easygl::CompareFunc ToEasyGLCompareFunc(int xnaCompare)
        {
            switch (xnaCompare)
            {
            case 1: return ::easygl::CompareFunc::Never;
            case 2: return ::easygl::CompareFunc::Less;
            case 3: return ::easygl::CompareFunc::Lequal;
            case 4: return ::easygl::CompareFunc::Equal;
            case 5: return ::easygl::CompareFunc::Gequal;
            case 6: return ::easygl::CompareFunc::Greater;
            case 7: return ::easygl::CompareFunc::Notequal;
            default: return ::easygl::CompareFunc::Always;  // CompareFunction::Always = 0
            }
        }

        // XNA StencilOperation ordinals: Keep=0, Zero=1, Replace=2, Increment=3,
        // Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7
        ::easygl::StencilOp ToEasyGLStencilOp(int xnaOp)
        {
            switch (xnaOp)
            {
            case 1: return ::easygl::StencilOp::Zero;
            case 2: return ::easygl::StencilOp::Replace;
            case 3: return ::easygl::StencilOp::IncrWrap;
            case 4: return ::easygl::StencilOp::DecrWrap;
            case 5: return ::easygl::StencilOp::Incr;
            case 6: return ::easygl::StencilOp::Decr;
            case 7: return ::easygl::StencilOp::Invert;
            default: return ::easygl::StencilOp::Keep;  // StencilOperation::Keep = 0
            }
        }

        ::easygl::PrimitiveType ToEasyGl(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return ::easygl::PrimitiveType::Triangles;
            case PrimitiveType::TriangleStrip: return ::easygl::PrimitiveType::TriangleStrip;
            case PrimitiveType::LineList:      return ::easygl::PrimitiveType::Lines;
            case PrimitiveType::LineStrip:     return ::easygl::PrimitiveType::LineStrip;
            case PrimitiveType::PointListEXT:  return ::easygl::PrimitiveType::Points;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }

        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:       return primitiveCount * 2;
            case PrimitiveType::LineStrip:      return primitiveCount + 1;
            case PrimitiveType::PointListEXT:   return primitiveCount;
            default:
                throw System::InvalidOperationException("Unrecognized primitive type!");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Graphics state
    // -------------------------------------------------------------------------

    void EasyGLRenderer::ApplyCurrentColorWriteMasks()
    {
        const int slot0 = currentColorWriteMasks_[0];
        device.set_color_mask(
            ColorWriteHasRed(slot0), ColorWriteHasGreen(slot0),
            ColorWriteHasBlue(slot0), ColorWriteHasAlpha(slot0));
        if (supportsIndexedColorMasks_)
        {
            for (int i = 0; i < maxMrtTargets_; ++i)
            {
                const int mask = currentColorWriteMasks_[i];
                device.set_color_mask(
                    static_cast<unsigned int>(i),
                    ColorWriteHasRed(mask), ColorWriteHasGreen(mask),
                    ColorWriteHasBlue(mask), ColorWriteHasAlpha(mask));
            }
        }
    }

    void EasyGLRenderer::ForceAllColorWriteMasks()
    {
        device.set_color_mask(true, true, true, true);
        if (supportsIndexedColorMasks_)
            for (int i = 0; i < maxMrtTargets_; ++i)
                device.set_color_mask(
                    static_cast<unsigned int>(i), true, true, true, true);
    }

    bool EasyGLRenderer::HasRestrictedActiveColorWriteMask() const
    {
        const int activeCount = bound_->mrtCount > 0 ? bound_->mrtCount : 1;
        for (int i = 0; i < activeCount; ++i)
            if (currentColorWriteMasks_[i] != 15) return true;
        return false;
    }

    void EasyGLRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& writeState)
    {
        if (metagl::IsContextLost()) return;
        if (!supportsIndexedColorMasks_ && bound_->mrtCount > 1)
        {
            for (int i = 1; i < bound_->mrtCount; ++i)
                if (writeState.colorWriteChannels[i]
                    != writeState.colorWriteChannels[0])
                    throw std::runtime_error(
                        "EasyGL ApplyBlendState: this GL profile cannot express distinct "
                        "ColorWriteChannels values for active MRT slots.");
        }
        // Blend::One=0, Blend::Zero=1 → Opaque preset: src=One, dst=Zero → effectively no blending
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        device.set_blend_enabled(blendEnabled);
        if (blendEnabled)
        {
            device.set_blend_func_separate(
                ToEasyGLBlendFactor(colorSrcBlend), ToEasyGLBlendFactor(colorDstBlend),
                ToEasyGLBlendFactor(alphaSrcBlend), ToEasyGLBlendFactor(alphaDstBlend));
            device.set_blend_equation_separate(
                ToEasyGLBlendEquation(colorBlendFunc),
                ToEasyGLBlendEquation(alphaBlendFunc));
        }
        for (int i = 0; i < 4; ++i)
            currentColorWriteMasks_[i] = writeState.colorWriteChannels[i];
        ApplyCurrentColorWriteMasks();
        // BlendState.MultiSampleMask: EasyGL could express a coverage mask via glSampleMaski
        // (GL ES 3.1+, requires GL_SAMPLE_MASK enable). It is left at the all-ones default here —
        // a non-default coverage mask on the GL/GLES profile is a documented capability gap, not
        // silently dropped: the value reaches the renderer and only the (rare) non-default path is
        // unimplemented.
    }

    void EasyGLRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                        int depthFunc,
                                                        bool stencilEnable, int stencilFunc,
                                                        int stencilPass, int stencilFail, int stencilDepthFail,
                                                        int stencilMask, int stencilWriteMask, int referenceStencil,
                                                        bool twoSidedStencilMode,
                                                        int ccwStencilFunc, int ccwStencilPass,
                                                        int ccwStencilFail, int ccwStencilDepthFail)
    {
        if (metagl::IsContextLost()) return;

        device.set_depth_test_enabled(depthEnable);
        device.set_depth_mask(depthWriteEnable);
        if (depthEnable)
            device.set_depth_func(ToEasyGLCompareFunc(depthFunc));

        // Task 870/319: remember what this installs, so SetReferenceStencil can reissue the
        // function call with a new reference. Recorded even when the stencil test is off, because
        // the reference survives a disabled state and applies again when one re-enables it.
        stencilEnabled_   = stencilEnable;
        depthWriteEnabled_ = depthWriteEnable;   // REMED-GFX-237
        stencilWriteMask_  = stencilWriteMask;   // REMED-GFX-237
        stencilTwoSided_  = twoSidedStencilMode;
        stencilFunc_      = stencilFunc;
        stencilCcwFunc_   = ccwStencilFunc;
        stencilReadMask_  = stencilMask;
        referenceStencil_ = referenceStencil;

        device.set_stencil_test_enabled(stencilEnable);
        if (stencilEnable)
        {
            const auto eglSFail  = ToEasyGLStencilOp(stencilFail);
            const auto eglDFail  = ToEasyGLStencilOp(stencilDepthFail);
            const auto eglPass   = ToEasyGLStencilOp(stencilPass);
            if (twoSidedStencilMode)
            {
                device.set_stencil_func_separate(::easygl::CullFace::Front,
                    ToEasyGLCompareFunc(stencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op_separate(::easygl::CullFace::Front,
                    eglSFail, eglDFail, eglPass);
                device.set_stencil_mask_separate(::easygl::CullFace::Front,
                    static_cast<unsigned int>(stencilWriteMask));

                device.set_stencil_func_separate(::easygl::CullFace::Back,
                    ToEasyGLCompareFunc(ccwStencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op_separate(::easygl::CullFace::Back,
                    ToEasyGLStencilOp(ccwStencilFail),
                    ToEasyGLStencilOp(ccwStencilDepthFail),
                    ToEasyGLStencilOp(ccwStencilPass));
                device.set_stencil_mask_separate(::easygl::CullFace::Back,
                    static_cast<unsigned int>(stencilWriteMask));
            }
            else
            {
                device.set_stencil_func(ToEasyGLCompareFunc(stencilFunc),
                    referenceStencil, static_cast<unsigned int>(stencilMask));
                device.set_stencil_op(eglSFail, eglDFail, eglPass);
                device.set_stencil_mask(static_cast<unsigned int>(stencilWriteMask));
            }
        }
    }

    void EasyGLRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float depthBias,
                                                      float slopeScaleDepthBias)
    {
        if (metagl::IsContextLost()) return;
        // CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2
        // OpenGL default front face is CCW; CW faces are back faces.
        if (cullMode == 0)
        {
            device.set_cull_face_enabled(false);
        }
        else
        {
            device.set_cull_face_enabled(true);
            device.set_cull_face(cullMode == 1 ? ::easygl::CullFace::Back
                                                : ::easygl::CullFace::Front);
        }
        device.set_scissor_test_enabled(scissorTestEnable);
        // OpenGL ES has no glPolygonMode; FillMode::WireFrame (1) is emulated at draw
        // time by re-expanding triangles into GL_LINES (see DrawWireframe).
        wireframe_ = (fillMode == 1);
        // Task 767: DepthBias/SlopeScaleDepthBias map directly onto real GL polygon offset
        // (matches this project's own already-established Vulkan convention, see
        // VulkanRenderer::ApplyRasterizerState's comment: "matching FNA's
        // glPolygonOffset(slopeScaleDepthBias, depthBias)"). Always enabled -- factor=0/units=0
        // is a genuine no-op in GL, so there is no need to conditionally disable it.
        device.set_polygon_offset_fill_enabled(true);
        device.set_polygon_offset(slopeScaleDepthBias, depthBias);
    }

    void EasyGLRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        if (metagl::IsContextLost()) return;
        if (w <= 0 || h <= 0) return; // invalid rect — leave scissor state unchanged
        // OpenGL scissor origin is bottom-left; convert from top-left XNA coordinates.
        // Use the render target's own height for the Y-flip when an RT is bound (mirrors
        // ReadBackbuffer's identical fbH pattern); fall back to the window's physical
        // height for the default framebuffer. Task 880: previously always used the
        // window's physical height even while an RT was bound, which is only latent
        // (scissor test is opt-in via RasterizerState.ScissorTestEnable) but wrong.
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int physW;
            getPhysicalSize(physW, fbH);
        }
        device.set_scissor(x, fbH - y - h, w, h);
        // Do NOT enable/disable scissor test here — that is controlled exclusively
        // by ApplyRasterizerState via RasterizerState.ScissorTestEnable.
    }

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
    void EasyGLRenderer::SetCompiledEffectDepthRangeEXT(bool begin)
    {
        if (metagl::IsContextLost()) return;
        if (begin)
        {
            const float midpoint =
                0.5f * (viewportMinDepth_ + viewportMaxDepth_);
            device.set_depth_range(midpoint, viewportMaxDepth_);
        }
        else
        {
            device.set_depth_range(viewportMinDepth_, viewportMaxDepth_);
        }
    }

    namespace
    {
        // Scope guard so every early return and every throw out of a compiled-effect draw still
        // restores the viewport's own depth range.
        class CompiledEffectDepthRangeScope
        {
        public:
            explicit CompiledEffectDepthRangeScope(EasyGLRenderer& renderer)
                : renderer_(renderer)
            {
                renderer_.SetCompiledEffectDepthRangeEXT(true);
            }
            ~CompiledEffectDepthRangeScope()
            {
                renderer_.SetCompiledEffectDepthRangeEXT(false);
            }
            CompiledEffectDepthRangeScope(const CompiledEffectDepthRangeScope&) = delete;
            CompiledEffectDepthRangeScope& operator=(const CompiledEffectDepthRangeScope&) = delete;

        private:
            EasyGLRenderer& renderer_;
        };
    }
#endif

    void EasyGLRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        device.set_blend_color(r, g, b, a);
    }

    void EasyGLRenderer::SetReferenceStencil(int value)
    {
        if (metagl::IsContextLost()) return;
        referenceStencil_ = value;
        // Same standalone-property discipline SetBlendFactor has, but GL gives that one a call of
        // its own (glBlendColor) and gives this one none: glStencilFunc sets function, reference
        // and mask together, so reissuing it with the remembered function and mask is the only way
        // to change the reference without waiting for the next DepthStencilState assignment.
        // Nothing to reissue while the stencil test is off -- the value is kept, and whichever
        // ApplyDepthStencilState re-enables the test carries its own reference anyway.
        if (!stencilEnabled_) return;
        if (stencilTwoSided_)
        {
            device.set_stencil_func_separate(::easygl::CullFace::Front,
                ToEasyGLCompareFunc(stencilFunc_),
                referenceStencil_, static_cast<unsigned int>(stencilReadMask_));
            device.set_stencil_func_separate(::easygl::CullFace::Back,
                ToEasyGLCompareFunc(stencilCcwFunc_),
                referenceStencil_, static_cast<unsigned int>(stencilReadMask_));
        }
        else
        {
            device.set_stencil_func(ToEasyGLCompareFunc(stencilFunc_),
                referenceStencil_, static_cast<unsigned int>(stencilReadMask_));
        }
    }

    void EasyGLRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        if (metagl::IsContextLost()) return;
        if (w <= 0 || h <= 0) return; // invalid rect — leave viewport state unchanged
        // OpenGL viewport origin is bottom-left; convert from top-left XNA coordinates.
        // Use the render target's own height for the Y-flip when an RT is bound (mirrors
        // ReadBackbuffer's/SetScissorRect's identical fbH pattern); fall back to the
        // window's physical height for the default framebuffer. Using the window's height
        // unconditionally while an RT is bound produces a viewport y-offset that falls
        // entirely outside the RT's actual pixel range whenever the RT is smaller than the
        // window, discarding every fragment.
        int fbH = bound_->height;
        if (fbH == 0)
        {
            int physW;
            getPhysicalSize(physW, fbH);
        }
        device.set_viewport(x, fbH - y - h, w, h);
        device.set_depth_range(minDepth, maxDepth);
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    void EasyGLRenderer::ApplySamplerState(int slot, int filter,
                                                   int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (metagl::IsContextLost()) return;
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 has no sampler objects (glGenSamplers/glBindSampler are ES 3.0) -- sampling
        // state lives on the texture object itself, the same shape FNA3D's pre-3.0 GL path used.
        // The request is recorded per slot and written onto whatever texture(s) are bound to this
        // unit now AND onto any texture bound to it later (Es2ApplyPendingSamplerToUnit's other
        // call sites), covering both call orders CNA uses: SpriteBatch binds then applies;
        // GraphicsDevice applies state first and the draw binds afterwards. samplers_[slot] and
        // the sampler-object body below stay untouched -- and unreachable -- under this profile.
        Es2PendingSamplers()[slot] = { filter, addressU, addressV, maxAnisotropy };
        Es2ApplyPendingSamplerToUnit(slot);
        return;
}
else
{
        ::easygl::Sampler& s = samplers_[slot];
        if (!s.is_created())
            s.create();

        // TextureFilter → min/mag filter
        // XNA: Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        //      PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        //      MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8
        //
        // REMED-GFX-175: an ordinal names a MIPMAP component as well as a minification and a
        // magnification one, and ordinals 0 and 1 were losing theirs. Linear is min LINEAR, mag
        // LINEAR, mip LINEAR and Point is min POINT, mag POINT, mip POINT -- FNA's own decomposition
        // tables say so and FNA3D's GL driver maps them onto GL_LINEAR_MIPMAP_LINEAR and
        // GL_NEAREST_MIPMAP_NEAREST accordingly. Mapping them onto plain GL_LINEAR/GL_NEAREST drops
        // the mipmap term entirely, so a texture that really owns a chain never mip-filtered under
        // either -- including under the DEFAULT filter every game gets unless it says otherwise.
        // Every ordinal now carries its mipmap term, which is only safe because the level range of
        // every sampleable kind is clamped to its real level count (Task 924, REMED-GFX-174) and
        // because a declared chain now allocates every one of its levels (see EasyGLTextureRenderer).
        ::easygl::TextureMinFilter minF;
        ::easygl::TextureMagFilter magF;
        switch (filter)
        {
        case 1: // Point — mip point, min point, mag point
            minF = ::easygl::TextureMinFilter::NearestMipmapNearest;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 2: // Anisotropic
            minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 3: // LinearMipPoint
            minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 4: // PointMipLinear
            minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 5: // MinLinearMagPointMipLinear
            minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 6: // MinLinearMagPointMipPoint
            minF = ::easygl::TextureMinFilter::LinearMipmapNearest;
            magF = ::easygl::TextureMagFilter::Nearest;
            break;
        case 7: // MinPointMagLinearMipLinear
            minF = ::easygl::TextureMinFilter::NearestMipmapLinear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        case 8: // MinPointMagLinearMipPoint
            minF = ::easygl::TextureMinFilter::NearestMipmapNearest;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        default: // Linear — mip linear, min linear, mag linear
            minF = ::easygl::TextureMinFilter::LinearMipmapLinear;
            magF = ::easygl::TextureMagFilter::Linear;
            break;
        }
        s.set_parameter(::easygl::SamplerParameter::MinFilter, static_cast<int>(minF));
        s.set_parameter(::easygl::SamplerParameter::MagFilter, static_cast<int>(magF));

        // Task 918: real anisotropic filtering via GL_EXT_texture_filter_anisotropic, gated on
        // the extension genuinely being available; falls back to the plain trilinear filter set
        // above (unchanged) when it isn't, exactly like before this fix.
        //
        // REMED-GFX-174: anisotropy is a COMPONENT OF THE ORDINAL, so it must be written on every
        // application exactly like min, mag, wrapS and wrapT above -- not only when the ordinal
        // happens to be Anisotropic. samplers_[slot] is ONE long-lived GL sampler object that is
        // mutated in place and reused for every later application on that slot, so a write that
        // only ever RAISES the value leaves it raised forever: once any ordinal 2 draw set it to
        // SamplerState's default MaxAnisotropy of 4, every subsequent Point/Linear draw on that
        // slot kept sampling anisotropically. Anisotropic taps average across the pixel footprint,
        // which is why the contaminated slot returned the same wide box average for all nine
        // ordinals and no Point draw could ever return a stored texel again. Vulkan cannot have
        // this defect because it builds a fresh VkSamplerCreateInfo per sampler; the mutable
        // shared object is what makes the unconditional write necessary here.
        if (metagl::HasExtension("GL_EXT_texture_filter_anisotropic"))
        {
            float clamped = 1.0f;
            if (filter == 2)
            {
                GLfloat maxAnisoCap = 1.0f;
                metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
                const float requested = static_cast<float>(maxAnisotropy);
                clamped = (maxAnisoCap > 0.0f && requested > maxAnisoCap) ? maxAnisoCap : requested;
                if (clamped < 1.0f) clamped = 1.0f;
            }
            s.set_parameter(::easygl::SamplerParameter::MaxAnisotropy, clamped);
        }

        // TextureAddressMode → GL wrap: Wrap=0→Repeat, Clamp=1→ClampToEdge, Mirror=2→MirroredRepeat
        auto toWrap = [](int mode) -> int {
            switch (mode) {
            case 1:  return static_cast<int>(::easygl::TextureWrapMode::ClampToEdge);
            case 2:  return static_cast<int>(::easygl::TextureWrapMode::MirroredRepeat);
            default: return static_cast<int>(::easygl::TextureWrapMode::Repeat);
            }
        };
        s.set_parameter(::easygl::SamplerParameter::WrapS, toWrap(addressU));
        s.set_parameter(::easygl::SamplerParameter::WrapT, toWrap(addressV));

        // plans/plan_fx.md FX-092: samplers_[slot] is ONE long-lived GL object, mutated in place and
        // reused for every later application on that slot -- the same shape that made REMED-GFX-174
        // necessary for anisotropy. Every property this call does not write therefore survives from
        // whoever wrote it last, and ApplySamplerMipState (a compiled Effect's own MaxMipLevel and
        // MipMapLevelOfDetailBias) writes three of them. A SpriteBatch flush calls only
        // ApplySamplerState, so an effect that clamped the slot to mip 3 left every later stock
        // sprite sampling from mip 3 as well.
        //
        // This block makes the sampler's complete state a function of THIS call's arguments and
        // nothing else. GraphicsDevice::applySamplerStatesToRenderer runs before every draw and
        // follows this call with ApplySamplerMipState and ApplySamplerAddressW, so a device-driven
        // application overwrites the three defaults below with the real SamplerState values
        // immediately; only a caller that applies filter/addressing alone -- SpriteBatch's own
        // flush -- keeps them, which is exactly the XNA default it means.
        //
        // The W axis follows addressU because every XNA 4.0 SamplerState preset (PointClamp,
        // LinearWrap, AnisotropicClamp, ...) sets all three axes to the same mode, and a
        // SpriteBatch's sampler state is always one of those shapes.
        s.set_parameter(::easygl::SamplerParameter::WrapR, toWrap(addressU));
        // MaxMipLevel's default of 0 under ApplySamplerMipState's own mapping, and GL's default
        // upper bound. Both written unconditionally so a previous MinLod cannot survive here.
        s.set_parameter(::easygl::SamplerParameter::MinLod, 0.0f);
        s.set_parameter(::easygl::SamplerParameter::MaxLod, 1000.0f);
        if (ProfileIsDesktopCore())
        {
            // Spelled numerically for the same reason ApplySamplerMipState spells it that way:
            // GL_TEXTURE_LOD_BIAS does not exist in OpenGL ES, and one translation unit serves
            // both profiles.
            constexpr unsigned int kGlTextureLodBias = 0x8501u;
            s.set_parameter(static_cast<::easygl::SamplerParameter>(kGlTextureLodBias), 0.0f);
        }
        // XNA 4.0 has no shadow-comparison sampler at all, so nothing in CNA ever enables one --
        // but the property is mutable on this shared object, and "nothing writes it today" is
        // exactly the assumption the mip states were built on. GL_NONE, written every time.
        s.set_parameter(::easygl::SamplerParameter::CompareMode, 0);

        s.bind(static_cast<unsigned int>(slot));

        if (SamplerTraceEnabled())
        {
            int gotMin = 0, gotMag = 0, gotWrapS = 0, gotWrapT = 0;
            float gotAniso = 1.0f;
            s.get_parameter_iv(::easygl::SamplerParameter::MinFilter, &gotMin);
            s.get_parameter_iv(::easygl::SamplerParameter::MagFilter, &gotMag);
            s.get_parameter_iv(::easygl::SamplerParameter::WrapS, &gotWrapS);
            s.get_parameter_iv(::easygl::SamplerParameter::WrapT, &gotWrapT);
            if (metagl::HasExtension("GL_EXT_texture_filter_anisotropic"))
                s.get_parameter_fv(::easygl::SamplerParameter::MaxAnisotropy, &gotAniso);
            std::ostringstream os;
            os << "slot=" << slot
               << " ordinal=" << filter << '(' << FilterOrdinalName(filter) << ')'
               << " min=0x" << std::hex << gotMin << " mag=0x" << gotMag
               << " wrapS=0x" << gotWrapS << " wrapT=0x" << gotWrapT << std::dec
               << " aniso=" << gotAniso
               << " minUsesMipChain=" << (GlMinFilterUsesMipChain(gotMin) ? 1 : 0);
            SamplerTrace("apply-sampler", os.str());
        }
}
    }

    void EasyGLRenderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        if (metagl::IsContextLost()) return;
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 / WebGL 1 have neither sampler objects nor GL_TEXTURE_MIN_LOD, so neither of
        // these two states is representable. Documented in docs/sampler-state-support.md rather
        // than approximated: silently applying a nearby state would be worse than not applying it.
        (void) maxMipLevel;
        (void) lodBias;
        return;
}
else
{
        ::easygl::Sampler& s = samplers_[slot];
        if (!s.is_created()) s.create();
        // XNA's MaxMipLevel is the most detailed level the sampler may use, which is a lower bound
        // on the computed level of detail -- GL_TEXTURE_MIN_LOD, the same mapping FNA3D's SDL_GPU
        // driver makes with min_lod.
        s.set_parameter(::easygl::SamplerParameter::MinLod,
                        static_cast<float>(std::max(maxMipLevel, 0)));
        if (ProfileIsDesktopCore())
        {
            // Desktop-only: GL_TEXTURE_LOD_BIAS (0x8501) does not exist in OpenGL ES at all, which
            // is why FNA3D's own GL driver guards the identical write with !renderer->useES3. It
            // is spelled as its numeric token because the ES headers an ES-profile build compiles
            // against do not declare the name, and this one translation unit serves both.
            constexpr unsigned int kGlTextureLodBias = 0x8501u;
            s.set_parameter(static_cast<::easygl::SamplerParameter>(kGlTextureLodBias), lodBias);
        }
        s.bind(static_cast<unsigned int>(slot));
}
    }

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
    const ::easygl::Texture& EasyGLRenderer::AcquireCompiledEffectFlippedSourceEXT(
        int slot, const EasyGLRenderTargetRenderer& source)
    {
        // plans/plan_fx.md FX-099. See the declaration for why the correction is applied to the pixels
        // rather than to the sampling coordinate.
        if (ProfileIsEs2ApiGeneration())
        {
            // glBlitFramebuffer is OpenGL ES 3.0. Refused by name rather than served upside down,
            // which is the whole point of this function existing.
            throw System::NotSupportedException(
                "CNA EasyGL: a compiled Effect cannot sample a RenderTarget2D on the OpenGL ES 2.0 "
                "/ WebGL 1 profiles -- correcting the target's row order needs glBlitFramebuffer, "
                "which those profiles do not have.");
        }
        if (slot < 0 || slot >= kMaxSamplerSlots)
        {
            throw System::NotSupportedException(
                "CNA EasyGL: a compiled Effect's sampler slot is outside this renderer's range.");
        }
        // Reading a target while drawing into it is undefined in XNA too, and here it would also
        // be an invalid framebuffer blit. Named, not silently produced.
        bool sourceIsCurrentTarget = bound_->rt2D == static_cast<const IRenderTargetRenderer*>(&source);
        for (int i = 0; i < bound_->mrtCount; ++i)
        {
            sourceIsCurrentTarget = sourceIsCurrentTarget ||
                bound_->mrt[static_cast<std::size_t>(i)] == &source;
        }
        if (sourceIsCurrentTarget)
        {
            throw System::NotSupportedException(
                "CNA EasyGL: a compiled Effect cannot sample the RenderTarget2D it is drawing "
                "into.");
        }

        // Saved BEFORE anything below binds a framebuffer of its own. The lazy creation further
        // down binds this slot's copy as the draw target to attach its texture, so reading the
        // binding after that block would record the copy's framebuffer as the one to restore --
        // and the compiled draw that follows would render into the copy instead of into the
        // caller's render target, leaving that target showing nothing but its clear colour.
        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        metagl::glGetIntegerv(::metagl::GetParameter::DrawFramebufferBinding,
                              &previousDrawFramebuffer);
        metagl::glGetIntegerv(::metagl::GetParameter::ReadFramebufferBinding,
                              &previousReadFramebuffer);

        const int width = source.GetWidth();
        const int height = source.GetHeight();
        const int levelCount = std::max(1, source.levelCount_);
        CompiledEffectFlippedSourceEXT& copy =
            compiledFlippedSources_[static_cast<std::size_t>(slot)];
        if (copy.created &&
            (copy.width != width || copy.height != height || copy.levelCount != levelCount))
        {
            copy.framebuffer.destroy();
            copy.texture.destroy();
            copy.created = false;
        }
        if (!copy.created)
        {
            copy.texture.create();
            copy.texture.bind(::easygl::TextureTarget::Texture2D);
            int levelW = width, levelH = height;
            for (int level = 0; level < levelCount; ++level)
            {
                copy.texture.set_image_2d(::easygl::TextureTarget::Texture2D, level,
                                          RgbaTexImageInternalFormat(), levelW, levelH,
                                          ::metagl::PixelFormat::Rgba,
                                          ::metagl::PixelType::UnsignedByte, nullptr);
                levelW = std::max(1, levelW / 2);
                levelH = std::max(1, levelH / 2);
            }
            // Same completeness clamp REMED-GFX-174 applies to every other sampleable kind: GL
            // evaluates mipmap completeness over [BASE_LEVEL, MAX_LEVEL] and defaults MAX_LEVEL to
            // 1000, so without this a one-level copy samples black under any mip-using filter.
            copy.texture.set_parameter(::easygl::TextureTarget::Texture2D,
                                       ::easygl::TextureParameterSetter::MaxLevel, levelCount - 1);
            copy.framebuffer.create();
            copy.framebuffer.bind(::easygl::FramebufferTarget::DrawFramebuffer);
            copy.framebuffer.attach_texture_2d(::easygl::FramebufferTarget::DrawFramebuffer,
                                               ::metagl::to_framebuffer_attachment(
                                                   ::metagl::ColorAttachment::Color0),
                                               ::easygl::TextureTarget::Texture2D,
                                               copy.texture, 0);
            copy.width = width;
            copy.height = height;
            copy.levelCount = levelCount;
            copy.created = true;
        }
        if (!compiledFlipReadFboCreated_)
        {
            compiledFlipReadFbo_.create();
            compiledFlipReadFboCreated_ = true;
        }

        // The source's OWN colour texture is attached to this renderer's read framebuffer rather
        // than binding the target's framebuffers, so a multisample target -- whose fbo_ carries a
        // renderbuffer, not the texture -- is read from its already-resolved single-sample texture
        // and neither of its framebuffers is disturbed.
        //
        // glBlitFramebuffer is subject to the scissor test; a game's active scissor rectangle has
        // nothing to do with this internal copy.
        const bool scissorWasEnabled = metagl::glIsEnabled(::metagl::Capability::ScissorTest);
        if (scissorWasEnabled) device.set_scissor_test_enabled(false);

        compiledFlipReadFbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
        compiledFlipReadFbo_.attach_texture_2d(::easygl::FramebufferTarget::ReadFramebuffer,
                                               ::metagl::to_framebuffer_attachment(
                                                   ::metagl::ColorAttachment::Color0),
                                               ::easygl::TextureTarget::Texture2D,
                                               source.GetEasyGLColorTexture(), 0);
        copy.framebuffer.bind(::easygl::FramebufferTarget::DrawFramebuffer);
        // The destination Y range runs the other way, which is the whole correction.
        ::easygl::Framebuffer::blit(0, 0, width, height,
                                    0, height, width, 0,
                                    ::metagl::ClearBufferBit::Color,
                                    ::metagl::BlitFilter::Nearest);

        if (scissorWasEnabled) device.set_scissor_test_enabled(true);
        metagl::glBindFramebuffer(::metagl::FramebufferTarget::ReadFramebuffer,
                                  ::metagl::FramebufferId{
                                      static_cast<unsigned int>(previousReadFramebuffer)});
        metagl::glBindFramebuffer(::metagl::FramebufferTarget::DrawFramebuffer,
                                  ::metagl::FramebufferId{
                                      static_cast<unsigned int>(previousDrawFramebuffer)});

        if (levelCount > 1)
        {
            // The blit fills level 0 only; the rest of the chain is rebuilt from it, exactly as
            // UnbindAsRenderTarget does for the target itself.
            copy.texture.bind(::easygl::TextureTarget::Texture2D);
            copy.texture.generate_mipmap(::easygl::TextureTarget::Texture2D);
        }
        return copy.texture;
    }
#endif  // CNA_EASYGL_COMPILED_EFFECTS

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
    void EasyGLRenderer::ReleaseCompiledEffectGlObjectsForContextLossEXT()
    {
        // plans/plan_fx.md FX-108. Handles are dropped WITHOUT a GL call: the names belong to a context
        // that is gone, and deleting them would either be a no-op or address someone else's object
        // in the replacement context. The `*Created`/extent bookkeeping is reset with them so the
        // lazy creators rebuild on next use instead of trusting a stale flag.
        compiledEffectVao_.reset_handle_no_gl();
        compiledEffectVaoCreated_ = false;
        for (CompiledEffectFlippedSourceEXT& copy : compiledFlippedSources_)
        {
            copy.texture.reset_handle_no_gl();
            copy.framebuffer.reset_handle_no_gl();
            copy.width = 0;
            copy.height = 0;
            copy.levelCount = 1;
            copy.created = false;
        }
        compiledFlipReadFbo_.reset_handle_no_gl();
        compiledFlipReadFboCreated_ = false;
    }
#endif

    void EasyGLRenderer::ApplySamplerAddressW(int slot, int addressW)
    {
        if (metagl::IsContextLost()) return;
        if (slot < 0 || slot >= kMaxSamplerSlots) return;
if (ProfileIsEs2ApiGeneration())
{
        // OpenGL ES 2.0 / WebGL 1 have neither sampler objects nor GL_TEXTURE_WRAP_R, and no
        // volume textures for the axis to address. Documented as unrepresentable rather than
        // approximated, exactly like ApplySamplerMipState's own ES 2 branch above.
        (void) addressW;
        return;
}
else
{
        ::easygl::Sampler& s = samplers_[slot];
        if (!s.is_created()) s.create();
        // Same TextureAddressMode -> GL wrap table ApplySamplerState uses for S and T.
        int wrap = static_cast<int>(::easygl::TextureWrapMode::Repeat);
        if (addressW == 1) wrap = static_cast<int>(::easygl::TextureWrapMode::ClampToEdge);
        else if (addressW == 2) wrap = static_cast<int>(::easygl::TextureWrapMode::MirroredRepeat);
        s.set_parameter(::easygl::SamplerParameter::WrapR, wrap);
        s.bind(static_cast<unsigned int>(slot));
}
    }

    // -------------------------------------------------------------------------
    // 3D pipeline
    // -------------------------------------------------------------------------

    void EasyGLVertexBufferRenderer::InitializeLayout()
    {
        vbo.create();
        if (!ProfileIsEs2ApiGeneration())
            vao.create();
        // Attribute layout is configured lazily in ApplyLayout() once stride is known.
    }

    namespace
    {
        struct VertexAttribFormat
        {
            int componentCount;
            ::easygl::DataType type;
            bool normalized;
            bool isInteger;
        };

        // Task 1080: maps XNA's VertexElementFormat to the GL attribute shape needed to bind it
        // -- component count, GL scalar type, whether values are normalized to [0,1]/[-1,1], and
        // whether the attribute must be read as a true integer (glVertexAttribIPointer) rather
        // than converted to float (glVertexAttribPointer). Byte4 is the one format needing the
        // integer path -- XNA's own format for BLENDINDICES-style semantics (read as int4 in
        // HLSL), matching the existing skinned-vertex BlendIndices precedent below (offset 48,
        // case 52) that this table generalizes to arbitrary declarations.
        VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
        {
            switch (format)
            {
            case VertexElementFormat::Single:          return { 1, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector2:         return { 2, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector3:         return { 3, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Vector4:         return { 4, ::easygl::DataType::Float,        false, false };
            case VertexElementFormat::Color:           return { 4, ::easygl::DataType::UnsignedByte, true,  false };
            case VertexElementFormat::Byte4:
                // FX-127: read as floats on every profile, not as true integers. Byte4 is used
                // exclusively for BLENDINDICES-style bone-index attributes, and the same semantic
                // is equally legal as Vector4 -- XNA's vertex element format describes the bytes,
                // while the shader register is a float4 either way, so a processor is free to
                // write either (CustomModelAnimation's own SkinnedModelProcessor converts
                // BlendIndices0 to Vector4 and real XNA renders it). One shader attribute cannot
                // be both an integer and a float, so the skinned programs declare
                // "in vec4 aBoneIndices" and cast to int() when indexing uBones[], and both
                // formats bind to it as floats. The range is 0-255 at most, far more than the
                // <= 72 bone count needs, and exactly float-representable.
                return { 4, ::easygl::DataType::UnsignedByte, false, false };
            case VertexElementFormat::Short2:          return { 2, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::Short4:          return { 4, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::NormalizedShort2:return { 2, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::NormalizedShort4:return { 4, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::HalfVector2:     return { 2, ::easygl::DataType::HalfFloat,    false, false };
            case VertexElementFormat::HalfVector4:     return { 4, ::easygl::DataType::HalfFloat,    false, false };
            }
            return { 3, ::easygl::DataType::Float, false, false };
        }

        /// Binds a BLENDINDICES-style Byte4 bone-index attribute for ApplyLayout's fixed-stride
        /// skinned layouts (52/56/68), in the one read mode every profile's skinned shaders now
        /// expect: plain float reads, because "in vec4 aBoneIndices" is a float attribute on all
        /// of them (FX-127). Same choice DescribeVertexElementFormat's Byte4 case makes for
        /// declaration-driven layouts.
        void SetBoneIndicesAttributePointer(::easygl::VertexArray& vao, unsigned int location,
                                            std::size_t stride, const void* offset)
        {
            vao.set_attribute_pointer(location, 4, ::easygl::DataType::UnsignedByte, false, stride, offset);
        }

        void ConfigureDeclarationAttributes(
            ::easygl::VertexArray& vao,
            const EasyGLVertexBufferRenderer& buffer,
            unsigned int firstLocation,
            int vertexOffset,
            unsigned int divisor,
            std::size_t elementCount)
        {
            const auto& declaration = buffer.GetDeclarationElements();
            if (elementCount > declaration.size() || firstLocation + elementCount > 16)
            {
                throw System::InvalidOperationException(
                    "EasyGL instanced drawing requires a complete vertex declaration within "
                    "the 16-attribute XNA profile limit.");
            }

            const std::size_t stride = buffer.GetStride();
            buffer.vbo.bind(::easygl::BufferTarget::Array);
            for (std::size_t i = 0; i < elementCount; ++i)
            {
                const VertexElement& element = declaration[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const unsigned int location = firstLocation + static_cast<unsigned int>(i);
                const std::size_t byteOffset =
                    static_cast<std::size_t>(vertexOffset) * stride +
                    static_cast<std::size_t>(element.getOffsetProperty());
                const void* pointer = reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(byteOffset));
                vao.enable_attribute(location);
                if (desc.isInteger)
                {
                    vao.set_attribute_i_pointer(
                        location, desc.componentCount, desc.type, stride, pointer);
                }
                else
                {
                    vao.set_attribute_pointer(
                        location, desc.componentCount, desc.type,
                        desc.normalized, stride, pointer);
                }
                vao.set_attribute_divisor(location, divisor);
            }
        }

        void DisableDeclarationAttributes(
            ::easygl::VertexArray& vao,
            unsigned int firstLocation,
            std::size_t elementCount)
        {
            for (std::size_t i = 0; i < elementCount; ++i)
            {
                const unsigned int location = firstLocation + static_cast<unsigned int>(i);
                vao.disable_attribute(location);
                vao.set_attribute_divisor(location, 0);
            }
        }

        /// REMED-GFX-201: how many attribute locations the streams before @p streamIndex occupy.
        /// Locations run in binding-slot order across the concatenated declarations, which is the
        /// same "location N == Nth field of the ported HLSL input struct" convention ApplyLayout()
        /// uses for one stream and DrawInstancedPrimitivesEx already uses to append a second one.
        unsigned int FirstLocationForStream(const GpuDrawParams& params, int streamIndex)
        {
            unsigned int location = 0;
            for (int i = 0; i < streamIndex; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                location += static_cast<unsigned int>(
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer)
                        ->GetDeclarationElements().size());
            }
            return location;
        }

        /// REMED-GFX-201: binds every per-vertex stream into @p vao. Stream 0's own attributes are
        /// only rewritten when it carries a residual offset, so the overwhelmingly common
        /// "several streams, binding 0 has the smallest offset" case leaves ApplyLayout()'s work
        /// untouched. Returns false when a bound stream has no declaration, which is the one shape
        /// this path genuinely cannot express -- the caller then throws instead of drawing from
        /// stream 0 alone.
        bool ConfigureMultiStreamAttributes(
            ::easygl::VertexArray& vao, const GpuDrawParams& params)
        {
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                const auto* buffer =
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty())
                    return false;
                if (i == 0 && stream.vertexOffset == 0)
                    continue;   // exactly what ApplyLayout() already configured
                ConfigureDeclarationAttributes(
                    vao, *buffer, FirstLocationForStream(params, i),
                    stream.vertexOffset, 0, buffer->GetDeclarationElements().size());
            }
            return true;
        }

        /// REMED-GFX-202: the first attribute location the per-instance streams occupy.
        ///
        /// The stock instanced shaders declare their per-instance world matrix at the fixed
        /// locations 12..15 (`cnaInstanceCol0..3`), chosen so it cannot collide with an extended
        /// mesh declaration; a `ShaderEffect` program instead continues straight after the
        /// per-vertex streams, in the same "location N == Nth field of the ported HLSL input
        /// struct" order every other EasyGL layout uses.
        constexpr unsigned int kStockInstanceBaseLocation = 12u;

        /// The XNA 4.0 profile's vertex-attribute ceiling, and GL ES 3's guaranteed minimum.
        constexpr unsigned int kMaxAttributeLocations = 16u;

        /// REMED-GFX-202: how many attribute locations every per-vertex stream occupies together.
        unsigned int PerVertexLocationCount(const GpuDrawParams& params)
        {
            unsigned int total = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                total += static_cast<unsigned int>(
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer)
                        ->GetDeclarationElements().size());
            }
            return total;
        }

        /// One per-instance stream's place in the attribute space: where its declaration starts and
        /// how many of its elements actually have a location to go to. FNA3D skips an element whose
        /// shader input does not exist ("Stream not in use!"), so an over-long declaration loses its
        /// tail rather than failing -- but a stream that gets NO location at all supplies nothing,
        /// which is the shape this renderer genuinely cannot express.
        struct InstanceStreamPlacement
        {
            unsigned int firstLocation = 0;
            std::size_t elementCount = 0;
        };

        /// Fixed-capacity, so an instanced draw still allocates nothing: there can never be more
        /// per-instance streams than XNA's own binding ceiling.
        struct InstanceStreamPlacements
        {
            std::array<InstanceStreamPlacement, kMaxVertexStreams> entries{};
            int count = 0;
        };

        /// Walks the per-instance streams in public slot order, concatenating their declarations
        /// after @p baseLocation. Returns false when a bound per-instance stream has no
        /// declaration at all or would receive no location.
        bool PlaceInstanceStreams(
            const GpuDrawParams& params,
            unsigned int baseLocation,
            InstanceStreamPlacements& placements)
        {
            placements.count = 0;
            unsigned int location = baseLocation;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0)
                    continue;
                const auto* buffer =
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty())
                    return false;
                if (location >= kMaxAttributeLocations)
                    return false;
                const std::size_t available =
                    static_cast<std::size_t>(kMaxAttributeLocations - location);
                const std::size_t count =
                    std::min(buffer->GetDeclarationElements().size(), available);
                placements.entries[static_cast<std::size_t>(placements.count++)] =
                    InstanceStreamPlacement{location, count};
                location += static_cast<unsigned int>(count);
            }
            return true;
        }

        /// REMED-GFX-201: undoes ConfigureMultiStreamAttributes so the VAO is left exactly as
        /// ApplyLayout() built it. Without this a later single-stream draw through the same buffer
        /// would still have the secondary streams' locations enabled and pointing at a foreign VBO.
        void RestoreSingleStreamAttributes(
            ::easygl::VertexArray& vao, const GpuDrawParams& params)
        {
            for (int i = params.vertexStreamCount - 1; i >= 0; --i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                const auto* buffer =
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr || buffer->GetDeclarationElements().empty())
                    continue;
                if (i == 0)
                {
                    if (stream.vertexOffset != 0)
                    {
                        ConfigureDeclarationAttributes(
                            vao, *buffer, 0, 0, 0, buffer->GetDeclarationElements().size());
                    }
                    continue;
                }
                DisableDeclarationAttributes(
                    vao, FirstLocationForStream(params, i),
                    buffer->GetDeclarationElements().size());
            }
        }
    }

    void EasyGLVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        declarationElements_ = vertexDeclaration.GetVertexElements();
    }

    void EasyGLVertexBufferRenderer::ApplyLayout(std::size_t stride)
    {
        const int s = static_cast<int>(stride);
        if (!ProfileIsEs2ApiGeneration())
            vao.bind();
        vbo.bind(::easygl::BufferTarget::Array);

        if (!declarationElements_.empty())
        {
            // Task 1080: generic layout binding driven by the caller's own VertexDeclaration.
            // Attribute location = the element's own index within the declaration's element
            // list, matching this project's established "layout(location=N) == Nth field of the
            // ported HLSL input struct" convention used by every hand-ported .fx vertex shader
            // this session -- not a fixed byte-stride dispatch, so this covers genuinely custom
            // layouts (e.g. NormalMapping.fx's Position+Normal+Tangent+TexCoord) that don't match
            // any of the built-in strides the switch below recognizes.
            for (std::size_t i = 0; i < declarationElements_.size(); ++i)
            {
                const VertexElement& element = declarationElements_[i];
                const VertexAttribFormat desc =
                    DescribeVertexElementFormat(element.getVertexElementFormatProperty());
                const auto location = static_cast<unsigned int>(i);
                const void* offset = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(element.getOffsetProperty()));
                vao.enable_attribute(location);
                if (desc.isInteger)
                    vao.set_attribute_i_pointer(location, desc.componentCount, desc.type, s, offset);
                else
                    vao.set_attribute_pointer(location, desc.componentCount, desc.type,
                                              desc.normalized, s, offset);
            }
            if (!ProfileIsEs2ApiGeneration())
                vao.unbind();
            return;
        }

        switch (stride)
        {
        case 16:
            // VertexPositionColor (packed): float3 position + ubyte4 color
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)12);
            break;
        case 20:
            // VertexPositionTexture (packed): float3 position + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false, s, (void*)12);
            break;
        case 24:
            // VertexPositionColorTexture (packed): float3 position + ubyte4 color + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)16);
            break;
        case 32:
            // VertexPositionNormalTexture (packed): float3 position + float3 normal + float2 texcoord
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)24);
            break;
        case 48:
            // plans/plan_cnj.md CNB-57 (Phase 13A): VertexPositionNormalTangentTexture (packed):
            // float3 position + float3 normal + float4 tangent (xyz + bitangent handedness in w)
            // + float2 texcoord -- the layout PbrEffect's normal mapping needs to build a
            // per-pixel TBN basis.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            break;
        case 60:
            // GLTF-182/183: collision-free rigid PBR dual-UV layout. Bytes 0..47 are the
            // established stride-48 prefix and UV1 is appended at 48.
            //
            // plans/plan_gltf.md GLTF-462: bytes 56..59 were reserved padding and are the packed COLOR_0
            // slot now, which is what lets a vertex-coloured metallic-roughness primitive keep its
            // material instead of falling back to a layout with no Normal at all. Location 5
            // mirrors the stride-52/56 precedent exactly: the slot is always bound, and
            // GpuDrawParams::vertexColorEnabled (from PbrEffect::VertexColorEnabledEXT) decides
            // whether the shader reads it. An uncoloured primitive fills it with opaque white, so
            // even a mis-set gate multiplies by one rather than darkening the surface.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            vao.enable_attribute(4);
            vao.set_attribute_pointer(4, 2, ::easygl::DataType::Float, false, s, (void*)48);
            vao.enable_attribute(5);
            vao.set_attribute_pointer(5, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)56);
            break;
        case 52:
            // Task 11.10: this layout is independently duplicated (magic stride 52) in
            // BgfxRenderer.cpp's MakeBgfxLayout and VulkanRenderer.cpp's
            // GetOrCreatePipelineSkinned3D - none derive it from the canonical
            // VertexPositionNormalTextureSkinned::getVertexDeclarationStatic() (5 VertexElements:
            // offset 0 Vector3 Position, 12 Vector3 Normal, 24 Vector2 TextureCoordinate,
            // 32 Vector4 BlendWeight, 48 Byte4 BlendIndices). If that struct's field order or
            // any individual offset ever changes (its total size is already guarded by
            // VertexBuffer.cpp's own static_assert(sizeof(GpuVertex) == 52)), all 3 copies below
            // must be updated together - investigated deriving this from a shared VertexElement
            // list instead, but every renderer's API boundary currently only receives a raw
            // stride, not a VertexDeclaration; doing so would mean widening IGraphicsRenderer's
            // interface across all 4 renderers for every existing magic-stride case (16/32/52),
            // not just this one - deferred as a larger cross-renderer refactor, not attempted here.
            // SkinnedVertex: float3 pos + float3 normal + float2 uv + float4 weights + ubyte4 indices
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 4, ::easygl::DataType::Float, false, s, (void*)32);
            vao.enable_attribute(4);
            SetBoneIndicesAttributePointer(vao, 4, s, (void*)48);
            break;
        case 56:
            // CNB-67 (Phase 13C): the stride-52 SkinnedVertex layout above with a per-vertex
            // Color (normalized ubyte4) appended at the end (offset 52), rather than inserted
            // mid-layout -- keeps attribute locations 0-4 byte-identical to the stride-52 case
            // above, so EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()'s single shader
            // pair serves both strides; location 5 (aColor) is simply left unbound for stride-52
            // draws, and GpuDrawParams::vertexColorEnabled (gated by SkinnedEffect::
            // VertexColorEnabled, see FillGpuDrawParams) already governs whether the shader reads
            // it at all.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 2, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 4, ::easygl::DataType::Float, false, s, (void*)32);
            vao.enable_attribute(4);
            SetBoneIndicesAttributePointer(vao, 4, s, (void*)48);
            vao.enable_attribute(5);
            vao.set_attribute_pointer(5, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)52);
            break;
        case 68:
            // PBR + skinning combo (VertexPositionNormalTangentTextureSkinned): the stride-48
            // Position+Normal+Tangent+TextureCoordinate layout with the stride-52/56 skinning
            // suffix (BlendWeight, BlendIndices) appended, matching those precedents' own
            // "append rather than insert" convention -- locations 0-3 stay byte-identical to the
            // stride-48 PbrEffect layout, locations 4-5 mirror the stride-52 skinning suffix's
            // own attribute shape.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            vao.enable_attribute(4);
            vao.set_attribute_pointer(4, 4, ::easygl::DataType::Float, false, s, (void*)48);
            vao.enable_attribute(5);
            SetBoneIndicesAttributePointer(vao, 5, s, (void*)64);
            break;
        case 76:
            // GLTF-182/183: the stride-68 skinned PBR record with packed UV1 appended at 68.
            // Keeping the original six locations byte-for-byte stable lets both layouts share
            // one shader; location 6 is unused/defaulted when an old stride-68 buffer is bound.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            vao.enable_attribute(4);
            vao.set_attribute_pointer(4, 4, ::easygl::DataType::Float, false, s, (void*)48);
            vao.enable_attribute(5);
            SetBoneIndicesAttributePointer(vao, 5, s, (void*)64);
            vao.enable_attribute(6);
            vao.set_attribute_pointer(6, 2, ::easygl::DataType::Float, false, s, (void*)68);
            break;
        case 80:
            // plans/plan_gltf.md GLTF-463: the stride-76 skinned PBR record with a packed COLOR_0
            // appended at 76 -- the skinned counterpart of stride 60's own colour slot. Locations
            // 0..6 stay byte-for-byte identical to stride 76, so one shader serves both, and
            // GpuDrawParams::vertexColorEnabled (from SkinnedPbrEffect::VertexColorEnabledEXT)
            // decides whether location 7 is read.
            vao.enable_attribute(0);
            vao.set_attribute_pointer(0, 3, ::easygl::DataType::Float, false, s, (void*)0);
            vao.enable_attribute(1);
            vao.set_attribute_pointer(1, 3, ::easygl::DataType::Float, false, s, (void*)12);
            vao.enable_attribute(2);
            vao.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, s, (void*)24);
            vao.enable_attribute(3);
            vao.set_attribute_pointer(3, 2, ::easygl::DataType::Float, false, s, (void*)40);
            vao.enable_attribute(4);
            vao.set_attribute_pointer(4, 4, ::easygl::DataType::Float, false, s, (void*)48);
            vao.enable_attribute(5);
            SetBoneIndicesAttributePointer(vao, 5, s, (void*)64);
            vao.enable_attribute(6);
            vao.set_attribute_pointer(6, 2, ::easygl::DataType::Float, false, s, (void*)68);
            vao.enable_attribute(7);
            vao.set_attribute_pointer(7, 4, ::easygl::DataType::UnsignedByte, true, s, (void*)76);
            break;
        default:
            // plans/plan_gltf.md GLTF-157: a byte stride does not describe which attributes exist.
            // Treating every unknown record as position-only left the other locations in stale
            // VAO state and rendered normals, UVs or skin weights from unrelated buffers. Refuse
            // it loudly; a genuinely custom layout reaches the generic declaration path above.
            if (!ProfileIsEs2ApiGeneration())
                vao.unbind();
            throw System::NotSupportedException(
                "EasyGLRenderer::ApplyLayout: unsupported vertex stride " +
                std::to_string(stride) +
                " without a VertexDeclaration; the upload is refused rather than bound as "
                "position-only.");
        }
        if (!ProfileIsEs2ApiGeneration())
            vao.unbind();
    }

    void EasyGLVertexBufferRenderer::BindForDraw() const
    {
        if (ProfileIsEs2ApiGeneration())
        {
            // ES 2.0/WebGL 1 keeps attribute pointers in context state rather than
            // a core VAO. SpriteBatch and 3D draws share that state, so restore
            // this buffer's layout immediately before every draw.
            const_cast<EasyGLVertexBufferRenderer*>(this)->ApplyLayout(stride_in_bytes_);
        }
        else
        {
            vao.bind();
        }
    }

    void EasyGLVertexBufferRenderer::UnbindAfterDraw() const
    {
        if (!ProfileIsEs2ApiGeneration())
            vao.unbind();
    }

    EasyGLVertexBufferRenderer::EasyGLVertexBufferRenderer(int vertex_capacity, std::shared_ptr<::easygl::ResourceRegistry> registry)
        : capacity(vertex_capacity)
        , registry_(registry)
    {
        InitializeLayout();
        if (auto reg = registry_.lock()) reg->add(this);
        CNA_RENDER_LOG("VertexBuffer created: capacity=" << capacity);
    }

    EasyGLVertexBufferRenderer::~EasyGLVertexBufferRenderer()
    {
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    void EasyGLVertexBufferRenderer::release_gl_handle_only()
    {
        vbo.reset_handle_no_gl();
        vao.reset_handle_no_gl();
    }

    void EasyGLVertexBufferRenderer::recreate_gl_resource()
    {
        InitializeLayout();
        if (!cpu_data_.empty() && stride_in_bytes_ > 0)
        {
            vbo.bind(::easygl::BufferTarget::Array);
            vbo.set_data(::easygl::BufferTarget::Array, cpu_data_.data(), cpu_data_.size());
            ApplyLayout(stride_in_bytes_);
        }
    }

    void EasyGLVertexBufferRenderer::uploadWithOptions(const void* data,
                                                      std::size_t byte_count,
                                                      SetDataOptions options)
    {
        vbo.bind(::easygl::BufferTarget::Array);
        if (options == SetDataOptions::Discard) {
            // Orphan strategy: discard old storage without stalling the GPU pipeline.
            const std::size_t total = static_cast<std::size_t>(capacity) * stride_in_bytes_;
            vbo.set_data(::easygl::BufferTarget::Array, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            vbo.set_sub_data(::easygl::BufferTarget::Array, data, byte_count, 0);
            gpu_allocated_ = true;
        } else if (options == SetDataOptions::NoOverwrite && gpu_allocated_) {
            // NoOverwrite: driver hint that no in-flight data is overwritten.
            vbo.set_sub_data(::easygl::BufferTarget::Array, data, byte_count, 0);
        } else {
            // None (or first-ever upload): standard glBufferData.
            vbo.set_data(::easygl::BufferTarget::Array, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            gpu_allocated_ = true;
        }
    }

    void EasyGLVertexBufferRenderer::SetData(const void* data, int count, std::size_t stride_in_bytes)
    {
        vertex_count = count;
        stride_in_bytes_ = stride_in_bytes;
        const std::size_t byte_count = static_cast<std::size_t>(count) * stride_in_bytes;
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        uploadWithOptions(data, byte_count, SetDataOptions::None);
        ApplyLayout(stride_in_bytes_);
        CNA_RENDER_LOG("VertexBuffer SetData: count=" << count << " stride=" << stride_in_bytes
            << " bytes=" << byte_count);
    }

    void EasyGLVertexBufferRenderer::SetDataWithOptions(const void* data, int count,
                                                       std::size_t stride_in_bytes,
                                                       SetDataOptions options)
    {
        vertex_count = count;
        stride_in_bytes_ = stride_in_bytes;
        const std::size_t byte_count = static_cast<std::size_t>(count) * stride_in_bytes;
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        uploadWithOptions(data, byte_count, options);
        ApplyLayout(stride_in_bytes_);
        CNA_RENDER_LOG("VertexBuffer SetDataWithOptions: count=" << count
            << " stride=" << stride_in_bytes << " options=" << static_cast<int>(options));
    }

    EasyGLIndexBufferRenderer::EasyGLIndexBufferRenderer(int index_capacity, bool is32bit,
                                                       std::shared_ptr<::easygl::ResourceRegistry> registry)
        : thirtyTwoBit(is32bit)
        , capacity(index_capacity)
        , registry_(registry)
    {
        ibo.create();
        if (auto reg = registry_.lock()) reg->add(this);
        CNA_RENDER_LOG("IndexBuffer created: capacity=" << capacity << " 32bit=" << is32bit);
    }

    EasyGLIndexBufferRenderer::~EasyGLIndexBufferRenderer()
    {
        if (auto reg = registry_.lock()) reg->remove(this);
    }

    void EasyGLIndexBufferRenderer::release_gl_handle_only()
    {
        ibo.reset_handle_no_gl();
    }

    void EasyGLIndexBufferRenderer::recreate_gl_resource()
    {
        ibo.create();
        if (!cpu_data_.empty())
        {
            ibo.bind(::easygl::BufferTarget::ElementArray);
            ibo.set_data(::easygl::BufferTarget::ElementArray, cpu_data_.data(), cpu_data_.size());
        }
    }

    void EasyGLIndexBufferRenderer::SetData16(const void* data, int count)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint16_t);
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData16: count=" << count);
    }

    void EasyGLIndexBufferRenderer::SetData32(const void* data, int count)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint32_t);
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count);
        CNA_RENDER_LOG("IndexBuffer SetData32: count=" << count);
    }

    void EasyGLIndexBufferRenderer::SetData16WithOptions(const void* data, int count,
                                                        SetDataOptions options)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint16_t);
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        if (options == SetDataOptions::Discard) {
            const std::size_t total = static_cast<std::size_t>(capacity) * sizeof(std::uint16_t);
            ibo.set_data(::easygl::BufferTarget::ElementArray, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else if (options == SetDataOptions::NoOverwrite && !cpu_data_.empty()) {
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else {
            ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
        }
        CNA_RENDER_LOG("IndexBuffer SetData16WithOptions: count=" << count
            << " options=" << static_cast<int>(options));
    }

    void EasyGLIndexBufferRenderer::SetData32WithOptions(const void* data, int count,
                                                        SetDataOptions options)
    {
        index_count = count;
        const std::size_t byte_count = static_cast<std::size_t>(count) * sizeof(std::uint32_t);
        if (!registry_.expired())
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            cpu_data_.assign(bytes, bytes + byte_count);
        }
        ibo.bind(::easygl::BufferTarget::ElementArray);
        if (options == SetDataOptions::Discard) {
            const std::size_t total = static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
            ibo.set_data(::easygl::BufferTarget::ElementArray, nullptr,
                         total > 0 ? total : byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else if (options == SetDataOptions::NoOverwrite && !cpu_data_.empty()) {
            ibo.set_sub_data(::easygl::BufferTarget::ElementArray, data, byte_count, 0);
        } else {
            ibo.set_data(::easygl::BufferTarget::ElementArray, data, byte_count,
                         ::easygl::BufferUsage::DynamicDraw);
        }
        CNA_RENDER_LOG("IndexBuffer SetData32WithOptions: count=" << count
            << " options=" << static_cast<int>(options));
    }

    namespace
    {
        [[nodiscard]] std::string DefineStockPointSize(const char* source)
        {
            std::string result(source);
            if (result.find("gl_PointSize") != std::string::npos)
                return result;

            const std::size_t position = result.find("gl_Position");
            if (position == std::string::npos)
                return result;

            const std::size_t terminator = result.find(';', position);
            if (terminator != std::string::npos)
                result.insert(terminator + 1, "\n    gl_PointSize=1.0;");
            return result;
        }

        void CompileAndLink(::easygl::Program& prog, const char* vsrc, const char* fsrc,
                            const char* label)
        {
            const std::string definedVsrc = DefineStockPointSize(vsrc);
            const std::string adaptedVsrc =
                AdaptGlslEs300ForActiveProfile(definedVsrc.c_str(), GlShaderStageKind::Vertex);
            const std::string adaptedFsrc = AdaptGlslEs300ForActiveProfile(fsrc, GlShaderStageKind::Fragment);

            ::easygl::Shader vs(::easygl::ShaderType::Vertex);
            vs.create();
            vs.compile_from_source(adaptedVsrc.c_str());
            if (!vs.is_compiled())
                std::cerr << "[CNA EasyGL 3D] " << label << " VS failed:\n" << vs.info_log() << "\n";

            ::easygl::Shader fs(::easygl::ShaderType::Fragment);
            fs.create();
            fs.compile_from_source(adaptedFsrc.c_str());
            if (!fs.is_compiled())
                std::cerr << "[CNA EasyGL 3D] " << label << " FS failed:\n" << fs.info_log() << "\n";

            prog.create();
            prog.attach(vs);
            prog.attach(fs);
if (ProfileUsesGlslEs100())
{
            // plans/plan_glbackends.md GLB-36: these profiles' GLSL ES 1.00 shader text has no
            // layout(location=N) (GLSL ES 1.00 doesn't support it) -- rebind the SAME numeric
            // locations here, extracted from the ORIGINAL ES 3.00 source, so every
            // VertexArray/VAO attribute-binding call site elsewhere in this file (all hardcoded
            // numeric indices) keeps working unchanged.
            for (const auto& [location, name] : ExtractVertexAttribLocations(vsrc))
            {
                prog.bind_attrib_location(static_cast<unsigned int>(location), name);
            }
}
            prog.link();
            if (!prog.is_linked())
                std::cerr << "[CNA EasyGL 3D] " << label << " link failed:\n" << prog.info_log() << "\n";
        }
    }

    void EasyGLRenderer::EnsureColored3DProgram()
    {
        if (prog_colored_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec4 vColor;\n"
"in float vFogFactor;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_colored_.prog, vsrc, fsrc, "colored");
        ResolveRenderTargetOrientationUniforms(prog_colored_);
        prog_colored_.loc_wvp         = prog_colored_.prog.uniform_location("uWVP");
        prog_colored_.loc_diffuse     = prog_colored_.prog.uniform_location("uDiffuseColor");
        prog_colored_.loc_alphatest   = prog_colored_.prog.uniform_location("uAlphaTest");
        prog_colored_.loc_fog_vector = prog_colored_.prog.uniform_location("uFogVector");
        prog_colored_.loc_fog_color   = prog_colored_.prog.uniform_location("uFogColor");
        prog_colored_.loc_vertexcolor = prog_colored_.prog.uniform_location("uVertexColorEnabled");
        prog_colored_.ready           = true;
        CNA_RENDER_LOG("colored3D ready loc_wvp=" << prog_colored_.loc_wvp);
    }

    void EasyGLRenderer::EnsureTextured3DProgram()
    {
        if (prog_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_textured_.prog, vsrc, fsrc, "textured");
        ResolveRenderTargetOrientationUniforms(prog_textured_);
        prog_textured_.loc_wvp         = prog_textured_.prog.uniform_location("uWVP");
        prog_textured_.loc_diffuse     = prog_textured_.prog.uniform_location("uDiffuseColor");
        prog_textured_.loc_texture     = prog_textured_.prog.uniform_location("uTexture");
        prog_textured_.loc_alphatest   = prog_textured_.prog.uniform_location("uAlphaTest");
        prog_textured_.loc_fog_vector = prog_textured_.prog.uniform_location("uFogVector");
        prog_textured_.loc_fog_color   = prog_textured_.prog.uniform_location("uFogColor");
        prog_textured_.ready           = true;
        CNA_RENDER_LOG("textured3D ready loc_wvp=" << prog_textured_.loc_wvp);
    }

    void EasyGLRenderer::EnsureColoredTextured3DProgram()
    {
        if (prog_col_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_col_textured_.prog, vsrc, fsrc, "col+textured");
        ResolveRenderTargetOrientationUniforms(prog_col_textured_);
        prog_col_textured_.loc_wvp         = prog_col_textured_.prog.uniform_location("uWVP");
        prog_col_textured_.loc_texture     = prog_col_textured_.prog.uniform_location("uTexture");
        prog_col_textured_.loc_diffuse     = prog_col_textured_.prog.uniform_location("uDiffuseColor");
        prog_col_textured_.loc_alphatest   = prog_col_textured_.prog.uniform_location("uAlphaTest");
        prog_col_textured_.loc_fog_vector = prog_col_textured_.prog.uniform_location("uFogVector");
        prog_col_textured_.loc_fog_color   = prog_col_textured_.prog.uniform_location("uFogColor");
        prog_col_textured_.loc_vertexcolor = prog_col_textured_.prog.uniform_location("uVertexColorEnabled");
        prog_col_textured_.ready           = true;
        CNA_RENDER_LOG("col+textured3D ready loc_wvp=" << prog_col_textured_.loc_wvp);
    }

    void EasyGLRenderer::EnsureLit3DProgram()
    {
        if (prog_lit_textured_.ready) return;

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
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_.prog, vsrc, fsrc, "lit+textured");
        ResolveRenderTargetOrientationUniforms(prog_lit_textured_);
        ResolveShadowUniforms(prog_lit_textured_);
        prog_lit_textured_.loc_wvp         = prog_lit_textured_.prog.uniform_location("uWVP");
        prog_lit_textured_.loc_world       = prog_lit_textured_.prog.uniform_location("uWorld");
        prog_lit_textured_.loc_normalmat   = prog_lit_textured_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_.loc_diffuse     = prog_lit_textured_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_.loc_lighting_enabled = prog_lit_textured_.prog.uniform_location("uLightingEnabled");
        prog_lit_textured_.loc_ambient     = prog_lit_textured_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_.loc_l0dir       = prog_lit_textured_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_.loc_l0diff      = prog_lit_textured_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_.loc_l1dir       = prog_lit_textured_.prog.uniform_location("uLight1Dir");
        prog_lit_textured_.loc_l1diff      = prog_lit_textured_.prog.uniform_location("uLight1Diffuse");
        prog_lit_textured_.loc_l2dir       = prog_lit_textured_.prog.uniform_location("uLight2Dir");
        prog_lit_textured_.loc_l2diff      = prog_lit_textured_.prog.uniform_location("uLight2Diffuse");
        prog_lit_textured_.loc_l0spec      = prog_lit_textured_.prog.uniform_location("uLight0Specular");
        prog_lit_textured_.loc_l1spec      = prog_lit_textured_.prog.uniform_location("uLight1Specular");
        prog_lit_textured_.loc_l2spec      = prog_lit_textured_.prog.uniform_location("uLight2Specular");
        prog_lit_textured_.loc_specularcolor = prog_lit_textured_.prog.uniform_location("uSpecularColor");
        prog_lit_textured_.loc_specularpower = prog_lit_textured_.prog.uniform_location("uSpecularPower");
        prog_lit_textured_.loc_eyepos      = prog_lit_textured_.prog.uniform_location("uEyePosition");
        prog_lit_textured_.loc_emissive    = prog_lit_textured_.prog.uniform_location("uEmissiveColor");
        prog_lit_textured_.loc_texture     = prog_lit_textured_.prog.uniform_location("uTexture");
        prog_lit_textured_.loc_alphatest   = prog_lit_textured_.prog.uniform_location("uAlphaTest");
        prog_lit_textured_.loc_fog_vector = prog_lit_textured_.prog.uniform_location("uFogVector");
        prog_lit_textured_.loc_fog_color   = prog_lit_textured_.prog.uniform_location("uFogColor");
        prog_lit_textured_.loc_vertexcolor = prog_lit_textured_.prog.uniform_location("uVertexColorEnabled");
        prog_lit_textured_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D ready loc_wvp=" << prog_lit_textured_.loc_wvp);
    }

    // Task 1102 (plans/plan_graphics.md Phase 80 / plans/plan_dx9.md Divergence 1): real XNA's
    // BasicEffect/SkinnedEffect default to PreferPerPixelLighting=false, which selects a
    // per-vertex-lit shader family (VSBasicVertexLighting*) -- lighting is computed ONCE per
    // vertex and Gouraud-interpolated across the triangle, not re-evaluated per fragment.
    // EnsureLit3DProgram() above is the PreferPerPixelLighting=true family; this is its
    // per-vertex-lit sibling, selected by SelectProgram() when the flag is false (XNA's own
    // default). Identical Blinn-Phong math to EnsureLit3DProgram() (FNA's own Lighting.fxh
    // ComputeLights(), same formula, same inputs) -- only the STAGE it runs in changes: the lit
    // RGB (ambient+diffuse sum) and specular RGB are computed once per vertex and passed as
    // `out` varyings, and the fragment shader's own math (texture sample, alpha test, fog) is
    // structurally identical to EnsureLit3DProgram()'s, just reading the interpolated varyings
    // instead of recomputing them. Declares the SAME uniform names as EnsureLit3DProgram() (just
    // in the vertex stage for the lighting-specific ones) so BindDrawParams() needs no changes at
    // all -- it already looks up each uniform generically by Prog3D's own loc_* fields, regardless
    // of which shader stage actually declared that uniform in the linked program.
    void EasyGLRenderer::EnsureLit3DVertexLitProgram()
    {
        if (prog_lit_textured_vertexlit_.ready) return;

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
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_vertexlit_.prog, vsrc, fsrc, "lit+textured (vertex-lit)");
        ResolveRenderTargetOrientationUniforms(prog_lit_textured_vertexlit_);
        prog_lit_textured_vertexlit_.loc_wvp         = prog_lit_textured_vertexlit_.prog.uniform_location("uWVP");
        prog_lit_textured_vertexlit_.loc_world       = prog_lit_textured_vertexlit_.prog.uniform_location("uWorld");
        prog_lit_textured_vertexlit_.loc_normalmat   = prog_lit_textured_vertexlit_.prog.uniform_location("uNormalMatrix");
        prog_lit_textured_vertexlit_.loc_diffuse     = prog_lit_textured_vertexlit_.prog.uniform_location("uDiffuseColor");
        prog_lit_textured_vertexlit_.loc_ambient     = prog_lit_textured_vertexlit_.prog.uniform_location("uAmbientColor");
        prog_lit_textured_vertexlit_.loc_l0dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Dir");
        prog_lit_textured_vertexlit_.loc_l0diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Diffuse");
        prog_lit_textured_vertexlit_.loc_l1dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Dir");
        prog_lit_textured_vertexlit_.loc_l1diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Diffuse");
        prog_lit_textured_vertexlit_.loc_l2dir       = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Dir");
        prog_lit_textured_vertexlit_.loc_l2diff      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Diffuse");
        prog_lit_textured_vertexlit_.loc_l0spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight0Specular");
        prog_lit_textured_vertexlit_.loc_l1spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight1Specular");
        prog_lit_textured_vertexlit_.loc_l2spec      = prog_lit_textured_vertexlit_.prog.uniform_location("uLight2Specular");
        prog_lit_textured_vertexlit_.loc_specularcolor = prog_lit_textured_vertexlit_.prog.uniform_location("uSpecularColor");
        prog_lit_textured_vertexlit_.loc_specularpower = prog_lit_textured_vertexlit_.prog.uniform_location("uSpecularPower");
        prog_lit_textured_vertexlit_.loc_eyepos      = prog_lit_textured_vertexlit_.prog.uniform_location("uEyePosition");
        prog_lit_textured_vertexlit_.loc_emissive    = prog_lit_textured_vertexlit_.prog.uniform_location("uEmissiveColor");
        prog_lit_textured_vertexlit_.loc_texture     = prog_lit_textured_vertexlit_.prog.uniform_location("uTexture");
        prog_lit_textured_vertexlit_.loc_alphatest   = prog_lit_textured_vertexlit_.prog.uniform_location("uAlphaTest");
        prog_lit_textured_vertexlit_.loc_fog_vector = prog_lit_textured_vertexlit_.prog.uniform_location("uFogVector");
        prog_lit_textured_vertexlit_.loc_fog_color   = prog_lit_textured_vertexlit_.prog.uniform_location("uFogColor");
        prog_lit_textured_vertexlit_.loc_vertexcolor = prog_lit_textured_vertexlit_.prog.uniform_location("uVertexColorEnabled");
        prog_lit_textured_vertexlit_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D (vertex-lit) ready loc_wvp=" << prog_lit_textured_vertexlit_.loc_wvp);
    }

    void EasyGLRenderer::EnsureDualTextured3DProgram()
    {
        if (prog_dual_textured_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
"in float vFogFactor;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uTexture2;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 base=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,cnaSampleUV(vUV,uRtFlipV.y))*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_dual_textured_.prog, vsrc, fsrc, "dual+textured");
        ResolveRenderTargetOrientationUniforms(prog_dual_textured_);
        prog_dual_textured_.loc_wvp         = prog_dual_textured_.prog.uniform_location("uWVP");
        prog_dual_textured_.loc_texture     = prog_dual_textured_.prog.uniform_location("uTexture");
        prog_dual_textured_.loc_texture2    = prog_dual_textured_.prog.uniform_location("uTexture2");
        prog_dual_textured_.loc_diffuse     = prog_dual_textured_.prog.uniform_location("uDiffuseColor");
        prog_dual_textured_.loc_alphatest   = prog_dual_textured_.prog.uniform_location("uAlphaTest");
        prog_dual_textured_.loc_fog_vector = prog_dual_textured_.prog.uniform_location("uFogVector");
        prog_dual_textured_.loc_fog_color   = prog_dual_textured_.prog.uniform_location("uFogColor");
        prog_dual_textured_.ready           = true;
        CNA_RENDER_LOG("dual+textured3D ready loc_wvp=" << prog_dual_textured_.loc_wvp);
    }

    void EasyGLRenderer::EnsureDualTexturedColored3DProgram()
    {
        if (prog_dual_textured_colored_.ready) return;

        // Task 889: stride-24 (VertexPositionColorTexture) variant of the dual-texture shader
        // above -- reads the vertex color and gates it by VertexColorEnabled, mirroring
        // EnsureColoredTextured3DProgram()'s already-correct BasicEffect formula.
        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec4 aColor;\n"
"layout(location=2) in vec2 aUV;\n"
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform vec4 uFogVector;\n"
"out vec4 vColor;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vColor=aColor;\n"
"    vUV=aUV;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"uniform sampler2D uTexture2;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec4 uAlphaTest;\n"
"uniform vec3 uFogColor;\n"
"uniform float uVertexColorEnabled;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
"void main(){\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    vec4 base=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    base.rgb*=2.0;\n"
"    FragColor=base*texture(uTexture2,cnaSampleUV(vUV,uRtFlipV.y))*vc*uDiffuseColor;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_dual_textured_colored_.prog, vsrc, fsrc, "dual+textured+colored");
        ResolveRenderTargetOrientationUniforms(prog_dual_textured_colored_);
        prog_dual_textured_colored_.loc_wvp         = prog_dual_textured_colored_.prog.uniform_location("uWVP");
        prog_dual_textured_colored_.loc_texture     = prog_dual_textured_colored_.prog.uniform_location("uTexture");
        prog_dual_textured_colored_.loc_texture2    = prog_dual_textured_colored_.prog.uniform_location("uTexture2");
        prog_dual_textured_colored_.loc_diffuse     = prog_dual_textured_colored_.prog.uniform_location("uDiffuseColor");
        prog_dual_textured_colored_.loc_alphatest   = prog_dual_textured_colored_.prog.uniform_location("uAlphaTest");
        prog_dual_textured_colored_.loc_fog_vector = prog_dual_textured_colored_.prog.uniform_location("uFogVector");
        prog_dual_textured_colored_.loc_fog_color   = prog_dual_textured_colored_.prog.uniform_location("uFogColor");
        prog_dual_textured_colored_.loc_vertexcolor = prog_dual_textured_colored_.prog.uniform_location("uVertexColorEnabled");
        prog_dual_textured_colored_.ready           = true;
        CNA_RENDER_LOG("dual+textured+colored3D ready loc_wvp=" << prog_dual_textured_colored_.loc_wvp);
    }

    void EasyGLRenderer::EnsureEnvMapped3DProgram()
    {
        if (prog_env_mapped_.ready) return;

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
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_env_mapped_.prog, vsrc, fsrc, "env+mapped");
        ResolveRenderTargetOrientationUniforms(prog_env_mapped_);
        auto& p = prog_env_mapped_;
        p.loc_wvp           = p.prog.uniform_location("uWVP");
        p.loc_normalmat     = p.prog.uniform_location("uNormalMatrix");
        p.loc_world         = p.prog.uniform_location("uWorld");
        p.loc_eyepos        = p.prog.uniform_location("uEyePosition");
        p.loc_texture       = p.prog.uniform_location("uTexture");
        p.loc_envmap        = p.prog.uniform_location("uEnvMap");
        p.loc_diffuse       = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive      = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir         = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff        = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir         = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff        = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir         = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff        = p.prog.uniform_location("uLight2Diffuse");
        p.loc_envmap_amount   = p.prog.uniform_location("uEnvMapAmount");
        p.loc_envmap_spec     = p.prog.uniform_location("uEnvMapSpecular");
        p.loc_fresnel_enabled = p.prog.uniform_location("uFresnelEnabled");
        p.loc_fresnel_factor  = p.prog.uniform_location("uFresnelFactor");
        p.loc_alphatest       = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector     = p.prog.uniform_location("uFogVector");
        p.loc_fog_color       = p.prog.uniform_location("uFogColor");
        p.ready               = true;
        CNA_RENDER_LOG("env+mapped3D ready loc_wvp=" << p.loc_wvp);
    }

    void EasyGLRenderer::EnsureSkinnedProgram()
    {
        if (prog_skinned_.ready) return;

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
"    vec3 skinnedNormal=cnaSkinNormal(mat3(skinMat),aNormal);\n"
"    float skinnedNormalLen=length(skinnedNormal);\n"
"    vec3 boneNormal=(skinnedNormalLen>1e-6)?(skinnedNormal/skinnedNormalLen):aNormal;\n"
// REMED-GFX-006: compose the bone-skin normal with the outer world normal matrix
// (uNormalMatrix = transpose(inverse(World3x3)), CPU-precomputed in BindDrawParams() exactly as
// every non-skinned lit program here already receives it). FNA's SkinnedEffect.fx establishes the
// composition order; GLTF-264 strengthens its direct bone 3x3 to inverse-transpose because glTF
// joints may carry non-uniform scale. This shader also used to drop the outer world factor entirely
// (audit Variant A), so any rotated or non-uniformly-scaled skinned model was lit as if World were
// identity. The fragment stage re-normalizes vNormal.
"    vNormal=uNormalMatrix*cnaInstanceDirection(boneNormal);\n"
"    vUV=aUV;\n"
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vColor=aColor;\n"
// REMED-GFX-010: FNA EffectHelpers.SetFogVector / Common.fxh ComputeFogFactor. Fog is a true
// VIEW-SPACE Z term: fogFactor = saturate(dot(pos, uFogVector)), where uFogVector bakes the third
// column of World*View (CPU-side, GpuDrawParams.fogVector). EasyGL's vFogFactor is the inverse
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
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
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_skinned_.prog, vsrc, fsrc, "skinned");
        ResolveRenderTargetOrientationUniforms(prog_skinned_);
        ResolveShadowUniforms(prog_skinned_);
        auto& p = prog_skinned_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_l0spec    = p.prog.uniform_location("uLight0Specular");
        p.loc_l1spec    = p.prog.uniform_location("uLight1Specular");
        p.loc_l2spec    = p.prog.uniform_location("uLight2Specular");
        p.loc_specularcolor = p.prog.uniform_location("uSpecularColor");
        p.loc_specularpower = p.prog.uniform_location("uSpecularPower");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.ready         = true;
        CNA_RENDER_LOG("skinned3D ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    // Task 1102b (plans/plan_graphics.md Phase 80 / plans/plan_dx9.md Divergence 1): SkinnedEffect's own
    // per-vertex-lit sibling, mirroring Task 1102's EnsureLit3DVertexLitProgram() for BasicEffect
    // exactly -- same technique (move FNA's Lighting.fxh ComputeLights() Blinn-Phong math from the
    // fragment stage into the vertex stage, Gouraud-interpolate the result via varyings, keep the
    // fragment stage's non-lighting math -- texture sample, alpha test, fog -- structurally
    // identical), applied to EnsureSkinnedProgram() above (the PreferPerPixelLighting=true family)
    // instead of EnsureLit3DProgram(). The skinning itself is unchanged -- only WHERE lighting is
    // evaluated moves, exactly like Task 1102's own BasicEffect case. Selected by SelectProgram()
    // when params.skinned && params.lightingEnabled && !params.preferPerPixelLighting (XNA's own
    // default). No separate uAmbientColor uniform here, matching EnsureSkinnedProgram()'s own
    // shape: SkinnedEffect::FillGpuDrawParams() already pre-folds ambient into emissiveColor
    // (verified at that call site), so BindDrawParams()'s existing "p.loc_ambient < 0" gating
    // (which already distinguishes the ambient-having BasicEffect programs from the
    // ambient-less EnvironmentMapEffect/SkinnedEffect ones) correctly routes this new program's
    // light/specular uniform uploads too, with zero BindDrawParams() changes needed -- identical to
    // Task 1102's own finding for BasicEffect.
    void EasyGLRenderer::EnsureSkinnedVertexLitProgram()
    {
        if (prog_skinned_vertexlit_.ready) return;

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
// "keep" (mix(uFogColor,color,vFogFactor)), so vFogFactor = 1 - saturate(dot(pos, uFogVector)).
// uFogVector is 0 when fog is disabled (=> keep 1, no-op) and (0,0,0,1) for the fogStart==fogEnd
// degenerate case (=> keep 0, fully fogged) -- all handled CPU-side, matching FNA exactly.
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"    vec3 worldPos=(uWorld*cnaPos).xyz;\n"
// Same degenerate-blend-normal guard as EnsureSkinnedProgram() above (see its own
// comment for the root cause) -- this vertex-lit sibling does the identical skinning
// and normal transform, just with lighting evaluated per-vertex instead of per-pixel.
"    vec3 skinnedNormal=cnaSkinNormal(mat3(skinMat),aNormal);\n"
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
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_skinned_vertexlit_.prog, vsrc, fsrc, "skinned (vertex-lit)");
        ResolveRenderTargetOrientationUniforms(prog_skinned_vertexlit_);
        auto& p = prog_skinned_vertexlit_;
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_l0spec    = p.prog.uniform_location("uLight0Specular");
        p.loc_l1spec    = p.prog.uniform_location("uLight1Specular");
        p.loc_l2spec    = p.prog.uniform_location("uLight2Specular");
        p.loc_specularcolor = p.prog.uniform_location("uSpecularColor");
        p.loc_specularpower = p.prog.uniform_location("uSpecularPower");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.ready         = true;
        CNA_RENDER_LOG("skinned3D (vertex-lit) ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    // plans/plan_cnj.md CNB-58 (Phase 13A): real glTF metallic-roughness BRDF (glTF 2.0 spec Appendix
    // B) -- GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX visibility, Schlick
    // Fresnel -- driven by the same 3-DirectionalLight + AmbientLightColor convention every other
    // CNA stock effect already uses (so existing scene-lighting setup code transfers directly),
    // rather than image-based lighting (a much larger, separate feature: irradiance/prefiltered
    // environment maps + a BRDF LUT). Normal mapping via a per-pixel TBN basis built from the
    // vertex tangent (re-orthogonalized against the interpolated normal) and glTF's own
    // bitangent-handedness-sign convention (Bitangent = cross(Normal,Tangent.xyz)*Tangent.w),
    // including GLTF-176's per-draw determinant correction under mirrored direction transforms.
    // This was CNA's first PBR program (CNB-58); the same normalized GpuDrawParams contract and
    // reference BRDF are now implemented by every PBR-capable renderer, with a cross-renderer
    // source audit guarding the fields whose native bindings necessarily differ by backend.
    void EasyGLRenderer::EnsurePbrProgram(bool dualUv)
    {
        // GLTF-182/183: keep the stride-48 program source byte-equivalent to the established
        // single-UV shader. Merely adding an unused varying/selector changed thousands of
        // llvmpipe fragments by one RGB unit, violating the zero-tolerance L7 oracle. Stride 60
        // alone pays for the dual-UV variant, so existing content keeps its exact pixel path.
        Prog3D& program = dualUv ? prog_pbr_dual_uv_ : prog_pbr_;
        if (program.ready) return;

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
+ CnaGlIblDecl(!ProfileUsesGlslEs100()) +
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

        CompileAndLink(program.prog, vsrc.c_str(), fsrc.c_str(),
                       dualUv ? "pbr_dual_uv" : "pbr");
        ResolveRenderTargetOrientationUniforms(program);
        ResolveShadowUniforms(program);
        ResolveIblUniforms(program);
        auto& p = program;
        // GLTF-462: -1 on the single-UV program, which has no colour slot to gate; BindDrawParams
        // skips a negative location like every other optional uniform in Prog3D.
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_ambient   = p.prog.uniform_location("uAmbientColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_pbr_normalmap     = p.prog.uniform_location("uNormalMap");
        p.loc_pbr_mr            = p.prog.uniform_location("uMetallicRoughnessMap");
        p.loc_pbr_emissivemap   = p.prog.uniform_location("uEmissiveMap");
        p.loc_pbr_occlusionmap  = p.prog.uniform_location("uOcclusionMap");
        p.loc_pbr_specularmap   = p.prog.uniform_location("uSpecularMap");
        p.loc_pbr_specularcolormap = p.prog.uniform_location("uSpecularColorMap");
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_pbr_dielectric_fresnel = p.prog.uniform_location("uDielectricFresnel");
        p.loc_pbr_specular_fresnel_inputs =
            p.prog.uniform_location("uSpecularFresnelInputs");
        p.loc_pbr_srgb          = p.prog.uniform_location("uSrgb");
        p.loc_pbr_normalscale   = p.prog.uniform_location("uNormalScale");
        p.loc_pbr_occlstrength  = p.prog.uniform_location("uOcclusionStrength");
        p.loc_pbr_texcoordsets  = p.prog.uniform_location("uTextureCoordinateSets");
        p.loc_pbr_occlusiontexcoordset =
            p.prog.uniform_location("uOcclusionTextureCoordinateSet");
        p.loc_pbr_specular_texcoordsets =
            p.prog.uniform_location("uSpecularTextureCoordinateSets");
        for (std::size_t row = 0; row < p.loc_pbr_texture_transform_rows.size(); ++row)
        {
            p.loc_pbr_texture_transform_rows[row] = p.prog.uniform_location(
                "uTextureTransformRows[" + std::to_string(row) + "]");
        }
        for (std::size_t row = 0; row < p.loc_pbr_specular_texture_transform_rows.size(); ++row)
        {
            p.loc_pbr_specular_texture_transform_rows[row] = p.prog.uniform_location(
                "uSpecularTextureTransformRows[" + std::to_string(row) + "]");
        }
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.ready         = true;
        CNA_RENDER_LOG("pbr3D ready loc_wvp=" << p.loc_wvp);
    }

    // PBR + skinning combo: EnsureSkinnedProgram()'s bone-palette vertex transform (applied to
    // Position, Normal, and now also Tangent, since a skinned normal map needs a skinned TBN
    // basis too) feeding EnsurePbrProgram()'s own fragment-stage BRDF unchanged -- the two
    // programs' logic is additive, not a new algorithm (SkinnedPbrEffect, PBR+skinning combo).
    void EasyGLRenderer::EnsurePbrSkinnedProgram(bool dualUv)
    {
        // Match the rigid program's compatibility split: stride 68 retains the old shader shape,
        // while only stride 76 declares and samples aUV1.
        Prog3D& program = dualUv ? prog_pbr_skinned_dual_uv_ : prog_pbr_skinned_;
        if (program.ready) return;

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
+ CnaGlIblDecl(!ProfileUsesGlslEs100()) +
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

        CompileAndLink(program.prog, vsrc.c_str(), fsrc.c_str(),
                       dualUv ? "pbr_skinned_dual_uv" : "pbr_skinned");
        ResolveRenderTargetOrientationUniforms(program);
        ResolveShadowUniforms(program);
        ResolveIblUniforms(program);
        auto& p = program;
        // GLTF-463: -1 on the single-UV skinned program, which has no colour slot to gate;
        // BindDrawParams skips a negative location like every other optional uniform in Prog3D.
        p.loc_vertexcolor = p.prog.uniform_location("uVertexColorEnabled");
        p.loc_wvp       = p.prog.uniform_location("uWVP");
        p.loc_world     = p.prog.uniform_location("uWorld");
        p.loc_normalmat = p.prog.uniform_location("uNormalMatrix");
        p.loc_bones     = p.prog.uniform_location("uBones[0]");
        p.loc_weightsPerVertex = p.prog.uniform_location("uWeightsPerVertex");
        p.loc_diffuse   = p.prog.uniform_location("uDiffuseColor");
        p.loc_ambient   = p.prog.uniform_location("uAmbientColor");
        p.loc_emissive  = p.prog.uniform_location("uEmissiveColor");
        p.loc_l0dir     = p.prog.uniform_location("uLight0Dir");
        p.loc_l0diff    = p.prog.uniform_location("uLight0Diffuse");
        p.loc_l1dir     = p.prog.uniform_location("uLight1Dir");
        p.loc_l1diff    = p.prog.uniform_location("uLight1Diffuse");
        p.loc_l2dir     = p.prog.uniform_location("uLight2Dir");
        p.loc_l2diff    = p.prog.uniform_location("uLight2Diffuse");
        p.loc_eyepos    = p.prog.uniform_location("uEyePosition");
        p.loc_texture   = p.prog.uniform_location("uTexture");
        p.loc_pbr_normalmap     = p.prog.uniform_location("uNormalMap");
        p.loc_pbr_mr            = p.prog.uniform_location("uMetallicRoughnessMap");
        p.loc_pbr_emissivemap   = p.prog.uniform_location("uEmissiveMap");
        p.loc_pbr_occlusionmap  = p.prog.uniform_location("uOcclusionMap");
        p.loc_pbr_specularmap   = p.prog.uniform_location("uSpecularMap");
        p.loc_pbr_specularcolormap = p.prog.uniform_location("uSpecularColorMap");
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_pbr_dielectric_fresnel = p.prog.uniform_location("uDielectricFresnel");
        p.loc_pbr_specular_fresnel_inputs =
            p.prog.uniform_location("uSpecularFresnelInputs");
        p.loc_pbr_srgb          = p.prog.uniform_location("uSrgb");
        p.loc_pbr_normalscale   = p.prog.uniform_location("uNormalScale");
        p.loc_pbr_occlstrength  = p.prog.uniform_location("uOcclusionStrength");
        p.loc_pbr_texcoordsets  = p.prog.uniform_location("uTextureCoordinateSets");
        p.loc_pbr_occlusiontexcoordset =
            p.prog.uniform_location("uOcclusionTextureCoordinateSet");
        p.loc_pbr_specular_texcoordsets =
            p.prog.uniform_location("uSpecularTextureCoordinateSets");
        for (std::size_t row = 0; row < p.loc_pbr_texture_transform_rows.size(); ++row)
        {
            p.loc_pbr_texture_transform_rows[row] = p.prog.uniform_location(
                "uTextureTransformRows[" + std::to_string(row) + "]");
        }
        for (std::size_t row = 0; row < p.loc_pbr_specular_texture_transform_rows.size(); ++row)
        {
            p.loc_pbr_specular_texture_transform_rows[row] = p.prog.uniform_location(
                "uSpecularTextureTransformRows[" + std::to_string(row) + "]");
        }
        p.loc_alphatest = p.prog.uniform_location("uAlphaTest");
        p.loc_fog_vector = p.prog.uniform_location("uFogVector");
        p.loc_fog_color   = p.prog.uniform_location("uFogColor");
        p.ready         = true;
        CNA_RENDER_LOG("pbr_skinned3D ready loc_wvp=" << p.loc_wvp << " loc_bones=" << p.loc_bones);
    }

    void EasyGLRenderer::EnsureDefaultWhiteTexture()
    {
        if (default_white_texture_ready_) return;
        static const uint8_t white[4] = {255, 255, 255, 255};
        default_white_texture_.create();
        default_white_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, white);
        default_white_texture_ready_ = true;
    }

    // plans/plan_cnj.md CNB-58 (Phase 13A): fallback for PbrEffect::NormalMap when unbound -- a "flat"
    // tangent-space normal (0,0,1) encoded as RGB (128,128,255), so the sampled/decoded (rgb*2-1)
    // normal is exactly the geometric normal (no perturbation). The other 3 PBR map fallbacks
    // (metallic-roughness, emissive, occlusion) all reuse the existing default_white_texture_
    // instead -- their respective factor/no-op semantics already make (1,1,1,1) the correct
    // "map absent" value (factor*1.0=factor; emissive tint*1.0=tint; occlusion 1.0=unoccluded).
    void EasyGLRenderer::EnsureDefaultFlatNormalTexture()
    {
        if (default_flat_normal_texture_ready_) return;
        static const uint8_t flatNormal[4] = {128, 128, 255, 255};
        default_flat_normal_texture_.create();
        default_flat_normal_texture_.set_image_2d(::easygl::TextureTarget::Texture2D, 0, 1, 1, flatNormal);
        default_flat_normal_texture_ready_ = true;
    }

    /// REMED-GFX-234: does the declaration name this usage at all?
    ///
    /// The stock program a draw gets is still chosen by byte stride (REMED-GFX-217 is open), while
    /// its attributes are bound from the declaration's own offsets (REMED-GFX-218 landed). Where a
    /// stride is ambiguous, that pair silently drops whatever the chosen program has no input for,
    /// so the stride cases that can be ambiguous ask the declaration first.
    [[nodiscard]] bool DeclarationNamesUsage(
        const std::vector<VertexElement>& declaredElements,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
    {
        for (const VertexElement& element : declaredElements)
        {
            if (element.getVertexElementUsageProperty() == usage) { return true; }
        }
        return false;
    }

    // REMED-GFX-218 / REMED-GFX-DECL-GUARD: the ONE place that decides which stock program a draw
    // gets. SelectProgram() below and StockProgramInputsEXT() both read this, so the program a
    // draw is bound to and the input shape it is checked against can never drift apart.
    EasyGLRenderer::StockProgramShape EasyGLRenderer::SelectStockProgramShape(
        std::size_t stride, const GpuDrawParams& params,
        const std::vector<VertexElement>& declaredElements)
    {
        if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned;
        if (params.pbr) return StockProgramShape::Pbr;
        // plans/plan_modern.md MOD-840. A receiving draw is forced onto the per-pixel family whatever
        // PreferPerPixelLighting says. Per-vertex lighting evaluates the shadow lookup at the
        // corners and interpolates the result across the triangle, so a ground plane drawn as two
        // large triangles would carry one shadow value per corner -- a gradient, not a shadow.
        // The property is not a request for a particular quality of shadow, it is a request for
        // Gouraud shading, and reception is a CNAEXT extension that the property predates.
        const bool receivesShadow = params.shadowsEnabled && params.shadowMap != nullptr;
        if (params.skinned)
        {
            // Task 1102b (plans/plan_dx9.md Divergence 1): real XNA's SkinnedEffect defaults
            // PreferPerPixelLighting=false too, same as BasicEffect (Task 1102). Only
            // meaningfully distinct while lighting is actually on, same reasoning as Task 1102's
            // own stride-32 gate.
            return (params.lightingEnabled && !params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::SkinnedVertexLit
                       : StockProgramShape::Skinned;
        }
        if (params.envMapping) return StockProgramShape::EnvMapped;
        if (params.dualTexture)
        {
            // Task 889: stride 24 (VertexPositionColorTexture) gets its own vertex-color-aware
            // program; stride 20 (VertexPositionTexture) keeps the original color-less shader.
            return stride == 24 ? StockProgramShape::DualTexturedColored
                                : StockProgramShape::DualTextured;
        }
        // SAMPLE-002: XNA application-defined vertices are selected by their declaration, not
        // merely by stride. Position+Normal is 24 bytes just like VertexPositionColorTexture,
        // but BasicEffect must light it and must not reinterpret the normal as color/UV data.
        const bool positionNormal =
            stride == 24 && declaredElements.size() == 2 &&
            declaredElements[0] == VertexElement(
                0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0) &&
            declaredElements[1] == VertexElement(
                12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
        if (positionNormal)
        {
            return (params.lightingEnabled && !params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::LitVertexLitUntextured
                       : StockProgramShape::LitUntextured;
        }
        // plans/plan_fx.md FX-125: Position+Normal+Color+TextureCoordinate is 36 bytes, which is
        // what the stock ModelProcessor emits for a mesh that carries a colour channel -- and it
        // then sets BasicEffect.VertexColorEnabled, exactly as XNA does. No case matched 36, so
        // such a mesh fell through to the unlit prog_colored_ below and lost all its lighting:
        // SAMPLE-047's Sphere01 rendered as a flat green disc where the original has a shaded
        // sphere with a specular highlight. The lit programs now carry the colour attribute, so
        // this routes to the same family a stride-32 lit vertex takes.
        if (stride == 36 && params.lightingEnabled)
        {
            return (!params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::LitVertexLit
                       : StockProgramShape::Lit;
        }
        switch (stride)
        {
        case 20: return StockProgramShape::Textured;
        case 24: return StockProgramShape::ColoredTextured;
        case 32:
            // REMED-GFX-234: stride 32 is VertexPositionNormalTexture's, and this branch assumed
            // that was the only way to reach it. A Position+Colour vertex padded to 32 reaches it
            // too, and the lit programs take {aPos, aNormal, aUV} -- no colour input -- so the
            // declared Colour element had nothing to bind to and the draw rendered correct
            // geometry with its colour silently dropped. A declaration that names no normal cannot
            // be a lit vertex whatever its stride, so ask it. An absent declaration keeps the
            // stride's answer, which is the only thing there is to go on.
            if (!declaredElements.empty() &&
                !DeclarationNamesUsage(declaredElements, VertexElementUsage::Normal))
            {
                return StockProgramShape::Colored;
            }
            // Task 1102 (plans/plan_dx9.md Divergence 1): real XNA's BasicEffect defaults
            // PreferPerPixelLighting=false (per-vertex/Gouraud-shaded lighting), the opposite of
            // what this renderer rendered unconditionally before this task. Only meaningfully
            // distinct while lighting is actually on -- with lighting disabled, both programs
            // degenerate to the identical trivial ambient=(1,1,1) case (see BindDrawParams()'s
            // own else-branch), so the existing pixel-lit program stays selected there to avoid
            // an unnecessary program switch.
            return (params.lightingEnabled && !params.preferPerPixelLighting && !receivesShadow)
                       ? StockProgramShape::LitVertexLit
                       : StockProgramShape::Lit;
        default: return StockProgramShape::Colored;
        }
    }

    EasyGLRenderer::Prog3D& EasyGLRenderer::SelectProgram(
        std::size_t stride, const GpuDrawParams& params,
        const std::vector<VertexElement>& declaredElements)
    {
        switch (SelectStockProgramShape(stride, params, declaredElements))
        {
        case StockProgramShape::PbrSkinned:
            // GLTF-463: stride 80 is stride 76 with a colour appended, so it takes the same
            // dual-UV skinned program; the colour slot is gated by uVertexColorEnabled.
            EnsurePbrSkinnedProgram(stride == 76 || stride == 80);
            return (stride == 76 || stride == 80) ? prog_pbr_skinned_dual_uv_
                                                  : prog_pbr_skinned_;
        case StockProgramShape::Pbr:
            EnsurePbrProgram(stride == 60);
            return stride == 60 ? prog_pbr_dual_uv_ : prog_pbr_;
        case StockProgramShape::SkinnedVertexLit:
            EnsureSkinnedVertexLitProgram();    return prog_skinned_vertexlit_;
        case StockProgramShape::Skinned:
            EnsureSkinnedProgram();             return prog_skinned_;
        case StockProgramShape::EnvMapped:
            EnsureEnvMapped3DProgram();         return prog_env_mapped_;
        case StockProgramShape::DualTexturedColored:
            EnsureDualTexturedColored3DProgram(); return prog_dual_textured_colored_;
        case StockProgramShape::DualTextured:
            EnsureDualTextured3DProgram();      return prog_dual_textured_;
        case StockProgramShape::Textured:
            EnsureTextured3DProgram();          return prog_textured_;
        case StockProgramShape::ColoredTextured:
            EnsureColoredTextured3DProgram();   return prog_col_textured_;
        case StockProgramShape::LitVertexLitUntextured:
        case StockProgramShape::LitVertexLit:
            EnsureLit3DVertexLitProgram();      return prog_lit_textured_vertexlit_;
        case StockProgramShape::LitUntextured:
        case StockProgramShape::Lit:
            EnsureLit3DProgram();               return prog_lit_textured_;
        case StockProgramShape::Colored:
            break;
        }
        EnsureColored3DProgram();
        return prog_colored_;
    }

    // REMED-GFX-218 / SAMPLE-005: what each stock program declares, in attribute-location order.
    // The same location means different things in different programs, so validation and binding
    // select this per-program table, then locate each input by XNA usage/index in the caller's
    // declaration. Custom effects keep their separate declaration-order convention.
    void EasyGLRenderer::RequireDeclarationFitsStockProgramEXT(
        const std::vector<VertexElement>& declaredElements, std::size_t stride,
        const GpuDrawParams& params)
    {
        using CNA::Internal::Graphics::StockProgramInput;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        static constexpr StockProgramInput kPos{
            VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
        static constexpr StockProgramInput kColor{
            VertexElementUsage::Color, 0, VertexElementFormat::Color, "aColor"};
        static constexpr StockProgramInput kUv{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        static constexpr StockProgramInput kUv1{
            VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};
        static constexpr StockProgramInput kNormal{
            VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};
        static constexpr StockProgramInput kTangent{
            VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4, "aTangent"};
        static constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        // FX-127: Vector4 is as legal a spelling of BLENDINDICES as Byte4 -- the format describes
        // the bytes, the shader register is a float4 either way -- and a content processor may
        // write either. CustomModelAnimation's own SkinnedModelProcessor writes Vector4.
        static constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4, "aBoneIndices",
            VertexElementFormat::Vector4};

        static constexpr StockProgramInput kColored[]        = {kPos, kColor};
        static constexpr StockProgramInput kTextured[]       = {kPos, kUv};
        static constexpr StockProgramInput kColTextured[]    = {kPos, kColor, kUv};
        static constexpr StockProgramInput kLitUntextured[]  = {kPos, kNormal};
        static constexpr StockProgramInput kLit[]            = {kPos, kNormal, kUv};
        static constexpr StockProgramInput kLitColor[]            = {kPos, kNormal, kUv, kColor};
        static constexpr StockProgramInput kSkinned[]        = {kPos, kNormal, kUv, kWeights,
                                                                kIndices, kColor};
        static constexpr StockProgramInput kPbr[]            = {kPos, kNormal, kTangent, kUv};
        static constexpr StockProgramInput kPbrDualUv[]      = {kPos, kNormal, kTangent, kUv,
                                                                kUv1, kColor};
        static constexpr StockProgramInput kPbrSkinned[]     = {kPos, kNormal, kTangent, kUv,
                                                                kWeights, kIndices};
        static constexpr StockProgramInput kPbrSkinnedDualUv[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1};
        static constexpr StockProgramInput kPbrSkinnedDualUvColor[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1, kColor};

        const StockProgramInput* inputs = kColored;
        std::size_t count = std::size(kColored);
        const char* name = "colored3d";
        switch (SelectStockProgramShape(stride, params, declaredElements))
        {
        case StockProgramShape::PbrSkinned:
            if (stride == 80)
            {
                inputs = kPbrSkinnedDualUvColor;
                count = std::size(kPbrSkinnedDualUvColor);
            }
            else if (stride == 76)
            {
                inputs = kPbrSkinnedDualUv; count = std::size(kPbrSkinnedDualUv);
            }
            else
            {
                inputs = kPbrSkinned; count = std::size(kPbrSkinned);
            }
            name = "pbr_skinned3d"; break;
        case StockProgramShape::Pbr:
            if (stride == 60)
            {
                inputs = kPbrDualUv; count = std::size(kPbrDualUv);
            }
            else
            {
                inputs = kPbr; count = std::size(kPbr);
            }
            name = "pbr3d"; break;
        case StockProgramShape::SkinnedVertexLit:
            inputs = kSkinned; count = std::size(kSkinned); name = "skinned3d_vertexlit"; break;
        case StockProgramShape::Skinned:
            inputs = kSkinned; count = std::size(kSkinned); name = "skinned3d"; break;
        case StockProgramShape::EnvMapped:
            inputs = kLit; count = std::size(kLit); name = "env_mapped3d"; break;
        case StockProgramShape::DualTexturedColored:
            inputs = kColTextured; count = std::size(kColTextured);
            name = "dual_textured_colored3d"; break;
        case StockProgramShape::DualTextured:
            inputs = kTextured; count = std::size(kTextured); name = "dual_textured3d"; break;
        case StockProgramShape::Textured:
            inputs = kTextured; count = std::size(kTextured); name = "textured3d"; break;
        case StockProgramShape::ColoredTextured:
            inputs = kColTextured; count = std::size(kColTextured);
            name = "colored_textured3d"; break;
        case StockProgramShape::LitVertexLitUntextured:
            inputs = kLitUntextured; count = std::size(kLitUntextured);
            name = "lit_untextured3d_vertexlit"; break;
        case StockProgramShape::LitUntextured:
            inputs = kLitUntextured; count = std::size(kLitUntextured);
            name = "lit_untextured3d"; break;
        case StockProgramShape::LitVertexLit:
            // FX-125: a stride-36 lit vertex carries a colour element as well.
            if (stride == 36) { inputs = kLitColor; count = std::size(kLitColor); }
            else              { inputs = kLit;      count = std::size(kLit); }
            name = "lit_textured3d_vertexlit"; break;
        case StockProgramShape::Lit:
            if (stride == 36) { inputs = kLitColor; count = std::size(kLitColor); }
            else              { inputs = kLit;      count = std::size(kLit); }
            name = "lit_textured3d"; break;
        case StockProgramShape::Colored:
            break;
        }
        CNA::Internal::Graphics::RequireDeclarationMatchesStockProgram(
            declaredElements, inputs, count, "EasyGL", name);
    }

    bool EasyGLRenderer::ConfigureDeclarationForStockProgramEXT(
        EasyGLVertexBufferRenderer& buffer, std::size_t stride,
        const GpuDrawParams& params)
    {
        using CNA::Internal::Graphics::StockProgramInput;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        const std::vector<VertexElement>& primaryDeclaration = buffer.GetDeclarationElements();
        if (primaryDeclaration.empty()) return false;

        static constexpr StockProgramInput kPos{
            VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
        static constexpr StockProgramInput kColor{
            VertexElementUsage::Color, 0, VertexElementFormat::Color, "aColor"};
        static constexpr StockProgramInput kUv{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        static constexpr StockProgramInput kUv1{
            VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};
        static constexpr StockProgramInput kNormal{
            VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};
        static constexpr StockProgramInput kTangent{
            VertexElementUsage::Tangent, 0, VertexElementFormat::Vector4, "aTangent"};
        static constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        // FX-127: Vector4 is as legal a spelling of BLENDINDICES as Byte4 -- the format describes
        // the bytes, the shader register is a float4 either way -- and a content processor may
        // write either. CustomModelAnimation's own SkinnedModelProcessor writes Vector4.
        static constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4, "aBoneIndices",
            VertexElementFormat::Vector4};

        static constexpr StockProgramInput kColored[] = {kPos, kColor};
        static constexpr StockProgramInput kTextured[] = {kPos, kUv};
        static constexpr StockProgramInput kColTextured[] = {kPos, kColor, kUv};
        static constexpr StockProgramInput kLitUntextured[] = {kPos, kNormal};
        static constexpr StockProgramInput kLit[] = {kPos, kNormal, kUv};
        static constexpr StockProgramInput kLitColor[] = {kPos, kNormal, kUv, kColor};
        static constexpr StockProgramInput kSkinned[] = {
            kPos, kNormal, kUv, kWeights, kIndices, kColor};
        static constexpr StockProgramInput kPbr[] = {kPos, kNormal, kTangent, kUv};
        static constexpr StockProgramInput kPbrDualUv[] = {
            kPos, kNormal, kTangent, kUv, kUv1, kColor};
        static constexpr StockProgramInput kPbrSkinned[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices};
        static constexpr StockProgramInput kPbrSkinnedDualUv[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1};
        static constexpr StockProgramInput kPbrSkinnedDualUvColor[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1, kColor};

        const StockProgramInput* inputs = kColored;
        std::size_t count = std::size(kColored);
        switch (SelectStockProgramShape(stride, params, primaryDeclaration))
        {
        case StockProgramShape::PbrSkinned:
            if (stride == 80)
            {
                inputs = kPbrSkinnedDualUvColor;
                count = std::size(kPbrSkinnedDualUvColor);
            }
            else if (stride == 76)
            {
                inputs = kPbrSkinnedDualUv;
                count = std::size(kPbrSkinnedDualUv);
            }
            else
            {
                inputs = kPbrSkinned;
                count = std::size(kPbrSkinned);
            }
            break;
        case StockProgramShape::Pbr:
            if (stride == 60)
            {
                inputs = kPbrDualUv;
                count = std::size(kPbrDualUv);
            }
            else
            {
                inputs = kPbr;
                count = std::size(kPbr);
            }
            break;
        case StockProgramShape::SkinnedVertexLit:
        case StockProgramShape::Skinned:
            inputs = kSkinned; count = std::size(kSkinned); break;
        case StockProgramShape::EnvMapped:
        case StockProgramShape::LitVertexLit:
        case StockProgramShape::Lit:
            // FX-125: a stride-36 lit vertex carries a colour element as well.
            if (stride == 36) { inputs = kLitColor; count = std::size(kLitColor); }
            else              { inputs = kLit;      count = std::size(kLit); }
            break;
        case StockProgramShape::DualTexturedColored:
            inputs = kColTextured; count = std::size(kColTextured); break;
        case StockProgramShape::DualTextured:
        case StockProgramShape::Textured:
            inputs = kTextured; count = std::size(kTextured); break;
        case StockProgramShape::ColoredTextured:
            inputs = kColTextured; count = std::size(kColTextured); break;
        case StockProgramShape::LitVertexLitUntextured:
        case StockProgramShape::LitUntextured:
            inputs = kLitUntextured; count = std::size(kLitUntextured); break;
        case StockProgramShape::Colored:
            break;
        }

        if (!ProfileIsEs2ApiGeneration()) buffer.vao.bind();
        for (std::size_t location = 0; location < count; ++location)
        {
            buffer.vao.disable_attribute(static_cast<unsigned int>(location));
            buffer.vao.set_attribute_divisor(static_cast<unsigned int>(location), 0);

            const StockProgramInput& input = inputs[location];
            const EasyGLVertexBufferRenderer* sourceBuffer = nullptr;
            const VertexElement* sourceElement = nullptr;
            std::size_t sourceStride = buffer.GetStride();
            std::size_t sourceBaseOffset = 0;

            const auto findInStream =
                [&input, &sourceBuffer, &sourceElement, &sourceStride, &sourceBaseOffset](
                    const EasyGLVertexBufferRenderer& candidateBuffer,
                    std::size_t candidateStride,
                    int vertexOffset)
                {
                    const auto& candidateDeclaration = candidateBuffer.GetDeclarationElements();
                    const auto element = std::find_if(
                        candidateDeclaration.begin(), candidateDeclaration.end(),
                        [&input](const VertexElement& candidate)
                        {
                            return candidate.getVertexElementUsageProperty() == input.usage &&
                                   candidate.getUsageIndexProperty() == input.usageIndex;
                        });
                    if (element == candidateDeclaration.end()) return false;
                    sourceBuffer = &candidateBuffer;
                    sourceElement = &*element;
                    sourceStride = candidateStride;
                    sourceBaseOffset = static_cast<std::size_t>(std::max(vertexOffset, 0)) *
                                       candidateStride;
                    return true;
                };

            for (int streamIndex = 0;
                 sourceElement == nullptr && streamIndex < params.vertexStreamCount;
                 ++streamIndex)
            {
                const GpuVertexStreamBinding& stream =
                    params.vertexStreams[static_cast<std::size_t>(streamIndex)];
                if (stream.instanceFrequency != 0 || stream.buffer == nullptr) continue;
                const auto& candidateBuffer =
                    *static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer);
                const std::size_t candidateStride = stream.strideInBytes > 0
                    ? static_cast<std::size_t>(stream.strideInBytes)
                    : candidateBuffer.GetStride();
                findInStream(candidateBuffer, candidateStride, stream.vertexOffset);
            }
            if (sourceElement == nullptr)
                findInStream(buffer, buffer.GetStride(), 0);
            if (sourceElement == nullptr) continue;

            if (sourceElement->getVertexElementFormatProperty() != input.format &&
                sourceElement->getVertexElementFormatProperty() != input.alternateFormat)
            {
                throw System::NotSupportedException(
                    std::string("EasyGL: vertex semantic '") + input.name +
                    "' has a format incompatible with the selected stock program.");
            }

            const VertexAttribFormat desc =
                DescribeVertexElementFormat(sourceElement->getVertexElementFormatProperty());
            const void* offset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(
                    sourceBaseOffset +
                    static_cast<std::size_t>(sourceElement->getOffsetProperty())));
            const unsigned int attributeLocation = static_cast<unsigned int>(location);
            sourceBuffer->vbo.bind(::easygl::BufferTarget::Array);
            buffer.vao.enable_attribute(attributeLocation);
            if (desc.isInteger)
            {
                buffer.vao.set_attribute_i_pointer(
                    attributeLocation, desc.componentCount, desc.type, sourceStride, offset);
            }
            else
            {
                buffer.vao.set_attribute_pointer(
                    attributeLocation, desc.componentCount, desc.type,
                    desc.normalized, sourceStride, offset);
            }
        }
        // The caller bound this VAO for the draw and expects the semantic remap to remain active
        // through glDraw*. Unbinding here leaves WebGL2 drawing against VAO 0 (all attributes
        // disabled); the caller restores the declaration and unbinds after the draw.
        return true;
    }

    void EasyGLRenderer::RestoreDeclarationLayoutEXT(EasyGLVertexBufferRenderer& buffer)
    {
        if (!ProfileIsEs2ApiGeneration()) buffer.vao.bind();
        for (unsigned int location = 0; location < 16; ++location)
        {
            buffer.vao.disable_attribute(location);
            buffer.vao.set_attribute_divisor(location, 0);
        }
        if (!ProfileIsEs2ApiGeneration()) buffer.vao.unbind();
        buffer.ApplyLayout(buffer.GetStride());
    }

    // REMED-GFX-147: resolved for every stock 3D program from one place, right after it links, so
    // a program whose fragment shader samples a texture cannot silently miss the correction.
    // Programs that declare neither uniform (the untextured colour-only one) simply keep -1 and
    // BindDrawParams skips them, exactly like every other optional location in Prog3D.
    void EasyGLRenderer::ResolveRenderTargetOrientationUniforms(Prog3D& p)
    {
        p.loc_rt_flip_v    = p.prog.uniform_location("uRtFlipV");
        p.loc_rt_flip_v_hi = p.prog.uniform_location("uRtFlipVHi");
        p.loc_instanced    = p.prog.uniform_location("uCnaInstanced");
    }

    void EasyGLRenderer::ResolveShadowUniforms(Prog3D& p)
    {
        p.loc_shadowmap      = p.prog.uniform_location("uShadowMap");
        p.loc_lightviewproj  = p.prog.uniform_location("uLightViewProj");
        p.loc_shadows_on     = p.prog.uniform_location("uShadowsEnabled");
        p.loc_shadow_bias    = p.prog.uniform_location("uShadowBias");
        p.loc_shadow_texel   = p.prog.uniform_location("uShadowTexel");
        p.loc_shadow_pcf     = p.prog.uniform_location("uShadowPcfRadius");
        p.loc_cascade_count  = p.prog.uniform_location("uCascadeCount");
        p.loc_cascade_mats   = p.prog.uniform_location("uCascadeMatrices[0]");
        p.loc_cascade_splits = p.prog.uniform_location("uCascadeSplits");
        p.loc_cascade_viewz  = p.prog.uniform_location("uCascadeViewZ");
        p.loc_cascade_blend  = p.prog.uniform_location("uCascadeBlend");
        p.loc_cascade_debug  = p.prog.uniform_location("uCascadeDebug");
        p.loc_punctual_kind   = p.prog.uniform_location("uPunctualKind");
        p.loc_punctual_pos    = p.prog.uniform_location("uPunctualPosition");
        p.loc_punctual_dir    = p.prog.uniform_location("uPunctualDirection");
        p.loc_punctual_diff   = p.prog.uniform_location("uPunctualDiffuse");
        p.loc_punctual_range  = p.prog.uniform_location("uPunctualRange");
        p.loc_punctual_cosin  = p.prog.uniform_location("uPunctualCosInner");
        p.loc_punctual_cosout = p.prog.uniform_location("uPunctualCosOuter");
        p.loc_punctual_bias   = p.prog.uniform_location("uPunctualBias");
        p.loc_punctual_hasmap = p.prog.uniform_location("uPunctualHasShadow");
        p.loc_punctual_cube   = p.prog.uniform_location("uPunctualCube");
        p.loc_punctual_map    = p.prog.uniform_location("uPunctualMap");
        p.loc_punctual_vp     = p.prog.uniform_location("uPunctualViewProj");
        p.loc_punctual_texel  = p.prog.uniform_location("uPunctualTexel");
    }

    void EasyGLRenderer::ResolveIblUniforms(Prog3D& p)
    {
        p.loc_ibl_enabled    = p.prog.uniform_location("uIblEnabled");
        p.loc_ibl_irradiance = p.prog.uniform_location("uIblIrradiance");
        p.loc_ibl_specular   = p.prog.uniform_location("uIblSpecular");
        p.loc_ibl_brdf       = p.prog.uniform_location("uIblBrdfLut");
        p.loc_ibl_mipcount   = p.prog.uniform_location("uIblMipCount");
        p.loc_ibl_intensity  = p.prog.uniform_location("uIblIntensity");
    }

    void EasyGLRenderer::BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                                               const Matrix& projection, const GpuDrawParams& params)
    {
        // REMED-GFX-147: one flag per texture unit, filled in beside each unit's own bind below so
        // the flag and the resource it describes can never disagree, and uploaded once at the end.
        // A render target's GL texel memory is bottom-up; an uploaded texture's is not. Cube maps
        // are deliberately absent -- REMED-GFX-137 owns rendered cube faces, and uEnvMap is sampled
        // with a direction vector, not a UV.
        float rtFlipV[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        // XNA 4.0's Direct3D 9 coordinates address pixel centers while OpenGL addresses pixel
        // corners. Preserve D3D's top-left fill convention with the same slightly-less-than-half-
        // pixel displacement Wine/MonoGame use: 63/128 of a window pixel. Post-multiplying a
        // row-vector WVP by this clip-space translation produces clip.xy += offset * clip.w.
        int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
        device.get_viewport(viewportX, viewportY, viewportWidth, viewportHeight);
        // REMED-GFX-235: not while the destination is multisampled.
        //
        // The correction above is a GEOMETRY translation, and that is only equivalent to what it
        // means at ONE sample per pixel. There it decides which side of the fill edge the pixel
        // CENTRE lands on, and 63/128 is deliberately just under half a pixel so the centre stays
        // covered -- that margin is the whole design. At four samples the outer sample positions
        // sit at a quarter of a pixel, inside that margin, so the same translation also removes
        // coverage: the outermost row and column lose two of four samples and the corner three of
        // four, measured as exactly 1/2 and exactly 1/4 of the expected colour.
        //
        // Suppressing it here keeps the correction doing the job it was tuned for and stops it
        // doing one it was not. The cost is real and deliberate: geometry differs by ~0.49px
        // between a multisampled destination and a single-sampled one, so a game toggling MSAA
        // sees a sub-pixel shift. The alternative was to weaken four fixtures that seven renderers
        // share, only one of which applies this correction at all.
        bool multisampledDestination = false;
        if (bound_)
        {
            if (bound_->rt2D != nullptr && bound_->rt2D->GetMultiSampleCount() > 0)
                multisampledDestination = true;
            if (bound_->cube != nullptr && bound_->cube->GetMultiSampleCount() > 0)
                multisampledDestination = true;
            for (int slot = 0; slot < bound_->mrtCount; ++slot)
                if (bound_->mrt[static_cast<std::size_t>(slot)] != nullptr &&
                    bound_->mrt[static_cast<std::size_t>(slot)]->GetMultiSampleCount() > 0)
                    multisampledDestination = true;
        }
        Matrix xnaPixelCenter = Matrix::getIdentityProperty();
        if (viewportWidth > 0 && viewportHeight > 0 && !multisampledDestination)
        {
            xnaPixelCenter = Matrix::CreateTranslation(
                xnaPixelCenterScale_ / static_cast<float>(viewportWidth),
                -xnaPixelCenterScale_ / static_cast<float>(viewportHeight),
                0.0f);
        }

        // WVP
        const Matrix wvp = world * view * projection * xnaPixelCenter;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);
        if (p.loc_wvp >= 0)
            p.prog.set_uniform_matrix4(p.loc_wvp, wvp_col);
        if (p.loc_instanced >= 0)
            p.prog.set_uniform(
                p.loc_instanced, FirstInstanceStream(params) != nullptr ? 1.0f : 0.0f);

        // Normal matrix — transpose(inverse(world3x3)), via the cofactor/det shortcut, so
        // non-uniform-scale World transforms don't skew the transformed normal (Task 398 fix;
        // the raw upper-left 3x3 used before was only correct for rotation/uniform-scale/
        // translation World matrices).
        if (p.loc_normalmat >= 0)
        {
            const float* w = params.worldColMajor;
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            float nm[9] = {
                (e * i - f * h) * invDet, -(b * i - c * h) * invDet, (b * f - c * e) * invDet,
                -(d * i - f * g) * invDet, (a * i - c * g) * invDet, -(a * f - c * d) * invDet,
                (d * h - e * g) * invDet, -(a * h - b * g) * invDet, (a * e - b * d) * invDet,
            };
            p.prog.set_uniform_matrix3(p.loc_normalmat, nm);
        }

        // Full world matrix (EnvironmentMapEffect VS — position → world space)
        if (p.loc_world >= 0)
            p.prog.set_uniform_matrix4(p.loc_world, params.worldColMajor);

        // Diffuse color
        if (p.loc_diffuse >= 0)
            p.prog.set_uniform(p.loc_diffuse,
                params.diffuseColor[0], params.diffuseColor[1],
                params.diffuseColor[2], params.diffuseColor[3]);
        if (p.loc_lighting_enabled >= 0)
            p.prog.set_uniform(p.loc_lighting_enabled, params.lightingEnabled ? 1.0f : 0.0f);

        // VertexColorEnabled gate (colored3D / BasicEffect no-texture path only — Task 364).
        if (p.loc_vertexcolor >= 0)
            p.prog.set_uniform(p.loc_vertexcolor, params.vertexColorEnabled ? 1.0f : 0.0f);

        // Ambient + light0 (lit shader / BasicEffect path only)
        if (p.loc_ambient >= 0)
        {
            if (params.lightingEnabled)
            {
                p.prog.set_uniform(p.loc_ambient,
                    params.ambientColor[0], params.ambientColor[1], params.ambientColor[2]);
                if (p.loc_l0dir >= 0)
                    p.prog.set_uniform(p.loc_l0dir,
                        params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
                if (p.loc_l0diff >= 0)
                    p.prog.set_uniform(p.loc_l0diff,
                        params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]);
                if (p.loc_l1dir >= 0)
                    p.prog.set_uniform(p.loc_l1dir,
                        params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
                if (p.loc_l1diff >= 0)
                    p.prog.set_uniform(p.loc_l1diff,
                        params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]);
                if (p.loc_l2dir >= 0)
                    p.prog.set_uniform(p.loc_l2dir,
                        params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
                if (p.loc_l2diff >= 0)
                    p.prog.set_uniform(p.loc_l2diff,
                        params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]);
                // BasicEffect specular (Task 886) -- lit shader only.
                if (p.loc_l0spec >= 0)
                    p.prog.set_uniform(p.loc_l0spec,
                        params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]);
                if (p.loc_l1spec >= 0)
                    p.prog.set_uniform(p.loc_l1spec,
                        params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]);
                if (p.loc_l2spec >= 0)
                    p.prog.set_uniform(p.loc_l2spec,
                        params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]);
                if (p.loc_specularcolor >= 0)
                    p.prog.set_uniform(p.loc_specularcolor,
                        params.specularColor[0], params.specularColor[1], params.specularColor[2]);
                if (p.loc_specularpower >= 0)
                    p.prog.set_uniform(p.loc_specularpower, params.specularPower);
            }
            else
            {
                // No lighting: full ambient = diffuse color, light contribution = 0
                p.prog.set_uniform(p.loc_ambient, 1.0f, 1.0f, 1.0f);
                if (p.loc_l0dir  >= 0) p.prog.set_uniform(p.loc_l0dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l0diff >= 0) p.prog.set_uniform(p.loc_l0diff, 0.0f,  0.0f, 0.0f);
                if (p.loc_l1dir  >= 0) p.prog.set_uniform(p.loc_l1dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l1diff >= 0) p.prog.set_uniform(p.loc_l1diff, 0.0f,  0.0f, 0.0f);
                if (p.loc_l2dir  >= 0) p.prog.set_uniform(p.loc_l2dir,  0.0f, -1.0f, 0.0f);
                if (p.loc_l2diff >= 0) p.prog.set_uniform(p.loc_l2diff, 0.0f,  0.0f, 0.0f);
                // No lighting: zero every light's specular color regardless of its own
                // Enabled/SpecularColor forwarding, matching the diffuse-zeroing above.
                if (p.loc_l0spec >= 0) p.prog.set_uniform(p.loc_l0spec, 0.0f, 0.0f, 0.0f);
                if (p.loc_l1spec >= 0) p.prog.set_uniform(p.loc_l1spec, 0.0f, 0.0f, 0.0f);
                if (p.loc_l2spec >= 0) p.prog.set_uniform(p.loc_l2spec, 0.0f, 0.0f, 0.0f);
            }
        }

        // EnvironmentMapEffect: emissive+ambient (pre-combined) + light0 + eye pos + env map
        if (p.loc_emissive >= 0)
            p.prog.set_uniform(p.loc_emissive,
                params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2]);

        if (p.loc_l0dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0dir,
                params.light0Dir[0], params.light0Dir[1], params.light0Dir[2]);
        if (p.loc_l0diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0diff,
                params.light0Diffuse[0], params.light0Diffuse[1], params.light0Diffuse[2]);
        // Task 890: EnvironmentMapEffect's own DirectionalLight1/2 forwarding (same generic
        // Prog3D fields the lit-textured/BasicEffect path above uses, gated the same way via
        // loc_ambient's absence to identify this is the env-map program).
        if (p.loc_l1dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1dir,
                params.light1Dir[0], params.light1Dir[1], params.light1Dir[2]);
        if (p.loc_l1diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1diff,
                params.light1Diffuse[0], params.light1Diffuse[1], params.light1Diffuse[2]);
        if (p.loc_l2dir >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2dir,
                params.light2Dir[0], params.light2Dir[1], params.light2Dir[2]);
        if (p.loc_l2diff >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2diff,
                params.light2Diffuse[0], params.light2Diffuse[1], params.light2Diffuse[2]);
        // Task 894: SkinnedEffect's own specular forwarding (same generic Prog3D fields the
        // lit-textured/BasicEffect path above uses; harmless no-op for env-map, which never
        // declares these uniforms so loc_l0spec etc. stay -1 there).
        if (p.loc_l0spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l0spec,
                params.light0Specular[0], params.light0Specular[1], params.light0Specular[2]);
        if (p.loc_l1spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l1spec,
                params.light1Specular[0], params.light1Specular[1], params.light1Specular[2]);
        if (p.loc_l2spec >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_l2spec,
                params.light2Specular[0], params.light2Specular[1], params.light2Specular[2]);
        if (p.loc_specularcolor >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_specularcolor,
                params.specularColor[0], params.specularColor[1], params.specularColor[2]);
        if (p.loc_specularpower >= 0 && p.loc_ambient < 0)
            p.prog.set_uniform(p.loc_specularpower, params.specularPower);

        if (p.loc_eyepos >= 0)
            p.prog.set_uniform(p.loc_eyepos,
                params.eyePositionWorld[0], params.eyePositionWorld[1], params.eyePositionWorld[2]);

        // Bone palette (SkinnedEffect)
        if (p.loc_bones >= 0 && params.boneCount > 0)
            ::metagl::glUniformMatrix4fv(::metagl::UniformLocation{p.loc_bones}, params.boneCount, 0, params.boneTransforms);
        // Task 895: WeightsPerVertex (1, 2, or 4) -- only the first N weight/index pairs contribute.
        if (p.loc_weightsPerVertex >= 0)
            p.prog.set_uniform(p.loc_weightsPerVertex, params.weightsPerVertex);

        if (p.loc_envmap_amount >= 0)
            p.prog.set_uniform(p.loc_envmap_amount, params.envMapAmount);

        if (p.loc_envmap_spec >= 0)
            p.prog.set_uniform(p.loc_envmap_spec,
                params.envMapSpecular[0], params.envMapSpecular[1], params.envMapSpecular[2]);

        if (p.loc_fresnel_enabled >= 0)
            p.prog.set_uniform(p.loc_fresnel_enabled, params.fresnelEnabled ? 1.0f : 0.0f);

        if (p.loc_fresnel_factor >= 0)
            p.prog.set_uniform(p.loc_fresnel_factor, params.fresnelFactor);

        // Cube map (unit 1 — bind before texture0 to leave unit 0 active)
        if (p.loc_envmap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_envmap, 1);
            if (params.envMap)
                params.envMap->BindGL(1);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture1,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        // Second texture (DualTextureEffect — bind before unit 0 to leave unit 0 active)
        if (p.loc_texture2 >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_texture2, 1);
            rtFlipV[1] = SampledRowOrderIsBottomUp(params.texture1) ? 1.0f : 0.0f;
            if (params.texture1)
                params.texture1->BindGL(1);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture1,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        // plans/plan_cnj.md CNB-58 (Phase 13A): PbrEffect's 4 additional maps (units 1-4, bound before
        // unit 0 to leave it active last, matching the envMap/texture2 precedent above). Each
        // falls back to a texture whose sampled value is the correct "map absent" constant for
        // its own semantic (see EnsureDefaultFlatNormalTexture()'s own doc comment).
        if (p.loc_pbr_normalmap >= 0)
        {
            EnsureDefaultFlatNormalTexture();
            p.prog.set_uniform(p.loc_pbr_normalmap, 1);
            rtFlipV[1] = SampledRowOrderIsBottomUp(params.pbrNormalMap) ? 1.0f : 0.0f;
            if (params.pbrNormalMap)
                params.pbrNormalMap->BindGL(1);
            else
                default_flat_normal_texture_.active_bind(::easygl::TextureUnit::Texture1,
                                                         ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_mr >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_mr, 2);
            rtFlipV[2] = SampledRowOrderIsBottomUp(params.pbrMetallicRoughnessMap) ? 1.0f : 0.0f;
            if (params.pbrMetallicRoughnessMap)
                params.pbrMetallicRoughnessMap->BindGL(2);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture2,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_emissivemap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_emissivemap, 3);
            rtFlipV[3] = SampledRowOrderIsBottomUp(params.pbrEmissiveMap) ? 1.0f : 0.0f;
            if (params.pbrEmissiveMap)
                params.pbrEmissiveMap->BindGL(3);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture3,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_occlusionmap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_occlusionmap, 4);
            rtFlipV[4] = SampledRowOrderIsBottomUp(params.pbrOcclusionMap) ? 1.0f : 0.0f;
            if (params.pbrOcclusionMap)
                params.pbrOcclusionMap->BindGL(4);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture4,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        // GLTF-344: both KHR_materials_specular inputs use white as their identity fallback.
        // The strength texture consumes alpha; the colour texture consumes RGB in sRGB space.
        if (p.loc_pbr_specularmap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_specularmap, 5);
            rtFlipV[5] = SampledRowOrderIsBottomUp(params.pbrSpecularMap) ? 1.0f : 0.0f;
            if (params.pbrSpecularMap)
                params.pbrSpecularMap->BindGL(5);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture5,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_specularcolormap >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_pbr_specularcolormap, 6);
            rtFlipV[6] = SampledRowOrderIsBottomUp(params.pbrSpecularColorMap) ? 1.0f : 0.0f;
            if (params.pbrSpecularColorMap)
                params.pbrSpecularColorMap->BindGL(6);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture6,
                                                   ::easygl::TextureTarget::Texture2D);
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }
        if (p.loc_pbr_metallic >= 0)
            p.prog.set_uniform(p.loc_pbr_metallic, params.pbrMetallicFactor);
        if (p.loc_pbr_roughness >= 0)
            p.prog.set_uniform(p.loc_pbr_roughness, params.pbrRoughnessFactor);
        if (p.loc_pbr_dielectric_fresnel >= 0)
        {
            p.prog.set_uniform(
                p.loc_pbr_dielectric_fresnel,
                params.pbrDielectricF0[0], params.pbrDielectricF0[1],
                params.pbrDielectricF0[2], params.pbrDielectricF90);
        }
        if (p.loc_pbr_specular_fresnel_inputs >= 0)
        {
            p.prog.set_uniform(
                p.loc_pbr_specular_fresnel_inputs,
                params.pbrDielectricF0Unclamped[0], params.pbrDielectricF0Unclamped[1],
                params.pbrDielectricF0Unclamped[2], params.pbrSpecularFactor);
        }
        // plans/plan_gltf.md GLTF-210/GLTF-212. Three independent decisions, so three independent
        // components: two about what a bound texture contains, one about where the fragment is
        // going. A renderer that ignored this field entirely would keep the pre-GLTF-209
        // behaviour exactly, which is what makes adopting it a per-renderer step.
        if (p.loc_pbr_normalscale >= 0)
            p.prog.set_uniform(p.loc_pbr_normalscale, params.pbrNormalScale);
        if (p.loc_pbr_occlstrength >= 0)
            p.prog.set_uniform(p.loc_pbr_occlstrength, params.pbrOcclusionStrength);
        if (p.loc_pbr_texcoordsets >= 0)
        {
            const std::uint32_t mask = params.pbrTextureCoordinateSetMask;
            p.prog.set_uniform(
                p.loc_pbr_texcoordsets,
                (mask & (std::uint32_t{1} << 0)) != 0 ? 1.0f : 0.0f,
                (mask & (std::uint32_t{1} << 1)) != 0 ? 1.0f : 0.0f,
                (mask & (std::uint32_t{1} << 2)) != 0 ? 1.0f : 0.0f,
                (mask & (std::uint32_t{1} << 3)) != 0 ? 1.0f : 0.0f);
        }
        if (p.loc_pbr_occlusiontexcoordset >= 0)
        {
            p.prog.set_uniform(
                p.loc_pbr_occlusiontexcoordset,
                (params.pbrTextureCoordinateSetMask & (std::uint32_t{1} << 4)) != 0
                    ? 1.0f : 0.0f);
        }
        if (p.loc_pbr_specular_texcoordsets >= 0)
        {
            const std::uint32_t mask = params.pbrTextureCoordinateSetMask;
            p.prog.set_uniform(
                p.loc_pbr_specular_texcoordsets,
                (mask & (std::uint32_t{1} << 5)) != 0 ? 1.0f : 0.0f,
                (mask & (std::uint32_t{1} << 6)) != 0 ? 1.0f : 0.0f);
        }
        for (std::size_t row = 0; row < p.loc_pbr_texture_transform_rows.size(); ++row)
        {
            const int location = p.loc_pbr_texture_transform_rows[row];
            if (location < 0) { continue; }
            const float* values = params.pbrTextureTransformRows[row];
            p.prog.set_uniform(location, values[0], values[1], values[2], values[3]);
        }
        for (std::size_t row = 0; row < p.loc_pbr_specular_texture_transform_rows.size(); ++row)
        {
            const int location = p.loc_pbr_specular_texture_transform_rows[row];
            if (location < 0) { continue; }
            const float* values = params.pbrSpecularTextureTransformRows[row];
            p.prog.set_uniform(location, values[0], values[1], values[2], values[3]);
        }
        if (p.loc_pbr_srgb >= 0)
        {
            p.prog.set_uniform(p.loc_pbr_srgb,
                params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
                params.pbrEmissiveTextureIsSrgb  ? 1.0f : 0.0f,
                params.pbrEncodeOutputToSrgb     ? 1.0f : 0.0f,
                params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f);
        }

        // Texture (unit 0)
        if (p.loc_texture >= 0)
        {
            EnsureDefaultWhiteTexture();
            p.prog.set_uniform(p.loc_texture, 0);
            rtFlipV[0] = SampledRowOrderIsBottomUp(params.texture0) ? 1.0f : 0.0f;
            if (params.texture0)
                params.texture0->BindGL(0);
            else
                default_white_texture_.active_bind(::easygl::TextureUnit::Texture0,
                                                   ::easygl::TextureTarget::Texture2D);
            TraceBoundTextureUnit("stock3d-texture0", 0);
        }

if (ProfileIsEs2ApiGeneration())
{
        // ES 2.0 keeps sampling state on the texture objects -- re-apply each unit's recorded
        // SamplerState onto whatever this draw just bound above (GraphicsDevice applies sampler
        // state BEFORE the draw binds its textures on this route, so the bind is what must pull
        // the state in). Units 0-6 are the stock effects' complete sampling range.
        for (int unit = 0; unit < 7; ++unit)
            Es2ApplyPendingSamplerToUnit(unit);
}

        // Shadow map (MOD-835). Unit 7, past the stock effects' 0-6 range, so attaching one
        // cannot displace a texture an effect is already sampling. Every uniform is uploaded even
        // when shadows are off: leaving a stale uLightViewProj behind would make the first
        // shadowed draw after an unshadowed one sample with the previous light's matrix.
        if (p.loc_shadows_on >= 0)
        {
            const bool haveShadow = params.shadowsEnabled && params.shadowMap != nullptr;
            p.prog.set_uniform(p.loc_shadows_on, haveShadow ? 1.0f : 0.0f);
            if (p.loc_shadow_bias >= 0)
                p.prog.set_uniform(p.loc_shadow_bias, params.shadowDepthBias);
            if (p.loc_lightviewproj >= 0)
                p.prog.set_uniform_matrix4(p.loc_lightviewproj, params.lightViewProjColMajor);
            if (p.loc_shadow_pcf >= 0)
            {
                const int radius = params.shadowPcfRadius < 0 ? 0
                                 : (params.shadowPcfRadius > 2 ? 2 : params.shadowPcfRadius);
                p.prog.set_uniform(p.loc_shadow_pcf, static_cast<float>(radius));
            }
            if (p.loc_shadow_texel >= 0)
            {
                // 1/size per axis, because textureSize() does not exist in the ES 1.00 form these
                // shaders are also compiled in. Two components rather than one because a cascade
                // atlas is N times wider than it is tall, and a single scalar would step the PCF
                // taps N times too far in X and smear each cascade into its neighbour.
                const int width  = haveShadow ? params.shadowMap->GetWidth() : 1;
                const int height = haveShadow ? params.shadowMap->GetHeight() : 1;
                p.prog.set_uniform(p.loc_shadow_texel,
                                   width > 0 ? 1.0f / static_cast<float>(width) : 0.0f,
                                   height > 0 ? 1.0f / static_cast<float>(height) : 0.0f);
            }
            // Cascades (MOD-908). Count 0 is the single-map path, and every draw that has never
            // heard of cascades takes it -- which is what keeps this addition free.
            if (p.loc_cascade_count >= 0)
            {
                const int count = (haveShadow && params.cascadeCount > 0)
                                      ? (params.cascadeCount > 4 ? 4 : params.cascadeCount)
                                      : 0;
                p.prog.set_uniform(p.loc_cascade_count, static_cast<float>(count));
                if (p.loc_cascade_mats >= 0)
                    ::metagl::glUniformMatrix4fv(::metagl::UniformLocation{p.loc_cascade_mats}, 4,
                                                 0, params.cascadeMatricesColMajor);
                if (p.loc_cascade_splits >= 0)
                    p.prog.set_uniform(p.loc_cascade_splits, params.cascadeSplits[0],
                                       params.cascadeSplits[1], params.cascadeSplits[2],
                                       params.cascadeSplits[3]);
                if (p.loc_cascade_viewz >= 0)
                    p.prog.set_uniform(p.loc_cascade_viewz, params.cascadeViewZRow[0],
                                       params.cascadeViewZRow[1], params.cascadeViewZRow[2],
                                       params.cascadeViewZRow[3]);
                if (p.loc_cascade_blend >= 0)
                    p.prog.set_uniform(p.loc_cascade_blend, params.cascadeBlendBand);
                if (p.loc_cascade_debug >= 0)
                    p.prog.set_uniform(p.loc_cascade_debug, params.cascadeDebugTint ? 1.0f : 0.0f);
            }

            // Punctual light (MOD-1005). Units 8 and 9, past the shadow map's unit 7, so a lamp
            // and the sun can be shadowed in the same draw without displacing each other.
            if (p.loc_punctual_kind >= 0)
            {
                const int kind = params.punctualKind < 0 ? 0
                               : (params.punctualKind > 2 ? 0 : params.punctualKind);
                const bool haveCube = kind == 1 && params.punctualShadowCube != nullptr;
                const bool haveMap  = kind == 2 && params.punctualShadowMap != nullptr;
                p.prog.set_uniform(p.loc_punctual_kind, static_cast<float>(kind));
                if (p.loc_punctual_pos >= 0)
                    p.prog.set_uniform(p.loc_punctual_pos, params.punctualPosition[0],
                                       params.punctualPosition[1], params.punctualPosition[2]);
                if (p.loc_punctual_dir >= 0)
                    p.prog.set_uniform(p.loc_punctual_dir, params.punctualDirection[0],
                                       params.punctualDirection[1], params.punctualDirection[2]);
                if (p.loc_punctual_diff >= 0)
                    p.prog.set_uniform(p.loc_punctual_diff, params.punctualDiffuse[0],
                                       params.punctualDiffuse[1], params.punctualDiffuse[2]);
                if (p.loc_punctual_range >= 0)
                    p.prog.set_uniform(p.loc_punctual_range,
                                       params.punctualRange > 0.0f ? params.punctualRange : 1.0f);
                if (p.loc_punctual_cosin >= 0)
                    p.prog.set_uniform(p.loc_punctual_cosin, params.punctualCosInner);
                if (p.loc_punctual_cosout >= 0)
                    p.prog.set_uniform(p.loc_punctual_cosout, params.punctualCosOuter);
                if (p.loc_punctual_bias >= 0)
                    p.prog.set_uniform(p.loc_punctual_bias, params.punctualShadowBias);
                if (p.loc_punctual_hasmap >= 0)
                    p.prog.set_uniform(p.loc_punctual_hasmap,
                                       (haveCube || haveMap) ? 1.0f : 0.0f);
                if (p.loc_punctual_vp >= 0)
                    p.prog.set_uniform_matrix4(p.loc_punctual_vp, params.punctualViewProjColMajor);
                if (p.loc_punctual_texel >= 0)
                {
                    // Its own texel size, not the directional map's. Borrowing that one means a
                    // draw with no sun attached filters the spot map with a texel of 1.0 -- every
                    // tap clamped to a corner of the map, every corner white, and a spot shadow
                    // that silently never appears.
                    const int width  = haveMap ? params.punctualShadowMap->GetWidth() : 1;
                    const int height = haveMap ? params.punctualShadowMap->GetHeight() : 1;
                    p.prog.set_uniform(p.loc_punctual_texel,
                                       width > 0 ? 1.0f / static_cast<float>(width) : 0.0f,
                                       height > 0 ? 1.0f / static_cast<float>(height) : 0.0f);
                }
                if (p.loc_punctual_cube >= 0)
                {
                    p.prog.set_uniform(p.loc_punctual_cube, 8);
                    if (haveCube)
                        params.punctualShadowCube->BindGL(8);
                }
                if (p.loc_punctual_map >= 0)
                {
                    p.prog.set_uniform(p.loc_punctual_map, 9);
                    if (haveMap)
                        params.punctualShadowMap->BindGL(9);
                    else
                    {
                        EnsureDefaultWhiteTexture();
                        default_white_texture_.active_bind(::easygl::TextureUnit::Texture9,
                                                           ::easygl::TextureTarget::Texture2D);
                    }
                }
                ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
            }
            if (p.loc_shadowmap >= 0)
            {
                p.prog.set_uniform(p.loc_shadowmap, 7);
                if (haveShadow)
                    params.shadowMap->BindGL(7);
                else
                {
                    EnsureDefaultWhiteTexture();
                    // White means "infinitely far", so an unbound unit reads as nothing occluding
                    // -- the same convention ShadowMap clears its target to.
                    default_white_texture_.active_bind(::easygl::TextureUnit::Texture7,
                                                       ::easygl::TextureTarget::Texture2D);
                }
            }
        }

        // plans/plan_modern.md MOD-1225: image-based lighting, units 10-12. Bound only when the
        // program has the uniforms at all, and each unit falls back to a texture whose sampled
        // value is the "no environment" constant -- but with uIblEnabled at 0 the shader never
        // reads them, so the fallbacks exist to keep the units complete rather than to be seen.
        if (p.loc_ibl_enabled >= 0)
        {
            const bool haveIbl = params.iblEnabled && params.iblIrradiance != nullptr
                              && params.iblPrefilteredSpecular != nullptr
                              && params.iblBrdfLut != nullptr;
            p.prog.set_uniform(p.loc_ibl_enabled, haveIbl ? 1.0f : 0.0f);
            if (p.loc_ibl_mipcount >= 0)
                p.prog.set_uniform(p.loc_ibl_mipcount,
                                   static_cast<float>(params.iblPrefilteredMipCount > 0
                                                          ? params.iblPrefilteredMipCount : 1));
            if (p.loc_ibl_intensity >= 0)
                p.prog.set_uniform(p.loc_ibl_intensity, params.iblIntensity);
            if (p.loc_ibl_irradiance >= 0)
            {
                p.prog.set_uniform(p.loc_ibl_irradiance, 10);
                if (haveIbl) params.iblIrradiance->BindGL(10);
            }
            if (p.loc_ibl_specular >= 0)
            {
                p.prog.set_uniform(p.loc_ibl_specular, 11);
                if (haveIbl) params.iblPrefilteredSpecular->BindGL(11);
            }
            if (p.loc_ibl_brdf >= 0)
            {
                p.prog.set_uniform(p.loc_ibl_brdf, 12);
                if (haveIbl)
                {
                    params.iblBrdfLut->BindGL(12);
                }
                else
                {
                    EnsureDefaultWhiteTexture();
                    default_white_texture_.active_bind(::easygl::TextureUnit::Texture12,
                                                       ::easygl::TextureTarget::Texture2D);
                }
            }
            ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
        }

        // Alpha test (always uploaded; default {0,0,1,1} = Always pass)
        if (p.loc_alphatest >= 0)
            p.prog.set_uniform(p.loc_alphatest,
                params.alphaTest[0], params.alphaTest[1],
                params.alphaTest[2], params.alphaTest[3]);

        // REMED-GFX-010: upload the FNA view-space fog vector (GpuDrawParams.fogVector). It bakes
        // World*View's third column + fogStart/fogEnd, is zero when fog is disabled, and (0,0,0,1) for
        // the degenerate fogStart==fogEnd case, so no separate start/end/enabled uniforms are needed —
        // the shader computes vFogFactor = 1 - saturate(dot(pos, uFogVector)).
        if (p.loc_fog_vector >= 0)
            p.prog.set_uniform(p.loc_fog_vector,
                params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]);
        if (p.loc_fog_color >= 0)
            p.prog.set_uniform(p.loc_fog_color,
                params.fogColor[0], params.fogColor[1], params.fogColor[2]);

        // REMED-GFX-147: two uniform writes per draw at most, and only for programs that declare
        // them. Uploaded unconditionally rather than only when a flag is set, because these are
        // program state that outlives the draw -- leaving a previous draw's render-target flag
        // behind would mirror the next ordinary texture.
        if (p.loc_rt_flip_v >= 0)
            p.prog.set_uniform(p.loc_rt_flip_v, rtFlipV[0], rtFlipV[1], rtFlipV[2], rtFlipV[3]);
        if (p.loc_rt_flip_v_hi >= 0)
            p.prog.set_uniform(
                p.loc_rt_flip_v_hi, rtFlipV[4], rtFlipV[5], rtFlipV[6], 0.0f);
    }

    /// REMED-GFX-237: puts back what a clear had to override.
    ///
    /// XNA's `Clear` ignores the depth and stencil WRITE masks; `glClear` obeys them, so every
    /// clear path forces them open first. Restoring was left to "ApplyDepthStencilState reissues
    /// the real mask before the next draw anyway" -- true only if the game reassigns its
    /// DepthStencilState between the clear and that draw, which nothing requires it to do.
    ///
    /// The stencil mask is only put back while the stencil test is on, matching
    /// ApplyDepthStencilState, which does not install one otherwise.
    void EasyGLRenderer::RestoreWriteMasksAfterClear(bool depth, bool stencil)
    {
        if (depth && !depthWriteEnabled_) device.set_depth_mask(false);
        if (stencil && stencilEnabled_)
        {
            const auto mask = static_cast<unsigned int>(stencilWriteMask_);
            if (stencilTwoSided_)
            {
                device.set_stencil_mask_separate(::easygl::CullFace::Front, mask);
                device.set_stencil_mask_separate(::easygl::CullFace::Back, mask);
            }
            else
            {
                device.set_stencil_mask(mask);
            }
        }
    }

    void EasyGLRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        if (metagl::IsContextLost()) return;
        // Task 880: see Clear()'s identical comment -- glClear() is viewport-independent.
        device.set_clear_color(r, g, b, a);
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        // REMED-GFX-077: neutralise a non-default BlendState.ColorWriteChannels across the clear
        // (XNA Clear ignores it, glClear respects glColorMask) — mirrors the set_depth_mask(true)
        // override just above. No-op fast path when the mask is the default All.
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth);
        if (maskActive) ApplyCurrentColorWriteMasks();
        RestoreWriteMasksAfterClear(true, false);
    }

    // Task 871: glClear(GL_STENCIL_BUFFER_BIT) is itself masked by the currently-active
    // glStencilMask -- forcing it to all-1s here (mirroring ClearDepth's identical
    // set_depth_mask(true) override) guarantees the requested clear value actually reaches every
    // stencil bit regardless of whatever DepthStencilState::StencilWriteMask a previous draw left
    // active; ApplyDepthStencilState() reissues the real write mask before the next draw anyway.
    void EasyGLRenderer::ClearStencil(int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_stencil(stencil);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Stencil);
        RestoreWriteMasksAfterClear(false, true);
    }

    void EasyGLRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_clear_stencil(stencil);
        device.set_depth_mask(true);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Depth | ::easygl::ClearFlags::Stencil);
        RestoreWriteMasksAfterClear(true, true);
    }

    void EasyGLRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_color(r, g, b, a);
        device.set_clear_stencil(stencil);
        device.set_stencil_mask(0xFFFFFFFFu);
        // REMED-GFX-077: XNA Clear ignores BlendState.ColorWriteChannels; glClear respects glColorMask.
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Stencil);
        if (maskActive) ApplyCurrentColorWriteMasks();
    }

    void EasyGLRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_color(r, g, b, a);
        device.set_clear_depth(depth);
        device.set_clear_stencil(stencil);
        device.set_depth_mask(true);
        device.set_stencil_mask(0xFFFFFFFFu);
        // REMED-GFX-077: XNA Clear ignores BlendState.ColorWriteChannels; glClear respects glColorMask.
        // (Clear(const Color&) routes here via ClearOptions Target|DepthBuffer|Stencil.)
        const bool maskActive = HasRestrictedActiveColorWriteMask();
        if (maskActive) ForceAllColorWriteMasks();
        device.clear(::easygl::ClearFlags::Color | ::easygl::ClearFlags::Depth | ::easygl::ClearFlags::Stencil);
        RestoreWriteMasksAfterClear(true, true);
        if (maskActive) ApplyCurrentColorWriteMasks();
    }

    void EasyGLRenderer::ClearDepth(float depth)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        device.clear(::easygl::ClearFlags::Depth);
        RestoreWriteMasksAfterClear(true, false);
    }

    void EasyGLRenderer::SetDepthTestEnabled(bool enabled)
    {
        device.set_depth_test_enabled(enabled);
        if (enabled)
        {
            device.set_depth_func(::easygl::CompareFunc::Lequal);
            depthWriteEnabled_ = true;   // REMED-GFX-237: this really does install the mask.
            device.set_depth_mask(true);
        }
    }

    void EasyGLRenderer::SetBlendEnabled(bool enabled)
    {
        device.set_blend_enabled(enabled);
        if (enabled)
            device.set_blend_func(::easygl::BlendFactor::SrcAlpha,
                                  ::easygl::BlendFactor::OneMinusSrcAlpha);
    }

    void EasyGLRenderer::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;   // REMED-GFX-237: what a clear must put back.
        device.set_depth_mask(enabled);
    }

    std::unique_ptr<IVertexBufferRenderer> EasyGLRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<EasyGLVertexBufferRenderer>(vertex_capacity, RegistryPtr());
    }

    std::unique_ptr<IIndexBufferRenderer> EasyGLRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferRenderer>(index_capacity, false, RegistryPtr());
    }

    std::unique_ptr<IIndexBufferRenderer> EasyGLRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<EasyGLIndexBufferRenderer>(index_capacity, true, RegistryPtr());
    }

    bool EasyGLRenderer::DrawWireframe(const EasyGLVertexBufferRenderer& vb,
                                              const EasyGLIndexBufferRenderer* ib,
                                              PrimitiveType primitive, int primitiveCount,
                                              int startIndex, int baseVertex, int firstVertex)
    {
        // Only triangle geometry needs expanding; line/point primitives are already "wireframe".
        if (primitive != PrimitiveType::TriangleList &&
            primitive != PrimitiveType::TriangleStrip)
            return false;
        if (primitiveCount <= 0) return true;

        // Source vertex index at sequence position `pos` within this draw.
        auto readSrc = [&](int pos) -> std::uint32_t {
            if (!ib) return static_cast<std::uint32_t>(firstVertex + pos);
            const auto& bytes = ib->GetCpuBytes();
            if (ib->IsThirtyTwoBit()) {
                std::uint32_t v;
                std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 4, 4);
                return v;
            }
            std::uint16_t v;
            std::memcpy(&v, bytes.data() + static_cast<std::size_t>(startIndex + pos) * 2, 2);
            return static_cast<std::uint32_t>(v);
        };

        wireframeScratch_.clear();
        auto edge = [&](std::uint32_t a, std::uint32_t b) {
            wireframeScratch_.push_back(a);
            wireframeScratch_.push_back(b);
        };
        if (primitive == PrimitiveType::TriangleList) {
            for (int t = 0; t < primitiveCount; ++t) {
                const std::uint32_t a = readSrc(3 * t);
                const std::uint32_t b = readSrc(3 * t + 1);
                const std::uint32_t c = readSrc(3 * t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        } else { // TriangleStrip: primitiveCount triangles over primitiveCount+2 vertices
            for (int t = 0; t < primitiveCount; ++t) {
                const std::uint32_t a = readSrc(t);
                const std::uint32_t b = readSrc(t + 1);
                const std::uint32_t c = readSrc(t + 2);
                edge(a, b); edge(b, c); edge(c, a);
            }
        }

        if (!wireframeIboCreated_) { wireframeIbo_.create(); wireframeIboCreated_ = true; }
        vb.BindForDraw();
        wireframeIbo_.bind(::easygl::BufferTarget::ElementArray);
        wireframeIbo_.set_data(::easygl::BufferTarget::ElementArray,
                               wireframeScratch_.data(),
                               wireframeScratch_.size() * sizeof(std::uint32_t),
                               ::easygl::BufferUsage::DynamicDraw);
        const int lineIndexCount = static_cast<int>(wireframeScratch_.size());
        if (baseVertex == 0) {
            device.draw_elements(::easygl::PrimitiveType::Lines, lineIndexCount,
                                 ::easygl::DataType::UnsignedInt, nullptr);
        } else {
if (ProfileRequiresBaseVertexPointerRebase())
{
            // GLES/WebGL profiles cannot assume glDrawElementsBaseVertex (ES 3.2).
            ShiftEnabledPerVertexAttribPointers(baseVertex, +1);
            device.draw_elements(::easygl::PrimitiveType::Lines, lineIndexCount,
                                 ::easygl::DataType::UnsignedInt, nullptr);
            ShiftEnabledPerVertexAttribPointers(baseVertex, -1);
}
else
{
            ::metagl::glDrawElementsBaseVertex(::easygl::PrimitiveType::Lines, lineIndexCount,
                                               ::easygl::DataType::UnsignedInt, nullptr, baseVertex);
}
        }
        vb.UnbindAfterDraw();
        return true;
    }

    void EasyGLRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                      const Matrix& world,
                                                      const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive,
                                                      int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        prog_colored_.prog.use();
        if (prog_colored_.loc_wvp >= 0)
            prog_colored_.prog.set_uniform_matrix4(prog_colored_.loc_wvp, wvp_col);
        // This path carries no BasicEffect diffuse; output the raw vertex colors
        // (uDiffuseColor would otherwise default to 0 and render everything black).
        if (prog_colored_.loc_diffuse >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        // Same reasoning for uVertexColorEnabled: it would otherwise default to 0 (uninitialized
        // GLSL uniform) and force vColor out of the multiply, turning every pixel constant white.
        if (prog_colored_.loc_vertexcolor >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_vertexcolor, 1.0f);

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " verts=" << vertex_count);

        if (wireframe_ && DrawWireframe(vb, nullptr, primitive, primitiveCount, 0, 0, 0))
            return;

        vb.BindForDraw();
        device.draw_arrays(ToEasyGl(primitive), 0, vertex_count);
        vb.UnbindAfterDraw();
    }

    void EasyGLRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb_in,
                                                             const IIndexBufferRenderer& ib_in,
                                                             const Matrix& world,
                                                             const Matrix& view,
                                                             const Matrix& projection,
                                                             PrimitiveType primitive,
                                                             int primitiveCount)
    {
        EnsureColored3DProgram();
        const auto& vb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
        const auto& ib = static_cast<const EasyGLIndexBufferRenderer&>(ib_in);

        const Matrix wvp = world * view * projection;
        float wvp_col[16];
        wvp.ToColumnMajor(wvp_col);

        prog_colored_.prog.use();
        if (prog_colored_.loc_wvp >= 0)
            prog_colored_.prog.set_uniform_matrix4(prog_colored_.loc_wvp, wvp_col);
        // This path carries no BasicEffect diffuse; output the raw vertex colors
        // (uDiffuseColor would otherwise default to 0 and render everything black).
        if (prog_colored_.loc_diffuse >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_diffuse, 1.0f, 1.0f, 1.0f, 1.0f);
        // Same reasoning for uVertexColorEnabled: it would otherwise default to 0 (uninitialized
        // GLSL uniform) and force vColor out of the multiply, turning every pixel constant white.
        if (prog_colored_.loc_vertexcolor >= 0)
            prog_colored_.prog.set_uniform(prog_colored_.loc_vertexcolor, 1.0f);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedColoredPrimitives: prim=" << static_cast<int>(primitive)
            << " count=" << primitiveCount << " indices=" << index_count);

        if (wireframe_ && DrawWireframe(vb, &ib, primitive, primitiveCount, 0, 0, 0))
            return;

        vb.BindForDraw();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                              : ::easygl::DataType::UnsignedShort;
        device.draw_elements(ToEasyGl(primitive), index_count, idxType, nullptr);
        vb.UnbindAfterDraw();
    }

    namespace
    {
        // Task 1079: binds a ShaderEffect's own compiled program (bypassing the built-in
        // stride-dispatched shaders) and its World/View/Projection uniforms, matching the exact
        // uniform names every original XNA sample's own .fx source already declares.
        void BindCustomEffectMatrices(IEffectRenderer& renderer,
                                      const Matrix& world, const Matrix& view, const Matrix& projection)
        {
            renderer.Bind();
            float worldCM[16], viewCM[16], projCM[16];
            world.ToColumnMajor(worldCM);
            view.ToColumnMajor(viewCM);
            projection.ToColumnMajor(projCM);
            renderer.SetUniformMat4("World", worldCM);
            renderer.SetUniformMat4("View", viewCM);
            renderer.SetUniformMat4("Projection", projCM);
        }

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        /// plans/plan_fx.md FX-082: the streams a compiled-effect draw reads its attributes from, in
        /// public binding-slot order. The internal staged routes (`DrawUser*`, SpriteBatch,
        /// `DrawColoredPrimitives`) bind no public `VertexBufferBinding` and leave
        /// `vertexStreamCount` at 0; they contribute the one buffer the draw named, at its own
        /// stride. A per-instance stream keeps its `InstanceFrequency` so the divisor can be set
        /// after the program is ready.
        std::vector<EasyGLRenderer::CompiledEffectStreamEXT> CollectCompiledEffectStreams(
            const EasyGLVertexBufferRenderer& primary, const GpuDrawParams& params)
        {
            std::vector<EasyGLRenderer::CompiledEffectStreamEXT> streams;
            streams.reserve(static_cast<std::size_t>(std::max(params.vertexStreamCount, 1)));
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const GpuVertexStreamBinding& stream =
                    params.vertexStreams[static_cast<std::size_t>(i)];
                const auto* buffer =
                    static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr) continue;
                const std::size_t stride = stream.strideInBytes > 0
                    ? static_cast<std::size_t>(stream.strideInBytes)
                    : buffer->GetStride();
                EasyGLRenderer::CompiledEffectStreamEXT entry;
                entry.buffer = buffer;
                entry.stride = stride;
                entry.baseByteOffset =
                    static_cast<std::size_t>(std::max(stream.vertexOffset, 0)) * stride;
                entry.instanceFrequency = stream.instanceFrequency > 0
                    ? static_cast<unsigned int>(stream.instanceFrequency) : 0u;
                streams.push_back(entry);
            }
            if (streams.empty())
            {
                EasyGLRenderer::CompiledEffectStreamEXT entry;
                entry.buffer = &primary;
                entry.stride = primary.GetStride();
                streams.push_back(entry);
            }
            return streams;
        }

        /// A compiled effect's vertex shader declares arbitrary semantics, so a stride alone
        /// cannot describe its input. Every bound stream must therefore carry a real declaration.
        void RequireCompiledEffectDeclarations(
            const std::vector<EasyGLRenderer::CompiledEffectStreamEXT>& streams)
        {
            for (const auto& stream : streams)
            {
                if (stream.buffer != nullptr &&
                    !stream.buffer->GetDeclarationElements().empty())
                {
                    continue;
                }
                throw System::NotSupportedException(
                    "CNA EasyGL: a compiled-effect draw needs every bound vertex buffer's own "
                    "VertexDeclaration; this renderer does not infer one from stride for this "
                    "route.");
            }
        }
#endif
    }

    void EasyGLRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                 const Matrix& world,
                                                 const Matrix& view,
                                                 const Matrix& projection,
                                                 PrimitiveType primitive,
                                                 int primitiveCount,
                                                 const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-062: a compiled effect's vertex layout is arbitrary and validated against
        // the applied pass's own shader reflection (BindCompiledEffectForDrawEXT), not against the
        // fixed-stride table RequireDeclarationFitsStockProgramEXT enforces below -- so this
        // dispatches before that guard runs, not after.
        if (params.compiledEffectRuntime != nullptr)
        {
            const auto& compiledVb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
            const auto compiledStreams = CollectCompiledEffectStreams(compiledVb, params);
            RequireCompiledEffectDeclarations(compiledStreams);
            ::easygl::VertexArray& compiledVao = EnsureCompiledEffectVaoEXT();
            compiledVao.bind();
            const CompiledEffectDepthRangeScope compiledDepthRange(*this);
            BindCompiledEffectForDrawEXT(compiledStreams.data(), compiledStreams.size(),
                                         *params.compiledEffectRuntime);
            const int compiledVertexCount = VertexCountForPrimitives(primitive, primitiveCount);
            // glDrawArrays' `first` advances every bound stream by that many of its own records,
            // which is the same rule the stock multi-stream route relies on.
            device.draw_arrays(ToEasyGl(primitive), params.vertexStart, compiledVertexCount);
            compiledVao.unbind();
            return;
        }
#endif
        // REMED-GFX-DECL-GUARD (REMED-GFX-218): before the VAO is touched, before a program is
        // selected and before any draw is issued. A custom ShaderEffect owns its own
        // element-index attribute convention and is deliberately untouched.
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgramEXT(
                static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetDeclarationElements(),
                CombinedVertexStrideOr(
                    params, static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetStride()),
                params);
        const auto& vb  = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
        // REMED-GFX-201: every bound per-vertex stream is bound into this VAO, at locations
        // continuing after the previous stream's, and each with its own VBO, stride and byte
        // offset. glDrawArrays' `first` then advances all of them by params.vertexStart of their
        // own elements. A single-stream draw takes neither branch and is unchanged.
        const bool multiStream = HasMultipleVertexStreams(params);
        auto& vao = const_cast<::easygl::VertexArray&>(vb.vao);
        if (multiStream && params.customEffectRenderer != nullptr)
        {
            vao.bind();
            if (!ConfigureMultiStreamAttributes(vao, params))
            {
                vao.unbind();
                throw System::InvalidOperationException(
                    "EasyGL multi-stream drawing requires every bound VertexBuffer to carry a "
                    "VertexDeclaration.");
            }
            vao.unbind();
        }

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
            vb.BindForDraw();
            device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
            vb.UnbindAfterDraw();
            if (multiStream) { vao.bind(); RestoreSingleStreamAttributes(vao, params); vao.unbind(); }
            return;
        }

        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        Prog3D& p = SelectProgram(layoutStride, params, vb.GetDeclarationElements());
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);
        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawPrimitivesEx: stride=" << layoutStride
            << " prim=" << static_cast<int>(primitive) << " verts=" << vertex_count);

        if (!multiStream && wireframe_ &&
            DrawWireframe(vb, nullptr, primitive, primitiveCount, 0, 0, params.vertexStart))
            return;

        vb.BindForDraw();
        const bool semanticLayout = ConfigureDeclarationForStockProgramEXT(
            const_cast<EasyGLVertexBufferRenderer&>(vb), layoutStride, params);
        TraceBoundTextureUnit("draw-arrays-3d", 0);
        device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
        if (multiStream) RestoreSingleStreamAttributes(vao, params);
        vb.UnbindAfterDraw();
        if (semanticLayout)
            RestoreDeclarationLayoutEXT(const_cast<EasyGLVertexBufferRenderer&>(vb));
    }

    void EasyGLRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                        const IIndexBufferRenderer& ib_in,
                                                        const Matrix& world,
                                                        const Matrix& view,
                                                        const Matrix& projection,
                                                        PrimitiveType primitive,
                                                        int primitiveCount,
                                                        const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-062: see DrawPrimitivesEx's own compiled-effect branch for why this
        // dispatches before RequireDeclarationFitsStockProgramEXT runs.
        if (params.compiledEffectRuntime != nullptr)
        {
            const auto& compiledVb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
            const auto& compiledIb = static_cast<const EasyGLIndexBufferRenderer&>(ib_in);
            const auto compiledStreams = CollectCompiledEffectStreams(compiledVb, params);
            RequireCompiledEffectDeclarations(compiledStreams);
            ::easygl::VertexArray& compiledVao = EnsureCompiledEffectVaoEXT();
            compiledVao.bind();
            const CompiledEffectDepthRangeScope compiledDepthRange(*this);
            BindCompiledEffectForDrawEXT(compiledStreams.data(), compiledStreams.size(),
                                         *params.compiledEffectRuntime);
            compiledIb.ibo.bind(::easygl::BufferTarget::ElementArray);
            const int compiledIndexCount = VertexCountForPrimitives(primitive, primitiveCount);
            const auto compiledIdxType = compiledIb.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                                                  : ::easygl::DataType::UnsignedShort;
            const int compiledIndexSize = compiledIb.thirtyTwoBit ? 4 : 2;
            const void* compiledIndexOffset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) *
                static_cast<std::uintptr_t>(compiledIndexSize));
            if (params.baseVertex == 0)
            {
                device.draw_elements(ToEasyGl(primitive), compiledIndexCount, compiledIdxType,
                                     compiledIndexOffset);
            }
            else
            {
if (ProfileRequiresBaseVertexPointerRebase())
{
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, +1);
                device.draw_elements(ToEasyGl(primitive), compiledIndexCount, compiledIdxType,
                                     compiledIndexOffset);
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, -1);
}
else
{
                ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), compiledIndexCount,
                                                   compiledIdxType, compiledIndexOffset,
                                                   params.baseVertex);
}
            }
            compiledVao.unbind();
            return;
        }
#endif
        // REMED-GFX-DECL-GUARD (REMED-GFX-218): before the VAO is touched, before a program is
        // selected and before any draw is issued. A custom ShaderEffect owns its own
        // element-index attribute convention and is deliberately untouched.
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgramEXT(
                static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetDeclarationElements(),
                CombinedVertexStrideOr(
                    params, static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetStride()),
                params);
        const auto& vb  = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
        const auto& ib  = static_cast<const EasyGLIndexBufferRenderer&>(ib_in);
        // REMED-GFX-201: see DrawPrimitivesEx above. glDrawElementsBaseVertex's baseVertex plays
        // the role glDrawArrays' `first` plays there -- it advances every bound stream by that
        // many of its own elements.
        const bool multiStream = HasMultipleVertexStreams(params);
        auto& vao = const_cast<::easygl::VertexArray&>(vb.vao);
        if (multiStream && params.customEffectRenderer != nullptr)
        {
            vao.bind();
            if (!ConfigureMultiStreamAttributes(vao, params))
            {
                vao.unbind();
                throw System::InvalidOperationException(
                    "EasyGL multi-stream drawing requires every bound VertexBuffer to carry a "
                    "VertexDeclaration.");
            }
            vao.unbind();
        }

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
            vb.BindForDraw();
            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
            const auto idxTypeCustom = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                                        : ::easygl::DataType::UnsignedShort;
            const int indexSizeCustom = ib.thirtyTwoBit ? 4 : 2;
            const void* indexOffsetCustom = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSizeCustom));
            if (params.baseVertex == 0) {
                device.draw_elements(ToEasyGl(primitive), index_count, idxTypeCustom, indexOffsetCustom);
            } else {
if (ProfileRequiresBaseVertexPointerRebase())
{
                // GLES/WebGL profiles cannot assume glDrawElementsBaseVertex (ES 3.2).
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, +1);
                device.draw_elements(ToEasyGl(primitive), index_count, idxTypeCustom, indexOffsetCustom);
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, -1);
}
else
{
                ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxTypeCustom,
                                                   indexOffsetCustom, params.baseVertex);
}
            }
            if (multiStream) RestoreSingleStreamAttributes(vao, params);
            vb.UnbindAfterDraw();
            return;
        }

        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        Prog3D& p = SelectProgram(layoutStride, params, vb.GetDeclarationElements());
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);
        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedPrimitivesEx: stride=" << layoutStride
            << " prim=" << static_cast<int>(primitive) << " indices=" << index_count);

        if (!multiStream && wireframe_ &&
            DrawWireframe(vb, &ib, primitive, primitiveCount,
                          params.startIndex, params.baseVertex, 0))
            return;

        vb.BindForDraw();
        const bool semanticLayout = ConfigureDeclarationForStockProgramEXT(
            const_cast<EasyGLVertexBufferRenderer&>(vb), layoutStride, params);
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType2 = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                               : ::easygl::DataType::UnsignedShort;
        const int indexSize = ib.thirtyTwoBit ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSize));
        if (params.baseVertex == 0) {
            device.draw_elements(ToEasyGl(primitive), index_count, idxType2, indexOffset);
        } else {
if (ProfileRequiresBaseVertexPointerRebase())
{
            // GLES/WebGL profiles cannot assume glDrawElementsBaseVertex (ES 3.2).
            ShiftEnabledPerVertexAttribPointers(params.baseVertex, +1);
            device.draw_elements(ToEasyGl(primitive), index_count, idxType2, indexOffset);
            ShiftEnabledPerVertexAttribPointers(params.baseVertex, -1);
}
else
{
            ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxType2,
                                               indexOffset, params.baseVertex);
}
        }
        if (multiStream) RestoreSingleStreamAttributes(vao, params);
        vb.UnbindAfterDraw();
        if (semanticLayout)
            RestoreDeclarationLayoutEXT(const_cast<EasyGLVertexBufferRenderer&>(vb));
    }

    void EasyGLRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb_in,
                                                          const IIndexBufferRenderer& ib_in,
                                                          const Matrix& world,
                                                          const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          int primitiveCount,
                                                          int instanceCount,
                                                          const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
if (ProfileIsEs2ApiGeneration())
{
        // GLES 2.0 core has no glDrawElementsInstanced/glVertexAttribDivisor, and this profile
        // deliberately claims no instancing extension either -- SupportsCapability(Instancing)
        // answers false, so take the shared base-class refusal (the exact route OPENGLES1 keeps
        // for the same reason) rather than let the draw fail inside GL. This also preserves the
        // Unsupported3DGraphicsCallBehavior::WarnAndStub handling the base refusal implements.
        IGraphicsRenderer::DrawInstancedPrimitivesEx(vb_in, ib_in, world, view, projection,
                                                     primitive, primitiveCount, instanceCount,
                                                     params);
        return;
}
else
{
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plans/plan_fx.md FX-082: an instanced draw recognizes a compiled effect exactly as the other
        // two routes do. Before this branch existed the compiled runtime was ignored here and the
        // draw silently fell through to SelectProgram() -- a stock shader rendering geometry the
        // game had asked a compiled Effect to render. The per-instance streams keep their real
        // InstanceFrequency: BindCompiledEffectForDrawEXT sets each matched attribute's divisor
        // from the stream it was bound from.
        if (params.compiledEffectRuntime != nullptr)
        {
            const auto& compiledVb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
            const auto& compiledIb = static_cast<const EasyGLIndexBufferRenderer&>(ib_in);
            const auto compiledStreams = CollectCompiledEffectStreams(compiledVb, params);
            RequireCompiledEffectDeclarations(compiledStreams);
            ::easygl::VertexArray& compiledVao = EnsureCompiledEffectVaoEXT();
            compiledVao.bind();
            const CompiledEffectDepthRangeScope compiledDepthRange(*this);
            BindCompiledEffectForDrawEXT(compiledStreams.data(), compiledStreams.size(),
                                         *params.compiledEffectRuntime);
            compiledIb.ibo.bind(::easygl::BufferTarget::ElementArray);
            const int compiledIndexCount = VertexCountForPrimitives(primitive, primitiveCount);
            const auto compiledIdxType = compiledIb.thirtyTwoBit
                ? ::easygl::DataType::UnsignedInt : ::easygl::DataType::UnsignedShort;
            const int compiledIndexSize = compiledIb.thirtyTwoBit ? 4 : 2;
            const void* compiledIndexOffset = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) *
                static_cast<std::uintptr_t>(compiledIndexSize));
            if (params.baseVertex == 0)
            {
                device.draw_elements_instanced(ToEasyGl(primitive), compiledIndexCount,
                                               compiledIdxType, compiledIndexOffset,
                                               instanceCount);
            }
            else
            {
if (ProfileRequiresBaseVertexPointerRebase())
{
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, +1);
                device.draw_elements_instanced(ToEasyGl(primitive), compiledIndexCount,
                                               compiledIdxType, compiledIndexOffset,
                                               instanceCount);
                ShiftEnabledPerVertexAttribPointers(params.baseVertex, -1);
}
else
{
                ::metagl::glDrawElementsInstancedBaseVertex(
                    ToEasyGl(primitive), compiledIndexCount, compiledIdxType,
                    compiledIndexOffset, instanceCount, params.baseVertex);
}
            }
            compiledVao.unbind();
            return;
        }
#endif
        // REMED-GFX-DECL-GUARD (REMED-GFX-218): before the VAO is touched, before a program is
        // selected and before any draw is issued. A custom ShaderEffect owns its own
        // element-index attribute convention and is deliberately untouched.
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgramEXT(
                static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetDeclarationElements(),
                CombinedVertexStrideOr(
                    params, static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetStride()),
                params);
        const auto& vb  = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
        const auto& ib  = static_cast<const EasyGLIndexBufferRenderer&>(ib_in);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                             : ::easygl::DataType::UnsignedShort;
        const int indexSize = ib.thirtyTwoBit ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) *
            static_cast<std::uintptr_t>(indexSize));

        const auto& meshDecl = vb.GetDeclarationElements();
        // REMED-GFX-202: the per-vertex side is now exactly what the two ordinary routes do --
        // every bound per-vertex stream at its own locations, with its own VBO, stride and byte
        // offset. Stream 0 is only rewritten when it carries a nonzero offset, which is the
        // condition this route has always used, so a classic zero-offset instanced draw configures
        // nothing extra.
        const bool multiStream = HasMultipleVertexStreams(params);
        const GpuVertexStreamBinding* firstPerVertex = FirstPerVertexStream(params);
        const bool reconfigurePerVertex =
            multiStream || (firstPerVertex != nullptr && firstPerVertex->vertexOffset != 0);
        if (reconfigurePerVertex && meshDecl.empty())
        {
            throw System::InvalidOperationException(
                "EasyGL instanced drawing cannot apply a nonzero vertex-buffer offset "
                "without a VertexDeclaration.");
        }

        // REMED-GFX-202: every per-instance stream, not just the first, each at its own locations
        // with its own stride, element offset and divisor.
        InstanceStreamPlacements instancePlacements;
        const unsigned int instanceBaseLocation = params.customEffectRenderer != nullptr
            ? PerVertexLocationCount(params)
            : kStockInstanceBaseLocation;
        const bool hasInstanceStreams = FirstInstanceStream(params) != nullptr;
        if (hasInstanceStreams)
        {
            if (params.customEffectRenderer == nullptr &&
                instanceBaseLocation < PerVertexLocationCount(params))
            {
                throw System::InvalidOperationException(
                    "EasyGL instanced drawing requires a complete per-instance declaration "
                    "within the 16-attribute XNA profile limit.");
            }
            if (!PlaceInstanceStreams(params, instanceBaseLocation, instancePlacements))
            {
                throw System::InvalidOperationException(
                    "EasyGL instanced drawing requires a complete per-instance declaration "
                    "within the 16-attribute XNA profile limit.");
            }
        }

        auto& vao = const_cast<::easygl::VertexArray&>(vb.vao);
        const bool semanticLayout = params.customEffectRenderer == nullptr &&
            ConfigureDeclarationForStockProgramEXT(
                const_cast<EasyGLVertexBufferRenderer&>(vb),
                CombinedVertexStrideOr(params, vb.GetStride()), params);
        vao.bind();
        if (params.customEffectRenderer != nullptr && reconfigurePerVertex &&
            !ConfigureMultiStreamAttributes(vao, params))
        {
            vao.unbind();
            throw System::InvalidOperationException(
                "EasyGL multi-stream drawing requires every bound VertexBuffer to carry a "
                "VertexDeclaration.");
        }
        {
            std::size_t placementIndex = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0)
                    continue;
                const InstanceStreamPlacement& placement =
                    instancePlacements.entries[placementIndex++];
                // The divisor IS the public InstanceFrequency: GL advances the attribute once per
                // `divisor` instances, which is `floor(instanceIndex / frequency)` -- the same rule
                // D3D11's InstanceDataStepRate defines. The element offset is this stream's own
                // VertexOffset, converted with this stream's own stride inside
                // ConfigureDeclarationAttributes, never another stream's.
                ConfigureDeclarationAttributes(
                    vao, *static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer),
                    placement.firstLocation, stream.vertexOffset,
                    static_cast<unsigned int>(stream.instanceFrequency),
                    placement.elementCount);
            }
        }

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        }
        else
        {
            // REMED-GFX-201: the shader sees the CONCATENATION of the per-vertex streams, so the
            // program is selected by the combined stride -- which equals the one stream's own
            // stride whenever a single per-vertex buffer is bound.
            Prog3D& p = SelectProgram(
                CombinedVertexStrideOr(params, vb.GetStride()), params, meshDecl);
            p.prog.use();
            BindDrawParams(p, world, view, projection, params);
            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        }

        if (params.baseVertex == 0)
        {
            device.draw_elements_instanced(
                ToEasyGl(primitive), index_count, idxType, indexOffset, instanceCount);
        }
        else
        {
if (ProfileRequiresBaseVertexPointerRebase())
{
            ShiftEnabledPerVertexAttribPointers(params.baseVertex, +1);
            device.draw_elements_instanced(
                ToEasyGl(primitive), index_count, idxType, indexOffset, instanceCount);
            ShiftEnabledPerVertexAttribPointers(params.baseVertex, -1);
}
else
{
            ::metagl::glDrawElementsInstancedBaseVertex(
                ToEasyGl(primitive), index_count, idxType, indexOffset,
                instanceCount, params.baseVertex);
}
        }

        // REMED-GFX-202: every location this draw claimed is released again, in reverse, so a later
        // draw through the same VAO never inherits a stale divisor or a pointer into a foreign VBO.
        for (int i = instancePlacements.count; i-- > 0;)
        {
            const InstanceStreamPlacement& placement =
                instancePlacements.entries[static_cast<std::size_t>(i)];
            DisableDeclarationAttributes(
                vao, placement.firstLocation, placement.elementCount);
        }
        if (reconfigurePerVertex)
        {
            RestoreSingleStreamAttributes(vao, params);
        }
        if (params.customEffectRenderer == nullptr)
        {
            Prog3D& p = SelectProgram(
                CombinedVertexStrideOr(params, vb.GetStride()), params, meshDecl);
            if (p.loc_instanced >= 0)
                p.prog.set_uniform(p.loc_instanced, 0.0f);
        }
        vao.unbind();
        if (semanticLayout)
            RestoreDeclarationLayoutEXT(const_cast<EasyGLVertexBufferRenderer&>(vb));
}
    }

    void EasyGLRenderer::IssueIndirectDrawEXT(const IVertexBufferRenderer& vb_in,
                                              const IIndexBufferRenderer* ib_in,
                                              const Matrix& world,
                                              const Matrix& view,
                                              const Matrix& projection,
                                              PrimitiveType primitive,
                                              const IStorageBufferRenderer& argumentBuffer,
                                              int argumentByteOffset,
                                              const GpuDrawParams& params)
    {
        if (metagl::IsContextLost()) return;
        if (!SupportsIndirectDrawEXT())
            throw System::NotSupportedException(
                "CNA EasyGL: this GL context has no indirect draw (GL ES 3.1 / desktop GL 4.0 and "
                "later have it; WebGL has it in no version).");
        if (params.compiledEffectRuntime != nullptr)
            throw System::NotSupportedException(
                "CNA EasyGL: an indirect draw does not accept a compiled (FX) effect; the effect "
                "framework's own draw routes carry the primitive count this route reads from GPU "
                "memory instead.");

        // REMED-GFX-DECL-GUARD (REMED-GFX-218): before the VAO is touched, before a program is
        // selected and before any draw is issued -- exactly as on the three ordinary routes.
        if (params.customEffectRenderer == nullptr)
            RequireDeclarationFitsStockProgramEXT(
                static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetDeclarationElements(),
                CombinedVertexStrideOr(
                    params, static_cast<const EasyGLVertexBufferRenderer&>(vb_in).GetStride()),
                params);

        const auto& vb = static_cast<const EasyGLVertexBufferRenderer&>(vb_in);
        const auto* ib = static_cast<const EasyGLIndexBufferRenderer*>(ib_in);
        const auto& meshDecl = vb.GetDeclarationElements();

        // REMED-GFX-201/202: the stream configuration is the instanced route's, not the simple
        // one's, because an indirect draw always carries an instance count -- the argument buffer
        // has a word for it whether or not anything ever sets it above 1.
        const bool multiStream = HasMultipleVertexStreams(params);
        const GpuVertexStreamBinding* firstPerVertex = FirstPerVertexStream(params);
        const bool reconfigurePerVertex =
            multiStream || (firstPerVertex != nullptr && firstPerVertex->vertexOffset != 0);
        if (reconfigurePerVertex && meshDecl.empty())
            throw System::InvalidOperationException(
                "EasyGL indirect drawing cannot apply a nonzero vertex-buffer offset without a "
                "VertexDeclaration.");

        InstanceStreamPlacements instancePlacements;
        const unsigned int instanceBaseLocation = params.customEffectRenderer != nullptr
            ? PerVertexLocationCount(params)
            : kStockInstanceBaseLocation;
        if (FirstInstanceStream(params) != nullptr)
        {
            if ((params.customEffectRenderer == nullptr &&
                 instanceBaseLocation < PerVertexLocationCount(params)) ||
                !PlaceInstanceStreams(params, instanceBaseLocation, instancePlacements))
            {
                throw System::InvalidOperationException(
                    "EasyGL indirect drawing requires a complete per-instance declaration within "
                    "the 16-attribute XNA profile limit.");
            }
        }

        auto& vao = const_cast<::easygl::VertexArray&>(vb.vao);
        const bool semanticLayout = params.customEffectRenderer == nullptr &&
            ConfigureDeclarationForStockProgramEXT(
                const_cast<EasyGLVertexBufferRenderer&>(vb),
                CombinedVertexStrideOr(params, vb.GetStride()), params);
        vao.bind();
        if (params.customEffectRenderer != nullptr && reconfigurePerVertex &&
            !ConfigureMultiStreamAttributes(vao, params))
        {
            vao.unbind();
            throw System::InvalidOperationException(
                "EasyGL multi-stream drawing requires every bound VertexBuffer to carry a "
                "VertexDeclaration.");
        }
        {
            std::size_t placementIndex = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0)
                    continue;
                const InstanceStreamPlacement& placement =
                    instancePlacements.entries[placementIndex++];
                ConfigureDeclarationAttributes(
                    vao, *static_cast<const EasyGLVertexBufferRenderer*>(stream.buffer),
                    placement.firstLocation, stream.vertexOffset,
                    static_cast<unsigned int>(stream.instanceFrequency),
                    placement.elementCount);
            }
        }

        if (params.customEffectRenderer)
        {
            BindCustomEffectMatrices(*params.customEffectRenderer, world, view, projection);
        }
        else
        {
            Prog3D& p = SelectProgram(
                CombinedVertexStrideOr(params, vb.GetStride()), params, meshDecl);
            p.prog.use();
            BindDrawParams(p, world, view, projection, params);
        }
        if (ib != nullptr)
            ib->ibo.bind(::easygl::BufferTarget::ElementArray);

        // The arguments are fetched from this buffer by the GPU as the command is issued. Binding
        // it is the whole difference from an ordinary draw: nothing here reads the numbers, and
        // nothing waits for them.
        static_cast<const EasyGLStorageBufferRenderer&>(argumentBuffer).BindAsDrawIndirect();
        const void* argumentAddress = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(argumentByteOffset));

        // The wireframe fallback the ordinary routes take is deliberately absent: it rebuilds a
        // line-list from the primitive count, and this route does not have one to rebuild from.
        // A wireframe indirect draw renders filled rather than pretending otherwise.
        if (ib != nullptr)
        {
            const auto idxType = ib->thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                                  : ::easygl::DataType::UnsignedShort;
            ::metagl::glDrawElementsIndirect(ToEasyGl(primitive), idxType, argumentAddress);
        }
        else
        {
            ::metagl::glDrawArraysIndirect(ToEasyGl(primitive), argumentAddress);
        }

        // REMED-GFX-202: every location this draw claimed is released again, in reverse, so a later
        // draw through the same VAO never inherits a stale divisor or a pointer into a foreign VBO.
        for (int i = instancePlacements.count; i-- > 0;)
        {
            const InstanceStreamPlacement& placement =
                instancePlacements.entries[static_cast<std::size_t>(i)];
            DisableDeclarationAttributes(vao, placement.firstLocation, placement.elementCount);
        }
        if (reconfigurePerVertex)
            RestoreSingleStreamAttributes(vao, params);
        if (params.customEffectRenderer == nullptr)
        {
            Prog3D& p = SelectProgram(
                CombinedVertexStrideOr(params, vb.GetStride()), params, meshDecl);
            if (p.loc_instanced >= 0)
                p.prog.set_uniform(p.loc_instanced, 0.0f);
        }
        vao.unbind();
        if (semanticLayout)
            RestoreDeclarationLayoutEXT(const_cast<EasyGLVertexBufferRenderer&>(vb));
    }

    void EasyGLRenderer::DrawPrimitivesIndirectEXT(const IVertexBufferRenderer& vb,
                                                   const Matrix& world,
                                                   const Matrix& view,
                                                   const Matrix& projection,
                                                   PrimitiveType primitive,
                                                   const IStorageBufferRenderer& argumentBuffer,
                                                   int argumentByteOffset,
                                                   const GpuDrawParams& params)
    {
        IssueIndirectDrawEXT(vb, nullptr, world, view, projection, primitive, argumentBuffer,
                             argumentByteOffset, params);
    }

    void EasyGLRenderer::DrawIndexedPrimitivesIndirectEXT(const IVertexBufferRenderer& vb,
                                                          const IIndexBufferRenderer& ib,
                                                          const Matrix& world,
                                                          const Matrix& view,
                                                          const Matrix& projection,
                                                          PrimitiveType primitive,
                                                          const IStorageBufferRenderer& argumentBuffer,
                                                          int argumentByteOffset,
                                                          const GpuDrawParams& params)
    {
        IssueIndirectDrawEXT(vb, &ib, world, view, projection, primitive, argumentBuffer,
                             argumentByteOffset, params);
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_EASYGL
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace EasyGL
    {
        std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);
        std::unique_ptr<IGraphicsRenderer> CreateGraphicsRendererForProfile(
            const GraphicsRendererCreateArgs& args, GlProfile profile);
    }

    std::unique_ptr<IGraphicsRenderer> EasyGL::CreateGraphicsRendererForProfile(
        const GraphicsRendererCreateArgs& args, EasyGL::GlProfile profile)
    {
        if (args.glContext == nullptr)
        {
            throw CNA::Platform::PlatformNotSupportedException(
                CNA::Platform::PlatformCapability::OpenGlContext, "EasyGL renderer");
        }
        return std::make_unique<EasyGL::EasyGLRenderer>(
            args.surface, *args.glContext,
            args.virtualWidth, args.virtualHeight,
            args.presentationMode, args.contextRecoveryEnabled,
            args.multiSampleCount, args.swapInterval, profile);
    }

    std::unique_ptr<IGraphicsRenderer> EasyGL::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        // plans/plan_runtimerenderer.md P11: the build's default profile. Every registry entry uses
        // CreateGraphicsRendererForProfile with its own identity's profile instead.
        return EasyGL::CreateGraphicsRendererForProfile(args, EasyGL::kCompileTimeGlProfile);
    }
#endif
}
