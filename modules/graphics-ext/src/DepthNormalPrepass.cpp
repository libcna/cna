// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthNormalPrepass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// Pass indices. With MRT there is only pass 0 and it writes both; without, pass 0 writes
        /// depth and pass 1 writes normals.
        constexpr int kDepthPass  = 0;
        constexpr int kNormalPass = 1;
        /// MOD-2033: only ever reached without MRT, where velocity is a third pass over the geometry.
        constexpr int kVelocityPass = 2;

        /// The packing MOD-507 uses where a float target is unavailable. Written once here as GLSL
        /// and once in C++ below, and a test asserts the two agree -- the usual failure of a packed
        /// format is that the two halves drift and depth reads back as noise.
        constexpr const char* kPackGlsl = R"(
vec4 cnaPackDepth(float value) {
    // 8 bits per channel, most significant first. The subtraction removes each channel's
    // contribution before the next is extracted, which is what stops rounding accumulating.
    //
    // The clamp stops one texel short of 1.0 on purpose: fract(1.0) is 0, so an unclamped 1.0
    // packs to all zeroes and reads back as the *nearest* possible surface -- the exact inverse
    // of what it means. Depth is normalised by the far plane, so 1.0 is the most common value in
    // the buffer, and getting it inverted would put the whole background in front of the scene.
    // `channels` rather than `packed`: the latter is a reserved word in GLSL ES 3.00.
    const vec4 shift = vec4(16777216.0, 65536.0, 256.0, 1.0);
    const vec4 mask  = vec4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
    vec4 channels = fract(clamp(value, 0.0, 0.99999994) * shift);
    channels -= channels.xxyz * mask;
    return channels;
}
)";

        constexpr const char* kUnpackGlsl = R"(
float cnaUnpackDepth(vec4 channels) {
    const vec4 shift = vec4(1.0 / 16777216.0, 1.0 / 65536.0, 1.0 / 256.0, 1.0);
    return dot(channels, shift);
}
)";

        // ---- The prepass shaders ---------------------------------------------------------------
        //
        // Depth is *linear view depth normalised by the far plane*, not the non-linear value a
        // depth buffer holds. That choice is the whole reason this pass exists: a consumer can
        // reconstruct a view-space position from it with one multiply, and the value means the same
        // thing on every renderer, which a depth attachment's contents do not.

        constexpr const char* kVertexCommon = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uPreviousWorld;
uniform mat4 uPreviousViewProjection;
out vec3 vViewNormal;
out float vViewDepth;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
uniform float uFarPlane;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vec4 view  = uView * world;
    gl_Position = uProjection * view;
    // MOD-2033. Both clip positions go to the fragment stage undivided: the perspective divide is
    // not an affine operation, so interpolating the divided values would put the velocity of a
    // large triangle in the wrong place everywhere except at its vertices.
    vCurrentClip  = gl_Position;
    vPreviousClip = uPreviousViewProjection * (uPreviousWorld * vec4(aPosition, 1.0));
    // The normal matrix would be the inverse transpose; a uniformly-scaled world matrix makes the
    // upper 3x3 sufficient, which is what CNA's own model transforms are. Non-uniform scale skews
    // the normal here, and is documented rather than corrected -- correcting it needs an inverse
    // per draw that nothing else in this layer pays for.
    vViewNormal = normalize(mat3(uView) * mat3(uWorld) * aNormal);
    vViewDepth  = clamp(-view.z / uFarPlane, 0.0, 1.0);
}
)";

        constexpr const char* kSkinnedVertexCommon = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in uvec4 aBoneIndices;
uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
uniform float uFarPlane;
uniform mat4 uPreviousWorld;
uniform mat4 uPreviousViewProjection;
out vec3 vViewNormal;
out float vViewDepth;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
void main() {
    mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2) skin += uBones[aBoneIndices.y] * aBoneWeights.y;
    if (uWeightsPerVertex >= 4) skin += uBones[aBoneIndices.z] * aBoneWeights.z
                                      + uBones[aBoneIndices.w] * aBoneWeights.w;
    vec4 world = uWorld * skin * vec4(aPosition, 1.0);
    vec4 view  = uView * world;
    gl_Position = uProjection * view;
    vCurrentClip  = gl_Position;
    // The previous pose is deliberately NOT reconstructed here: the bones are this frame's, so a
    // skinned mesh's velocity is its object's motion and not its deformation. Recording the latter
    // needs the previous frame's bone set as a second array, which is an obligation on the app an
    // order of magnitude larger than one matrix; it is stated in the header rather than faked.
    vPreviousClip = uPreviousViewProjection * (uPreviousWorld * skin * vec4(aPosition, 1.0));
    vViewNormal = normalize(mat3(uView) * mat3(uWorld) * mat3(skin) * aNormal);
    vViewDepth  = clamp(-view.z / uFarPlane, 0.0, 1.0);
}
)";

        /// One fragment shader for every combination, branching on two uniforms rather than
        /// compiling four programs: the branches are uniform-controlled, so a driver folds them,
        /// and four near-identical shaders is four places for the encoding to drift.
        std::string MakeFragmentSource()
        {
            std::string source = R"(#version 300 es
precision highp float;
in vec3 vViewNormal;
in float vViewDepth;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
uniform int uPackDepth;
uniform int uOutputMode;   // 0 = every target (MRT), 1 = depth, 2 = normals, 3 = velocity
uniform float uRoughness;  // rides in the normal target's alpha; see setRoughness
layout(location = 0) out vec4 FragTarget0;
layout(location = 1) out vec4 FragTarget1;
layout(location = 2) out vec4 FragTarget2;
)";
            source += kPackGlsl;
            source += R"(
vec4 cnaVelocityOut(vec4 currentClip, vec4 previousClip) {
    // Behind the previous camera: there is no screen position to have come from, so the pixel is
    // marked as carrying no velocity rather than given a reprojection through a negative w.
    if (currentClip.w <= 0.0 || previousClip.w <= 0.0) return vec4(0.5, 0.5, 0.0, 1.0);
    vec2 currentUv  = (currentClip.xy / currentClip.w) * 0.5 + 0.5;
    vec2 previousUv = (previousClip.xy / previousClip.w) * 0.5 + 0.5;
    // Alpha 0 means "this texel has a velocity". Inverted on purpose -- see getVelocityTextureEXT:
    // the MRT path issues one clear for the whole bound set and depth must clear to white, so the
    // shared clear already writes the "nothing here" value.
    return vec4(clamp((currentUv - previousUv) * 0.5 + 0.5, 0.0, 1.0), 0.0, 0.0);
}

void main() {
    vec4 depthOut  = (uPackDepth != 0) ? cnaPackDepth(vViewDepth)
                                       : vec4(vViewDepth, vViewDepth, vViewDepth, 1.0);
    // The alpha of the normal target carried nothing until MOD-2003. Roughness rides there rather
    // than in a third target: MRT is capped and this pass already falls back to two passes without
    // it, so a third output would make that fallback three passes for one scalar.
    vec4 normalOut = vec4(vViewNormal * 0.5 + 0.5, uRoughness);
    vec4 velocityOut = cnaVelocityOut(vCurrentClip, vPreviousClip);
    if (uOutputMode == 1) {
        FragTarget0 = depthOut;
        FragTarget1 = depthOut;
        FragTarget2 = depthOut;
    } else if (uOutputMode == 2) {
        FragTarget0 = normalOut;
        FragTarget1 = normalOut;
        FragTarget2 = normalOut;
    } else if (uOutputMode == 3) {
        FragTarget0 = velocityOut;
        FragTarget1 = velocityOut;
        FragTarget2 = velocityOut;
    } else {
        FragTarget0 = depthOut;
        FragTarget1 = normalOut;
        FragTarget2 = velocityOut;
    }
}
)";
            return source;
        }

    } // namespace

    DepthNormalPrepass::DepthNormalPrepass(GraphicsDevice& device, const int width,
                                           const int height)
        : device_(device)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::DepthNormalPrepass: the target size must be positive");
        width_  = width;
        height_ = height;
        // Identity rather than a zeroed Matrix: a zero matrix collapses every previous position to
        // the origin, which is a full-screen smear rather than "no motion".
        previousWorld_ = Matrix::getIdentityProperty();
        previousViewProjection_ = Matrix::getIdentityProperty();

        useMrt_ = device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets);
        packDepth_ = usesPackedDepthEXT(device);

        const std::string fragment = MakeFragmentSource();
        effect_        = std::make_unique<ShaderEffect>(device, kVertexCommon, fragment);
        skinnedEffect_ = std::make_unique<ShaderEffect>(device, kSkinnedVertexCommon, fragment);

        bool logged = false;
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass", effect_.get(), logged);
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass (skinned)",
                                           skinnedEffect_.get(), logged);

        supported_ = effect_->IsEffectValid() && device.ExecutesShaderEffectSourceEXT();

        allocateTargets();

        // plan_modern.md MOD-1623, and the MOD-1699 lesson in a fourth guise: the
        // MultipleRenderTargets *capability* is a promise, and WebGPU is a renderer that makes it
        // and does not keep it -- SetRenderTargets throws "multiple simultaneous render targets are
        // not implemented on this renderer yet". Trusting the capability meant begin() threw where
        // the two-pass path would have worked perfectly. So the answer is probed by doing, once,
        // here: bind the pair this class will actually bind, and fall back to two passes if the
        // renderer refuses. One bind at construction is cheap; a throw on every frame is not.
        probeMultipleRenderTargets();
    }

    void DepthNormalPrepass::probeMultipleRenderTargets()
    {
        if (!device_.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        {
            useMrt_ = false;
            return;
        }
        useMrt_ = true;
        try
        {
            std::vector<RenderTargetBinding> bindings = {
                RenderTargetBinding(depthTarget_.get()),
                RenderTargetBinding(normalTarget_.get()),
            };
            if (velocity_ && velocityTarget_ != nullptr)
                bindings.emplace_back(velocityTarget_.get());
            device_.SetRenderTargets(bindings);
            device_.SetRenderTarget(nullptr);
        }
        catch (...)
        {
            useMrt_ = false;
            try { device_.SetRenderTarget(nullptr); } catch (...) { /* best-effort cleanup */ }
        }
    }

    DepthNormalPrepass::~DepthNormalPrepass() = default;

    void DepthNormalPrepass::allocateTargets()
    {
        const SurfaceFormat depthFormat =
            packDepth_ ? SurfaceFormat::Color : SurfaceFormat::HalfSingle;
        depthTarget_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                                        depthFormat, DepthFormat::Depth24);
        normalTarget_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                                          SurfaceFormat::Color,
                                                          DepthFormat::Depth24);
        // MOD-2033. `Color` rather than a float format: the velocity stored here is a UV delta,
        // and one screen's worth in a frame is already an absurd speed, so eight bits over the
        // whole range is finer than the blur can act on. A float target would also make this the
        // one prepass output that needs a capability the others do not.
        velocityTarget_ = velocity_
            ? std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                               SurfaceFormat::Color, DepthFormat::Depth24)
            : nullptr;
    }

    void DepthNormalPrepass::resize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::DepthNormalPrepass::resize: the target size must be positive");
        if (passOpen_)
            throw std::logic_error(
                "CNA::Graphics::DepthNormalPrepass::resize: a pass is open");
        if (width == width_ && height == height_)
            return;

        width_  = width;
        height_ = height;
        allocateTargets();
    }

    int DepthNormalPrepass::getPassCount() const
    {
        if (useMrt_) return 1;
        return velocity_ ? 3 : 2;
    }

    bool DepthNormalPrepass::isVelocityEnabledEXT() const { return velocity_; }

    Texture2D* DepthNormalPrepass::getVelocityTextureEXT() const { return velocityTarget_.get(); }

    void DepthNormalPrepass::setVelocityEnabledEXT(const bool value)
    {
        if (passOpen_)
            throw std::logic_error(
                "CNA::Graphics::DepthNormalPrepass::setVelocityEnabledEXT: a pass is open");
        if (velocity_ == value) return;
        velocity_ = value;
        allocateTargets();
        // The MRT verdict has to be re-taken, not assumed: a renderer that binds two targets is not
        // promising three, and MRT counts are capped. Same probe as the constructor's, and the same
        // reason -- one bind now instead of a throw on every frame.
        probeMultipleRenderTargets();
    }

    void DepthNormalPrepass::setPreviousWorldEXT(const Matrix& value)
    {
        previousWorld_ = value;
        // Applied immediately when a pass is open, for the reason setRoughness is: the prepass draws
        // whatever the app hands it and cannot tell one object from the next.
        if (passOpen_ && supported_)
            for (ShaderEffect* effect : {effect_.get(), skinnedEffect_.get()})
                if (effect != nullptr && effect->IsEffectValid())
                {
                    effect->Apply();
                    effect->SetUniformMat4("uPreviousWorld", &previousWorld_.M11);
                }
    }

    void DepthNormalPrepass::setPreviousCameraEXT(const Matrix& previousView,
                                                  const Matrix& previousProjection)
    {
        previousViewProjection_ = previousView * previousProjection;
        hasPreviousCamera_ = true;
    }

    void DepthNormalPrepass::begin(const int passIndex, const Matrix& view,
                                   const Matrix& projection, const float nearPlane,
                                   const float farPlane)
    {
        if (passOpen_)
            throw std::logic_error("CNA::Graphics::DepthNormalPrepass::begin: a pass is already open");
        if (passIndex < 0 || passIndex >= getPassCount())
            throw std::out_of_range("CNA::Graphics::DepthNormalPrepass::begin: no such pass");
        if (nearPlane <= 0.0f || farPlane <= nearPlane)
            throw std::invalid_argument(
                "CNA::Graphics::DepthNormalPrepass::begin: the near plane must be positive and the "
                "far plane beyond it -- depth is normalised by the far plane, so a zero or inverted "
                "range produces a buffer of NaNs rather than a wrong image");

        // Depth clears to white: 1.0 is the far plane, so an unwritten texel reads as "nothing here,
        // infinitely far", the same convention the shadow maps use and for the same reason -- black
        // would make every empty pixel the nearest possible occluder.
        //
        // Normals clear to (0.5, 0.5, 1.0), which decodes to +Z in view space: facing the camera.
        // A zero clear decodes to (-1,-1,-1), a direction no visible surface has, and SSAO reading
        // it produces occlusion out of empty space.
        static const Color kFarDepth   = Color::White;
        static const Color kFacingView = Color(128, 128, 255, 255);

        try
        {
            if (useMrt_)
            {
                std::vector<RenderTargetBinding> bindings = {
                    RenderTargetBinding(depthTarget_.get()),
                    RenderTargetBinding(normalTarget_.get()),
                };
                if (velocity_ && velocityTarget_ != nullptr)
                    bindings.emplace_back(velocityTarget_.get());
                device_.SetRenderTargets(bindings);
                // One clear for a bound set: the depth convention wins, because an unwritten normal
                // texel is only read where depth says something is there -- and MOD-2033's velocity
                // target reads a white clear as "no velocity here", which is why its flag is the
                // alpha inverted rather than the alpha.
                device_.Clear(kFarDepth);
            }
            else if (passIndex == kDepthPass)
            {
                device_.SetRenderTarget(depthTarget_.get());
                device_.Clear(kFarDepth);
            }
            else if (passIndex == kNormalPass)
            {
                device_.SetRenderTarget(normalTarget_.get());
                device_.Clear(kFacingView);
            }
            else
            {
                device_.SetRenderTarget(velocityTarget_.get());
                device_.Clear(kFarDepth);
            }

            if (supported_)
            {
                const int outputMode = useMrt_ ? 0
                                               : (passIndex == kVelocityPass ? 3
                                                  : (passIndex == kNormalPass ? 2 : 1));
                // With no previous camera supplied the current one stands in, which reads as "the
                // camera did not move" -- the honest answer for a first frame, and better than the
                // identity, which would smear the whole image from the world origin.
                const Matrix previousViewProjection =
                    hasPreviousCamera_ ? previousViewProjection_ : (view * projection);
                for (ShaderEffect* effect : {effect_.get(), skinnedEffect_.get()})
                {
                    if (effect == nullptr || !effect->IsEffectValid()) continue;
                    effect->Apply();
                    effect->SetUniformMat4("uView", &view.M11);
                    effect->SetUniformMat4("uProjection", &projection.M11);
                    const Matrix identity = Matrix::getIdentityProperty();
                    effect->SetUniformMat4("uWorld", &identity.M11);
                    effect->SetUniformFloat("uFarPlane", farPlane);
                    effect->SetUniformInt("uPackDepth", packDepth_ ? 1 : 0);
                    effect->SetUniformInt("uOutputMode", outputMode);
                    effect->SetUniformFloat("uRoughness", roughness_);
                    effect->SetUniformMat4("uPreviousWorld", &previousWorld_.M11);
                    effect->SetUniformMat4("uPreviousViewProjection", &previousViewProjection.M11);
                }
            }
        }
        catch (...)
        {
            try { device_.SetRenderTarget(nullptr); } catch (...) { /* best-effort cleanup */ }
            throw;
        }

        // Opened only once the binding and the uniforms are in place, so a renderer that refuses a
        // target does not leave the object permanently "already open" (the trap CubeShadowMap hit).
        passOpen_ = true;
        openPass_ = passIndex;
    }

    void DepthNormalPrepass::end()
    {
        if (!passOpen_)
            throw std::logic_error("CNA::Graphics::DepthNormalPrepass::end: no pass is open");
        passOpen_ = false;
        openPass_ = -1;
        device_.SetRenderTarget(nullptr);
    }

    float DepthNormalPrepass::getRoughness() const { return roughness_; }

    void DepthNormalPrepass::setRoughness(const float value)
    {
        roughness_ = std::clamp(value, 0.0f, 1.0f);
        // Applied immediately when a pass is open, so a caller can change it between draws inside
        // one begin()/end() -- which is the only way a scene with more than one material can
        // describe itself, since the prepass draws whatever the app hands it.
        if (passOpen_ && supported_)
            for (ShaderEffect* effect : {effect_.get(), skinnedEffect_.get()})
                if (effect != nullptr && effect->IsEffectValid())
                {
                    effect->Apply();
                    effect->SetUniformFloat("uRoughness", roughness_);
                }
    }

    ShaderEffect* DepthNormalPrepass::getPrepassEffect() const
    {
        return supported_ ? effect_.get() : nullptr;
    }

    ShaderEffect* DepthNormalPrepass::getSkinnedPrepassEffect() const
    {
        return supported_ ? skinnedEffect_.get() : nullptr;
    }

    Texture2D* DepthNormalPrepass::getDepthTexture() const { return depthTarget_.get(); }

    Texture2D* DepthNormalPrepass::getNormalTexture() const { return normalTarget_.get(); }

    bool DepthNormalPrepass::usesPackedDepthEXT(GraphicsDevice& device)
    {
        (void)device;
        // MOD-507 chose the half-float target wherever one existed. MOD-2035 measured what that
        // costs on the reference renderer, and the measurement is not subtle: with a half-float
        // depth target, SSAO driven from the prepass occludes **nothing** -- 0 pixels of 16384 --
        // and with a packed one it occludes 2101. `CNAEXT_Showcase`'s check E goes from 0
        // strongly-occluded pixels to 1022 on the same frame.
        //
        // **The mechanism is not established, and this comment does not claim one.** A minimal
        // reproducer -- two textures proven to hold identical values, one half-float and one 8-bit,
        // and the same loop inlined over each -- does *not* separate them
        // (`HalfFloatDepthSamplingTests` runs it and records what it finds). So what is known is
        // the effect in the real pass, not its cause, and the earlier bisection in this row's
        // history was measuring something real that a smaller test does not yet capture.
        //
        // Choosing the packed path anyway is not settling for a workaround, because packing is the
        // better encoding on its own terms and this class's own documentation already said so:
        // 1 part in 2^24 against a half-float's 11-bit mantissa, at the price of a little
        // arithmetic on both ends, and no capability required at all -- one fewer per-renderer
        // branch rather than one more.
        //
        // The half-float path is kept rather than deleted: it is one `return` away, and a renderer
        // that samples it correctly would prefer it for the bandwidth.
        return true;
    }

    bool DepthNormalPrepass::isSupported(GraphicsDevice& device) const
    {
        return supported_ && device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
    }

    bool DepthNormalPrepass::isUsingMultipleRenderTargets() const { return useMrt_; }

    bool DepthNormalPrepass::isDepthPacked() const { return packDepth_; }

    std::string DepthNormalPrepass::getDepthDecodeGlsl(const bool packed)
    {
        std::string source = packed ? std::string(kUnpackGlsl) : std::string();
        source += packed ? R"(
float cnaDecodeLinearDepth(vec4 texel) { return cnaUnpackDepth(texel); }
)" : R"(
float cnaDecodeLinearDepth(vec4 texel) { return texel.r; }
)";
        // The reconstruction the row asks for: a view-space position from a screen UV, the decoded
        // depth and the inverse projection. Written here rather than in each consumer so the
        // encoding and its inverse cannot drift apart.
        source += R"(
vec3 cnaViewPositionFromDepth(vec2 uv, float linearDepth, mat4 inverseProjection) {
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 ray  = inverseProjection * clip;
    vec3 direction = ray.xyz / ray.w;
    // The stored depth is distance along view -Z normalised by the far plane, so scaling the ray
    // to that depth is a division by the ray's own -Z rather than a multiply by its length.
    return direction * (linearDepth / max(-direction.z, 1e-6));
}
)";
        return source;
    }

    void DepthNormalPrepass::packDepth(const float value, float& r, float& g, float& b, float& a)
    {
        // Stops one texel short of 1.0, exactly as the GLSL does and for the same reason:
        // fract(1.0) is 0, so an unclamped far-plane depth would read back as the nearest possible
        // surface. See kPackGlsl.
        const float clamped = std::clamp(value, 0.0f, 0.99999994f);
        const float shift[4] = {16777216.0f, 65536.0f, 256.0f, 1.0f};
        float channels[4];
        for (int i = 0; i < 4; ++i)
        {
            const float scaled = clamped * shift[i];
            channels[i] = scaled - std::floor(scaled);
        }
        // Same subtraction as the GLSL: each channel drops the part the previous one already holds.
        const float raw[4] = {channels[0], channels[1], channels[2], channels[3]};
        r = raw[0];
        g = raw[1] - raw[0] / 256.0f;
        b = raw[2] - raw[1] / 256.0f;
        a = raw[3] - raw[2] / 256.0f;
    }

    float DepthNormalPrepass::unpackDepth(const float r, const float g, const float b,
                                          const float a)
    {
        return r / 16777216.0f + g / 65536.0f + b / 256.0f + a;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
