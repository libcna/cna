// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"

#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics
{
    SamplerStateCollection::SamplerStateCollection()
        : samplers_(MaxSamplers)
    {
    }

    SamplerState& SamplerStateCollection::operator[](int index)
    {
        if (index < 0 || index >= MaxSamplers)
        {
            throw std::out_of_range("Sampler index out of range.");
        }
        return samplers_[static_cast<std::size_t>(index)];
    }

    const SamplerState& SamplerStateCollection::operator[](int index) const
    {
        if (index < 0 || index >= MaxSamplers)
        {
            throw std::out_of_range("Sampler index out of range.");
        }
        return samplers_[static_cast<std::size_t>(index)];
    }
}
