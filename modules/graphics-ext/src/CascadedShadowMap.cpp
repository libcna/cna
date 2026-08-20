// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/CascadedShadowMap.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Viewport;

    namespace {

        /// The same caster `ShadowMap` uses, and for the same reason: the atlas is an ordinary
        /// colour target holding light-space distance, because CNA cannot sample a depth
        /// attachment as a texture on every renderer.
        constexpr const char* kCasterVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uLightViewProjection;
uniform mat4 uWorld;
out float vDistance;
void main() {
    vec4 lightSpace = uLightViewProjection * uWorld * vec4(aPosition, 1.0);
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

        Vector3 TransformCoordinate(const Vector3& point, const Matrix& m)
        {
            const float x = point.X * m.M11 + point.Y * m.M21 + point.Z * m.M31 + m.M41;
            const float y = point.X * m.M12 + point.Y * m.M22 + point.Z * m.M32 + m.M42;
            const float z = point.X * m.M13 + point.Y * m.M23 + point.Z * m.M33 + m.M43;
            const float w = point.X * m.M14 + point.Y * m.M24 + point.Z * m.M34 + m.M44;
            const float inverseW = std::abs(w) > 1e-9f ? 1.0f / w : 1.0f;
            return Vector3(x * inverseW, y * inverseW, z * inverseW);
        }

        /// Takes a clip-space position to the cascade's slice of the atlas: the usual
        /// NDC-to-UV half-scale, narrowed to 1/count in X and shifted onto the right slice.
        Matrix AtlasSubRectangle(int cascadeIndex, int cascadeCount)
        {
            const float scale = 1.0f / static_cast<float>(cascadeCount);
            Matrix m = Matrix::getIdentityProperty();
            m.M11 = 0.5f * scale;
            m.M22 = 0.5f;
            m.M33 = 0.5f;
            m.M41 = (0.5f + static_cast<float>(cascadeIndex)) * scale;
            m.M42 = 0.5f;
            m.M43 = 0.5f;
            return m;
        }

    } // namespace

    std::vector<float> CascadedShadowMap::computeSplitDistances(
        const float nearPlane, const float farPlane, const int cascadeCount, float lambda)
    {
        if (!(nearPlane > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::CascadedShadowMap::computeSplitDistances: the near plane must be "
                "positive -- the logarithmic term is undefined at zero");
        if (!(farPlane > nearPlane))
            throw std::invalid_argument(
                "CNA::Graphics::CascadedShadowMap::computeSplitDistances: the far plane must "
                "exceed the near plane");
        if (cascadeCount < 2 || cascadeCount > kMaxCascades)
            throw std::invalid_argument(
                "CNA::Graphics::CascadedShadowMap::computeSplitDistances: cascadeCount must be "
                "between 2 and 4");

        lambda = std::clamp(lambda, 0.0f, 1.0f);
        const float ratio = farPlane / nearPlane;
        const float range = farPlane - nearPlane;

        std::vector<float> splits;
        splits.reserve(static_cast<std::size_t>(cascadeCount));
        for (int i = 1; i <= cascadeCount; ++i)
        {
            const float fraction    = static_cast<float>(i) / static_cast<float>(cascadeCount);
            const float logarithmic = nearPlane * std::pow(ratio, fraction);
            const float uniform     = nearPlane + range * fraction;
            splits.push_back(lambda * logarithmic + (1.0f - lambda) * uniform);
        }
        // The last split is the far plane by definition; the blend above lands on it to within
        // float rounding, and a cascade that stops a hair short would leave a sliver unshadowed.
        splits.back() = farPlane;
        return splits;
    }

    std::array<Vector3, 8> CascadedShadowMap::computeFrustumCorners(const Matrix& view,
                                                                    const Matrix& projection)
    {
        const Matrix inverse = Matrix::Invert(view * projection);
        // Z runs 0..1, not -1..1. XNA's projection matrices are the Direct3D ones, so the near
        // plane lands on 0 and the far plane on 1 -- taking the GL convention here would put the
        // "near" corners half way to the camera and shrink every cascade toward it.
        const std::array<Vector3, 8> ndc{{
            {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f}, {1.0f,  1.0f, 0.0f},
            {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},
            {-1.0f,  1.0f, 1.0f}, {1.0f,  1.0f, 1.0f},
        }};

        std::array<Vector3, 8> corners{};
        for (std::size_t i = 0; i < corners.size(); ++i)
            corners[i] = TransformCoordinate(ndc[i], inverse);
        return corners;
    }

    float CascadedShadowMap::computeBoundingSphere(const std::array<Vector3, 8>& corners,
                                                    Vector3& centre)
    {
        Vector3 sum(0.0f, 0.0f, 0.0f);
        for (const Vector3& corner : corners)
        {
            sum.X += corner.X;
            sum.Y += corner.Y;
            sum.Z += corner.Z;
        }
        centre = Vector3(sum.X / 8.0f, sum.Y / 8.0f, sum.Z / 8.0f);

        float radius = 0.0f;
        for (const Vector3& corner : corners)
        {
            const float dx = corner.X - centre.X;
            const float dy = corner.Y - centre.Y;
            const float dz = corner.Z - centre.Z;
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        // A degenerate frustum would give a zero-radius sphere and an undefined projection.
        return std::max(radius, 1e-3f);
    }

    Vector3 CascadedShadowMap::snapToTexelGrid(const Vector3& centre, const float radius,
                                                const int cascadeSize)
    {
        if (cascadeSize <= 0 || !(radius > 0.0f))
            return centre;
        const float worldUnitsPerTexel = 2.0f * radius / static_cast<float>(cascadeSize);
        return Vector3(std::floor(centre.X / worldUnitsPerTexel) * worldUnitsPerTexel,
                       std::floor(centre.Y / worldUnitsPerTexel) * worldUnitsPerTexel,
                       // Z is not snapped: the depth range is not sampled on a texel grid, so
                       // quantizing it would only add a bias that varies with the camera.
                       centre.Z);
    }

    CascadedShadowMap::CascadedShadowMap(GraphicsDevice& device, const ShadowQuality quality,
                                          const int cascadeCount)
        : device_(device), quality_(quality), cascadeCount_(cascadeCount)
    {
        if (cascadeCount < 2 || cascadeCount > kMaxCascades)
            throw std::invalid_argument(
                "CNA::Graphics::CascadedShadowMap: cascadeCount must be between 2 and 4");

        // Each cascade gets what a single ShadowMap of this quality would get, so the quality
        // table means the same thing in both places and an atlas of four High cascades is four
        // times the memory of one High map rather than a quarter of the resolution.
        cascadeSize_ = ShadowMap::sizeForQuality(quality);

        const SurfaceFormat format =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)
                ? SurfaceFormat::Single
                : SurfaceFormat::Color;

        // PreserveContents, not the default. Each cascade is a separate pass that binds the same
        // target, and DiscardContents means exactly what it says: binding it for cascade 1 throws
        // away cascade 0. The symptom is an atlas holding only whichever cascade was drawn last,
        // which still renders and still looks like a shadow map.
        atlas_ = std::make_unique<RenderTarget2D>(device, cascadeSize_ * cascadeCount_,
                                                   cascadeSize_, false, format,
                                                   DepthFormat::Depth24, 0,
                                                   RenderTargetUsage::PreserveContents);

        const bool canRaster  = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        // Two questions, not one (plans/plan_modern.md `MOD-1699`): `CustomEffects` only means the
        // renderer *accepts* an effect. SOFTWARE and HEADLESS accept any shader source and go
        // on rendering with their own fixed path, so a caster that believed them would report
        // a working shadow map while writing depth nothing had shaded.
        const bool canCompile = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                             && device.ExecutesShaderEffectSourceEXT();
        if (canRaster && canCompile)
            casterEffect_ = std::make_unique<ShaderEffect>(device, kCasterVertexSource,
                                                           kCasterFragmentSource);
        supported_ = casterEffect_ != nullptr && casterEffect_->IsEffectValid();
        if (!supported_)
        {
            CNA::Logger::Info(
                "CNA::Graphics::CascadedShadowMap: cascades are unavailable on this renderer. "
                "Passes still open and close, and the atlas keeps meaning nothing occludes, so "
                "the frame renders unshadowed rather than failing.",
                CNA::LogCategory::RENDER);
        }

        for (int i = 0; i < cascadeCount_; ++i)
            cascades_[static_cast<std::size_t>(i)].matrix = Matrix::getIdentityProperty();
    }

    CascadedShadowMap::~CascadedShadowMap() = default;

    bool CascadedShadowMap::isSupported() const { return supported_; }

    int CascadedShadowMap::getCascadeCount() const { return cascadeCount_; }

    int CascadedShadowMap::getCascadeSize() const { return cascadeSize_; }

    Texture2D* CascadedShadowMap::getShadowTexture() const { return atlas_.get(); }

    ShaderEffect* CascadedShadowMap::getCasterEffect() const
    {
        return supported_ ? casterEffect_.get() : nullptr;
    }

    float CascadedShadowMap::getBlendBand() const { return blendBand_; }

    void CascadedShadowMap::setBlendBand(const float band)
    {
        blendBand_ = std::max(0.0f, band);
    }

    bool CascadedShadowMap::isDebugTintEnabled() const { return debugTint_; }

    void CascadedShadowMap::setDebugTintEnabled(const bool enabled) { debugTint_ = enabled; }

    void CascadedShadowMap::applyToReceiver(
        Microsoft::Xna::Framework::Graphics::IShadowReceiverEXT& receiver) const
    {
        if (!updated_)
            throw std::logic_error(
                "CNA::Graphics::CascadedShadowMap::applyToReceiver: update() must run first -- "
                "there are no cascade matrices to give");

        Microsoft::Xna::Framework::Graphics::ShadowCascadeStateEXT state;
        state.Count      = cascadeCount_;
        state.CameraView = cameraView_;
        state.BlendBand  = blendBand_;
        state.DebugTint  = debugTint_;
        for (int i = 0; i < cascadeCount_; ++i)
        {
            state.WorldToAtlas[i]  = cascades_[static_cast<std::size_t>(i)].matrix;
            state.SplitDistance[i] = cascades_[static_cast<std::size_t>(i)].splitDistance;
        }

        receiver.setShadowMapEXT(atlas_.get());
        receiver.setShadowCascadesEXT(state);
        receiver.setShadowFilterRadiusEXT(ShadowMap::filterRadiusForQuality(quality_));
    }

    int CascadedShadowMap::selectCascade(const float viewDepth) const
    {
        for (int i = 0; i < cascadeCount_; ++i)
            if (viewDepth <= cascades_[static_cast<std::size_t>(i)].splitDistance)
                return i;
        return cascadeCount_ - 1;
    }

    float CascadedShadowMap::getSplitLambda() const { return splitLambda_; }

    void CascadedShadowMap::setSplitLambda(const float lambda)
    {
        splitLambda_ = std::clamp(lambda, 0.0f, 1.0f);
    }

    Matrix CascadedShadowMap::getCascadeMatrix(const int cascadeIndex) const
    {
        if (cascadeIndex < 0 || cascadeIndex >= cascadeCount_)
            throw std::out_of_range(
                "CNA::Graphics::CascadedShadowMap::getCascadeMatrix: no such cascade");
        return cascades_[static_cast<std::size_t>(cascadeIndex)].matrix;
    }

    float CascadedShadowMap::getSplitDistance(const int cascadeIndex) const
    {
        if (cascadeIndex < 0 || cascadeIndex >= cascadeCount_)
            throw std::out_of_range(
                "CNA::Graphics::CascadedShadowMap::getSplitDistance: no such cascade");
        return cascades_[static_cast<std::size_t>(cascadeIndex)].splitDistance;
    }

    void CascadedShadowMap::update(const DirectionalLightEXT& light, const Matrix& cameraView,
                                    const Matrix& cameraProjection)
    {
        if (passOpen_)
            throw std::logic_error(
                "CNA::Graphics::CascadedShadowMap::update: a cascade pass is open");

        // The camera's own near and far, recovered from its projection rather than asked for
        // again: two sources for the same number is two chances for them to disagree, and a
        // cascade set fitted to a range the camera does not use is invisible until it is not.
        const std::array<Vector3, 8> viewCorners =
            computeFrustumCorners(Matrix::getIdentityProperty(), cameraProjection);
        float nearPlane = 1e30f;
        float farPlane  = -1e30f;
        for (const Vector3& corner : viewCorners)
        {
            // View space looks down -Z, so a distance in front of the camera is -Z.
            nearPlane = std::min(nearPlane, -corner.Z);
            farPlane  = std::max(farPlane, -corner.Z);
        }
        nearPlane = std::max(nearPlane, 1e-3f);
        farPlane  = std::max(farPlane, nearPlane + 1e-3f);

        const std::vector<float> splits =
            computeSplitDistances(nearPlane, farPlane, cascadeCount_, splitLambda_);

        const Vector3 direction = Normalized(light.Direction);
        const Matrix inverseView = Matrix::Invert(cameraView);

        float previousSplit = nearPlane;
        for (int i = 0; i < cascadeCount_; ++i)
        {
            const float thisSplit = splits[static_cast<std::size_t>(i)];

            // The cascade's own slice of the camera frustum, built in view space and moved to the
            // world by the inverse view. Building it from a sub-projection instead would mean
            // reconstructing a projection matrix for a range, which is the same arithmetic with
            // more chances to get the handedness wrong.
            std::array<Vector3, 8> corners{};
            for (int c = 0; c < 8; ++c)
            {
                // Always one of the four NEAR corners, even for the far four: each corner's X and
                // Y scale linearly with distance under a perspective projection, so the corner at
                // `depth` is a near corner scaled by depth/near. Taking the far corners as the
                // basis for the far four would apply that ratio twice and fit each cascade to a
                // volume tens of times too large -- which still renders, with every cascade
                // covered edge to edge by the first caster drawn into it.
                const Vector3& source = viewCorners[static_cast<std::size_t>(c % 4)];
                const float depth = (c < 4) ? previousSplit : thisSplit;
                const float scale = depth / std::max(nearPlane, 1e-6f);
                const Vector3 inView(source.X * scale, source.Y * scale, -depth);
                corners[static_cast<std::size_t>(c)] = TransformCoordinate(inView, inverseView);
            }

            Vector3 centre(0.0f, 0.0f, 0.0f);
            const float radius = computeBoundingSphere(corners, centre);

            // Look at the sphere from far enough back that the whole scene in front of it still
            // casts into this cascade -- twice the radius past it, matching ShadowMap's own margin.
            const Vector3 eye(centre.X - direction.X * radius * 2.0f,
                              centre.Y - direction.Y * radius * 2.0f,
                              centre.Z - direction.Z * radius * 2.0f);
            const Vector3 up = std::abs(direction.Y) > 0.99f ? Vector3(0.0f, 0.0f, 1.0f)
                                                             : Vector3(0.0f, 1.0f, 0.0f);
            const Matrix lightView = Matrix::CreateLookAt(eye, centre, up);

            // MOD-904: snap in light space, where a texel is an axis-aligned square. Snapping the
            // world-space centre instead would quantize along axes the map is not aligned to and
            // leave the crawl it is meant to remove.
            const Vector3 centreInLight = TransformCoordinate(centre, lightView);
            const Vector3 snapped = snapToTexelGrid(centreInLight, radius, cascadeSize_);
            const float offsetX = snapped.X - centreInLight.X;
            const float offsetY = snapped.Y - centreInLight.Y;

            const Matrix lightProjection = Matrix::CreateOrthographicOffCenter(
                -radius + offsetX, radius + offsetX,
                -radius + offsetY, radius + offsetY,
                0.0f, radius * 4.0f);

            Cascade& cascade = cascades_[static_cast<std::size_t>(i)];
            cascade.radius        = radius;
            cascade.splitDistance = thisSplit;
            cascade.matrix        = lightView * lightProjection
                                  * AtlasSubRectangle(i, cascadeCount_);
            previousSplit = thisSplit;
        }

        cameraView_   = cameraView;
        updated_      = true;
        atlasCleared_ = false;
    }

    void CascadedShadowMap::begin(const int cascadeIndex)
    {
        if (passOpen_)
            throw std::logic_error(
                "CNA::Graphics::CascadedShadowMap::begin: a cascade pass is already open");
        if (!updated_)
            throw std::logic_error(
                "CNA::Graphics::CascadedShadowMap::begin: update() must run first -- there are no "
                "cascade matrices to render with yet");
        if (cascadeIndex < 0 || cascadeIndex >= cascadeCount_)
            throw std::out_of_range("CNA::Graphics::CascadedShadowMap::begin: no such cascade");

        passOpen_    = true;
        openCascade_ = cascadeIndex;

        device_.SetRenderTarget(atlas_.get());
        if (!atlasCleared_)
        {
            // Once per frame, not once per cascade: a clear covers the whole target, so clearing
            // inside each pass would erase the cascades already drawn.
            device_.Clear(Color::White);
            atlasCleared_ = true;
        }

        Viewport viewport(cascadeIndex * cascadeSize_, 0, cascadeSize_, cascadeSize_);
        device_.setViewportProperty(viewport);

        if (supported_)
        {
            // The atlas sub-rectangle is baked into the cascade matrix, which is what the receiver
            // samples with -- but the caster must render into clip space, not into atlas UV space,
            // so its own matrix is the same product without that last factor.
            const Matrix casterMatrix =
                cascades_[static_cast<std::size_t>(cascadeIndex)].matrix
                * Matrix::Invert(AtlasSubRectangle(cascadeIndex, cascadeCount_));
            casterEffect_->Apply();
            casterEffect_->SetUniformMat4("uLightViewProjection", &casterMatrix.M11);
            const Matrix identity = Matrix::getIdentityProperty();
            casterEffect_->SetUniformMat4("uWorld", &identity.M11);
        }
    }

    void CascadedShadowMap::end()
    {
        if (!passOpen_)
            throw std::logic_error("CNA::Graphics::CascadedShadowMap::end: no cascade pass is open");
        passOpen_    = false;
        openCascade_ = -1;
        device_.SetRenderTarget(nullptr);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
