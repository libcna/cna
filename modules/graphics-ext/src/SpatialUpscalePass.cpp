// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SpatialUpscalePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    SpatialUpscalePass::SpatialUpscalePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateSpatialUpscaleShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "SpatialUpscalePass", effect_.get(), logged);
        supported_ = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                  && effect_ != nullptr && effect_->IsEffectValid();
    }

    SpatialUpscalePass::~SpatialUpscalePass() = default;

    bool SpatialUpscalePass::isSupported() const { return supported_; }

    bool SpatialUpscalePass::isIdentityScale(const int sourceWidth, const int sourceHeight,
                                             const int targetWidth, const int targetHeight)
    {
        return sourceWidth == targetWidth && sourceHeight == targetHeight;
    }

    void SpatialUpscalePass::draw(Texture2D* source, const int sourceWidth, const int sourceHeight,
                                  const int targetWidth, const int targetHeight)
    {
        if (source == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::SpatialUpscalePass::draw: there is nothing to upscale");
        if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::SpatialUpscalePass::draw: every dimension must be positive");
        if (!supported_)
        {
            fullscreen_->drawOverCurrentTarget(source, nullptr, targetWidth, targetHeight);
            return;
        }

        const bool identity = isIdentityScale(sourceWidth, sourceHeight, targetWidth, targetHeight);

        effect_->Apply();
        // A 1:1 draw sharpens nothing either: the pass is asked to change nothing, and a sharpen is
        // a change.
        effect_->SetUniformVec4("uUpscaleParams", static_cast<float>(sourceWidth),
                                static_cast<float>(sourceHeight),
                                identity ? 0.0f : sharpness_, edgeAdaptive_ ? 1.0f : 0.0f);
        effect_->SetUniformFloat("uIdentity", identity ? 1.0f : 0.0f);

        fullscreen_->drawOverCurrentTarget(source, effect_.get(), targetWidth, targetHeight);
    }

    float SpatialUpscalePass::getSharpness() const { return sharpness_; }
    void  SpatialUpscalePass::setSharpness(const float value)
    {
        sharpness_ = std::clamp(value, 0.0f, 1.0f);
    }

    bool SpatialUpscalePass::isEdgeAdaptive() const { return edgeAdaptive_; }
    void SpatialUpscalePass::setEdgeAdaptive(const bool value) { edgeAdaptive_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
