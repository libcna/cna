// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Stores frequency-domain and sample-domain data for media visualization. */
    class VisualizationData : public System::Object
    {
    public:
        /** @brief Number of frequency and sample values in each visualization buffer. */
        CNAEXT static constexpr SharpRuntime::intcs Size = 256;

        /** @brief Frequency-domain values used by the media visualization API. */
        CNAEXT std::array<float, Size> freq;

        /** @brief Sample-domain values used by the media visualization API. */
        CNAEXT std::array<float, Size> samp;

        /** @brief Creates visualization buffers initialized to zero. */
        VisualizationData();

        /**
         * @brief Gets the frequency-domain visualization values.
         *
         * @return Const reference to the frequency value array.
         */
        [[nodiscard]] const std::array<float, Size>& getFrequenciesProperty() const;

        /**
         * @brief Gets the sample-domain visualization values.
         *
         * @return Const reference to the sample value array.
         */
        [[nodiscard]] const std::array<float, Size>& getSamplesProperty() const;

        /** @brief Returns the fully-qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };
}
