// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    SpriteEffect::SpriteEffect(GraphicsDevice& device)
        : Effect(device)
    {
        CacheEffectParameters();
    }

    SpriteEffect::SpriteEffect(const SpriteEffect& cloneSource)
        : Effect(*cloneSource.device_)
    {
        CacheEffectParameters();
    }

    Effect* SpriteEffect::Clone()
    {
        return new SpriteEffect(*this);
    }

    void SpriteEffect::CacheEffectParameters()
    {
        matrixParam_ = getParametersProperty()["MatrixTransform"];
    }

    void SpriteEffect::OnApply()
    {
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
