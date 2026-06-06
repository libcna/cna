#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// A collection of SamplerState objects, one per texture sampler slot.
    class SamplerStateCollection
    {
    public:
        static constexpr int MaxSamplers = 16;

        SamplerStateCollection();

        [[nodiscard]] SamplerState& operator[](int index);
        [[nodiscard]] const SamplerState& operator[](int index) const;

    private:
        std::vector<SamplerState> samplers_;
    };
}
