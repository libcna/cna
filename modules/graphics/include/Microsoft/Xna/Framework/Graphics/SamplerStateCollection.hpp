// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /** @brief A collection of SamplerState objects, one per texture sampler slot. */
    class SamplerStateCollection
    {
    public:
        /** @brief Maximum number of sampler slots. */
        CNAEXT static constexpr int MaxSamplers = 16;

        /** @brief Constructs a SamplerStateCollection initialized with LinearWrap states. */
        CNAEXT SamplerStateCollection();

        /**
         * @brief Returns a mutable reference to the sampler state at the given slot.
         * @param index Sampler slot index within the owning profile's active range.
         * @return Reference to the SamplerState at the specified slot.
         */
        [[nodiscard]] SamplerState& operator[](int index);
        /**
         * @brief Returns a const reference to the sampler state at the given slot.
         * @param index Sampler slot index within the owning profile's active range.
         * @return Const reference to the SamplerState at the specified slot.
         */
        [[nodiscard]] const SamplerState& operator[](int index) const;

    private:
        SamplerStateCollection(GraphicsDevice* graphicsDevice, bool vertexStage);

        [[nodiscard]] int ActiveSamplerCount() const;

        std::vector<SamplerState> samplers_;
        GraphicsDevice* graphicsDevice_ = nullptr;
        bool vertexStage_ = false;

        friend class GraphicsDevice;
    };
}
