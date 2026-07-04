// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SamplerStateCollection;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

// Task 292: FNA's SamplerStateCollection constructor fills every slot with SamplerState.LinearWrap
// (SamplerStateCollection.cs). CNA previously default-constructed each slot instead, which happened
// to match LinearWrap's filter/address values but left Name empty rather than
// "SamplerState.LinearWrap" (a real, only-now-detectable divergence once Task 291 gave SamplerState
// presets a Name at all). Fixed alongside this test.

TEST(SamplerStateCollectionTest, MaxSamplersIsSixteen)
{
    EXPECT_EQ(SamplerStateCollection::MaxSamplers, 16);
}

TEST(SamplerStateCollectionTest, EverySlotDefaultsToLinearWrapFilter)
{
    SamplerStateCollection coll;
    for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
    {
        EXPECT_EQ(coll[i].getFilterProperty(), TextureFilter::Linear) << "slot " << i;
    }
}

TEST(SamplerStateCollectionTest, EverySlotDefaultsToLinearWrapAddressing)
{
    SamplerStateCollection coll;
    for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
    {
        EXPECT_EQ(coll[i].getAddressUProperty(), TextureAddressMode::Wrap) << "slot " << i;
        EXPECT_EQ(coll[i].getAddressVProperty(), TextureAddressMode::Wrap) << "slot " << i;
        EXPECT_EQ(coll[i].getAddressWProperty(), TextureAddressMode::Wrap) << "slot " << i;
    }
}

TEST(SamplerStateCollectionTest, EverySlotDefaultsToLinearWrapName)
{
    SamplerStateCollection coll;
    for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
    {
        EXPECT_EQ(coll[i].getNameProperty(), "SamplerState.LinearWrap") << "slot " << i;
    }
}

TEST(SamplerStateCollectionTest, IndexerAssignmentUpdatesSlot)
{
    SamplerStateCollection coll;
    coll[3] = SamplerState::PointClamp;
    EXPECT_EQ(coll[3].getFilterProperty(), TextureFilter::Point);
    EXPECT_EQ(coll[3].getNameProperty(), "SamplerState.PointClamp");
}

TEST(SamplerStateCollectionTest, NegativeIndexThrows)
{
    SamplerStateCollection coll;
    EXPECT_THROW((void)coll[-1], std::out_of_range);
}

TEST(SamplerStateCollectionTest, IndexAtMaxThrows)
{
    SamplerStateCollection coll;
    EXPECT_THROW((void)coll[SamplerStateCollection::MaxSamplers], std::out_of_range);
}

TEST(SamplerStateCollectionTest, ConstIndexerNegativeThrows)
{
    const SamplerStateCollection coll;
    EXPECT_THROW((void)coll[-1], std::out_of_range);
}

// -----------------------------------------------------------------------
// GraphicsDevice.SamplerStates / VertexSamplerStates defaults
// -----------------------------------------------------------------------

TEST(GraphicsDeviceSamplerStatesTest, DefaultSamplerStatesAreLinearWrap)
{
    GraphicsDevice gd;
    auto& states = gd.getSamplerStatesProperty();
    for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
    {
        EXPECT_EQ(states[i].getNameProperty(), "SamplerState.LinearWrap") << "slot " << i;
    }
}

TEST(GraphicsDeviceSamplerStatesTest, DefaultVertexSamplerStatesAreLinearWrap)
{
    GraphicsDevice gd;
    auto& states = gd.getVertexSamplerStatesProperty();
    for (int i = 0; i < SamplerStateCollection::MaxSamplers; ++i)
    {
        EXPECT_EQ(states[i].getNameProperty(), "SamplerState.LinearWrap") << "slot " << i;
    }
}
