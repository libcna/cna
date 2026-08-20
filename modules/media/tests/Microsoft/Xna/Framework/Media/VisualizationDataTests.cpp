// SPDX-License-Identifier: MS-PL
//
// plans/plan_media.md MEDIA-79: no test file existed for VisualizationData at all before this task.

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
