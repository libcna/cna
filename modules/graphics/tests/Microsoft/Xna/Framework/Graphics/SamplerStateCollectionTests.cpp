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

// SOFTWARE-350: XNA's collection setter compares the incoming SamplerState reference with the
// slot before Apply(), so an already-active disposed sampler is a no-op. A different slot above
// still invokes Apply and must reject the exact same disposed object.
TEST(SamplerStateCollectionTest, ReassigningSameDisposedSamplerIdentityIsNoOp)
{
    SamplerStateCollection coll;
    SamplerState custom;
    coll[3] = custom;
    custom.Dispose();

    EXPECT_NO_THROW(coll[3] = custom);
    EXPECT_NO_THROW(coll[3] = coll[3]);
    EXPECT_TRUE(coll[3].getIsDisposedProperty());
}

// House Simulator BL-16: XNA's `SamplerStates[i] = state` is the collection's indexer setter. The
// C++ `coll[i] = state` spelling goes through SamplerState's CNAEXT copy assignment, so strict-XNA
// code needs this non-CNAEXT setter, with the same semantics.
TEST(SamplerStateCollectionTest, IndexerSetterUpdatesSlot)
{
    SamplerStateCollection coll;
    coll(3, SamplerState::PointClamp);
    EXPECT_EQ(coll[3].getFilterProperty(), TextureFilter::Point);
    EXPECT_EQ(coll[3].getAddressUProperty(), TextureAddressMode::Clamp);
    EXPECT_EQ(coll[3].getNameProperty(), "SamplerState.PointClamp");
}

TEST(SamplerStateCollectionTest, IndexerSetterBindsSource)
{
    SamplerStateCollection coll;
    SamplerState custom;
    custom.setAddressUProperty(TextureAddressMode::Mirror);

    coll(3, custom);

    EXPECT_EQ(coll[3].getAddressUProperty(), TextureAddressMode::Mirror);
    EXPECT_THROW(custom.setFilterProperty(TextureFilter::Point),
                 System::InvalidOperationException);
}

TEST(SamplerStateCollectionTest, IndexerSetterRejectsDisposedSampler)
{
    SamplerStateCollection coll;
    SamplerState disposed;
    disposed.Dispose();

    EXPECT_THROW(coll(3, disposed), System::ObjectDisposedException);
    EXPECT_EQ(coll[3].getNameProperty(), "SamplerState.LinearWrap");
}

TEST(SamplerStateCollectionTest, IndexerSetterOutOfRangeThrows)
{
    SamplerStateCollection coll;
    EXPECT_THROW(coll(-1, SamplerState::PointClamp), System::ArgumentOutOfRangeException);
    EXPECT_THROW(coll(SamplerStateCollection::MaxSamplers, SamplerState::PointClamp),
                 System::ArgumentOutOfRangeException);
}

TEST(GraphicsDeviceSamplerStatesTest, IndexerSetterReachesDeviceSlot)
{
    GraphicsDevice gd;
    gd.getSamplerStatesProperty()(0, SamplerState::AnisotropicClamp);
    EXPECT_EQ(gd.getSamplerStatesProperty()[0].getNameProperty(),
              "SamplerState.AnisotropicClamp");
}

TEST(GraphicsDeviceSamplerStatesTest, VertexIndexerSetterHonoursProfileRange)
{
    GraphicsDevice gd;
    gd.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach);
    EXPECT_THROW(gd.getVertexSamplerStatesProperty()(0, SamplerState::PointClamp),
                 System::ArgumentOutOfRangeException);

    gd.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    gd.getVertexSamplerStatesProperty()(3, SamplerState::PointClamp);
    EXPECT_EQ(gd.getVertexSamplerStatesProperty()[3].getNameProperty(),
              "SamplerState.PointClamp");
    EXPECT_THROW(gd.getVertexSamplerStatesProperty()(4, SamplerState::PointClamp),
                 System::ArgumentOutOfRangeException);
}

TEST(GraphicsDeviceSamplerStatesTest, CustomSamplerCanBeReappliedAcrossDevices)
{
    GraphicsDevice first;
    GraphicsDevice second;
    SamplerState sampler;
    sampler.setAddressUProperty(TextureAddressMode::Mirror);

    EXPECT_NO_THROW(first.getSamplerStatesProperty()[3] = sampler);
    EXPECT_EQ(sampler.getGraphicsDeviceProperty(), &first);
    EXPECT_NO_THROW(second.getSamplerStatesProperty()[3] = sampler);
    EXPECT_EQ(sampler.getGraphicsDeviceProperty(), &second);
    EXPECT_NO_THROW(first.getSamplerStatesProperty()[3] = sampler);
    EXPECT_EQ(sampler.getGraphicsDeviceProperty(), &second);
    // A different first-device slot does not cache this identity yet and therefore calls Apply.
    EXPECT_NO_THROW(first.getSamplerStatesProperty()[4] = sampler);
    EXPECT_EQ(sampler.getGraphicsDeviceProperty(), &first);
    EXPECT_EQ(second.getSamplerStatesProperty()[3].getAddressUProperty(),
              TextureAddressMode::Mirror);
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
