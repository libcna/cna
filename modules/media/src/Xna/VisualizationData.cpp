// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"

namespace Microsoft::Xna::Framework::Media
{
    VisualizationData::VisualizationData()
        : freq{},
          samp{}
    {
    }

    const std::array<float, VisualizationData::Size>& VisualizationData::getFrequenciesProperty() const
    {
        return freq;
    }

    const std::array<float, VisualizationData::Size>& VisualizationData::getSamplesProperty() const
    {
        return samp;
    }

    const std::string& VisualizationData::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Media.VisualizationData";
        return typeName;
    }
}
