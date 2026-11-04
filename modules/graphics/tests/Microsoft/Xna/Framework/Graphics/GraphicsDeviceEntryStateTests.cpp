// SPDX-License-Identifier: MS-PL
// TQ-048: independent Refresh entry-point contracts.
#include <gtest/gtest.h>
#include <typeinfo>
#include <string>
#include <vector>
#include <limits>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
namespace
{
    // Only the diagnostic body is XNA-owned. sharp-runtime selects the Framework/Core
    // parameter suffix; remove that suffix, without manufacturing an expected CNA message.
    [[maybe_unused]] std::string DiagnosticBody(const System::Exception& error)
    {
        std::string message = error.getMessageProperty();
        auto pos = message.find("\nParameter name:");
        if (pos == std::string::npos) pos = message.find(" (Parameter '");
        return message.substr(0, pos);
    }
    template<class E, class F>
    void ExpectArgument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "call accepted: " << parameter; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            EXPECT_EQ(DiagnosticBody(e), body);
        }
    }
}

// Refresh {Blend,DepthStencil,Rasterizer,Sampler}State::ThrowIfBound IL_0000-0036,
// FrameworkResources.BoundStateObject. Test a custom state through the source after bind.
namespace
{
    template<class F> void ExpectReadOnly(const char* name, F action)
    {
        try { action(); ADD_FAILURE() << "bound " << name << " changed"; }
        catch (const System::InvalidOperationException& e)
        {
            EXPECT_EQ(typeid(e), typeid(System::InvalidOperationException));
            EXPECT_EQ(e.getMessageProperty(), std::string("Cannot change read-only ") + name +
                ". State objects become read-only the first time they are bound to a GraphicsDevice. "
                "To change property values, create a new " + name + " instance.");
        }
    }
}
TEST(GraphicsDeviceEntryStateTest, EveryCustomStateKeepsItsValueAndXnaReadOnlyDiagnostic)
{
    GraphicsDevice device;
    BlendState blend;
    blend.setMultiSampleMaskProperty(-1);
    device.setBlendStateProperty(blend);
    ExpectReadOnly("BlendState", [&] { blend.setMultiSampleMaskProperty(0); });
    EXPECT_EQ(blend.getMultiSampleMaskProperty(), -1);
    EXPECT_EQ(device.getBlendStateProperty().getMultiSampleMaskProperty(), -1);
    DepthStencilState depth;
    depth.setReferenceStencilProperty(7);
    device.setDepthStencilStateProperty(depth);
    ExpectReadOnly("DepthStencilState", [&] { depth.setReferenceStencilProperty(8); });
    EXPECT_EQ(depth.getReferenceStencilProperty(), 7);
    EXPECT_EQ(device.getDepthStencilStateProperty().getReferenceStencilProperty(), 7);
    RasterizerState rasterizer;
    rasterizer.setScissorTestEnableProperty(true);
    device.setRasterizerStateProperty(rasterizer);
    ExpectReadOnly("RasterizerState", [&] { rasterizer.setScissorTestEnableProperty(false); });
    EXPECT_TRUE(rasterizer.getScissorTestEnableProperty());
    EXPECT_TRUE(device.getRasterizerStateProperty().getScissorTestEnableProperty());
    SamplerState sampler;
    sampler.setMaxMipLevelProperty(2);
    device.getSamplerStatesProperty()(1, sampler);
    ExpectReadOnly("SamplerState", [&] { sampler.setMaxMipLevelProperty(3); });
    EXPECT_EQ(sampler.getMaxMipLevelProperty(), 2);
    EXPECT_EQ(device.getSamplerStatesProperty()[1].getMaxMipLevelProperty(), 2);
}
TEST(GraphicsDeviceEntryStateTest, StandaloneValuesDoNotMutateStateAndRebindRestoresAllValues)
{
    // GraphicsDevice set_BlendState IL_004c-006b; set_BlendFactor IL_0084-008f;
    // set_MultiSampleMask IL_0054-005e; set_ReferenceStencil IL_0054-005e.
    GraphicsDevice device;
    BlendState blend;
    const Microsoft::Xna::Framework::Color initial(17, 34, 51, 68);
    blend.setBlendFactorProperty(initial);
    blend.setMultiSampleMaskProperty(-1);
    device.setBlendStateProperty(blend);
    device.setBlendFactorProperty(Microsoft::Xna::Framework::Color(5, 6, 7, 8));
    device.setMultiSampleMaskProperty(0);
    EXPECT_EQ(blend.getBlendFactorProperty(), initial);
    EXPECT_EQ(blend.getMultiSampleMaskProperty(), -1);
    EXPECT_EQ(device.getBlendStateProperty().getBlendFactorProperty(), initial);
    EXPECT_EQ(device.getBlendFactorProperty(), Microsoft::Xna::Framework::Color(5, 6, 7, 8));
    EXPECT_EQ(device.getMultiSampleMaskProperty(), 0);
    device.setBlendStateProperty(blend);
    EXPECT_EQ(device.getBlendFactorProperty(), initial);
    EXPECT_EQ(device.getMultiSampleMaskProperty(), -1);
    DepthStencilState depth;
    depth.setReferenceStencilProperty(7);
    device.setDepthStencilStateProperty(depth);
    device.setReferenceStencilProperty(-1);
    EXPECT_EQ(device.getReferenceStencilProperty(), -1);
    EXPECT_EQ(depth.getReferenceStencilProperty(), 7);
    EXPECT_EQ(device.getDepthStencilStateProperty().getReferenceStencilProperty(), 7);
    device.setDepthStencilStateProperty(depth);
    EXPECT_EQ(device.getReferenceStencilProperty(), 7);
}
