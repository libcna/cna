// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DecalPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;        // the prepass depth
uniform sampler2D uDecalSampler;
uniform sampler2D uNormalSampler;
uniform mat4  uInverseProjection;
uniform mat4  uViewToDecal;
uniform vec3  uDecalAxisView;
uniform vec3  uTint;
uniform float uFarPlane;
uniform float uOpacity;
uniform float uMinFacing;
uniform float uHasNormals;

void main() {
    float depth = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    // Nothing was drawn here, so there is no surface to glue anything to. Painting the far plane
    // is how a decal ends up floating in the sky.
    if (depth >= 0.999) discard;

    vec3 viewPosition = cnaViewPositionFromDepth(TexCoord, depth, uInverseProjection) * uFarPlane;
    vec3 local = (uViewToDecal * vec4(viewPosition, 1.0)).xyz;

    // The box IS the test. A surface behind the decal's far face is outside it, which is what
    // keeps a decal off the wall behind the crate it was meant for.
    if (any(greaterThan(abs(local), vec3(0.5)))) discard;

    if (uHasNormals > 0.5) {
        vec3 normal = normalize(texture(uNormalSampler, TexCoord).xyz * 2.0 - 1.0);
        // The decal projects along its own +Z, so a surface facing it has a normal pointing back
        // along that axis. A surface nearly parallel to the axis is the smear case, and is dropped.
        if (dot(normal, -uDecalAxisView) < uMinFacing) discard;
    }

    vec4 decal = texture(uDecalSampler, local.xy + 0.5);
    FragColor = vec4(decal.rgb * uTint, decal.a * uOpacity);
}
)";

        std::string MakeFragmentSource(const bool packedDepth)
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            source += DepthNormalPrepass::getDepthDecodeGlsl(packedDepth);
            source += kFragmentBody;
            return source;
        }

    } // namespace

    DecalPass::DecalPass(GraphicsDevice& device)
        : device_(device), spriteBatch_(std::make_unique<SpriteBatch>(device))
    {
        const bool packed =
            !device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle);
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, MakeFragmentSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "DecalPass", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid() &&
                     device.ExecutesShaderEffectSourceEXT();
    }

    DecalPass::~DecalPass() = default;

    bool DecalPass::isSupported() const { return supported_; }

    void DecalPass::setPrepassInputs(Texture2D* depth, Texture2D* normals)
    {
        depth_   = depth;
        normals_ = normals;
    }

    void DecalPass::setCamera(const Matrix& view, const Matrix& projection, const float farPlane)
    {
        view_ = view;
        inverseProjection_ = Matrix::Invert(projection);
        if (farPlane > 0.0f) farPlane_ = farPlane;
    }

    bool DecalPass::isInsideDecalBox(const Vector3& decalLocalPosition)
    {
        return std::abs(decalLocalPosition.X) <= 0.5f && std::abs(decalLocalPosition.Y) <= 0.5f &&
               std::abs(decalLocalPosition.Z) <= 0.5f;
    }

    void DecalPass::draw(Texture2D* decal, const Matrix& decalWorld, const int width,
                         const int height)
    {
        if (decal == nullptr)
            throw std::invalid_argument("CNA::Graphics::DecalPass::draw: there is no decal to draw");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::DecalPass::draw: the target size must be positive");
        if (!supported_ || depth_ == nullptr || farPlane_ <= 0.0f) return;

        // View space to the decal's own space, in one matrix: undo the camera, then undo the
        // decal's placement. XNA composes for row vectors, so this reads in the order it is applied.
        const Matrix viewToDecal = Matrix::Invert(view_) * Matrix::Invert(decalWorld);

        // The decal's +Z, expressed in view space -- the direction it projects along. Taken from
        // the rotation of decalWorld * view rather than from decalWorld alone, because the slope
        // test compares it against a normal the prepass wrote in view space.
        const Matrix decalToView = decalWorld * view_;
        Vector3 axis(decalToView.M31, decalToView.M32, decalToView.M33);
        const float axisLength = std::sqrt(axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z);
        if (axisLength > 1e-6f) axis = Vector3(axis.X / axisLength, axis.Y / axisLength,
                                               axis.Z / axisLength);

        effect_->Apply();
        effect_->SetUniformInt("uDecalSampler", 1);
        effect_->SetTexture(1, *decal);
        if (normals_ != nullptr)
        {
            effect_->SetUniformInt("uNormalSampler", 2);
            effect_->SetTexture(2, *normals_);
        }
        effect_->SetUniformMat4("uInverseProjection", &inverseProjection_.M11);
        effect_->SetUniformMat4("uViewToDecal", &viewToDecal.M11);
        effect_->SetUniformVec3("uDecalAxisView", axis.X, axis.Y, axis.Z);
        effect_->SetUniformVec3("uTint", tint_.X, tint_.Y, tint_.Z);
        effect_->SetUniformFloat("uFarPlane", farPlane_);
        effect_->SetUniformFloat("uOpacity", opacity_);
        effect_->SetUniformFloat("uMinFacing", std::cos(maxSlopeAngle_));
        effect_->SetUniformFloat("uHasNormals", normals_ != nullptr ? 1.0f : 0.0f);

        // NonPremultiplied, and not the Opaque every post-process pass uses: a decal composites
        // onto the frame rather than replacing it, and its own alpha is the mask that decides
        // where. This is the one pass in the layer for which blending is the point.
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, nullptr,
                            nullptr, nullptr, effect_.get());
        spriteBatch_->Draw(*depth_, Rectangle(0, 0, width, height), Color::White);
        spriteBatch_->End();
    }

    float DecalPass::getOpacity() const { return opacity_; }
    void  DecalPass::setOpacity(const float value) { opacity_ = std::clamp(value, 0.0f, 1.0f); }

    Vector3 DecalPass::getTint() const { return tint_; }
    void    DecalPass::setTint(const Vector3& value) { tint_ = value; }

    float DecalPass::getMaxSlopeAngle() const { return maxSlopeAngle_; }
    void  DecalPass::setMaxSlopeAngle(const float radians)
    {
        maxSlopeAngle_ = std::clamp(radians, 0.0f, 1.5707964f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
