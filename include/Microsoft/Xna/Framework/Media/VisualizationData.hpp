#pragma once

#include <array>
#include <string>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Media
{
    /// Stores frequency-domain and sample-domain media visualization values.
    class VisualizationData : public System::Object
    {
    public:
        static constexpr SharpRuntime::intcs Size = 256;

        /// Frequency values used by the media visualization API.
        std::array<float, Size> freq;

        /// Sample values used by the media visualization API.
        std::array<float, Size> samp;

        /// Creates visualization buffers initialized to zero.
        VisualizationData();

        /// Gets the frequency values.
        [[nodiscard]] const std::array<float, Size>& getFrequenciesProperty() const;

        /// Gets the sample values.
        [[nodiscard]] const std::array<float, Size>& getSamplesProperty() const;

        [[nodiscard]] const std::string& GetTypeName() const override;
    };
}
