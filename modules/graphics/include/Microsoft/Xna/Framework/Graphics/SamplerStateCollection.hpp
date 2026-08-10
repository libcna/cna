// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief A collection of SamplerState objects, one per texture sampler slot. */
    class SamplerStateCollection
    {
    public:
        /** @brief Maximum number of sampler slots. */
        static constexpr int MaxSamplers = 16;

        /** @brief Constructs a SamplerStateCollection initialized with LinearWrap states. */
        SamplerStateCollection();

        /**
         * @brief Returns a mutable reference to the sampler state at the given slot.
         * @param index Sampler slot index (0 to MaxSamplers-1).
         * @return Reference to the SamplerState at the specified slot.
         */
        [[nodiscard]] SamplerState& operator[](int index);
        /**
         * @brief Returns a const reference to the sampler state at the given slot.
         * @param index Sampler slot index (0 to MaxSamplers-1).
         * @return Const reference to the SamplerState at the specified slot.
         */
        [[nodiscard]] const SamplerState& operator[](int index) const;

    private:
        std::vector<SamplerState> samplers_;
    };
}
