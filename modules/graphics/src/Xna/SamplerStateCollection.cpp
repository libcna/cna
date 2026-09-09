// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    SamplerStateCollection::SamplerStateCollection()
        : SamplerStateCollection(nullptr, false)
    {
    }

    SamplerStateCollection::SamplerStateCollection(
        GraphicsDevice* graphicsDevice,
        bool vertexStage)
        : samplers_(MaxSamplers, SamplerState::LinearWrap)
        , graphicsDevice_(graphicsDevice)
        , vertexStage_(vertexStage)
    {
    }

    int SamplerStateCollection::ActiveSamplerCount() const
    {
        if (graphicsDevice_ == nullptr || !vertexStage_)
            return MaxSamplers;
        return graphicsDevice_->getGraphicsProfileProperty() == GraphicsProfile::HiDef ? 4 : 0;
    }

    SamplerState& SamplerStateCollection::operator[](int index)
    {
        if (index < 0 || index >= ActiveSamplerCount())
        {
            throw std::out_of_range("Sampler index out of range.");
        }
        return samplers_[static_cast<std::size_t>(index)];
    }

    const SamplerState& SamplerStateCollection::operator[](int index) const
    {
        if (index < 0 || index >= ActiveSamplerCount())
        {
            throw std::out_of_range("Sampler index out of range.");
        }
        return samplers_[static_cast<std::size_t>(index)];
    }
}
