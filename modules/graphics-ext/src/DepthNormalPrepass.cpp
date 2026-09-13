// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthNormalPrepass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"
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
#include "shaders/depth_normal_prepass/DepthNormalPrepassShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
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

        constexpr const char* kUnpackGlsl = R"(
float cnaUnpackDepth(vec4 channels) {
    const vec4 shift = vec4(1.0 / 16581375.0, 1.0 / 65025.0, 1.0 / 255.0, 1.0);
    return dot(channels, shift);
}
)";

        struct TextStage
        {
            std::string_view source;
            const char* label;
        };

        struct SpirVStage
        {
            const std::uint32_t* words;
            std::size_t byteSize;
            const char* label;
        };

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(const SpirVStage& stage)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(stage.words);
            return std::vector<std::uint8_t>(begin, begin + stage.byteSize);
        }

        [[nodiscard]] ShaderPackageEXT MakePrepassPackage(const bool skinned,
                                                          const bool velocityOutputs)
        {
            using namespace CNA::Graphics::detail::DepthNormalPrepassGenerated;
            const TextStage esVertex = skinned
                ? TextStage{kSkinnedEsVertexSource, "depth_normal_prepass/skinned.es.vert.glsl"}
                : TextStage{kRigidEsVertexSource, "depth_normal_prepass/rigid.es.vert.glsl"};
            const TextStage desktopVertex = skinned
                ? TextStage{kSkinnedDesktopVertexSource,
                            "depth_normal_prepass/skinned.desktop.vert.glsl"}
                : TextStage{kRigidDesktopVertexSource,
                            "depth_normal_prepass/rigid.desktop.vert.glsl"};
            const SpirVStage vulkanVertex = skinned
                ? SpirVStage{kSkinnedVulkanVertexSpirV,
                             kSkinnedVulkanVertexSpirVByteSize,
                             "depth_normal_prepass/skinned.vulkan.vert.spv"}
                : SpirVStage{kRigidVulkanVertexSpirV,
                             kRigidVulkanVertexSpirVByteSize,
                             "depth_normal_prepass/rigid.vulkan.vert.spv"};
            const SpirVStage vulkanFragment = velocityOutputs
                ? SpirVStage{kPrepassVelocityVulkanFragmentSpirV,
                             kPrepassVelocityVulkanFragmentSpirVByteSize,
                             "depth_normal_prepass/prepass_velocity.vulkan.frag.spv"}
                : SpirVStage{kPrepassVulkanFragmentSpirV,
                             kPrepassVulkanFragmentSpirVByteSize,
                             "depth_normal_prepass/prepass.vulkan.frag.spv"};

            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main", esVertex.label,
                                  std::string(esVertex.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "depth_normal_prepass/prepass.es.frag.glsl",
                                  std::string(kPrepassEsFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main", desktopVertex.label,
                                  std::string(desktopVertex.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "depth_normal_prepass/prepass.desktop.frag.glsl",
                                  std::string(kPrepassDesktopFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main", vulkanVertex.label,
                                  ToBytes(vulkanVertex)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main", vulkanFragment.label,
                                  ToBytes(vulkanFragment)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
        }

    } // namespace

    DepthNormalPrepass::DepthNormalPrepass(GraphicsDevice& device, const int width,
                                           const int height)
        : DepthNormalPrepass(device, width, height, DepthEncoding::Automatic)
    {
    }

    DepthNormalPrepass::DepthNormalPrepass(GraphicsDevice& device, const int width,
                                           const int height, const DepthEncoding encoding)
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
        switch (encoding)
        {
        case DepthEncoding::Packed:    packDepth_ = true;  break;
        case DepthEncoding::HalfFloat: packDepth_ = false; break;
        case DepthEncoding::Automatic:
        default:                       packDepth_ = usesPackedDepthEXT(device); break;
        }

        const auto makeEffect = [&device](const bool skinned, const bool velocityOutputs) {
            const ShaderPackageEXT package = MakePrepassPackage(skinned, velocityOutputs);
            return package.selectFor(device).isUsable()
                ? std::make_unique<ShaderEffect>(device, package)
                : nullptr;
        };
        effect_ = makeEffect(false, false);
        skinnedEffect_ = makeEffect(true, false);
        velocityEffect_ = makeEffect(false, true);
        skinnedVelocityEffect_ = makeEffect(true, true);

        bool logged = false;
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass", effect_.get(), logged);
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass (skinned)",
                                           skinnedEffect_.get(), logged);
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass (velocity)",
                                           velocityEffect_.get(), logged);
        detail::reportShaderCompileFailure(device, "DepthNormalPrepass (skinned velocity)",
                                           skinnedVelocityEffect_.get(), logged);

        supported_ = effect_ && effect_->IsEffectValid()
                  && skinnedEffect_ && skinnedEffect_->IsEffectValid()
                  && velocityEffect_ && velocityEffect_->IsEffectValid()
                  && skinnedVelocityEffect_ && skinnedVelocityEffect_->IsEffectValid();

        allocateTargets();

        // plans/plan_modern.md MOD-1623, and the MOD-1699 lesson in a fourth guise: the
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
            for (ShaderEffect* effect : {
                     velocity_ ? velocityEffect_.get() : effect_.get(),
                     velocity_ ? skinnedVelocityEffect_.get() : skinnedEffect_.get()})
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
                openFarPlane_ = farPlane;
                const std::array prepassScalars{
                    farPlane,
                    packDepth_ ? 1.0f : 0.0f,
                    static_cast<float>(outputMode),
                    roughness_,
                };
                // With no previous camera supplied the current one stands in, which reads as "the
                // camera did not move" -- the honest answer for a first frame, and better than the
                // identity, which would smear the whole image from the world origin.
                const Matrix previousViewProjection =
                    hasPreviousCamera_ ? previousViewProjection_ : (view * projection);
                for (ShaderEffect* effect : {
                         velocity_ ? velocityEffect_.get() : effect_.get(),
                         velocity_ ? skinnedVelocityEffect_.get() : skinnedEffect_.get()})
                {
                    if (effect == nullptr || !effect->IsEffectValid()) continue;
                    effect->Apply();
                    effect->SetUniformMat4("uView", &view.M11);
                    effect->SetUniformMat4("uProjection", &projection.M11);
                    const Matrix identity = Matrix::getIdentityProperty();
                    effect->SetUniformMat4("uWorld", &identity.M11);
                    effect->SetUniformFloatArray(
                        "uPrepassScalars", prepassScalars.data(),
                        static_cast<int>(prepassScalars.size()));
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
        {
            const int outputMode = useMrt_ ? 0
                                           : (openPass_ == kVelocityPass ? 3
                                              : (openPass_ == kNormalPass ? 2 : 1));
            const std::array prepassScalars{
                openFarPlane_,
                packDepth_ ? 1.0f : 0.0f,
                static_cast<float>(outputMode),
                roughness_,
            };
            for (ShaderEffect* effect : {
                     velocity_ ? velocityEffect_.get() : effect_.get(),
                     velocity_ ? skinnedVelocityEffect_.get() : skinnedEffect_.get()})
                if (effect != nullptr && effect->IsEffectValid())
                {
                    effect->Apply();
                    effect->SetUniformFloatArray(
                        "uPrepassScalars", prepassScalars.data(),
                        static_cast<int>(prepassScalars.size()));
                }
        }
    }

    ShaderEffect* DepthNormalPrepass::getPrepassEffect() const
    {
        return supported_ ? (velocity_ ? velocityEffect_.get() : effect_.get()) : nullptr;
    }

    ShaderEffect* DepthNormalPrepass::getSkinnedPrepassEffect() const
    {
        return supported_
            ? (velocity_ ? skinnedVelocityEffect_.get() : skinnedEffect_.get())
            : nullptr;
    }

    Texture2D* DepthNormalPrepass::getDepthTexture() const { return depthTarget_.get(); }

    Texture2D* DepthNormalPrepass::getNormalTexture() const { return normalTarget_.get(); }

    bool DepthNormalPrepass::usesPackedDepthEXT(GraphicsDevice& device)
    {
        (void)device;
        // MOD-507 originally chose half-float whenever available. MOD-2035 then observed a real,
        // intermittent loss of SSAO occlusion from that path on the reference renderer; the focused
        // mechanism probes do not currently reproduce it. Automatic mode therefore stays packed:
        // it is deterministic across renderers, requires no optional format and retains roughly
        // 1 part in 255^3 rather than a half-float's 11-bit mantissa. The explicit HalfFloat
        // constructor path remains available so the alternative and the historical failure can
        // continue to be tested rather than being hidden by policy.
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

    std::string DepthNormalPrepass::getVelocityDecodeGlsl()
    {
        return R"(
vec4 cnaEncodeVelocity(vec2 velocityUv) {
    // Alpha 0 means "this texel has a velocity". Inverted on purpose -- see getVelocityTextureEXT:
    // the MRT path issues one clear for the whole bound set and depth must clear to white, so the
    // shared clear already writes the "nothing here" value.
    return vec4(clamp(velocityUv * 0.5 + 0.5, 0.0, 1.0), 0.0, 0.0);
}
vec4 cnaNoVelocity() { return vec4(0.5, 0.5, 0.0, 1.0); }
bool cnaHasVelocity(vec4 texel) { return texel.a < 0.5; }
vec2 cnaDecodeVelocity(vec4 texel) { return (texel.xy - 0.5) * 2.0; }
)";
    }

    bool DepthNormalPrepass::hasVelocityEXT(const Color& texel)
    {
        return texel.getAProperty() < 128;
    }

    Vector2 DepthNormalPrepass::decodeVelocityEXT(const Color& texel)
    {
        if (!hasVelocityEXT(texel)) return Vector2(0.0f, 0.0f);
        return Vector2((static_cast<float>(texel.getRProperty()) / 255.0f - 0.5f) * 2.0f,
                       (static_cast<float>(texel.getGProperty()) / 255.0f - 0.5f) * 2.0f);
    }

    void DepthNormalPrepass::packDepth(const float value, float& r, float& g, float& b, float& a)
    {
        // Stops one texel short of 1.0, exactly as the GLSL does and for the same reason:
        // fract(1.0) is 0, so an unclamped far-plane depth would read back as the nearest possible
        // surface. The packaged fragment shaders use the same constant and shifts.
        const float clamped = std::clamp(value, 0.0f, 0.99999994f);
        const float shift[4] = {16581375.0f, 65025.0f, 255.0f, 1.0f};
        float channels[4];
        for (int i = 0; i < 4; ++i)
        {
            const float scaled = clamped * shift[i];
            channels[i] = scaled - std::floor(scaled);
        }
        // Same subtraction as the GLSL: each channel drops the part the previous one already holds.
        const float raw[4] = {channels[0], channels[1], channels[2], channels[3]};
        r = raw[0];
        g = raw[1] - raw[0] / 255.0f;
        b = raw[2] - raw[1] / 255.0f;
        a = raw[3] - raw[2] / 255.0f;
    }

    float DepthNormalPrepass::unpackDepth(const float r, const float g, const float b,
                                          const float a)
    {
        return r / 16581375.0f + g / 65025.0f + b / 255.0f + a;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
