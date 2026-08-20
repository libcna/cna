// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr int kMinSteps = 4;
        constexpr int kMaxSteps = 64;

        constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

        // The march itself. `texture1` is the scene colour (SpriteBatch's own slot), slot 1 the
        // linear depth image. Everything here happens in view-space world units: the reconstruction
        // returns a position scaled by the stored depth, so multiplying by the far plane puts the
        // ray, the step length and the thickness in one set of units the caller can reason about.
        constexpr const char* kMarchBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform mat4  uCameraProjection;
uniform mat4  uInverseProjection;
uniform vec3  uLightDirectionView;
uniform vec2  uDepthSize;
uniform float uFarPlane;
uniform float uMaxDistance;
uniform float uThickness;
uniform float uBias;
uniform float uIntensity;
uniform int   uStepCount;

// One texel short of the far plane: the prepass clears depth to white, and a surface at the far
// plane occludes nothing worth marching for.
const float kCnaSkyDepth = 0.999;

// Point sampling through arithmetic the shader owns, for the reason SsrPass records: filtering a
// depth edge averages two surfaces into a distance nothing in the scene is at, and a per-unit
// sampler state does not reach a texture bound through ShaderEffect::SetTexture.
vec2 cnaSnapToTexel(vec2 uv, vec2 size) {
    return (floor(uv * size) + 0.5) / size;
}

void main() {
    vec4 scene = texture(texture1, TexCoord);
    float centerDepth =
        cnaDecodeLinearDepth(texture(uDepthSampler, cnaSnapToTexel(TexCoord, uDepthSize)));

    // Nothing was drawn here. Both spellings are checked: the prepass clears to white, and a
    // renderer that clears its target to black produces zero.
    if (centerDepth <= 0.0 || centerDepth >= kCnaSkyDepth) {
        FragColor = scene;
        return;
    }

    vec3 position = cnaViewPositionFromDepth(TexCoord, centerDepth, uInverseProjection) * uFarPlane;
    float stepLength = uMaxDistance / float(uStepCount);

    float occluded = 0.0;
    for (int i = 1; i <= 64; ++i) {
        if (i > uStepCount) break;

        vec3 samplePosition = position + uLightDirectionView * (stepLength * float(i));
        // Behind the eye: there is no screen position for this point, and projecting it anyway
        // produces one -- mirrored through the origin -- that reads as a plausible hit.
        if (samplePosition.z >= -1e-6) break;

        vec4 clip = uCameraProjection * vec4(samplePosition, 1.0);
        if (clip.w <= 0.0) break;
        vec2 sampleUv = (clip.xy / clip.w) * 0.5 + 0.5;
        // The ray left the frame. It has not been proven unoccluded -- there is simply nothing
        // left to ask, which is the boundary this pass cannot see past.
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) break;

        float sceneDepth =
            cnaDecodeLinearDepth(textureLod(uDepthSampler, cnaSnapToTexel(sampleUv, uDepthSize), 0.0));
        if (sceneDepth <= 0.0 || sceneDepth >= kCnaSkyDepth) continue;

        if (cnaContactOccluded(-samplePosition.z, sceneDepth * uFarPlane, uBias, uThickness)) {
            occluded = 1.0;
            break;
        }
    }

    float visibility = 1.0 - occluded * clamp(uIntensity, 0.0, 1.0);
    // The composition is this multiply and nothing else: the image being multiplied already
    // carries the shadow map's own term, so a pixel the map put in shadow cannot darken twice.
    FragColor = vec4(scene.rgb * visibility, scene.a);
}
)";

    } // namespace

    ContactShadowPass::ContactShadowPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        std::string source = "#version 300 es\nprecision highp float;\n";
        source += DepthNormalPrepass::getDepthDecodeGlsl(
            DepthNormalPrepass::usesPackedDepthEXT(device));
        source += getOcclusionTestGlsl();
        source += kMarchBody;
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, source);

        bool logged = false;
        detail::reportShaderCompileFailure(device, "ContactShadowPass", effect_.get(), logged);
    }

    ContactShadowPass::~ContactShadowPass() = default;

    std::string ContactShadowPass::getOcclusionTestGlsl()
    {
        return R"(
bool cnaContactOccluded(float rayViewDepth, float sceneViewDepth, float bias, float thickness) {
    float difference = rayViewDepth - sceneViewDepth;
    return difference > bias && difference < thickness;
}
)";
    }

    bool ContactShadowPass::isOccluded(const float rayViewDepth, const float sceneViewDepth,
                                       const float bias, const float thickness)
    {
        const float difference = rayViewDepth - sceneViewDepth;
        return difference > bias && difference < thickness;
    }

    float ContactShadowPass::combineVisibility(const float shadowMapVisibility,
                                               const float contactVisibility)
    {
        return std::clamp(shadowMapVisibility, 0.0f, 1.0f)
             * std::clamp(contactVisibility, 0.0f, 1.0f);
    }

    void ContactShadowPass::apply(const PostProcessContext& context)
    {
        fallbackReason_.clear();

        if (!effect_ || !effect_->IsEffectValid())
            fallbackReason_ = "the pass shader did not compile";
        else if (context.sourceDepth == nullptr)
            fallbackReason_ = "no depth image was supplied, so there is nothing to march through";
        else if (context.farPlane <= 0.0f)
            fallbackReason_ = "no far plane was supplied, so the stored depth has no world scale";
        else if (context.inverseView.M44 == 0.0f)
            fallbackReason_ = "no inverse view matrix was supplied, so the light direction cannot "
                              "be brought into view space";

        if (!fallbackReason_.empty())
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        // The march wants the direction *toward* the light; the setter takes the direction light
        // travels, so the two differ by a sign. TransformNormal rather than Transform because a
        // direction must not pick up the camera's translation.
        Vector3 toLight(-lightDirection_.X, -lightDirection_.Y, -lightDirection_.Z);
        const float length = std::sqrt(toLight.X * toLight.X + toLight.Y * toLight.Y
                                     + toLight.Z * toLight.Z);
        if (length < 1e-6f)
        {
            fallbackReason_ = "the light direction is degenerate, so there is no ray to march";
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }
        toLight.X /= length;
        toLight.Y /= length;
        toLight.Z /= length;

        const Matrix view = Matrix::Invert(context.inverseView);
        Vector3 viewLight = Vector3::TransformNormal(toLight, view);
        const float viewLength = std::sqrt(viewLight.X * viewLight.X + viewLight.Y * viewLight.Y
                                         + viewLight.Z * viewLight.Z);
        if (viewLength > 1e-6f)
        {
            viewLight.X /= viewLength;
            viewLight.Y /= viewLength;
            viewLight.Z /= viewLength;
        }

        const int steps = std::clamp(stepCount_, kMinSteps, kMaxSteps);

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformMat4("uCameraProjection", &context.projection.M11);
        effect_->SetUniformMat4("uInverseProjection", &context.inverseProjection.M11);
        effect_->SetUniformVec3("uLightDirectionView", viewLight.X, viewLight.Y, viewLight.Z);
        effect_->SetUniformVec2("uDepthSize",
                                static_cast<float>(context.sourceDepth->getWidthProperty()),
                                static_cast<float>(context.sourceDepth->getHeightProperty()));
        effect_->SetUniformFloat("uFarPlane", context.farPlane);
        effect_->SetUniformFloat("uMaxDistance", std::max(maxDistance_, 1e-4f));
        effect_->SetUniformFloat("uThickness", std::max(thickness_, 0.0f));
        effect_->SetUniformFloat("uBias", std::max(bias_, 0.0f));
        effect_->SetUniformFloat("uIntensity", std::clamp(intensity_, 0.0f, 1.0f));
        effect_->SetUniformInt("uStepCount", steps);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& ContactShadowPass::getName() const
    {
        static const std::string name = "ContactShadow";
        return name;
    }

    bool ContactShadowPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    Vector3 ContactShadowPass::getLightDirection() const { return lightDirection_; }

    void ContactShadowPass::setLightDirection(const Vector3& value) { lightDirection_ = value; }

    float ContactShadowPass::getMaxDistance() const            { return maxDistance_; }
    void  ContactShadowPass::setMaxDistance(const float value) { maxDistance_ = value; }

    int  ContactShadowPass::getStepCount() const          { return stepCount_; }
    void ContactShadowPass::setStepCount(const int value) { stepCount_ = value; }

    float ContactShadowPass::getThickness() const            { return thickness_; }
    void  ContactShadowPass::setThickness(const float value) { thickness_ = value; }

    float ContactShadowPass::getIntensity() const            { return intensity_; }
    void  ContactShadowPass::setIntensity(const float value) { intensity_ = value; }

    float ContactShadowPass::getBias() const            { return bias_; }
    void  ContactShadowPass::setBias(const float value) { bias_ = value; }

    const std::string& ContactShadowPass::getFallbackReason() const { return fallbackReason_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
