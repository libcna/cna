// SPDX-License-Identifier: MS-PL
//
// MEDIA-79: no test file existed for VisualizationData at all before this task.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/VisualizationData.hpp"

using Microsoft::Xna::Framework::Media::VisualizationData;

TEST(VisualizationDataTest, SizeIs256)
{
    EXPECT_EQ(VisualizationData::Size, 256);
}

TEST(VisualizationDataTest, FrequenciesIsZeroInitializedAndCorrectlySized)
{
    VisualizationData data;
    EXPECT_EQ(data.getFrequenciesProperty().size(), 256u);
    for (float v : data.getFrequenciesProperty())
    {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}

TEST(VisualizationDataTest, SamplesIsZeroInitializedAndCorrectlySized)
{
    VisualizationData data;
    EXPECT_EQ(data.getSamplesProperty().size(), 256u);
    for (float v : data.getSamplesProperty())
    {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}

TEST(VisualizationDataTest, GetTypeNameIsFullyQualified)
{
    VisualizationData data;
    EXPECT_EQ(data.GetTypeName(), "Microsoft.Xna.Framework.Media.VisualizationData");
}


TEST(VisualizationDataTest, Tq023GettersExposeDistinctLiveBuffers)
{
    // XNA get_Frequencies/get_Samples IL_0000..0006 return their respective stored buffers.
    // Different sentinels catch swapped getters, copies, and constant-zero implementations.
    VisualizationData data;
    const auto& frequencies = data.getFrequenciesProperty();
    const auto& samples = data.getSamplesProperty();
    data.freq[0] = 0.25f;
    data.freq[255] = 0.75f;
    data.samp[0] = -0.5f;
    data.samp[255] = 0.5f;
    EXPECT_FLOAT_EQ(frequencies[0], 0.25f);
    EXPECT_FLOAT_EQ(frequencies[255], 0.75f);
    EXPECT_FLOAT_EQ(samples[0], -0.5f);
    EXPECT_FLOAT_EQ(samples[255], 0.5f);
}
