#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"

#include "CNA/Internal/Graphics/SrgbTransfer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>

#include "CNA/Platform.hpp"

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
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <SDL3/SDL.h>
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
// uRtFlipV.x/.y/.z/.w carry texture units 0-3 and uRtFlipVHi.x carries unit 4: 1 when the source
// bound there is a render-target colour attachment, 0 for an ordinary texture (and 0 always on a
// renderer whose framebuffer origin is top-left). Exactly one correction is applied, at sample
// time, per bound source -- nothing is copied, read back or re-uploaded, and no shader variant is
// created, because the flag is a uniform rather than a compile-time switch. Kept as one macro so
// the eleven shaders cannot drift apart; the same per-slot shape bgfx uses for u_rtFlipV
// (REMED-GFX-067/078).
#define CNA_GL_RT_SAMPLE_UV_DECL \
"uniform vec4 uRtFlipV;\n" \
"vec2 cnaSampleUV(vec2 uv,float flip){return vec2(uv.x,mix(uv.y,1.0-uv.y,flip));}\n"

/// REMED-GFX-147: unit 4's flag, declared only by the shaders that reach that far (PbrEffect).
#define CNA_GL_RT_SAMPLE_UV_HI_DECL \
"uniform vec4 uRtFlipVHi;\n"

/// plan_gltf.md GLTF-210/GLTF-212: the sRGB transfer, pasted into the PbrEffect shaders. The text
/// is not written here -- it comes from CNA/Internal/Graphics/SrgbTransfer.hpp, which is also
/// where the C++ implementation of the same formula lives, so the two cannot drift into two
/// slightly different curves.
#define CNA_GL_SRGB_TRANSFER_DECL CNA_GLSL_SRGB_TRANSFER

// plan_gltf.md GLTF-264: a normal follows the inverse transpose of the blended skin matrix.
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

// plan_gltf.md GLTF-176: a tangent frame changes orientation under a negative-determinant
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

namespace CNA::Internal::Renderers::EasyGL
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;

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

    // plan_glbackends.md Phase B (GLB-10/11): every embedded shader in this file is authored
    // once, against GLSL ES 3.00 (the OPENGLES3/WEBGL2 profiles' native syntax). Body syntax
    // (in/out, texture(), no varying/attribute) is shared with desktop GLSL 3.30 core -- only
    // the "#version ...\nprecision ... float;\n" header two lines differ, so OPENGL33 does not
    // need a second copy of every shader, just a header rewrite performed here at first-use time.
    enum class GlShaderStageKind { Vertex, Fragment };

    namespace
    {
        // plan_glbackends.md GLB-36 helper: true whole-word replace (identifiers only), used for
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

        // plan_glbackends.md GLB-36 helper: GLSL ES 1.00 has no unified texture() overload set --
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

        // plan_glbackends.md GLB-36 follow-up: GLSL ES 1.00 has no integer vertex attribute types
        // at all -- "layout(location=N) in uvec4 aBoneIndices;" (this file's
        // SkinnedEffect/SkinnedPbrEffect shaders) has no direct equivalent. Encodes bone indices
        // as a float vec4 instead (the matching C++-side change is
        // DescribeVertexElementFormat()'s Byte4 case, which reads the same underlying
        // UnsignedByte bytes as floats under this profile rather than true integers -- 0-255
        // range, far more than the <=72 bone count needs, exactly float-representable). Rewrites,
        // applied only when the source actually declares "aBoneIndices" (a no-op for every other
        // shader in this file):
        //   - "uvec4" -> "vec4" (only ever appears in the aBoneIndices declaration itself, so a
        //     blanket whole-word replace is safe)
        //   - "aBoneIndices.x"/".y"/".z"/".w" -> "int(aBoneIndices.x)"/etc. at their 4 uBones[]
        //     index expressions (the only place aBoneIndices is ever used in this file) -- array
        //     indices must be integer-typed even though the attribute itself is now a float vec4.
        std::string RewriteBoneIndicesForEs100(std::string text)
        {
            if (text.find("aBoneIndices") == std::string::npos) return text;
            text = ReplaceWholeWord(std::move(text), "uvec4", "vec4");
            for (char component : { 'x', 'y', 'z', 'w' })
            {
                const std::string from = std::string("aBoneIndices.") + component;
                const std::string to = "int(" + from + ")";
                size_t pos = 0;
                while ((pos = text.find(from, pos)) != std::string::npos)
                {
                    text.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }
            return text;
        }

        // plan_glbackends.md GLB-36: rewrites a GLSL ES 3.00 shader body to GLSL ES 1.00
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
        //   - "uvec4 aBoneIndices" (SkinnedEffect/SkinnedPbrEffect only) -> float-encoded, see
        //     RewriteBoneIndicesForEs100 above.
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
            // "attribute uvec4 aBoneIndices;" declaration line produced by the
            // layout(location=N) branch above (which `continue`s past the per-line pipeline).
            return RewriteBoneIndicesForEs100(std::move(out));
        }
    }

    // plan_glbackends.md GLB-36: extracts (location, name) pairs from
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
#if defined(CNA_GL_PROFILE_OPENGL33)
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
#elif defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
        std::string src(es300Source);
        const std::string versionLine = "#version 300 es\n";
        const auto versionPos = src.find(versionLine);
        if (versionPos == std::string::npos) return src;
        src.replace(versionPos, versionLine.size(), "#version 100\n");
        std::string transformed = TransformGlslEs300BodyToEs100(src, stage);
        // SkinnedEffect/SkinnedPbrEffect's "uvec4 aBoneIndices" is converted away by
        // TransformGlslEs300BodyToEs100/RewriteBoneIndicesForEs100 (float-encoded bone indices,
        // GLB-36 follow-up). This check is now a defensive safety net for any FUTURE shader that
        // introduces a genuinely new integer vertex attribute the transform doesn't yet handle --
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
#else
        (void)stage;
        return std::string(es300Source);
#endif
    }

#if defined(CNA_GL_PROFILE_OPENGL33)
    namespace
    {
        // plan_glbackends.md GLB-40: desktop GL core profile -- unlike GLES/WebGL, which always
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
        // convention (meta-gl itself loads every GL entry point the same way).
        void EnableVertexProgramPointSize()
        {
            using GlEnableFn = void (*)(unsigned int);
            static const auto glEnableFn =
                reinterpret_cast<GlEnableFn>(SDL_GL_GetProcAddress("glEnable"));
            constexpr unsigned int kGlVertexProgramPointSize = 0x8642;
            if (glEnableFn) glEnableFn(kGlVertexProgramPointSize);
        }
    }
#endif

    // --- EasyGLTexture3DRenderer ---

    static constexpr int kTexLinear       = static_cast<int>(::metagl::TextureMagFilter::Linear);
    static constexpr int kTexClampToEdge  = static_cast<int>(::metagl::TextureWrapMode::ClampToEdge);

    static ::easygl::TextureUnit ToTextureUnit(int unit)
    {
        return static_cast<::easygl::TextureUnit>(
            static_cast<GLenum>(::easygl::TextureUnit::Texture0) + unit);
    }

    // =============================================================================================
    // OPENGLES2 profile support (plan_opengles2.md)
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
    //     enabled attribute pointer by baseVertex elements of its own stride instead
    //     (Es2ShiftEnabledVertexAttribPointers -- FNA3D's own no-base-vertex GL fallback shape);
    //   - GL_READ_FRAMEBUFFER does not exist (ES 3.0): readbacks bind GL_FRAMEBUFFER, whose single
    //     color attachment is the implicit read source (kReadbackFramebufferTarget below).
    // =============================================================================================

    /// Framebuffer binding point used by the non-MSAA texture/render-target readback paths.
    /// GLES 2.0 has only the combined GL_FRAMEBUFFER target (GL_READ_FRAMEBUFFER is ES 3.0);
    /// reads then come from the bound framebuffer's single color attachment implicitly. The MSAA
    /// resolve paths keep their separate READ/DRAW targets -- they are unreachable under
    /// OPENGLES2, which forces every sample count to 1.
#if defined(CNA_GL_PROFILE_OPENGLES2)
    static constexpr ::easygl::FramebufferTarget kReadbackFramebufferTarget =
        ::easygl::FramebufferTarget::Framebuffer;
#else
    static constexpr ::easygl::FramebufferTarget kReadbackFramebufferTarget =
        ::easygl::FramebufferTarget::ReadFramebuffer;
#endif

    /// Internal format for the explicit-internal-format RGBA8 texture allocations in this file
    /// (render-target color storage and cube-map faces). GLES 2.0's glTexImage2D accepts only
    /// UNSIZED internal formats (sized RGBA8 arrived with ES 3.0 / GL_OES_required_internalformat),
    /// so the OPENGLES2 profile allocates GL_RGBA; every other profile keeps the sized RGBA8
    /// allocation unchanged.
#if defined(CNA_GL_PROFILE_OPENGLES2)
    static constexpr ::metagl::InternalFormat kRgbaTexImageInternalFormat = ::metagl::InternalFormat::Rgba;
#else
    static constexpr ::metagl::InternalFormat kRgbaTexImageInternalFormat = ::metagl::InternalFormat::Rgba8;
#endif

    /// Attaches a render target's depth (or packed depth+stencil) renderbuffer to the bound FBO.
    /// GLES 2.0 has no GL_DEPTH_STENCIL_ATTACHMENT (the combined point is ES 3.0) -- a packed
    /// GL_DEPTH24_STENCIL8 renderbuffer (GL_OES_packed_depth_stencil) is attached to the DEPTH
    /// and STENCIL points separately there; every other profile keeps the single combined attach.
    static void AttachDepthRenderbufferToBoundFbo(::easygl::Framebuffer& fbo,
                                                  ::metagl::FramebufferAttachment attachment,
                                                  ::easygl::Renderbuffer& rbo)
    {
#if defined(CNA_GL_PROFILE_OPENGLES2)
        if (attachment == ::metagl::FramebufferAttachment::DepthStencil)
        {
            fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferAttachment::Depth, rbo);
            fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferAttachment::Stencil, rbo);
            return;
        }
#endif
        fbo.attach_renderbuffer(::easygl::FramebufferTarget::Framebuffer, attachment, rbo);
    }

#if defined(CNA_GL_PROFILE_OPENGLES2)
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
                reinterpret_cast<GlTexParameterfFn>(SDL_GL_GetProcAddress("glTexParameterf"));
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

        /// Re-offsets every enabled vertex attribute's pointer by @p baseVertex elements of its
        /// own stride, in @p direction (+1 applies the shift, -1 restores it). Must run while the
        /// draw's VAO is bound; uses only core ES 2.0 queries and calls. This produces exactly
        /// glDrawElementsBaseVertex's addressing for the single-stream layouts reachable under
        /// this profile (multi-stream input and instancing are refused up front -- see
        /// SupportsCapability), because every enabled attribute belongs to the one vertex stream
        /// whose elements baseVertex counts.
        void Es2ShiftEnabledVertexAttribPointers(int baseVertex, int direction)
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

                GLint size = 4, type = GL_FLOAT, normalized = 0, stride = 0, bufferBinding = 0;
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArraySize, &size);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayType, &type);
                ::metagl::glGetVertexAttribiv(location,
                                              ::metagl::VertexAttribParameter::ArrayNormalized, &normalized);
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
                ::metagl::glVertexAttribPointer(
                    location, size, static_cast<::metagl::DataType>(type),
                    normalized != 0 ? 1 : 0, stride,
                    static_cast<const std::uint8_t*>(pointer) + delta);
            }

            ::metagl::glBindBuffer(::metagl::BufferTarget::Array,
                                   ::metagl::BufferId{static_cast<GLuint>(previousArrayBuffer)});
        }
    }
#endif // CNA_GL_PROFILE_OPENGLES2

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
                                  kRgbaTexImageInternalFormat,
                                  levelSize, levelSize,
                                  ::metagl::PixelFormat::Rgba,
                                  ::metagl::PixelType::UnsignedByte,
                                  nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- completeness is instead handled by
        // Es2ApplyPendingSamplerToUnit's mip-term demotion, driven by this registration.
        Es2RegisterTextureLevels(tex_.native_handle(), levelCount_);
#else
        // REMED-GFX-174: see EasyGLRenderTargetRenderer's identical clamp -- a cube sampled through
        // EnvironmentMapEffect's slot-1 sampler faces exactly the same completeness rule.
        tex_.set_parameter(::easygl::TextureTarget::TextureCubeMap, ::easygl::TextureParameterSetter::MaxLevel,
                           levelCount_ - 1);
#endif
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GL reuses deleted names; drop the level registration before tex_'s destructor frees it.
        Es2UnregisterTexture(tex_.native_handle());
#endif
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
#if !defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there, so the explicit selection exists only for the ES 3.0 profiles.
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
#endif

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

    void EasyGLEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count)), 1);
    }

    void EasyGLEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        const int loc = program_.uniform_location(name);
        if (loc >= 0) program_.set_uniform_fv(loc, std::span<const float>(values, static_cast<std::size_t>(count) * 2), 2);
    }

    void EasyGLEffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        TraceBoundTextureUnit("bind-texture-3d", unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // ES 2.0 keeps sampling state on the texture object, so a texture bound AFTER the
        // GraphicsDevice applied this slot's SamplerState must receive that state now.
        Es2ApplyPendingSamplerToUnit(unit);
#endif

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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // See BindTexture just above -- same ES 2.0 texture-object sampling-state rule.
        Es2ApplyPendingSamplerToUnit(unit);
#endif
    }

    // plan_graphics.md Task 863: same shape as BindTextureCube(), but for a sampler3D --
    // ITexture3DRenderer is its own interface (not a subtype of ITextureRenderer), so this can't
    // just overload/reuse BindTexture() at the call site.
    void EasyGLEffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (!texture) return;
        texture->BindGL(unit);
        ::metagl::glActiveTexture(::metagl::TextureUnit::Texture0);
    }

    // --- EasyGLOcclusionQueryRenderer ---

    EasyGLOcclusionQueryRenderer::EasyGLOcclusionQueryRenderer(::easygl::ResourceRegistry* registry)
        : registry_(registry)
    {
#if !defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no query objects (glGenQueries is ES 3.0), and
        // SupportsCapability(OcclusionQuery) reports false under that profile -- never creating
        // the GL query there makes every method below take its existing !is_created() no-op
        // path, honest and crash-free on any ES 2.0 driver (IsComplete stays false,
        // PixelCount stays 0).
        query_.create();
#endif
        if (registry_) registry_->add(this);
    }

    EasyGLOcclusionQueryRenderer::~EasyGLOcclusionQueryRenderer()
    {
        if (registry_) registry_->remove(this);
    }

    void EasyGLOcclusionQueryRenderer::Begin()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        query_.begin(::easygl::QueryTarget::AnySamplesPassed);
    }

    void EasyGLOcclusionQueryRenderer::End()
    {
        if (metagl::IsContextLost() || !query_.is_created()) return;
        query_.end(::easygl::QueryTarget::AnySamplesPassed);
    }

    bool EasyGLOcclusionQueryRenderer::IsComplete() const
    {
        if (metagl::IsContextLost() || !query_.is_created()) return false;
        return query_.is_result_available();
    }

    int EasyGLOcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete()) return 0;
        // GLES3 uses GL_ANY_SAMPLES_PASSED — result is 0 (none) or 1 (any)
        return static_cast<int>(query_.result());
    }

    void EasyGLOcclusionQueryRenderer::release_gl_handle_only()
    {
        query_.reset_handle_no_gl();
    }

    void EasyGLOcclusionQueryRenderer::recreate_gl_resource()
    {
#if !defined(CNA_GL_PROFILE_OPENGLES2)
        // See the constructor -- no GL query objects exist under the OPENGLES2 profile.
        query_.create();
#endif
    }

    // --- EasyGLTextureRenderer ---

    EasyGLTextureRenderer::EasyGLTextureRenderer(const ImageData& data, ::easygl::ResourceRegistry* registry)
        : registry_(registry), mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        width = data.width;
        height = data.height;
        texture.create();
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, data.pixels.data());
        AllocateDeclaredLevels();
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL, so Task 924's clamp cannot exist there --
        // completeness under mip-carrying filters is instead handled by
        // Es2ApplyPendingSamplerToUnit's mip-term demotion, driven by this registration.
        Es2RegisterTextureLevels(texture.native_handle(), mipLevels_);
#else
        // Task 924: clamp GL_TEXTURE_MAX_LEVEL to the real level count -- otherwise a mipmap-
        // requiring TextureFilter (e.g. Anisotropic) treats this as an incomplete mipmap chain
        // (GL's own default max level is 1000) and renders solid black, even for an ordinary
        // single-level (mipLevels_==1) texture that never uploads any level beyond 0.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::easygl::TextureParameterSetter::MaxLevel,
                               mipLevels_ - 1);
#endif
        if (registry_) registry_->add(this);
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
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, level, levelW, levelH, nullptr);
        }
    }

    EasyGLTextureRenderer::~EasyGLTextureRenderer()
    {
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GL reuses deleted names; drop the level registration before texture's destructor frees it.
        Es2UnregisterTexture(texture.native_handle());
#endif
        if (registry_) registry_->remove(this);
    }

    void EasyGLTextureRenderer::release_gl_handle_only()
    {
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // Context loss: the name dies with the old context (and the new one may re-issue it), so
        // the registration must go BEFORE the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(texture.native_handle());
#endif
        texture.reset_handle_no_gl();
    }

    void EasyGLTextureRenderer::recreate_gl_resource()
    {
        texture.create();
        if (pixels_ && !pixels_->empty())
        {
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                                 width, height, pixels_->data());
        }
        else
        {
            const std::vector<uint8_t> blank(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
            texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0,
                                 width, height, blank.data());
        }
        // REMED-GFX-175: the fresh GL texture object has storage for level 0 only, so a declared
        // chain has to be re-allocated here too or the texture comes back from a context loss
        // mipmap-incomplete and samples black under every mip-filtering ordinal.
        AllocateDeclaredLevels();
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // See the constructor: the fresh name replaces whatever release_gl_handle_only dropped.
        Es2RegisterTextureLevels(texture.native_handle(), mipLevels_);
#else
        // Task 924: the fresh GL texture object defaults GL_TEXTURE_MAX_LEVEL back to 1000 --
        // reapply the same clamp the constructor set, matching this texture's real level count.
        texture.set_parameter(::easygl::TextureTarget::Texture2D, ::easygl::TextureParameterSetter::MaxLevel,
                               mipLevels_ - 1);
#endif
    }

    void EasyGLTextureRenderer::BindGL(int unit) const
    {
        texture.active_bind(ToTextureUnit(unit), ::easygl::TextureTarget::Texture2D);
    }

    void EasyGLTextureRenderer::ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels)
    {
        if (registry_) pixels_ = std::move(pixels);
    }

    void EasyGLTextureRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        // pixels_ (shared with Texture2D::cpuPixels_) is already updated by the caller
        // before this method is invoked — no need to update it here.
        texture.bind(::easygl::TextureTarget::Texture2D);
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, 0, width, height, rgba);
    }

    void EasyGLTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        texture.bind(::easygl::TextureTarget::Texture2D);
        texture.set_image_2d(::easygl::TextureTarget::Texture2D, level, levelW, levelH, rgba);
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

    EasyGLRenderTargetRenderer::EasyGLRenderTargetRenderer(int w, int h, int depthFormat,
                                                          ::easygl::ResourceRegistry* registry,
                                                          std::weak_ptr<EasyGLBoundTargetEXT> binding,
                                                          bool mipMap, int multiSampleCount)
        : width_(w), height_(h), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount), registry_(registry), binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(w, h) : 1;
        CreateResources();
        if (registry_) registry_->add(this);
        TargetTrace("rt2d.create", this, TraceNativeDetailEXT());
    }

    EasyGLRenderTargetRenderer::~EasyGLRenderTargetRenderer()
    {
        TargetTrace("rt2d.destroy", this, TraceNativeDetailEXT());
        DetachFromBindingEXT();
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GL reuses deleted names; drop the level registration before colorTex_ is freed.
        Es2UnregisterTexture(colorTex_.native_handle());
#endif
        if (registry_) registry_->remove(this);
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no multisample renderbuffers and no blit to resolve them
        // (glRenderbufferStorageMultisample/glBlitFramebuffer are ES 3.0), so the requested
        // preference degrades to single-sample -- the same silent clamp the GL_MAX_SAMPLES limit
        // applies on the ES 3.0 profiles, taken to this profile's real ceiling of 1.
        // GetMultiSampleCount() then reports 0, keeping the public applied count truthful.
        multiSampleCount_ = 0;
#endif
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
                                       kRgbaTexImageInternalFormat,
                                       levelW, levelH,
                                       ::metagl::PixelFormat::Rgba,
                                       ::metagl::PixelType::UnsignedByte,
                                       nullptr);
                levelW = std::max(1, levelW / 2);
                levelH = std::max(1, levelH / 2);
            }
        }
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- the same completeness problem REMED-GFX-174
        // describes is handled by Es2ApplyPendingSamplerToUnit's mip-term demotion instead,
        // driven by this registration.
        Es2RegisterTextureLevels(colorTex_.native_handle(), levelCount_);
#else
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
#endif
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
            msaaColorRbo_.set_storage_multisample(multiSampleCount_,
                                                   ::metagl::InternalFormat::Rgba8,
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
        if (!data || level < 0 || w <= 0 || h <= 0
            || static_cast<std::int64_t>(dataLength)
                < static_cast<std::int64_t>(w) * h * 4)
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

#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has only the combined GL_FRAMEBUFFER binding (kReadbackFramebufferTarget), so
        // selecting a read source below also redirects draws; remember the current binding and
        // restore it afterwards, preserving the ES 3.0 paths' read-only semantics.
        GLint previousFramebuffer = 0;
        ::metagl::glGetIntegerv(::metagl::GetParameter::FramebufferBinding, &previousFramebuffer);
#endif
        ::easygl::Framebuffer mipFbo;
        if (level == 0)
        {
            if (multiSampleCount_ > 0)
                resolveFbo_.bind(::easygl::FramebufferTarget::ReadFramebuffer);
            else
                fbo_.bind(kReadbackFramebufferTarget);
        }
        else
        {
            // Attaching a level above 0 needs GL_OES_fbo_render_mipmap on a real ES 2.0 context
            // (core ES 2.0 restricts glFramebufferTexture2D to level 0) -- universally shipped
            // wherever mip chains exist at all, and advertised by Mesa.
            mipFbo.create();
            mipFbo.bind(kReadbackFramebufferTarget);
            mipFbo.attach_texture_2d(
                kReadbackFramebufferTarget,
                ::metagl::to_framebuffer_attachment(
                    ::metagl::ColorAttachment::Color0),
                ::easygl::TextureTarget::Texture2D, colorTex_, level);
        }
#if !defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there.
        ::metagl::glReadBuffer(
            ::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
#endif
        ::metagl::glReadPixels(
            x, levelHeight - y - h, w, h,
            ::metagl::PixelFormat::Rgba,
            ::metagl::PixelType::UnsignedByte, data);

        const int rowBytes = w * 4;
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        ::metagl::glBindFramebuffer(::metagl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferId{static_cast<GLuint>(previousFramebuffer)});
#else
        ::easygl::Framebuffer::unbind(
            ::easygl::FramebufferTarget::ReadFramebuffer);
#endif
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // Context loss: unregister before the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(colorTex_.native_handle());
#endif
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

    EasyGLRenderTargetCubeRenderer::EasyGLRenderTargetCubeRenderer(int size, int depthFormat,
                                                                    ::easygl::ResourceRegistry* registry,
                                                                    std::weak_ptr<EasyGLBoundTargetEXT> binding,
                                                                    bool mipMap, int multiSampleCount)
        : size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          multiSampleCount_(multiSampleCount), registry_(registry), binding_(std::move(binding))
    {
        levelCount_ = mipMap_ ? CalculateRenderTargetMipLevels(size, size) : 1;
        CreateResources();
        if (registry_) registry_->add(this);
        TargetTrace("cube.create", this, TraceNativeDetailEXT());
    }

    EasyGLRenderTargetCubeRenderer::~EasyGLRenderTargetCubeRenderer()
    {
        TargetTrace("cube.destroy", this, TraceNativeDetailEXT());
        DetachFromBindingEXT();
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GL reuses deleted names; drop the level registration before cubeTex_ is freed.
        Es2UnregisterTexture(cubeTex_.native_handle());
#endif
        if (registry_) registry_->remove(this);
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // See EasyGLRenderTargetRenderer::CreateResources -- GLES 2.0 has no multisample
        // renderbuffers/blit, so the requested preference degrades to single-sample.
        multiSampleCount_ = 0;
#endif
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
                                       kRgbaTexImageInternalFormat,
                                       levelSize, levelSize,
                                       ::metagl::PixelFormat::Rgba,
                                       ::metagl::PixelType::UnsignedByte,
                                       nullptr);
                levelSize = std::max(1, levelSize / 2);
            }
        }
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no GL_TEXTURE_MAX_LEVEL -- see EasyGLRenderTargetRenderer::CreateResources.
        Es2RegisterTextureLevels(cubeTex_.native_handle(), levelCount_);
#else
        // REMED-GFX-174: see EasyGLRenderTargetRenderer's identical clamp.
        cubeTex_.set_parameter(::easygl::TextureTarget::TextureCubeMap,
                               ::easygl::TextureParameterSetter::MaxLevel,
                               levelCount_ - 1);
#endif
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
                rbo.set_storage_multisample(multiSampleCount_,
                                             ::metagl::InternalFormat::Rgba8,
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

#if defined(CNA_GL_PROFILE_OPENGLES2)
        // See EasyGLRenderTargetRenderer::GetData -- the combined GL_FRAMEBUFFER binding must be
        // restored so a read here cannot redirect subsequent draws.
        GLint previousFramebuffer = 0;
        ::metagl::glGetIntegerv(::metagl::GetParameter::FramebufferBinding, &previousFramebuffer);
#endif
        ::easygl::Framebuffer fbo;
        fbo.create();
        fbo.bind(kReadbackFramebufferTarget);
        fbo.attach_texture_2d(kReadbackFramebufferTarget,
                              ::metagl::to_framebuffer_attachment(::metagl::ColorAttachment::Color0),
                              kCubeFaceTargets[face],
                              cubeTex_, level);
#if !defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no glReadBuffer; the bound framebuffer's single color attachment is the
        // implicit read source there.
        fbo.set_read_buffer(::metagl::to_read_buffer(::metagl::ColorAttachment::Color0));
#endif

        const bool complete = fbo.is_complete(kReadbackFramebufferTarget);
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

#if defined(CNA_GL_PROFILE_OPENGLES2)
        ::metagl::glBindFramebuffer(::metagl::FramebufferTarget::Framebuffer,
                                    ::metagl::FramebufferId{static_cast<GLuint>(previousFramebuffer)});
#else
        ::easygl::Framebuffer::unbind(::easygl::FramebufferTarget::ReadFramebuffer);
#endif
        return complete;
    }

    void EasyGLRenderTargetCubeRenderer::release_gl_handle_only()
    {
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // Context loss: unregister before the handle is zeroed; recreate_gl_resource re-registers.
        Es2UnregisterTexture(cubeTex_.native_handle());
#endif
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

    EasyGLSpriteBatchRenderer::EasyGLSpriteBatchRenderer(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                                       EasyGLRenderer* renderer)
        : device_(device)
        , registry_(registry)
        , graphicsRenderer_(renderer)
    {
        InitializeResources();
        if (registry_) registry_->add(this);
    }

    EasyGLSpriteBatchRenderer::~EasyGLSpriteBatchRenderer()
    {
        if (registry_) registry_->remove(this);
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

void main()
{
    FragColor = texture(texture1, TexCoord) * Color;
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
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
        // plan_glbackends.md GLB-36: see CompileAndLink's identical comment -- rebind the same
        // numeric attribute locations the ES 3.00 source's layout(location=N) qualifiers
        // specified, since the GLSL ES 1.00 shader text these profiles compile has no
        // layout(location=N) at all.
        for (const auto& [location, name] : ExtractVertexAttribLocations(vertexShaderSource))
        {
            program_.bind_attrib_location(static_cast<unsigned int>(location), name);
        }
#endif
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
        vao_.create();

        vao_.bind();
        vbo_.bind(::easygl::BufferTarget::Array);

        // Position (0), TexCoord (1), Color (2)
        vao_.enable_attribute(0);
        vao_.set_attribute_pointer(0, 2, ::easygl::DataType::Float, false, 8 * sizeof(float), (void*)0);

        vao_.enable_attribute(1);
        vao_.set_attribute_pointer(1, 2, ::easygl::DataType::Float, false, 8 * sizeof(float),
                                   (void*)(2 * sizeof(float)));

        vao_.enable_attribute(2);
        vao_.set_attribute_pointer(2, 4, ::easygl::DataType::Float, false, 8 * sizeof(float),
                                   (void*)(4 * sizeof(float)));

        ibo_.bind(::easygl::BufferTarget::ElementArray);
        vao_.unbind();
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
        // GraphicsDevice.BlendState still claimed was active. Matches the same bug shape SDL_Renderer
        // already fixed (Task 695) -- see docs/sdl-renderer-2d-completeness.md.
        begun = true;
    }

    void EasyGLSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void EasyGLSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
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
        if (graphicsRenderer_)
            graphicsRenderer_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);

        vbo_.bind(::easygl::BufferTarget::Array);
        vbo_.set_data(::easygl::BufferTarget::Array,
                      pending_vertices_.data(),
                      pending_vertices_.size() * sizeof(Vertex));

        vao_.bind();

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

        vao_.unbind();

        pending_vertices_.clear();
        pending_indices_.clear();
        current_texture_ = nullptr;
    }

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
        if (!begun) throw std::runtime_error("Draw called before Begin()");

        // Flush pending batch if texture changes
        if (current_texture_ != &texture)
        {
            if (current_texture_ != nullptr) FlushBatch();
            current_texture_ = &texture;
            // REMED-GFX-147: resolved once per bound source rather than once per sprite -- a
            // batch is by construction one texture, so this is a binding-time decision.
            current_texture_bottom_up_ = SampledRowOrderIsBottomUp(&texture);
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

        float dx = (float)destinationRectangle.X;
        float dy = (float)destinationRectangle.Y;
        float dw = (float)destinationRectangle.Width;
        float dh = (float)destinationRectangle.Height;

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

    EasyGLRenderer::EasyGLRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode mode, bool contextRecoveryEnabled,
                                                  int multiSampleCount, int swapInterval)
        : window(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
        , contextRecoveryEnabled_(contextRecoveryEnabled)
        , sampleCount_(multiSampleCount > 1 ? multiSampleCount : 1)
        , swapInterval_(swapInterval)
    {
        if (!window) throw std::runtime_error("EasyGLRenderer initialized with null window.");

#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 has no multisample renderbuffers and no blit to resolve them
        // (glRenderbufferStorageMultisample/glBlitFramebuffer are ES 3.0), so the requested
        // backbuffer MultiSampleCount preference degrades to single-sample -- the profile's real
        // ceiling. GetMultiSampleCount() then reports 0, keeping the applied count truthful.
        sampleCount_ = 1;
#endif

        // NOTE: SDL_Window is NOT owned by EasyGL renderer.
        // It is owned by GraphicsDevice or higher level platform layer.

        // plan_glbackends.md GLB-8: context attributes depend on which of the 5 public GL
        // profiles this translation unit was compiled for (see cmake/RendererSelection.cmake).
        // OPENGLES3/WEBGL2 request GLES 3.0 (today's original, unchanged behavior); WEBGL1
        // requests GLES 2.0 (Emscripten maps this to a real WebGL 1 context); OPENGLES2 requests
        // the same GLES 2.0 attributes through the NATIVE (EGL/GLX) path -- the driver may
        // legally return any ES context backward-compatible with 2.0, which is the same
        // version-floor semantic every other profile's request already has; OPENGL33 requests a
        // desktop GL 3.3 core profile context instead of an ES profile.
#if defined(CNA_GL_PROFILE_OPENGL33)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#elif defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else // CNA_GL_PROFILE_OPENGLES3 or CNA_GL_PROFILE_WEBGL2
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
        // Without this, no window ever gets stencil bits (SDL defaults to 0), making
        // DepthStencilState.StencilEnable a permanent no-op regardless of what's requested.
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        // NOTE: GL context IS owned by EasyGL renderer.
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
        {
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
        }

        device.initialize(reinterpret_cast<::easygl::GLGetProcAddressFn>(SDL_GL_GetProcAddress));
#if defined(CNA_GL_PROFILE_OPENGL33)
        EnableVertexProgramPointSize();
#endif
        std::cout << "EasyGLRenderer initialized with OpenGL "
            << device.capabilities().context_info().version_string << std::endl;

        // Task 456: one-time startup capability dump. Task 918 wired up real
        // GL_EXT_texture_filter_anisotropic support in ApplySamplerState(); report the real,
        // runtime-detected status here instead of a hardcoded claim.
        {
#if defined(CNA_GL_PROFILE_OPENGLES2)
            // GLES 2.0 defines none of GL_MAX_SAMPLES / GL_MAX_DRAW_BUFFERS /
            // GL_MAX_COLOR_ATTACHMENTS (all ES 3.0) -- querying them on a strict ES 2.0 context
            // raises GL_INVALID_ENUM, and a driver that generously returned a
            // backward-compatible higher-version context would report ES 3.0 numbers this
            // profile must not act on. Pin the ES 2.0 truth instead: one sample, one color
            // attachment, no indexed color masks -- regardless of what the runtime context
            // could additionally do.
            GLint maxSamplesCap = 1;
            const GLint maxDrawBuffers = 1;
            const GLint maxColorAttachments = 1;
            maxMrtTargets_ = 1;
            supportsIndexedColorMasks_ = false;
#else
            GLint maxSamplesCap = 0;
            GLint maxDrawBuffers = 1;
            GLint maxColorAttachments = 1;
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
            supportsIndexedColorMasks_ =
                capabilities.is_opengles()
                    ? capabilities.is_at_least(3, 2)
                    : capabilities.is_at_least(3, 0);
#endif
            const bool hasAniso = metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            GLfloat maxAnisoCap = 1.0f;
            if (hasAniso)
                metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &maxAnisoCap);
            std::cout << "CNA: EasyGL capabilities -- MSAA up to " << maxSamplesCap
                      << "x; MRT up to " << maxMrtTargets_
                      << " targets (GL draw buffers=" << maxDrawBuffers
                      << ", color attachments=" << maxColorAttachments
                      << ", CNA/FNA cap=4); indexed color masks: "
                      << (supportsIndexedColorMasks_ ? "supported" : "not supported")
                      << "; "
                         "anisotropic filtering: "
                      << (hasAniso ? ("supported (Task 918, up to " + std::to_string(static_cast<int>(maxAnisoCap)) + "x)")
                                   : std::string("NOT supported (falls back to trilinear)"))
                      << "; SurfaceFormat: Color only (Task 176)" << std::endl;
        }

        SDL_GL_SetSwapInterval(swapInterval_);

        registry_.register_with_meta_gl();

        if (sampleCount_ > 1)
        {
            int physW, physH;
            SDL_GetWindowSize(window, &physW, &physH);
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
        IGraphicsRenderer::RegisterForWindow(window, this);
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
            SDL_GetWindowSize(window, &physW, &physH);
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
        if (window) IGraphicsRenderer::UnregisterForWindow(window);
        if (gl_context) SDL_GL_DestroyContext(gl_context);
        // window is NOT owned by the renderer.
        // No SDL_Quit or subsystem shutdown here - managed centrally.
    }

    bool EasyGLRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            {
#if defined(CNA_GL_PROFILE_OPENGLES2)
                // GLES 2.0 has no multisample renderbuffers/blit (both ES 3.0), and GL_MAX_SAMPLES
                // itself is undefined there -- reported false regardless of what a generously
                // higher-versioned runtime context could do, matching the profile's forced
                // single-sample surfaces.
                return false;
#else
                GLint maxSamplesCap = 0;
                metagl::glGetIntegerv(::metagl::GetParameter::MaxSamples, &maxSamplesCap);
                return maxSamplesCap > 1;
#endif
            }
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return metagl::HasExtension("GL_EXT_texture_filter_anisotropic");
            case CNA::GraphicsCapability::WireFrame:
                // REMED-GFX-219 resolved: this renderer's GL_LINES re-expansion renders a genuinely
                // correct wireframe (shared pixel oracle: interior 0/1089, all three triangle edges
                // present), so the previous `false` under-stated the implementation. The emulation
                // draws line primitives and depends on no polygon-mode API, so it holds for every
                // GL profile (OPENGLES3/OPENGL33/WEBGL1/WEBGL2) alike.
#if defined(CNA_GL_PROFILE_OPENGLES2)
                // ...with one ES 2.0 nuance: the re-expanded line indices are 32-bit, and
                // GL_UNSIGNED_INT element indices are an extension there (core in ES 3.0), so the
                // report is conditional on the runtime genuinely providing it.
                return metagl::HasExtension("GL_OES_element_index_uint");
#else
                return true;
#endif
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: implemented -- Draw*PrimitivesEx binds every per-vertex stream
                // into the VAO at locations continuing after the previous stream's, each with its
                // own VBO, stride and byte offset, and restores the single-stream layout after.
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // WebGL 1 / GLES 2.0 lack the attrib-divisor entry points the Ex routes bind
                // through; claiming support would fail inside GL instead of being refused up front.
                return false;
#else
                return true;
#endif
            case CNA::GraphicsCapability::MultipleRenderTargets:
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // WebGL 1 / GLES 2.0 core have no draw-buffers MRT.
                return false;
#else
                return true;
#endif
            case CNA::GraphicsCapability::OcclusionQuery:
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // WebGL 1 / GLES 2.0 have no query objects.
                return false;
#else
                return true;
#endif
            case CNA::GraphicsCapability::Texture3D:
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // WebGL 1 / GLES 2.0 have no 3D textures at all.
                return false;
#else
                return true;
#endif
            case CNA::GraphicsCapability::Instancing:
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // WebGL 1 / GLES 2.0 core have no glDrawElementsInstanced/glVertexAttribDivisor.
                return false;
#else
                return true;
#endif
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

#if defined(CNA_GL_PROFILE_OPENGLES2)
        // Textures outside the recovery registry (e.g. plain cube maps) leave stale entries in
        // the ES 2.0 level registry on context loss, and the fresh context may re-issue their GL
        // names -- start the registry empty; recreate_gl_resource() below re-registers every
        // recoverable texture with its fresh name.
        Es2TextureLevelCounts().clear();
#endif

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
        prog_pbr_skinned_.reset_no_gl();
        default_white_texture_.reset_handle_no_gl();
        default_white_texture_ready_ = false;
        default_flat_normal_texture_.reset_handle_no_gl();
        default_flat_normal_texture_ready_ = false;

        // 2. Destroy and recreate the SDL GL context.
        if (gl_context)
        {
            SDL_GL_MakeCurrent(window, nullptr);
            SDL_GL_DestroyContext(gl_context);
            gl_context = nullptr;
        }
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
            throw std::runtime_error(std::string("SDL_GL_CreateContext failed during debug context loss: ") + SDL_GetError());
        SDL_GL_MakeCurrent(window, gl_context);

        // 3. Reload GL function pointers and increment context generation. `easygl::Device` is
        // already initialized and its initialize() method is deliberately one-shot, so calling
        // it here returns without touching meta-gl. Context loss invalidated meta-gl's function
        // table above; the next GL wrapper would consequently call std::terminate(). Reload the
        // context-facing table directly while retaining Device's context-independent facade.
        if (!metagl::LoadCurrentContext(
                reinterpret_cast<metagl::GlGetProcAddressFn>(SDL_GL_GetProcAddress)))
        {
            throw std::runtime_error(
                "meta-gl failed to reload GL entry points after debug context loss");
        }
#if defined(CNA_GL_PROFILE_OPENGL33)
        EnableVertexProgramPointSize();
#endif

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
#if !defined(CNA_GL_PROFILE_OPENGLES2)
            // GLES 2.0 has no glReadBuffer at all -- there the default framebuffer's color buffer
            // is the one and only read source, so the explicit GL_BACK selection this comment
            // block describes for EGL/GLES3 contexts neither exists nor is needed.
            device.set_read_buffer(::easygl::ReadBuffer::Back);
#endif
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
        SDL_GL_SwapWindow(window);
        if (sampleCount_ > 1)
            msaaFbo_.bind(::easygl::FramebufferTarget::Framebuffer);
    }

    void EasyGLRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void EasyGLRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void EasyGLRenderer::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        SDL_GL_SetSwapInterval(interval);
    }

    void EasyGLRenderer::getLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            SDL_GetWindowSize(window, &width, &height);
            return;
        }
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>((double)physW * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void EasyGLRenderer::getPhysicalSize(int& width, int& height) const
    {
        SDL_GetWindowSize(window, &width, &height);
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
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool EasyGLRenderer::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical: logical = window * (virtualHeight_ / physH), so
        // window = logical * (physH / virtualHeight_). This is a pure uniform scale with NO offset,
        // which is exact for EasyGL's default FixedHeightDynamicWidth presentation: the logical
        // height is fixed and the logical *width* is derived from the window aspect
        // (getLogicalSize), so the logical viewport fills the whole window — there are no letterbox
        // bars and hence no offset to apply (unlike the SDL_Renderer renderer's true-letterbox
        // modes, whose offset is handled by SDL_RenderCoordinates{From,To}Window). EasyGL does not
        // implement per-mode offset transforms for its non-default modes; that is a pre-existing
        // graphics-presentation concern, not an input-layer one, and the default mode is exact.
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    void EasyGLRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
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

    void EasyGLRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        if (metagl::IsContextLost()) return;
        device.set_blend_color(r, g, b, a);
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
    }

    void EasyGLRenderer::ApplySamplerState(int slot, int filter,
                                                   int addressU, int addressV,
                                                   int maxAnisotropy)
    {
        if (metagl::IsContextLost()) return;
        if (slot < 0 || slot >= kMaxSamplerSlots) return;

#if defined(CNA_GL_PROFILE_OPENGLES2)
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
#else
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
#endif // !CNA_GL_PROFILE_OPENGLES2
    }

    // -------------------------------------------------------------------------
    // 3D pipeline
    // -------------------------------------------------------------------------

    void EasyGLVertexBufferRenderer::InitializeLayout()
    {
        vbo.create();
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
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
                // plan_glbackends.md GLB-36 follow-up: GLSL ES 1.00 (WEBGL1 and OPENGLES2) has no
                // integer vertex attributes at all -- glVertexAttribIPointer isn't available.
                // Byte4 is used exclusively for BLENDINDICES-style bone-index attributes
                // (confirmed: only VertexPositionNormalTangentTextureSkinned/
                // VertexPositionNormalTextureSkinned declare it), so read the same underlying
                // bytes as floats instead (0-255 range, far more than the <=72 bone count needs,
                // exactly float-representable) rather than as a true integer. The skinned shaders
                // themselves declare "attribute vec4 aBoneIndices;" (not uvec4) and cast to int()
                // when indexing uBones[] for these profiles -- see TransformGlslEs300BodyToEs100's
                // bone-index rewrite.
                return { 4, ::easygl::DataType::UnsignedByte, false, false };
#else
                return { 4, ::easygl::DataType::UnsignedByte, false, true  };
#endif
            case VertexElementFormat::Short2:          return { 2, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::Short4:          return { 4, ::easygl::DataType::Short,        false, false };
            case VertexElementFormat::NormalizedShort2:return { 2, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::NormalizedShort4:return { 4, ::easygl::DataType::Short,        true,  false };
            case VertexElementFormat::HalfVector2:     return { 2, ::easygl::DataType::HalfFloat,    false, false };
            case VertexElementFormat::HalfVector4:     return { 4, ::easygl::DataType::HalfFloat,    false, false };
            }
            return { 3, ::easygl::DataType::Float, false, false };
        }

        /// Binds a BLENDINDICES-style Byte4 bone-index attribute in the read mode the active
        /// profile's skinned shaders expect: true integers (glVertexAttribIPointer) for the
        /// GLSL ES 3.00 / desktop profiles, plain float reads for the GLSL ES 1.00 profiles --
        /// whose transformed shaders declare "attribute vec4 aBoneIndices;", so an integer
        /// pointer would feed a float attribute there. This is the same per-profile read-mode
        /// choice DescribeVertexElementFormat's Byte4 case makes for declaration-driven layouts,
        /// applied to ApplyLayout's fixed-stride skinned layouts (52/56/68) as well.
        void SetBoneIndicesAttributePointer(::easygl::VertexArray& vao, unsigned int location,
                                            std::size_t stride, const void* offset)
        {
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
            vao.set_attribute_pointer(location, 4, ::easygl::DataType::UnsignedByte, false, stride, offset);
#else
            vao.set_attribute_i_pointer(location, 4, ::easygl::DataType::UnsignedByte, stride, offset);
#endif
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
            // plan_cnj.md CNB-57 (Phase 13A): VertexPositionNormalTangentTexture (packed):
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
            // established stride-48 prefix, UV1 is appended at 48 and bytes 56..59 are padding.
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
        default:
            // plan_gltf.md GLTF-157: a byte stride does not describe which attributes exist.
            // Treating every unknown record as position-only left the other locations in stale
            // VAO state and rendered normals, UVs or skin weights from unrelated buffers. Refuse
            // it loudly; a genuinely custom layout reaches the generic declaration path above.
            vao.unbind();
            throw System::NotSupportedException(
                "EasyGLRenderer::ApplyLayout: unsupported vertex stride " +
                std::to_string(stride) +
                " without a VertexDeclaration; the upload is refused rather than bound as "
                "position-only.");
        }
        vao.unbind();
    }

    EasyGLVertexBufferRenderer::EasyGLVertexBufferRenderer(int vertex_capacity, ::easygl::ResourceRegistry* registry)
        : capacity(vertex_capacity)
        , registry_(registry)
    {
        InitializeLayout();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("VertexBuffer created: capacity=" << capacity);
    }

    EasyGLVertexBufferRenderer::~EasyGLVertexBufferRenderer()
    {
        if (registry_) registry_->remove(this);
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
        if (registry_)
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
        if (registry_)
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
                                                       ::easygl::ResourceRegistry* registry)
        : thirtyTwoBit(is32bit)
        , capacity(index_capacity)
        , registry_(registry)
    {
        ibo.create();
        if (registry_) registry_->add(this);
        CNA_RENDER_LOG("IndexBuffer created: capacity=" << capacity << " 32bit=" << is32bit);
    }

    EasyGLIndexBufferRenderer::~EasyGLIndexBufferRenderer()
    {
        if (registry_) registry_->remove(this);
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
        if (registry_)
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
        if (registry_)
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
        if (registry_)
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
        if (registry_)
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
        void CompileAndLink(::easygl::Program& prog, const char* vsrc, const char* fsrc,
                            const char* label)
        {
            const std::string adaptedVsrc = AdaptGlslEs300ForActiveProfile(vsrc, GlShaderStageKind::Vertex);
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
#if defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
            // plan_glbackends.md GLB-36: these profiles' GLSL ES 1.00 shader text has no
            // layout(location=N) (GLSL ES 1.00 doesn't support it) -- rebind the SAME numeric
            // locations here, extracted from the ORIGINAL ES 3.00 source, so every
            // VertexArray/VAO attribute-binding call site elsewhere in this file (all hardcoded
            // numeric indices) keeps working unchanged.
            for (const auto& [location, name] : ExtractVertexAttribLocations(vsrc))
            {
                prog.bind_attrib_location(static_cast<unsigned int>(location), name);
            }
#endif
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
CNA_GL_INSTANCE_TRANSFORM_DECL
"uniform mat4 uWVP;\n"
"uniform mat4 uWorld;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform vec4 uFogVector;\n"
"out vec3 vNormal;\n"
"out vec2 vUV;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
"void main(){\n"
"    vec4 cnaPos=cnaInstancePosition(vec4(aPos,1.0));\n"
"    gl_Position=uWVP*cnaPos;\n"
"    vNormal=uNormalMatrix*cnaInstanceDirection(aNormal);\n"
"    vUV=aUV;\n"
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
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec2 vUV;\n"
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
"uniform vec3 uFogColor;\n"
"out vec4 FragColor;\n"
CNA_GL_RT_SAMPLE_UV_DECL
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
"        vec3 lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
"        litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"        vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"        vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"        vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"        specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"    }\n"
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vec4(litRGB,uDiffuseColor.a);\n"
"    FragColor.rgb+=specularRGB*FragColor.a;\n"
"    float _at=(uAlphaTest.y>0.0)?((abs(FragColor.a-uAlphaTest.x)<uAlphaTest.y)?uAlphaTest.z:uAlphaTest.w):((FragColor.a<uAlphaTest.x)?uAlphaTest.z:uAlphaTest.w);\n"
"    if(_at<0.0)discard;\n"
"    FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);\n"
"}\n";

        CompileAndLink(prog_lit_textured_.prog, vsrc, fsrc, "lit+textured");
        ResolveRenderTargetOrientationUniforms(prog_lit_textured_);
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
        prog_lit_textured_.ready           = true;
        CNA_RENDER_LOG("lit+textured3D ready loc_wvp=" << prog_lit_textured_.loc_wvp);
    }

    // Task 1102 (plan_graphics.md Phase 80 / plan_dx9.md Divergence 1): real XNA's
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
"out vec2 vUV;\n"
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
"    vLitRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"}\n";
        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec2 vUV;\n"
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
"    FragColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x))*vec4(vLitRGB,uDiffuseColor.a);\n"
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
"    vFresnel=(uFresnelEnabled>0.5)\n"
"        ? pow(max(1.0-abs(viewAngle),0.0),uFresnelFactor)*uEnvMapAmount\n"
"        : uEnvMapAmount;\n"
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
"layout(location=4) in uvec4 aBoneIndices;\n"
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
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
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
"precision mediump float;\n"
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
"void main(){\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 E=normalize(uEyePosition-vWorldPos);\n"
"    float dotL0=dot(N,-uLight0Dir); float zeroL0=step(0.0,dotL0); float NdotL0=max(dotL0,0.0);\n"
"    float dotL1=dot(N,-uLight1Dir); float zeroL1=step(0.0,dotL1); float NdotL1=max(dotL1,0.0);\n"
"    float dotL2=dot(N,-uLight2Dir); float zeroL2=step(0.0,dotL2); float NdotL2=max(dotL2,0.0);\n"
"    vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;\n"
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
"    vec3 specularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
"    vec4 texColor=texture(uTexture,cnaSampleUV(vUV,uRtFlipV.x));\n"
"    vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,1.0,1.0,1.0);\n"
"    FragColor=vec4(litRGB*texColor.rgb,uDiffuseColor.a*texColor.a*vc.a);\n"
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

    // Task 1102b (plan_graphics.md Phase 80 / plan_dx9.md Divergence 1): SkinnedEffect's own
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
"layout(location=4) in uvec4 aBoneIndices;\n"
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
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
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
"    vLitRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;\n"
"    vec3 h0=normalize(E-uLight0Dir); float spec0=pow(max(dot(h0,N),0.0)*zeroL0,uSpecularPower);\n"
"    vec3 h1=normalize(E-uLight1Dir); float spec1=pow(max(dot(h1,N),0.0)*zeroL1,uSpecularPower);\n"
"    vec3 h2=normalize(E-uLight2Dir); float spec2=pow(max(dot(h2,N),0.0)*zeroL2,uSpecularPower);\n"
"    vSpecularRGB=(spec0*uLight0Specular+spec1*uLight1Specular+spec2*uLight2Specular)*uSpecularColor;\n"
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

    // plan_cnj.md CNB-58 (Phase 13A): real glTF metallic-roughness BRDF (glTF 2.0 spec Appendix
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
    void EasyGLRenderer::EnsurePbrProgram()
    {
        if (prog_pbr_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
"layout(location=4) in vec2 aUV1;\n"
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
"out vec2 vUV1;\n"
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
"    vUV1=aUV1;\n"
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
"in vec2 vUV1;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
// plan_gltf.md GLTF-343/344: factor-only KHR_materials_ior/specular state, already reduced by
// FillGpuDrawParams to the exact shader-ready Fresnel endpoints. xyz is dielectric F0; w is F90.
"uniform vec4 uDielectricFresnel;\n"
// plan_gltf.md GLTF-210/GLTF-212: x = decode the base-colour sample from sRGB, y = decode the
// emissive sample, z = encode the fragment's RGB back. Each is 0 or 1 and drives a mix() rather
// than a branch, so every fragment costs the same whichever way it is set.
"uniform vec3 uSrgb;\n"
// plan_gltf.md GLTF-224/GLTF-225: normalTexture.scale and occlusionTexture.strength. Two scalar
// uniforms rather than one vec2, to stay on the single-float set_uniform overload this file
// already uses everywhere.
"uniform float uNormalScale;\n"
"uniform float uOcclusionStrength;\n"
"uniform vec4 uTextureCoordinateSets;\n"
"uniform float uOcclusionTextureCoordinateSet;\n"
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
"vec2 cnaPbrUV(float setIndex){return mix(vUV,vUV1,setIndex);}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.x),uRtFlipV.x));\n"
// glTF §3.9.2: the base-colour TEXTURE is sRGB-encoded, the base-colour FACTOR is linear. Only
// the sample is decoded -- transferring both would apply it twice to one of them.
"    vec3 baseRGB=mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x);\n"
"    vec3 albedo=baseRGB*uDiffuseColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.y),uRtFlipV.y)).rgb*2.0-1.0;\n"
// glTF §3.9.3: normalTexture.scale scales the tangent-space X and Y only. Scaling Z as well would
// merely rescale the whole vector, which normalization then undoes -- the perturbation would not
// change at all.
"    sampledNormal.xy*=uNormalScale;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.z),uRtFlipV.z));\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    vec3 F0=mix(uDielectricFresnel.xyz,albedo,metallic);\n"
"    vec3 F90=mix(vec3(uDielectricFresnel.w),vec3(1.0),metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    float occlusion=texture(uOcclusionMap,cnaSampleUV(cnaPbrUV(uOcclusionTextureCoordinateSet),uRtFlipVHi.x)).r;\n"
// §3.9.3's own formula: 1 + strength * (sampled - 1). At strength 0 this is 1 whatever the map
// holds, which is what "no occlusion" has to mean -- multiplying by the strength instead would
// darken everything to black.
"    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n"
"    vec3 ambient=uAmbientColor*albedo*occlusion;\n"
"    vec3 emissiveTex=texture(uEmissiveMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.w),uRtFlipV.w)).rgb;\n"
// Same split as the base colour. The factor is additionally allowed above 1 by
// KHR_materials_emissive_strength, which is a second reason never to transfer it.
"    vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y);\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
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

        CompileAndLink(prog_pbr_.prog, vsrc, fsrc, "pbr");
        ResolveRenderTargetOrientationUniforms(prog_pbr_);
        auto& p = prog_pbr_;
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
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_pbr_dielectric_fresnel = p.prog.uniform_location("uDielectricFresnel");
        p.loc_pbr_srgb          = p.prog.uniform_location("uSrgb");
        p.loc_pbr_normalscale   = p.prog.uniform_location("uNormalScale");
        p.loc_pbr_occlstrength  = p.prog.uniform_location("uOcclusionStrength");
        p.loc_pbr_texcoordsets  = p.prog.uniform_location("uTextureCoordinateSets");
        p.loc_pbr_occlusiontexcoordset =
            p.prog.uniform_location("uOcclusionTextureCoordinateSet");
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
    void EasyGLRenderer::EnsurePbrSkinnedProgram()
    {
        if (prog_pbr_skinned_.ready) return;

        static const char* vsrc =
"#version 300 es\n"
"precision highp float;\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNormal;\n"
"layout(location=2) in vec4 aTangent;\n"
"layout(location=3) in vec2 aUV;\n"
"layout(location=4) in vec4 aBoneWeights;\n"
"layout(location=5) in uvec4 aBoneIndices;\n"
"layout(location=6) in vec2 aUV1;\n"
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
"out vec2 vUV1;\n"
"out float vFogFactor;\n"
"out vec3 vWorldPos;\n"
CNA_GL_SKIN_NORMAL_DECL
"void main(){\n"
"    mat4 skinMat=uBones[aBoneIndices.x]*aBoneWeights.x;\n"
"    if(uWeightsPerVertex>=2) skinMat+=uBones[aBoneIndices.y]*aBoneWeights.y;\n"
"    if(uWeightsPerVertex>=4) skinMat+=uBones[aBoneIndices.z]*aBoneWeights.z+uBones[aBoneIndices.w]*aBoneWeights.w;\n"
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
"    vUV1=aUV1;\n"
"    vWorldPos=(uWorld*cnaPos).xyz;\n"
"    vFogFactor=1.0-clamp(dot(cnaPos,uFogVector),0.0,1.0);\n"
"}\n";

        static const char* fsrc =
"#version 300 es\n"
"precision mediump float;\n"
"in vec3 vNormal;\n"
"in vec3 vTangent;\n"
"in float vBitangentSign;\n"
"in vec2 vUV;\n"
"in vec2 vUV1;\n"
"in float vFogFactor;\n"
"in vec3 vWorldPos;\n"
"uniform sampler2D uTexture;\n"
"uniform sampler2D uNormalMap;\n"
"uniform sampler2D uMetallicRoughnessMap;\n"
"uniform sampler2D uEmissiveMap;\n"
"uniform sampler2D uOcclusionMap;\n"
"uniform vec4 uDiffuseColor;\n"
"uniform vec3 uAmbientColor;\n"
"uniform vec3 uEmissiveColor;\n"
"uniform float uMetallicFactor;\n"
"uniform float uRoughnessFactor;\n"
// plan_gltf.md GLTF-343/344: same shader-ready dielectric Fresnel endpoints as unskinned PBR.
"uniform vec4 uDielectricFresnel;\n"
// plan_gltf.md GLTF-210/GLTF-212: x = decode the base-colour sample from sRGB, y = decode the
// emissive sample, z = encode the fragment's RGB back. Each is 0 or 1 and drives a mix() rather
// than a branch, so every fragment costs the same whichever way it is set.
"uniform vec3 uSrgb;\n"
// plan_gltf.md GLTF-224/GLTF-225: normalTexture.scale and occlusionTexture.strength. Two scalar
// uniforms rather than one vec2, to stay on the single-float set_uniform overload this file
// already uses everywhere.
"uniform float uNormalScale;\n"
"uniform float uOcclusionStrength;\n"
"uniform vec4 uTextureCoordinateSets;\n"
"uniform float uOcclusionTextureCoordinateSet;\n"
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
"vec2 cnaPbrUV(float setIndex){return mix(vUV,vUV1,setIndex);}\n"
"void main(){\n"
"    vec4 baseColorTex=texture(uTexture,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.x),uRtFlipV.x));\n"
// glTF §3.9.2: the base-colour TEXTURE is sRGB-encoded, the base-colour FACTOR is linear. Only
// the sample is decoded -- transferring both would apply it twice to one of them.
"    vec3 baseRGB=mix(baseColorTex.rgb,cnaSrgbToLinear(baseColorTex.rgb),uSrgb.x);\n"
"    vec3 albedo=baseRGB*uDiffuseColor.rgb;\n"
"    float alpha=baseColorTex.a*uDiffuseColor.a;\n"
"    vec3 N=normalize(vNormal);\n"
"    vec3 T=normalize(vTangent-N*dot(N,vTangent));\n"
"    vec3 B=cross(N,T)*vBitangentSign;\n"
"    mat3 TBN=mat3(T,B,N);\n"
"    vec3 sampledNormal=texture(uNormalMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.y),uRtFlipV.y)).rgb*2.0-1.0;\n"
// glTF §3.9.3: normalTexture.scale scales the tangent-space X and Y only. Scaling Z as well would
// merely rescale the whole vector, which normalization then undoes -- the perturbation would not
// change at all.
"    sampledNormal.xy*=uNormalScale;\n"
"    vec3 finalNormal=normalize(TBN*sampledNormal);\n"
"    vec4 mr=texture(uMetallicRoughnessMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.z),uRtFlipV.z));\n"
"    float roughness=clamp(mr.g*uRoughnessFactor,0.045,1.0);\n"
"    float metallic=clamp(mr.b*uMetallicFactor,0.0,1.0);\n"
"    vec3 V=normalize(uEyePosition-vWorldPos);\n"
"    vec3 F0=mix(uDielectricFresnel.xyz,albedo,metallic);\n"
"    vec3 F90=mix(vec3(uDielectricFresnel.w),vec3(1.0),metallic);\n"
"    vec3 Lo=vec3(0.0);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight0Dir),uLight0Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight1Dir),uLight1Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    Lo+=PbrLight(finalNormal,V,normalize(-uLight2Dir),uLight2Diffuse,albedo,F0,F90,roughness,metallic);\n"
"    float occlusion=texture(uOcclusionMap,cnaSampleUV(cnaPbrUV(uOcclusionTextureCoordinateSet),uRtFlipVHi.x)).r;\n"
// §3.9.3's own formula: 1 + strength * (sampled - 1). At strength 0 this is 1 whatever the map
// holds, which is what "no occlusion" has to mean -- multiplying by the strength instead would
// darken everything to black.
"    occlusion=1.0+uOcclusionStrength*(occlusion-1.0);\n"
"    vec3 ambient=uAmbientColor*albedo*occlusion;\n"
"    vec3 emissiveTex=texture(uEmissiveMap,cnaSampleUV(cnaPbrUV(uTextureCoordinateSets.w),uRtFlipV.w)).rgb;\n"
// Same split as the base colour. The factor is additionally allowed above 1 by
// KHR_materials_emissive_strength, which is a second reason never to transfer it.
"    vec3 emissive=uEmissiveColor*mix(emissiveTex,cnaSrgbToLinear(emissiveTex),uSrgb.y);\n"
"    FragColor=vec4(ambient+Lo+emissive,alpha);\n"
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

        CompileAndLink(prog_pbr_skinned_.prog, vsrc, fsrc, "pbr_skinned");
        ResolveRenderTargetOrientationUniforms(prog_pbr_skinned_);
        auto& p = prog_pbr_skinned_;
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
        p.loc_pbr_metallic      = p.prog.uniform_location("uMetallicFactor");
        p.loc_pbr_roughness     = p.prog.uniform_location("uRoughnessFactor");
        p.loc_pbr_dielectric_fresnel = p.prog.uniform_location("uDielectricFresnel");
        p.loc_pbr_srgb          = p.prog.uniform_location("uSrgb");
        p.loc_pbr_normalscale   = p.prog.uniform_location("uNormalScale");
        p.loc_pbr_occlstrength  = p.prog.uniform_location("uOcclusionStrength");
        p.loc_pbr_texcoordsets  = p.prog.uniform_location("uTextureCoordinateSets");
        p.loc_pbr_occlusiontexcoordset =
            p.prog.uniform_location("uOcclusionTextureCoordinateSet");
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

    // plan_cnj.md CNB-58 (Phase 13A): fallback for PbrEffect::NormalMap when unbound -- a "flat"
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

    // REMED-GFX-218 / REMED-GFX-DECL-GUARD: the ONE place that decides which stock program a draw
    // gets. SelectProgram() below and StockProgramInputsEXT() both read this, so the program a
    // draw is bound to and the input shape it is checked against can never drift apart.
    EasyGLRenderer::StockProgramShape EasyGLRenderer::SelectStockProgramShape(
        std::size_t stride, const GpuDrawParams& params)
    {
        if (params.pbr && params.skinned) return StockProgramShape::PbrSkinned;
        if (params.pbr) return StockProgramShape::Pbr;
        if (params.skinned)
        {
            // Task 1102b (plan_dx9.md Divergence 1): real XNA's SkinnedEffect defaults
            // PreferPerPixelLighting=false too, same as BasicEffect (Task 1102). Only
            // meaningfully distinct while lighting is actually on, same reasoning as Task 1102's
            // own stride-32 gate.
            return (params.lightingEnabled && !params.preferPerPixelLighting)
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
        switch (stride)
        {
        case 20: return StockProgramShape::Textured;
        case 24: return StockProgramShape::ColoredTextured;
        case 32:
            // Task 1102 (plan_dx9.md Divergence 1): real XNA's BasicEffect defaults
            // PreferPerPixelLighting=false (per-vertex/Gouraud-shaded lighting), the opposite of
            // what this renderer rendered unconditionally before this task. Only meaningfully
            // distinct while lighting is actually on -- with lighting disabled, both programs
            // degenerate to the identical trivial ambient=(1,1,1) case (see BindDrawParams()'s
            // own else-branch), so the existing pixel-lit program stays selected there to avoid
            // an unnecessary program switch.
            return (params.lightingEnabled && !params.preferPerPixelLighting)
                       ? StockProgramShape::LitVertexLit
                       : StockProgramShape::Lit;
        default: return StockProgramShape::Colored;
        }
    }

    EasyGLRenderer::Prog3D& EasyGLRenderer::SelectProgram(std::size_t stride,
                                                                          const GpuDrawParams& params)
    {
        switch (SelectStockProgramShape(stride, params))
        {
        case StockProgramShape::PbrSkinned:
            EnsurePbrSkinnedProgram();          return prog_pbr_skinned_;
        case StockProgramShape::Pbr:
            EnsurePbrProgram();                 return prog_pbr_;
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
        case StockProgramShape::LitVertexLit:
            EnsureLit3DVertexLitProgram();      return prog_lit_textured_vertexlit_;
        case StockProgramShape::Lit:
            EnsureLit3DProgram();               return prog_lit_textured_;
        case StockProgramShape::Colored:
            break;
        }
        EnsureColored3DProgram();
        return prog_colored_;
    }

    // REMED-GFX-218 / REMED-GFX-DECL-GUARD: what each stock program declares, in attribute-location
    // order, transcribed from the `layout(location=N) in ...` lines of the very shaders the
    // Ensure*Program() calls above compile. EasyGL binds each declaration element at the location
    // matching its INDEX (ApplyLayout), and the same location means different things in different
    // programs -- location 1 is aColor at stride 16, aUV at 20 and aNormal at 32 -- so there is no
    // global semantic-to-location function and this ordered list is the only truthful comparison.
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
        static constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Byte4, "aBoneIndices"};

        static constexpr StockProgramInput kColored[]        = {kPos, kColor};
        static constexpr StockProgramInput kTextured[]       = {kPos, kUv};
        static constexpr StockProgramInput kColTextured[]    = {kPos, kColor, kUv};
        static constexpr StockProgramInput kLit[]            = {kPos, kNormal, kUv};
        static constexpr StockProgramInput kSkinned[]        = {kPos, kNormal, kUv, kWeights,
                                                                kIndices, kColor};
        static constexpr StockProgramInput kPbr[]            = {kPos, kNormal, kTangent, kUv};
        static constexpr StockProgramInput kPbrDualUv[]      = {kPos, kNormal, kTangent, kUv,
                                                                kUv1};
        static constexpr StockProgramInput kPbrSkinned[]     = {kPos, kNormal, kTangent, kUv,
                                                                kWeights, kIndices};
        static constexpr StockProgramInput kPbrSkinnedDualUv[] = {
            kPos, kNormal, kTangent, kUv, kWeights, kIndices, kUv1};

        const StockProgramInput* inputs = kColored;
        std::size_t count = std::size(kColored);
        const char* name = "colored3d";
        switch (SelectStockProgramShape(stride, params))
        {
        case StockProgramShape::PbrSkinned:
            if (stride == 76)
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
        case StockProgramShape::LitVertexLit:
            inputs = kLit; count = std::size(kLit); name = "lit_textured3d_vertexlit"; break;
        case StockProgramShape::Lit:
            inputs = kLit; count = std::size(kLit); name = "lit_textured3d"; break;
        case StockProgramShape::Colored:
            break;
        }
        CNA::Internal::Graphics::RequireDeclarationMatchesStockProgram(
            declaredElements, inputs, count, "EasyGL", name);
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

    void EasyGLRenderer::BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                                               const Matrix& projection, const GpuDrawParams& params)
    {
        // REMED-GFX-147: one flag per texture unit, filled in beside each unit's own bind below so
        // the flag and the resource it describes can never disagree, and uploaded once at the end.
        // A render target's GL texel memory is bottom-up; an uploaded texture's is not. Cube maps
        // are deliberately absent -- REMED-GFX-137 owns rendered cube faces, and uEnvMap is sampled
        // with a direction vector, not a UV.
        float rtFlipV[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        // WVP
        const Matrix wvp = world * view * projection;
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

        // plan_cnj.md CNB-58 (Phase 13A): PbrEffect's 4 additional maps (units 1-4, bound before
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
        // plan_gltf.md GLTF-210/GLTF-212. Three independent decisions, so three independent
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
        if (p.loc_pbr_srgb >= 0)
        {
            p.prog.set_uniform(p.loc_pbr_srgb,
                params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f,
                params.pbrEmissiveTextureIsSrgb  ? 1.0f : 0.0f,
                params.pbrEncodeOutputToSrgb     ? 1.0f : 0.0f);
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

#if defined(CNA_GL_PROFILE_OPENGLES2)
        // ES 2.0 keeps sampling state on the texture objects -- re-apply each unit's recorded
        // SamplerState onto whatever this draw just bound above (GraphicsDevice applies sampler
        // state BEFORE the draw binds its textures on this route, so the bind is what must pull
        // the state in). Units 0-4 are the stock effects' complete sampling range.
        for (int unit = 0; unit < 5; ++unit)
            Es2ApplyPendingSamplerToUnit(unit);
#endif

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
            p.prog.set_uniform(p.loc_rt_flip_v_hi, rtFlipV[4], 0.0f, 0.0f, 0.0f);
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
    }

    void EasyGLRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_clear_stencil(stencil);
        device.set_depth_mask(true);
        device.set_stencil_mask(0xFFFFFFFFu);
        device.clear(::easygl::ClearFlags::Depth | ::easygl::ClearFlags::Stencil);
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
        if (maskActive) ApplyCurrentColorWriteMasks();
    }

    void EasyGLRenderer::ClearDepth(float depth)
    {
        if (metagl::IsContextLost()) return;
        device.set_clear_depth(depth);
        device.set_depth_mask(true);
        device.clear(::easygl::ClearFlags::Depth);
    }

    void EasyGLRenderer::SetDepthTestEnabled(bool enabled)
    {
        device.set_depth_test_enabled(enabled);
        if (enabled)
        {
            device.set_depth_func(::easygl::CompareFunc::Lequal);
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
        vb.vao.bind();
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
            // GLES 2.0 has no glDrawElementsBaseVertex (ES 3.2) -- shift every enabled attribute
            // pointer by baseVertex elements instead, draw, and restore (see the helper's doc).
            Es2ShiftEnabledVertexAttribPointers(baseVertex, +1);
            device.draw_elements(::easygl::PrimitiveType::Lines, lineIndexCount,
                                 ::easygl::DataType::UnsignedInt, nullptr);
            Es2ShiftEnabledVertexAttribPointers(baseVertex, -1);
#else
            ::metagl::glDrawElementsBaseVertex(::easygl::PrimitiveType::Lines, lineIndexCount,
                                               ::easygl::DataType::UnsignedInt, nullptr, baseVertex);
#endif
        }
        vb.vao.unbind();
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

        vb.vao.bind();
        device.draw_arrays(ToEasyGl(primitive), 0, vertex_count);
        vb.vao.unbind();
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

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                              : ::easygl::DataType::UnsignedShort;
        device.draw_elements(ToEasyGl(primitive), index_count, idxType, nullptr);
        vb.vao.unbind();
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
        if (multiStream)
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
            vb.vao.bind();
            device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
            vb.vao.unbind();
            if (multiStream) { vao.bind(); RestoreSingleStreamAttributes(vao, params); vao.unbind(); }
            return;
        }

        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        Prog3D& p = SelectProgram(layoutStride, params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int vertex_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawPrimitivesEx: stride=" << layoutStride
            << " prim=" << static_cast<int>(primitive) << " verts=" << vertex_count);

        if (!multiStream && wireframe_ &&
            DrawWireframe(vb, nullptr, primitive, primitiveCount, 0, 0, params.vertexStart))
            return;

        vb.vao.bind();
        TraceBoundTextureUnit("draw-arrays-3d", 0);
        device.draw_arrays(ToEasyGl(primitive), params.vertexStart, vertex_count);
        if (multiStream) RestoreSingleStreamAttributes(vao, params);
        vb.vao.unbind();
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
        if (multiStream)
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
            vb.vao.bind();
            ib.ibo.bind(::easygl::BufferTarget::ElementArray);
            const auto idxTypeCustom = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                                        : ::easygl::DataType::UnsignedShort;
            const int indexSizeCustom = ib.thirtyTwoBit ? 4 : 2;
            const void* indexOffsetCustom = reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSizeCustom));
            if (params.baseVertex == 0) {
                device.draw_elements(ToEasyGl(primitive), index_count, idxTypeCustom, indexOffsetCustom);
            } else {
#if defined(CNA_GL_PROFILE_OPENGLES2)
                // GLES 2.0 has no glDrawElementsBaseVertex (ES 3.2) -- shift every enabled
                // attribute pointer by baseVertex elements instead, draw, and restore.
                Es2ShiftEnabledVertexAttribPointers(params.baseVertex, +1);
                device.draw_elements(ToEasyGl(primitive), index_count, idxTypeCustom, indexOffsetCustom);
                Es2ShiftEnabledVertexAttribPointers(params.baseVertex, -1);
#else
                ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxTypeCustom,
                                                   indexOffsetCustom, params.baseVertex);
#endif
            }
            if (multiStream) RestoreSingleStreamAttributes(vao, params);
            vb.vao.unbind();
            return;
        }

        const std::size_t layoutStride = CombinedVertexStrideOr(params, vb.GetStride());
        Prog3D& p = SelectProgram(layoutStride, params);
        p.prog.use();
        BindDrawParams(p, world, view, projection, params);

        const int index_count = VertexCountForPrimitives(primitive, primitiveCount);
        CNA_RENDER_LOG("DrawIndexedPrimitivesEx: stride=" << layoutStride
            << " prim=" << static_cast<int>(primitive) << " indices=" << index_count);

        if (!multiStream && wireframe_ &&
            DrawWireframe(vb, &ib, primitive, primitiveCount,
                          params.startIndex, params.baseVertex, 0))
            return;

        vb.vao.bind();
        ib.ibo.bind(::easygl::BufferTarget::ElementArray);
        const auto idxType2 = ib.thirtyTwoBit ? ::easygl::DataType::UnsignedInt
                                               : ::easygl::DataType::UnsignedShort;
        const int indexSize = ib.thirtyTwoBit ? 4 : 2;
        const void* indexOffset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(params.startIndex) * static_cast<std::uintptr_t>(indexSize));
        if (params.baseVertex == 0) {
            device.draw_elements(ToEasyGl(primitive), index_count, idxType2, indexOffset);
        } else {
#if defined(CNA_GL_PROFILE_OPENGLES2)
            // GLES 2.0 has no glDrawElementsBaseVertex (ES 3.2) -- shift every enabled attribute
            // pointer by baseVertex elements instead, draw, and restore.
            Es2ShiftEnabledVertexAttribPointers(params.baseVertex, +1);
            device.draw_elements(ToEasyGl(primitive), index_count, idxType2, indexOffset);
            Es2ShiftEnabledVertexAttribPointers(params.baseVertex, -1);
#else
            ::metagl::glDrawElementsBaseVertex(ToEasyGl(primitive), index_count, idxType2,
                                               indexOffset, params.baseVertex);
#endif
        }
        if (multiStream) RestoreSingleStreamAttributes(vao, params);
        vb.vao.unbind();
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
#if defined(CNA_GL_PROFILE_OPENGLES2)
        // GLES 2.0 core has no glDrawElementsInstanced/glVertexAttribDivisor, and this profile
        // deliberately claims no instancing extension either -- SupportsCapability(Instancing)
        // answers false, so take the shared base-class refusal (the exact route OPENGLES1 keeps
        // for the same reason) rather than let the draw fail inside GL. This also preserves the
        // Unsupported3DGraphicsCallBehavior::WarnAndStub handling the base refusal implements.
        IGraphicsRenderer::DrawInstancedPrimitivesEx(vb_in, ib_in, world, view, projection,
                                                     primitive, primitiveCount, instanceCount,
                                                     params);
        return;
#else
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
        vao.bind();
        if (reconfigurePerVertex && !ConfigureMultiStreamAttributes(vao, params))
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
            Prog3D& p = SelectProgram(CombinedVertexStrideOr(params, vb.GetStride()), params);
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
            ::metagl::glDrawElementsInstancedBaseVertex(
                ToEasyGl(primitive), index_count, idxType, indexOffset,
                instanceCount, params.baseVertex);
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
            Prog3D& p = SelectProgram(CombinedVertexStrideOr(params, vb.GetStride()), params);
            if (p.loc_instanced >= 0)
                p.prog.set_uniform(p.loc_instanced, 0.0f);
        }
        vao.unbind();
#endif // !CNA_GL_PROFILE_OPENGLES2
    }
}

namespace CNA::Internal::Renderers
{
#ifdef CNA_RENDERER_EASYGL
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<EasyGL::EasyGLRenderer>(
            args.window, args.virtualWidth, args.virtualHeight,
            args.presentationMode, args.contextRecoveryEnabled,
            args.multiSampleCount, args.swapInterval);
    }
#endif
}
