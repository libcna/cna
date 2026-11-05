// SPDX-License-Identifier: MS-PL
// TQ-048: independent Refresh entry-point contracts.
#include <gtest/gtest.h>
#include <typeinfo>
#include <string>
#include <vector>
#include <limits>
#include "System/EventArgs.hpp"
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

// Refresh Apply sets isBound permanently; no device setter clears the outgoing state's flag.
// Each ThrowIfBound IL_0000-0037 reads only isBound, including after Dispose.
TEST(GraphicsDeviceEntryStateTest, ReplacingAndDisposingBoundStatesNeverMakesThemMutable)
{
    GraphicsDevice device;
    BlendState blend;
    DepthStencilState depth;
    RasterizerState rasterizer;
    SamplerState sampler;
    device.setBlendStateProperty(blend);
    device.setDepthStencilStateProperty(depth);
    device.setRasterizerStateProperty(rasterizer);
    device.getSamplerStatesProperty()(0, sampler);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
    device.getSamplerStatesProperty()(0, SamplerState::LinearWrap);
    for (bool disposed : {false, true})
    {
        SCOPED_TRACE(disposed);
        if (disposed)
        {
            blend.Dispose(); depth.Dispose(); rasterizer.Dispose(); sampler.Dispose();
        }
        ExpectReadOnly("BlendState", [&] { blend.setMultiSampleMaskProperty(0); });
        ExpectReadOnly("DepthStencilState", [&] { depth.setReferenceStencilProperty(9); });
        ExpectReadOnly("RasterizerState", [&] { rasterizer.setScissorTestEnableProperty(true); });
        ExpectReadOnly("SamplerState", [&] { sampler.setMaxMipLevelProperty(3); });
        EXPECT_EQ(blend.getMultiSampleMaskProperty(), -1);
        EXPECT_EQ(depth.getReferenceStencilProperty(), 0);
        EXPECT_FALSE(rasterizer.getScissorTestEnableProperty());
        EXPECT_EQ(sampler.getMaxMipLevelProperty(), 0);
    }
}

namespace
{
    template<class TState, class TBind>
    void CheckUnboundDisposal(const char* typeName, TBind bind)
    {
        TState state;
        TState tag; // A distinct live System::Object; no resource allocation required.
        EXPECT_EQ(state.getGraphicsDeviceProperty(), nullptr);
        EXPECT_TRUE(state.getNameProperty().empty()); // C++ empty string represents managed null.
        EXPECT_EQ(state.getTagProperty(), nullptr);
        EXPECT_FALSE(state.getIsDisposedProperty());
        int events = 0;
        System::Object* sender = nullptr;
        state.Disposing += [&](System::Object* actual, const System::EventArgs&)
        {
            ++events;
            sender = actual;
            EXPECT_TRUE(state.getIsDisposedProperty());
        };
        // GraphicsResource::~GraphicsResource IL_0000-0026: flag before event, once only.
        state.Dispose();
        state.Dispose();
        EXPECT_EQ(events, 1);
        EXPECT_EQ(sender, &state);
        EXPECT_TRUE(state.getIsDisposedProperty());
        // Name/Tag setters IL_0000-0027 do not check disposal or binding.
        state.setNameProperty("metadata-after-dispose");
        state.setTagProperty(&tag);
        EXPECT_EQ(state.getNameProperty(), "metadata-after-dispose");
        EXPECT_EQ(state.ToString(), "metadata-after-dispose");
        EXPECT_EQ(state.getTagProperty(), &tag);
        GraphicsDevice device;
        try { bind(device, state); ADD_FAILURE() << "disposed state bound"; }
        catch (const System::ObjectDisposedException& error)
        {
            // Each state Apply IL_0000-001c uses the type name, not Resource.Name.
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), typeName);
        }
        EXPECT_EQ(state.getGraphicsDeviceProperty(), nullptr);
        state.setTagProperty(nullptr);
        EXPECT_EQ(state.getTagProperty(), nullptr);
    }
}

TEST(GraphicsDeviceEntryStateTest, BlendStateDisposalKeepsMetadataAndNamesBindingRefusal)
{
    CheckUnboundDisposal<BlendState>("BlendState",
        [](GraphicsDevice& d, const BlendState& s) { d.setBlendStateProperty(s); });
}

TEST(GraphicsDeviceEntryStateTest, DepthStencilStateDisposalKeepsMetadataAndNamesBindingRefusal)
{
    CheckUnboundDisposal<DepthStencilState>("DepthStencilState",
        [](GraphicsDevice& d, const DepthStencilState& s) { d.setDepthStencilStateProperty(s); });
}

TEST(GraphicsDeviceEntryStateTest, RasterizerStateDisposalKeepsMetadataAndNamesBindingRefusal)
{
    CheckUnboundDisposal<RasterizerState>("RasterizerState",
        [](GraphicsDevice& d, const RasterizerState& s) { d.setRasterizerStateProperty(s); });
}

TEST(GraphicsDeviceEntryStateTest, SamplerStateDisposalKeepsMetadataAndNamesBindingRefusal)
{
    CheckUnboundDisposal<SamplerState>("SamplerState",
        [](GraphicsDevice& d, const SamplerState& s) { d.getSamplerStatesProperty()(0, s); });
}
