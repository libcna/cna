// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    // XNA has no public SpriteEffect; this is FNA's (StockEffects/SpriteEffect.cs), whose
    // compiled SpriteEffect.fx declares a single float4x4 MatrixTransform (owner decision
    // 2026-11-04, TQ-035).
    SpriteEffect::SpriteEffect(GraphicsDevice& device)
        : Effect(device)
    {
        AddStockParameterInternal(getParametersProperty(), "MatrixTransform", 4, 4,
                                  EffectParameterClass::Matrix, EffectParameterType::Single);
        CacheEffectParameters();
    }

    // FNA SpriteEffect(SpriteEffect) is `: base(cloneSource)`, so the clone starts with the
    // source's current MatrixTransform in storage of its own.
    SpriteEffect::SpriteEffect(const SpriteEffect& cloneSource)
        : Effect(*cloneSource.device_)
    {
        AddStockParameterInternal(getParametersProperty(), "MatrixTransform", 4, 4,
                                  EffectParameterClass::Matrix, EffectParameterType::Single);
        CacheEffectParameters();
        matrixParam_->SetValue(cloneSource.matrixParam_->GetValueMatrix());
    }

    Effect* SpriteEffect::Clone()
    {
        ThrowIfDisposedForCloneInternal();
        return new SpriteEffect(*this);
    }

    void SpriteEffect::CacheEffectParameters()
    {
        matrixParam_ = getParametersProperty()["MatrixTransform"];
    }

    void SpriteEffect::OnApply()
    {
        // FNA SpriteEffect.OnApply: halfPixelOffset * CreateOrthographicOffCenter(0, viewport
        // width, viewport height, 0, 0, 1) -- FNA's matrix, M33 = -1 included.
        if (!device_ || !matrixParam_) return;

        const Viewport& viewport = device_->getViewportProperty();
        Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f,
            static_cast<float>(viewport.getWidthProperty()),
            static_cast<float>(viewport.getHeightProperty()),
            0.0f,
            0.0f,
            1.0f);
        Matrix halfPixelOffset = Matrix::CreateTranslation(-0.5f, -0.5f, 0.0f);
        matrixParam_->SetValue(halfPixelOffset * projection);
    }

    const std::string& SpriteEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.SpriteEffect";
        return name;
    }
}
