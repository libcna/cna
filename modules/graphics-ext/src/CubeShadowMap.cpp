// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/CubeShadowMap.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::MathHelper;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::CubeMapFace;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
    using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    namespace {

        /// The caster. Unlike the directional one it writes *linear distance from the light over
        /// the light's range*, not projected depth -- see the class comment for why that is the
        /// only value a receiver can compare against without knowing which face it came from.
        constexpr const char* kCasterVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uFaceViewProjection;
uniform mat4 uWorld;
out vec3 vWorldPos;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uFaceViewProjection * world;
}
)";

        constexpr const char* kCasterFragmentSource = R"(#version 300 es
precision highp float;
in vec3 vWorldPos;
uniform vec3 uLightPosition;
uniform float uLightRange;
out vec4 FragColor;
void main() {
    // Clamped, because a caster beyond the range is still rasterized where it overlaps the near
    // part of a face, and a stored value above 1 would read as "further than infinitely far".
    float distance = clamp(length(vWorldPos - uLightPosition) / uLightRange, 0.0, 1.0);
    FragColor = vec4(distance, distance, distance, 1.0);
}
)";

    } // namespace

    int CubeShadowMap::sizeForQuality(const ShadowQuality quality)
    {
        return std::min(ShadowMap::sizeForQuality(quality), 1024);
    }

    Matrix CubeShadowMap::computeFaceView(const CubeMapFace face, const Vector3& position)
    {
        // The cube-map face orientations. The two Y faces use a Z-axis up vector because their own
        // view direction is the Y axis; the other four use -Y, which is what makes a cube map's
        // faces line up as one continuous image rather than four mirrored ones.
        Vector3 forward(1.0f, 0.0f, 0.0f);
        Vector3 up(0.0f, -1.0f, 0.0f);
        switch (face)
        {
        case CubeMapFace::PositiveX: forward = Vector3( 1.0f,  0.0f,  0.0f); break;
        case CubeMapFace::NegativeX: forward = Vector3(-1.0f,  0.0f,  0.0f); break;
        case CubeMapFace::PositiveY: forward = Vector3( 0.0f,  1.0f,  0.0f);
                                     up      = Vector3( 0.0f,  0.0f,  1.0f); break;
        case CubeMapFace::NegativeY: forward = Vector3( 0.0f, -1.0f,  0.0f);
                                     up      = Vector3( 0.0f,  0.0f, -1.0f); break;
        case CubeMapFace::PositiveZ: forward = Vector3( 0.0f,  0.0f,  1.0f); break;
        case CubeMapFace::NegativeZ: forward = Vector3( 0.0f,  0.0f, -1.0f); break;
        }
        const Vector3 target(position.X + forward.X, position.Y + forward.Y,
                             position.Z + forward.Z);
        return Matrix::CreateLookAt(position, target, up);
    }

    Matrix CubeShadowMap::computeFaceProjection(const float range)
    {
        // 90 degrees and square, because six of them have to tile a sphere exactly. A near plane
        // that is a fraction of the range rather than a constant keeps the depth buffer usable for
        // a small light without clipping geometry close to a large one.
        const float nearPlane = std::max(range * 0.005f, 0.01f);
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, nearPlane,
                                                    std::max(range, nearPlane + 0.01f));
    }

    CubeShadowMap::CubeShadowMap(GraphicsDevice& device, const ShadowQuality quality)
        : device_(device), quality_(quality), size_(sizeForQuality(quality))
    {
        const SurfaceFormat format =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)
                ? SurfaceFormat::Single
                : SurfaceFormat::Color;

        // PreserveContents: the six faces are six passes against one resource, and the default
        // would discard the five already drawn each time another is bound.
        cube_ = std::make_unique<RenderTargetCube>(device, size_, false, format,
                                                    DepthFormat::Depth24, 0,
                                                    RenderTargetUsage::PreserveContents);

        const bool canRaster  = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        // Two questions, not one (plan_modern.md `MOD-1699`): `CustomEffects` only means the
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
                "CNA::Graphics::CubeShadowMap: point shadows are unavailable on this renderer. "
                "Face passes still open and close, and the cube keeps meaning nothing occludes, "
                "so the frame renders unshadowed rather than failing.",
                CNA::LogCategory::RENDER);
        }

        for (Matrix& matrix : faceViewProjection_)
            matrix = Matrix::getIdentityProperty();
    }

    CubeShadowMap::~CubeShadowMap() = default;

    bool CubeShadowMap::isSupported() const { return supported_; }

    int CubeShadowMap::getSize() const { return size_; }

    ShadowQuality CubeShadowMap::getQuality() const { return quality_; }

    Vector3 CubeShadowMap::getLightPosition() const { return lightPosition_; }

    float CubeShadowMap::getLightRange() const { return lightRange_; }

    float CubeShadowMap::getDepthBias() const { return depthBias_; }

    void CubeShadowMap::setDepthBias(const float value) { depthBias_ = value; }

    TextureCube* CubeShadowMap::getShadowTexture() const { return cube_.get(); }

    ShaderEffect* CubeShadowMap::getCasterEffect() const
    {
        return supported_ ? casterEffect_.get() : nullptr;
    }

    void CubeShadowMap::update(const PointLightEXT& light)
    {
        if (passOpen_)
            throw std::logic_error("CNA::Graphics::CubeShadowMap::update: a face pass is open");
        if (!(light.Range > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::CubeShadowMap::update: the light's range must be positive -- the "
                "stored distance is divided by it");

        lightPosition_ = light.Position;
        lightRange_    = light.Range;

        const Matrix projection = computeFaceProjection(light.Range);
        for (int i = 0; i < kFaceCount; ++i)
            faceViewProjection_[i] =
                computeFaceView(static_cast<CubeMapFace>(i), light.Position) * projection;

        updated_ = true;
    }

    void CubeShadowMap::begin(const int faceIndex)
    {
        if (passOpen_)
            throw std::logic_error("CNA::Graphics::CubeShadowMap::begin: a face pass is already open");
        if (!updated_)
            throw std::logic_error(
                "CNA::Graphics::CubeShadowMap::begin: update() must run first -- there are no face "
                "matrices to render with yet");
        if (faceIndex < 0 || faceIndex >= kFaceCount)
            throw std::out_of_range("CNA::Graphics::CubeShadowMap::begin: no such face");

        // The pass counts as open only once the face is actually bound and cleared. A renderer that
        // refuses cube render targets throws out of the calls below, and marking the pass open
        // first would leave every later begin() reporting "already open" -- one unsupported face
        // turning into an object that can never be used again.
        try
        {
            device_.SetRenderTarget(cube_.get(), static_cast<CubeMapFace>(faceIndex));
            // White is "nothing here, at the far end of the range" -- the same convention the 2D
            // map uses, and the reason an undrawn face leaves the world lit rather than in shadow.
            device_.Clear(Color::White);

            if (supported_)
            {
                casterEffect_->Apply();
                casterEffect_->SetUniformMat4("uFaceViewProjection",
                                              &faceViewProjection_[faceIndex].M11);
                const Matrix identity = Matrix::getIdentityProperty();
                casterEffect_->SetUniformMat4("uWorld", &identity.M11);
                casterEffect_->SetUniformVec3("uLightPosition", lightPosition_.X, lightPosition_.Y,
                                              lightPosition_.Z);
                casterEffect_->SetUniformFloat("uLightRange", lightRange_);
            }
        }
        catch (...)
        {
            try
            {
                device_.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr),
                                        CubeMapFace::PositiveX);
            }
            catch (...)
            {
                // Unbinding is best-effort cleanup; the original failure is the one worth reporting.
            }
            throw;
        }

        passOpen_ = true;
    }

    void CubeShadowMap::end()
    {
        if (!passOpen_)
            throw std::logic_error("CNA::Graphics::CubeShadowMap::end: no face pass is open");
        passOpen_ = false;
        device_.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
