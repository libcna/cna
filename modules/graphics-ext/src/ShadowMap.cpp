// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShadowMap.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingBox;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// The caster shader. It writes normalized light-space distance, which is what makes the
        /// map readable as an ordinary colour texture on every renderer -- see the class comment
        /// for why a real depth attachment is not an option here.
        constexpr const char* kCasterVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
out float vDistance;
void main() {
    vec4 lightSpace = uLightViewProjection * uWorld * vec4(aPosition, 1.0);
    gl_Position = lightSpace;
    // Normalized device depth mapped to [0,1]: the same value the receiver will compute for a
    // point it wants to test, so the comparison needs no further transform.
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
)";

        /// The skinned caster (MOD-810). Same output as the rigid one; the difference is that the
        /// position is blended through the bone palette first, so the silhouette recorded in the
        /// map is the pose the mesh is actually in. Attribute locations follow the custom-effect
        /// convention -- location N is the Nth element of the vertex declaration -- which for the
        /// skinned stride is position, normal, uv, weights, indices.
        constexpr const char* kSkinnedCasterVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in uvec4 aBoneIndices;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
uniform mat4 uBones[72];
uniform int uWeightsPerVertex;
out float vDistance;
void main() {
    // Only the first uWeightsPerVertex pairs contribute, matching SkinnedEffect: a mesh authored
    // with one weight per vertex has undefined values in the other three slots.
    mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x;
    if (uWeightsPerVertex >= 2) skin += uBones[aBoneIndices.y] * aBoneWeights.y;
    if (uWeightsPerVertex >= 4) skin += uBones[aBoneIndices.z] * aBoneWeights.z
                                      + uBones[aBoneIndices.w] * aBoneWeights.w;
    vec4 lightSpace = uLightViewProjection * uWorld * skin * vec4(aPosition, 1.0);
    gl_Position = lightSpace;
    vDistance = lightSpace.z / lightSpace.w * 0.5 + 0.5;
}
)";

        constexpr const char* kCasterFragmentSource = R"(#version 300 es
precision highp float;
in float vDistance;
out vec4 FragColor;
void main() {
    FragColor = vec4(vDistance, vDistance, vDistance, 1.0);
}
)";

        Vector3 Normalized(const Vector3& value)
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (lengthSquared <= 1e-12f)
                return Vector3(0.0f, -1.0f, 0.0f);
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            return Vector3(value.X * inverse, value.Y * inverse, value.Z * inverse);
        }

        /// The palette size the skinned caster declares, matching the stock skinned programs.
        constexpr std::size_t kMaxCasterBones = 72;

    } // namespace

    int ShadowMap::sizeForQuality(const ShadowQuality quality)
    {
        switch (quality)
        {
        case ShadowQuality::Ultra:  return 4096;
        case ShadowQuality::High:   return 2048;
        case ShadowQuality::Medium: return 1024;
        case ShadowQuality::Low:
        case ShadowQuality::Disabled:
        default:
            // Disabled still gets a real map, at the smallest size: a game toggling quality at
            // run time should not have to destroy and recreate the object to do it.
            return 512;
        }
    }

    int ShadowMap::filterRadiusForQuality(const ShadowQuality quality)
    {
        switch (quality)
        {
        case ShadowQuality::Ultra:
        case ShadowQuality::High:   return 2;
        case ShadowQuality::Medium: return 1;
        case ShadowQuality::Low:
        case ShadowQuality::Disabled:
        default:                    return 0;
        }
    }

    int ShadowMap::getFilterRadius() const
    {
        return filterRadiusForQuality(quality_);
    }

    ShadowMap::ShadowMap(GraphicsDevice& device, const ShadowQuality quality)
        : device_(device), quality_(quality), size_(sizeForQuality(quality))
    {
        // A float map where the renderer has one: distance in 8 bits gives 256 distinguishable
        // depths across the whole scene, which is enough for a demo and visibly stepped in
        // anything larger.
        const SurfaceFormat format =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)
                ? SurfaceFormat::Single
                : SurfaceFormat::Color;

        target_ = std::make_unique<RenderTarget2D>(device, size_, size_, false, format,
                                                   DepthFormat::Depth24);
        lightViewProjection_ = Matrix::getIdentityProperty();

        // MOD-811 / design decision D1. Both are needed and they fail differently, so the log
        // names which one is missing rather than reporting "shadows unavailable" and leaving the
        // reader to guess.
        const bool canRaster = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        // Two questions, not one (plan_modern.md `MOD-1699`): `CustomEffects` only means the
        // renderer *accepts* an effect. SOFTWARE and HEADLESS accept any shader source and go
        // on rendering with their own fixed path, so a caster that believed them would report
        // a working shadow map while writing depth nothing had shaded.
        const bool canCompile = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                             && device.ExecutesShaderEffectSourceEXT();
        if (canRaster && canCompile)
        {
            casterEffect_ = std::make_unique<ShaderEffect>(device, kCasterVertexSource,
                                                           kCasterFragmentSource);
            skinnedCasterEffect_ = std::make_unique<ShaderEffect>(
                device, kSkinnedCasterVertexSource, kCasterFragmentSource);
        }
        // Compilation can still fail on a renderer that claims the capability, so the answer is
        // the effect that actually exists and links, not the promise that one could.
        supported_ = casterEffect_ != nullptr && casterEffect_->IsEffectValid();
        if (!supported_)
        {
            CNA::Logger::Info(
                std::string("CNA::Graphics::ShadowMap: shadows are unavailable on this renderer (")
                + (!canRaster ? "it does not raster 3D triangles"
                              : (!canCompile ? "it cannot compile custom effects"
                                             : "the caster shader failed to compile"))
                + "). A shadow pass will leave the map meaning nothing occludes, so the frame "
                  "renders unshadowed rather than failing.",
                CNA::LogCategory::RENDER);
        }
    }

    ShadowMap::~ShadowMap() = default;

    bool ShadowMap::isSupported() const
    {
        return supported_;
    }

    Matrix ShadowMap::computeLightView(const DirectionalLightEXT& light, const BoundingBox& sceneBounds)
    {
        const Vector3 direction = Normalized(light.Direction);
        const Vector3 centre((sceneBounds.Min.X + sceneBounds.Max.X) * 0.5f,
                             (sceneBounds.Min.Y + sceneBounds.Max.Y) * 0.5f,
                             (sceneBounds.Min.Z + sceneBounds.Max.Z) * 0.5f);

        const Vector3 extents(sceneBounds.Max.X - sceneBounds.Min.X,
                              sceneBounds.Max.Y - sceneBounds.Min.Y,
                              sceneBounds.Max.Z - sceneBounds.Min.Z);
        const float radius = 0.5f * std::sqrt(extents.X * extents.X + extents.Y * extents.Y
                                              + extents.Z * extents.Z);
        // Stand far enough back to contain the scene whatever its orientation. A degenerate box
        // (a single point) still needs a non-zero distance, or the view matrix is undefined.
        const float distance = std::max(radius, 1.0f) * 2.0f;

        const Vector3 eye(centre.X - direction.X * distance,
                          centre.Y - direction.Y * distance,
                          centre.Z - direction.Z * distance);

        // Any up vector works except one parallel to the light; a straight-down sun is the common
        // case that breaks the obvious choice, so it is handled rather than left to chance.
        const Vector3 up = std::abs(direction.Y) > 0.99f ? Vector3(0.0f, 0.0f, 1.0f)
                                                         : Vector3(0.0f, 1.0f, 0.0f);
        return Matrix::CreateLookAt(eye, centre, up);
    }

    Matrix ShadowMap::computeLightProjection(const Matrix& lightView, const BoundingBox& sceneBounds)
    {
        // Fit to the scene's eight corners *in light space*: fitting to the world-space box
        // instead would size the volume for an axis-aligned box that the light does not see
        // axis-aligned, wasting resolution in proportion to how far the light is off-axis.
        const std::array<Vector3, 8> corners{{
            {sceneBounds.Min.X, sceneBounds.Min.Y, sceneBounds.Min.Z},
            {sceneBounds.Max.X, sceneBounds.Min.Y, sceneBounds.Min.Z},
            {sceneBounds.Min.X, sceneBounds.Max.Y, sceneBounds.Min.Z},
            {sceneBounds.Max.X, sceneBounds.Max.Y, sceneBounds.Min.Z},
            {sceneBounds.Min.X, sceneBounds.Min.Y, sceneBounds.Max.Z},
            {sceneBounds.Max.X, sceneBounds.Min.Y, sceneBounds.Max.Z},
            {sceneBounds.Min.X, sceneBounds.Max.Y, sceneBounds.Max.Z},
            {sceneBounds.Max.X, sceneBounds.Max.Y, sceneBounds.Max.Z},
        }};

        float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
        float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
        for (const Vector3& corner : corners)
        {
            const Vector3 inLightSpace = Vector3::Transform(corner, lightView);
            minX = std::min(minX, inLightSpace.X);
            maxX = std::max(maxX, inLightSpace.X);
            minY = std::min(minY, inLightSpace.Y);
            maxY = std::max(maxY, inLightSpace.Y);
            minZ = std::min(minZ, inLightSpace.Z);
            maxZ = std::max(maxZ, inLightSpace.Z);
        }

        // The view looks down -Z, so light-space Z is negative in front of the light; the
        // near/far planes are distances and therefore the negated bounds, swapped.
        const float nearPlane = std::max(0.01f, -maxZ);
        const float farPlane  = std::max(nearPlane + 0.02f, -minZ);

        // A degenerate box would produce a zero-width volume and an undefined projection.
        const float padX = std::max(0.01f, (maxX - minX) * 0.001f);
        const float padY = std::max(0.01f, (maxY - minY) * 0.001f);

        return Matrix::CreateOrthographicOffCenter(minX - padX, maxX + padX,
                                                   minY - padY, maxY + padY,
                                                   nearPlane, farPlane);
    }

    void ShadowMap::begin(const DirectionalLightEXT& light, const BoundingBox& sceneBounds)
    {
        if (passOpen_)
            throw std::logic_error("CNA::Graphics::ShadowMap::begin: a shadow pass is already open");

        const Matrix view       = computeLightView(light, sceneBounds);
        const Matrix projection = computeLightProjection(view, sceneBounds);
        lightViewProjection_    = view * projection;

        // The pass counts as open only once the target is bound and cleared -- the same correction
        // CubeShadowMap needed (plan_modern.md MOD-1697). Marking it open first meant that a
        // renderer refusing the bind left every later begin() reporting "already open", turning one
        // unsupported pass into an object that could never be used again.
        try
        {
            device_.SetRenderTarget(target_.get());
            // Cleared to white, meaning "nothing here, and it is infinitely far away". Clearing to
            // black would mean every unwritten texel reads as the nearest possible occluder, and
            // the whole scene would be in shadow wherever no caster was drawn.
            device_.Clear(Color::White);

            if (supported_)
            {
                passOpen_ = true;   // applyCaster refuses unless a pass is open
                applyCaster();
            }
        }
        catch (...)
        {
            passOpen_ = false;
            try { device_.SetRenderTarget(nullptr); } catch (...) { /* best-effort cleanup */ }
            throw;
        }

        passOpen_ = true;
    }

    void ShadowMap::applyCaster()
    {
        if (!passOpen_)
            throw std::logic_error(
                "CNA::Graphics::ShadowMap::applyCaster: no shadow pass is open");
        if (!supported_)
            return;

        casterEffect_->Apply();
        casterEffect_->SetUniformMat4("uLightViewProjection", &lightViewProjection_.M11);
        const Matrix identity = Matrix::getIdentityProperty();
        casterEffect_->SetUniformMat4("uWorld", &identity.M11);
    }

    void ShadowMap::applySkinnedCaster(const std::vector<Matrix>& boneTransforms,
                                       const int weightsPerVertex)
    {
        if (!passOpen_)
            throw std::logic_error(
                "CNA::Graphics::ShadowMap::applySkinnedCaster: no shadow pass is open");
        if (weightsPerVertex != 1 && weightsPerVertex != 2 && weightsPerVertex != 4)
            throw std::invalid_argument(
                "CNA::Graphics::ShadowMap::applySkinnedCaster: weightsPerVertex must be 1, 2 or 4");
        if (boneTransforms.empty() || boneTransforms.size() > kMaxCasterBones)
            throw std::invalid_argument(
                "CNA::Graphics::ShadowMap::applySkinnedCaster: the bone palette must hold between "
                "1 and 72 matrices");
        if (!supported_)
            return;

        skinnedCasterEffect_->Apply();
        skinnedCasterEffect_->SetUniformMat4("uLightViewProjection", &lightViewProjection_.M11);
        const Matrix identity = Matrix::getIdentityProperty();
        skinnedCasterEffect_->SetUniformMat4("uWorld", &identity.M11);
        // Matrix's own storage is the contiguous column-major block the uniform expects, so the
        // palette goes up as one upload rather than 72.
        skinnedCasterEffect_->SetUniformMat4Array("uBones", &boneTransforms.front().M11,
                                                  static_cast<int>(boneTransforms.size()));
        skinnedCasterEffect_->SetUniformInt("uWeightsPerVertex", weightsPerVertex);
    }

    void ShadowMap::end()
    {
        if (!passOpen_)
            throw std::logic_error("CNA::Graphics::ShadowMap::end: no shadow pass is open");
        passOpen_ = false;
        device_.SetRenderTarget(nullptr);
    }

    ShaderEffect* ShadowMap::getCasterEffect() const
    {
        return supported_ ? casterEffect_.get() : nullptr;
    }

    ShaderEffect* ShadowMap::getSkinnedCasterEffect() const
    {
        return supported_ && skinnedCasterEffect_ != nullptr
                       && skinnedCasterEffect_->IsEffectValid()
                   ? skinnedCasterEffect_.get()
                   : nullptr;
    }

    Texture2D* ShadowMap::getShadowTexture() const
    {
        return target_.get();
    }

    Matrix ShadowMap::getLightViewProjection() const
    {
        return lightViewProjection_;
    }

    int ShadowMap::getSize() const
    {
        return size_;
    }

    ShadowQuality ShadowMap::getQuality() const
    {
        return quality_;
    }

    float ShadowMap::getDepthBias() const
    {
        return depthBias_;
    }

    void ShadowMap::setDepthBias(const float value)
    {
        depthBias_ = value;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
