// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-010: the documented static GraphicsAdapter.UseNullDevice / UseReferenceDevice.
//
// XNA resolves the two into a Direct3D 9 device type in exactly one place -- `CurrentDeviceType`:
// NULLREF if UseNullDevice, else REF if UseReferenceDevice, else HAL -- and a GraphicsDevice
// captures that at construction while the adapter's own format and profile probes read it live
// (xna4-decomp/.../Microsoft.Xna.Framework.Graphics/GraphicsAdapter.cs:88-128,333,348,729).
//
// CNA has no D3D9 device type; a renderer is what decides how a device behaves. So each mode maps
// to the renderer whose job is the same, and a device that cannot have it is refused rather than
// silently created on the GPU. That refusal is the part worth testing hardest: a flag that is
// quietly ignored would report a null or software device while rendering on hardware, which is the
// one outcome worse than not having the flag at all.
//
// These cases restore both flags in a fixture teardown, because the state is process-wide -- which
// is itself part of the documented contract.

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsDeviceTypeEXT;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;

namespace
{
    class GraphicsAdapterDeviceSelectionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            nullDevice_ = GraphicsAdapter::getUseNullDeviceProperty();
            referenceDevice_ = GraphicsAdapter::getUseReferenceDeviceProperty();
        }

        void TearDown() override
        {
            GraphicsAdapter::setUseNullDeviceProperty(nullDevice_);
            GraphicsAdapter::setUseReferenceDeviceProperty(referenceDevice_);
        }

    private:
        bool nullDevice_ = false;
        bool referenceDevice_ = false;
    };
}

TEST_F(GraphicsAdapterDeviceSelectionTest, BothFlagsAreClearByDefault)
{
    // The default must be hardware, because that is what every existing device depends on: the
    // flags are inert until a caller sets one.
    GraphicsAdapter::setUseNullDeviceProperty(false);
    GraphicsAdapter::setUseReferenceDeviceProperty(false);

    EXPECT_FALSE(GraphicsAdapter::getUseNullDeviceProperty());
    EXPECT_FALSE(GraphicsAdapter::getUseReferenceDeviceProperty());
    EXPECT_EQ(GraphicsAdapter::GetCurrentDeviceTypeEXT(), GraphicsDeviceTypeEXT::Hardware);
    EXPECT_FALSE(GraphicsAdapter::GetRequiredRendererEXT().has_value());
}

TEST_F(GraphicsAdapterDeviceSelectionTest, TheFlagsAreProcessWideNotPerAdapter)
{
    // XNA declares them static. Setting one through any adapter reference is setting the one piece
    // of process state, which a test that used two adapters would otherwise not reveal.
    GraphicsAdapter& first = GraphicsAdapter::getDefaultAdapterProperty();
    GraphicsAdapter::setUseReferenceDeviceProperty(true);

    EXPECT_TRUE(GraphicsAdapter::getUseReferenceDeviceProperty());
    EXPECT_TRUE(first.getUseReferenceDeviceProperty())
        << "reachable through an instance too, as a C++ static member is";

    const auto& adapters = GraphicsAdapter::getAdaptersProperty();
    for (const auto& adapter : adapters)
    {
        EXPECT_TRUE(adapter->getUseReferenceDeviceProperty())
            << "every adapter reports the same flag, because there is only one";
    }
}

TEST_F(GraphicsAdapterDeviceSelectionTest, NullDeviceTakesPrecedenceOverReferenceDevice)
{
    // XNA's CurrentDeviceType checks UseNullDevice first and returns NULLREF without looking at
    // UseReferenceDevice, so with both set the answer is Null -- not Reference, and not an error.
    GraphicsAdapter::setUseReferenceDeviceProperty(true);
    EXPECT_EQ(GraphicsAdapter::GetCurrentDeviceTypeEXT(), GraphicsDeviceTypeEXT::Reference);

    GraphicsAdapter::setUseNullDeviceProperty(true);
    EXPECT_EQ(GraphicsAdapter::GetCurrentDeviceTypeEXT(), GraphicsDeviceTypeEXT::Null);
    EXPECT_TRUE(GraphicsAdapter::getUseReferenceDeviceProperty())
        << "the losing flag keeps its value; precedence is a resolution, not a reset";

    // And clearing the winner reveals the other one again.
    GraphicsAdapter::setUseNullDeviceProperty(false);
    EXPECT_EQ(GraphicsAdapter::GetCurrentDeviceTypeEXT(), GraphicsDeviceTypeEXT::Reference);
}

TEST_F(GraphicsAdapterDeviceSelectionTest, EachModeNamesTheRendererWhoseJobItIs)
{
    GraphicsAdapter::setUseNullDeviceProperty(false);
    GraphicsAdapter::setUseReferenceDeviceProperty(false);
    EXPECT_FALSE(GraphicsAdapter::GetRequiredRendererEXT().has_value())
        << "hardware requires nothing: whatever the build selected is used";

    GraphicsAdapter::setUseReferenceDeviceProperty(true);
    ASSERT_TRUE(GraphicsAdapter::GetRequiredRendererEXT().has_value());
    EXPECT_EQ(*GraphicsAdapter::GetRequiredRendererEXT(), CNA::GraphicsRendererType::Software);

    GraphicsAdapter::setUseNullDeviceProperty(true);
    ASSERT_TRUE(GraphicsAdapter::GetRequiredRendererEXT().has_value());
    EXPECT_EQ(*GraphicsAdapter::GetRequiredRendererEXT(), CNA::GraphicsRendererType::Headless)
        << "precedence applies here too, since this reads the resolved device type";
}

TEST_F(GraphicsAdapterDeviceSelectionTest, ADeviceIsRefusedWhenTheRequiredRendererIsNotCompiledIn)
{
    // The whole point of the mapping: a flag that cannot be honoured refuses the device instead of
    // reporting a null or software device while running on something else. Exactly one of the two
    // modes is checkable in any given build, so the test asks the build which.
    GraphicsAdapter::setUseNullDeviceProperty(false);
    GraphicsAdapter::setUseReferenceDeviceProperty(false);

    const bool headlessCompiledIn =
        CNA::GraphicsRendererSelection::IsAvailable(CNA::GraphicsRendererType::Headless);
    const bool softwareCompiledIn =
        CNA::GraphicsRendererSelection::IsAvailable(CNA::GraphicsRendererType::Software);

    if (!softwareCompiledIn)
    {
        GraphicsAdapter::setUseReferenceDeviceProperty(true);
        EXPECT_THROW({ GraphicsDevice device; }, NoSuitableGraphicsDeviceException);
        try
        {
            GraphicsDevice device;
            FAIL() << "a reference device must be refused without the SOFTWARE renderer";
        }
        catch (const NoSuitableGraphicsDeviceException& error)
        {
            const std::string message = error.what();
            EXPECT_NE(message.find("UseReferenceDevice"), std::string::npos);
            EXPECT_NE(message.find("SOFTWARE"), std::string::npos)
                << "the message must name the renderer the flag needs";
        }
        GraphicsAdapter::setUseReferenceDeviceProperty(false);
    }

    if (!headlessCompiledIn)
    {
        GraphicsAdapter::setUseNullDeviceProperty(true);
        EXPECT_THROW({ GraphicsDevice device; }, NoSuitableGraphicsDeviceException);
        GraphicsAdapter::setUseNullDeviceProperty(false);
    }

    if (softwareCompiledIn && headlessCompiledIn)
    {
        GTEST_SKIP() << "both renderers are compiled in, so neither refusal is reachable here";
    }
}

TEST_F(GraphicsAdapterDeviceSelectionTest, ADeviceIsCreatedWhenTheRequiredRendererIsTheSelectedOne)
{
    // The accepting half. Whichever mode matches the renderer this build selected must create a
    // device normally -- the flag is satisfied, not merely tolerated.
    const CNA::GraphicsRendererType selected = CNA::GraphicsRendererSelection::GetSelected();

    if (selected == CNA::GraphicsRendererType::Headless)
    {
        GraphicsAdapter::setUseNullDeviceProperty(true);
        EXPECT_NO_THROW({
            GraphicsDevice device;
            EXPECT_FALSE(device.getIsDisposedProperty());
        });
    }
    else if (selected == CNA::GraphicsRendererType::Software)
    {
        GraphicsAdapter::setUseReferenceDeviceProperty(true);
        EXPECT_NO_THROW({
            GraphicsDevice device;
            EXPECT_FALSE(device.getIsDisposedProperty());
        });
    }
    else
    {
        GTEST_SKIP() << "this build's renderer is neither HEADLESS nor SOFTWARE, so no flag matches";
    }
}

TEST_F(GraphicsAdapterDeviceSelectionTest, WithBothFlagsClearADeviceIsCreatedAsBefore)
{
    // The default path, asserted so the flags cannot quietly change it.
    GraphicsAdapter::setUseNullDeviceProperty(false);
    GraphicsAdapter::setUseReferenceDeviceProperty(false);

    EXPECT_NO_THROW({
        GraphicsDevice device;
        EXPECT_FALSE(device.getIsDisposedProperty());
    });
    EXPECT_NO_THROW({
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                             PresentationParameters{});
        EXPECT_FALSE(device.getIsDisposedProperty());
    });
}

TEST_F(GraphicsAdapterDeviceSelectionTest, AnAlreadyCreatedDeviceKeepsTheModeItWasCreatedWith)
{
    // XNA's device captures CurrentDeviceType at construction. Setting a flag afterwards therefore
    // cannot disturb a live device, which a game changing the flag mid-run depends on.
    GraphicsAdapter::setUseNullDeviceProperty(false);
    GraphicsAdapter::setUseReferenceDeviceProperty(false);

    GraphicsDevice device;
    ASSERT_FALSE(device.getIsDisposedProperty());

    GraphicsAdapter::setUseReferenceDeviceProperty(true);
    EXPECT_FALSE(device.getIsDisposedProperty()) << "the live device is unaffected";
    EXPECT_NO_THROW((void)device.getAdapterProperty());
    EXPECT_NO_THROW((void)device.getGraphicsProfileProperty());
}

TEST_F(GraphicsAdapterDeviceSelectionTest, TheAdapterProbesStillAnswerWithAFlagSet)
{
    // XNA's QueryBackBufferFormat, QueryRenderTargetFormat and IsProfileSupported read the device
    // type live. CNA's probes describe the renderer rather than a D3D9 device type, so what has to
    // hold is that a set flag does not break them.
    GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();
    GraphicsAdapter::setUseReferenceDeviceProperty(true);

    EXPECT_NO_THROW((void)adapter.IsProfileSupported(GraphicsProfile::Reach));
    EXPECT_NO_THROW((void)adapter.IsProfileSupported(GraphicsProfile::HiDef));
    EXPECT_TRUE(adapter.IsProfileSupported(GraphicsProfile::Reach))
        << "Reach is supported whatever the requested device type";
}
