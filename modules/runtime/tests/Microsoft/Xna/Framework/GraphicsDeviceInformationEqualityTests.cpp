// SPDX-License-Identifier: MS-PL
//
// GraphicsDeviceInformation.Equals(object) / GetHashCode. XNA compares the adapter, the profile,
// and exactly ten PresentationParameters fields -- not the whole presentation state -- and XORs
// the same set into the hash
// (xna4-decomp/.../Microsoft.Xna.Framework.Game/Microsoft.Xna.Framework/GraphicsDeviceInformation.cs).
// Everything below is driven from that list, including the fields it deliberately omits.

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/Object.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// A settings object with every compared field set to a non-default value, so a test that
    /// changes one field is genuinely changing it rather than reaching the same default twice.
    GraphicsDeviceInformation MakeConfigured()
    {
        GraphicsDeviceInformation information;
        information.setGraphicsProfileProperty(GraphicsProfile::HiDef);
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(1280);
        parameters.setBackBufferHeightProperty(720);
        parameters.setBackBufferFormatProperty(SurfaceFormat::Color);
        parameters.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        parameters.setMultiSampleCountProperty(4);
        parameters.setDisplayOrientationProperty(DisplayOrientation::LandscapeLeft);
        parameters.setPresentationIntervalProperty(PresentInterval::One);
        parameters.setRenderTargetUsageProperty(RenderTargetUsage::PreserveContents);
        parameters.setDeviceWindowHandleProperty(0x1234);
        parameters.setIsFullScreenProperty(false);
        information.setPresentationParametersProperty(parameters);
        return information;
    }

    /// The ten presentation-parameter fields XNA compares, each with a mutation that must make
    /// two otherwise identical settings objects unequal.
    const std::vector<std::pair<std::string, std::function<void(PresentationParameters&)>>>&
    ComparedFields()
    {
        static const std::vector<std::pair<std::string, std::function<void(PresentationParameters&)>>>
            fields = {
                {"BackBufferWidth", [](PresentationParameters& p) { p.setBackBufferWidthProperty(1024); }},
                {"BackBufferHeight", [](PresentationParameters& p) { p.setBackBufferHeightProperty(768); }},
                {"BackBufferFormat", [](PresentationParameters& p) { p.setBackBufferFormatProperty(SurfaceFormat::Bgr565); }},
                {"DepthStencilFormat", [](PresentationParameters& p) { p.setDepthStencilFormatProperty(DepthFormat::Depth16); }},
                {"MultiSampleCount", [](PresentationParameters& p) { p.setMultiSampleCountProperty(8); }},
                {"DisplayOrientation", [](PresentationParameters& p) { p.setDisplayOrientationProperty(DisplayOrientation::Portrait); }},
                {"PresentationInterval", [](PresentationParameters& p) { p.setPresentationIntervalProperty(PresentInterval::Immediate); }},
                {"RenderTargetUsage", [](PresentationParameters& p) { p.setRenderTargetUsageProperty(RenderTargetUsage::DiscardContents); }},
                {"DeviceWindowHandle", [](PresentationParameters& p) { p.setDeviceWindowHandleProperty(0x4321); }},
                {"IsFullScreen", [](PresentationParameters& p) { p.setIsFullScreenProperty(true); }},
            };
        return fields;
    }
}

TEST(GraphicsDeviceInformationEqualityTest, DefaultInstancesAreEqualAndShareAHash)
{
    const GraphicsDeviceInformation first;
    const GraphicsDeviceInformation second;
    EXPECT_TRUE(first.Equals(&second));
    EXPECT_EQ(first.GetHashCode(), second.GetHashCode());
}

TEST(GraphicsDeviceInformationEqualityTest, ACloneEqualsItsSourceAndSharesItsHash)
{
    const GraphicsDeviceInformation source = MakeConfigured();
    const GraphicsDeviceInformation clone = source.Clone();
    EXPECT_TRUE(source.Equals(&clone));
    EXPECT_TRUE(clone.Equals(&source));
    EXPECT_EQ(source.GetHashCode(), clone.GetHashCode());
}

TEST(GraphicsDeviceInformationEqualityTest, ANullObjectIsNeverEqual)
{
    const GraphicsDeviceInformation information = MakeConfigured();
    EXPECT_FALSE(information.Equals(nullptr));
}

TEST(GraphicsDeviceInformationEqualityTest, AnotherObjectTypeIsNeverEqual)
{
    // A System::Object that is not a GraphicsDeviceInformation must not compare equal, which is
    // the `obj is GraphicsDeviceInformation` half of the Microsoft implementation.
    class Unrelated final : public System::Object
    {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "CnaTests.Unrelated";
            return name;
        }
    };

    const GraphicsDeviceInformation information = MakeConfigured();
    const Unrelated other;
    EXPECT_FALSE(information.Equals(&other));
}

TEST(GraphicsDeviceInformationEqualityTest, EachComparedPresentationFieldBreaksEquality)
{
    for (const auto& [name, mutate] : ComparedFields())
    {
        const GraphicsDeviceInformation baseline = MakeConfigured();
        GraphicsDeviceInformation changed = MakeConfigured();
        PresentationParameters parameters = changed.getPresentationParametersProperty();
        mutate(parameters);
        changed.setPresentationParametersProperty(parameters);

        EXPECT_FALSE(baseline.Equals(&changed)) << name << " must be compared";
        EXPECT_FALSE(changed.Equals(&baseline)) << name << " must be compared symmetrically";
        EXPECT_NE(baseline.GetHashCode(), changed.GetHashCode())
            << name << " must contribute to the hash";
    }
}

TEST(GraphicsDeviceInformationEqualityTest, TheProfileIsCompared)
{
    const GraphicsDeviceInformation hiDef = MakeConfigured();
    GraphicsDeviceInformation reach = MakeConfigured();
    reach.setGraphicsProfileProperty(GraphicsProfile::Reach);

    EXPECT_FALSE(hiDef.Equals(&reach));
    EXPECT_NE(hiDef.GetHashCode(), reach.GetHashCode());
}

TEST(GraphicsDeviceInformationEqualityTest, TheAdapterIsCompared)
{
    const GraphicsDeviceInformation withoutAdapter = MakeConfigured();
    GraphicsDeviceInformation withAdapter = MakeConfigured();

    // Any distinct adapter identity is enough; XNA compares adapter references, and CNA holds a
    // pointer, so a non-null value must separate the two.
    GraphicsAdapter* const sentinel = reinterpret_cast<GraphicsAdapter*>(0x10);
    withAdapter.setAdapterProperty(sentinel);

    EXPECT_FALSE(withoutAdapter.Equals(&withAdapter));
    EXPECT_FALSE(withAdapter.Equals(&withoutAdapter));
    EXPECT_NE(withoutAdapter.GetHashCode(), withAdapter.GetHashCode());
}

TEST(GraphicsDeviceInformationEqualityTest, UncomparedPresentationFieldsDoNotBreakEquality)
{
    // XNA's list stops at those ten fields, so presentation state outside it must not change the
    // verdict. CNA's own HeadlessEXT flag is such a field: it is not one of the ten.
    const GraphicsDeviceInformation baseline = MakeConfigured();
    GraphicsDeviceInformation changed = MakeConfigured();
    PresentationParameters parameters = changed.getPresentationParametersProperty();
    ASSERT_FALSE(parameters.getHeadlessEXTProperty());
    parameters.setHeadlessEXTProperty(true);
    changed.setPresentationParametersProperty(parameters);
    ASSERT_TRUE(changed.getPresentationParametersProperty().getHeadlessEXTProperty());

    EXPECT_TRUE(baseline.Equals(&changed));
    EXPECT_EQ(baseline.GetHashCode(), changed.GetHashCode());
}

TEST(GraphicsDeviceInformationEqualityTest, EqualityIsReachableThroughTheObjectBase)
{
    // The override has to be found through a System::Object reference, which is how a CLR caller
    // would reach Equals(object).
    const GraphicsDeviceInformation first = MakeConfigured();
    const GraphicsDeviceInformation second = MakeConfigured();
    const System::Object& asObject = first;
    EXPECT_TRUE(asObject.Equals(&second));
    EXPECT_EQ(asObject.GetHashCode(), second.GetHashCode());
}
