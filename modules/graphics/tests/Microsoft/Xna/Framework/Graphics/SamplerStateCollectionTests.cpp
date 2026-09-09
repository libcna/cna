// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

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

TEST(SamplerStateCollectionTest, IndexerAssignmentBindsSourceAndSlot)
{
    SamplerStateCollection coll;
    SamplerState custom;
    custom.setFilterProperty(TextureFilter::Point);

    coll[3] = custom;

    EXPECT_THROW(custom.setAddressUProperty(TextureAddressMode::Clamp),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setAddressVProperty(TextureAddressMode::Clamp),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setAddressWProperty(TextureAddressMode::Clamp),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setFilterProperty(TextureFilter::Linear),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setMaxAnisotropyProperty(16),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setMaxMipLevelProperty(2),
                 System::InvalidOperationException);
    EXPECT_THROW(custom.setMipMapLevelOfDetailBiasProperty(1.0f),
                 System::InvalidOperationException);
    EXPECT_THROW(coll[3].setFilterProperty(TextureFilter::Linear),
                 System::InvalidOperationException);
    EXPECT_EQ(coll[3].getFilterProperty(), TextureFilter::Point);
}

TEST(SamplerStateCollectionTest, IndexerAssignmentRejectsDisposedSampler)
{
    SamplerStateCollection coll;
    SamplerState disposed;
    disposed.Dispose();

    EXPECT_THROW(coll[3] = disposed, System::ObjectDisposedException);
    EXPECT_EQ(coll[3].getNameProperty(), "SamplerState.LinearWrap");
}

TEST(SamplerStateCollectionTest, DisposalAfterAssignmentInvalidatesSharedPayload)
{
    SamplerStateCollection coll;
    SamplerState custom;
    coll[3] = custom;
    custom.Dispose();

    EXPECT_THROW(coll[4] = coll[3], System::ObjectDisposedException);
    EXPECT_EQ(coll[4].getNameProperty(), "SamplerState.LinearWrap");
}

TEST(SamplerStateCollectionTest, NegativeIndexThrows)
{
    SamplerStateCollection coll;
    EXPECT_THROW((void)coll[-1], System::ArgumentOutOfRangeException);
}

TEST(SamplerStateCollectionTest, IndexAtMaxThrows)
{
    SamplerStateCollection coll;
    EXPECT_THROW((void)coll[SamplerStateCollection::MaxSamplers],
                 System::ArgumentOutOfRangeException);
}

TEST(SamplerStateCollectionTest, ConstIndexerNegativeThrows)
{
    const SamplerStateCollection coll;
    EXPECT_THROW((void)coll[-1], System::ArgumentOutOfRangeException);
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
    gd.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    auto& states = gd.getVertexSamplerStatesProperty();
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(states[i].getNameProperty(), "SamplerState.LinearWrap") << "slot " << i;
    }
}
