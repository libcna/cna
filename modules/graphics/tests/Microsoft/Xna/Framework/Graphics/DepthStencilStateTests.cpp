// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;

// --- Default constructor ---

TEST(DepthStencilStateTest, DefaultDepthBufferEnabled)
{
    DepthStencilState ds;
    EXPECT_TRUE(ds.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, DefaultDepthBufferWriteEnabled)
{
    DepthStencilState ds;
    EXPECT_TRUE(ds.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, DefaultDepthBufferFunctionLessEqual)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

TEST(DepthStencilStateTest, DefaultStencilDisabled)
{
    DepthStencilState ds;
    EXPECT_FALSE(ds.getStencilEnableProperty());
}

TEST(DepthStencilStateTest, DefaultStencilFunctionAlways)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilFunctionProperty(), CompareFunction::Always);
}

TEST(DepthStencilStateTest, DefaultStencilMask)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilMaskProperty(), -1);
}

TEST(DepthStencilStateTest, DefaultStencilWriteMask)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilWriteMaskProperty(), -1);
}

TEST(DepthStencilStateTest, DefaultReferenceStencilZero)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getReferenceStencilProperty(), 0);
}

TEST(DepthStencilStateTest, DefaultStencilOperationsKeep)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilFailProperty(),            StencilOperation::Keep);
    EXPECT_EQ(ds.getStencilDepthBufferFailProperty(), StencilOperation::Keep);
    EXPECT_EQ(ds.getStencilPassProperty(),            StencilOperation::Keep);
}

TEST(DepthStencilStateTest, DefaultTwoSidedStencilModeDisabled)
{
    DepthStencilState ds;
    EXPECT_FALSE(ds.getTwoSidedStencilModeProperty());
}

TEST(DepthStencilStateTest, DefaultCCWStencilFunctionAlways)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getCounterClockwiseStencilFunctionProperty(), CompareFunction::Always);
}

TEST(DepthStencilStateTest, DefaultCCWStencilOperationsKeep)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getCounterClockwiseStencilFailProperty(),            StencilOperation::Keep);
    EXPECT_EQ(ds.getCounterClockwiseStencilDepthBufferFailProperty(), StencilOperation::Keep);
    EXPECT_EQ(ds.getCounterClockwiseStencilPassProperty(),            StencilOperation::Keep);
}

// --- Predefined: Default ---

TEST(DepthStencilStateTest, PresetDefaultDepthEnabled)
{
    EXPECT_TRUE(DepthStencilState::Default.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetDefaultDepthWriteEnabled)
{
    EXPECT_TRUE(DepthStencilState::Default.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, PresetDefaultDepthFunctionLessEqual)
{
    EXPECT_EQ(DepthStencilState::Default.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

TEST(DepthStencilStateTest, PresetMasksDefaultToMinusOne)
{
    for (const DepthStencilState* state : {
             &DepthStencilState::Default, &DepthStencilState::DepthRead,
             &DepthStencilState::None})
    {
        EXPECT_EQ(state->getStencilMaskProperty(), -1);
        EXPECT_EQ(state->getStencilWriteMaskProperty(), -1);
    }
}

// --- Predefined: DepthRead ---

TEST(DepthStencilStateTest, PresetDepthReadDepthEnabled)
{
    EXPECT_TRUE(DepthStencilState::DepthRead.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetDepthReadWriteDisabled)
{
    EXPECT_FALSE(DepthStencilState::DepthRead.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, PresetDepthReadDepthFunctionLessEqual)
{
    EXPECT_EQ(DepthStencilState::DepthRead.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

// --- Predefined: None ---

TEST(DepthStencilStateTest, PresetNoneDepthDisabled)
{
    EXPECT_FALSE(DepthStencilState::None.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetNoneDepthWriteDisabled)
{
    EXPECT_FALSE(DepthStencilState::None.getDepthBufferWriteEnableProperty());
}

// --- Setters ---

TEST(DepthStencilStateTest, SetDepthBufferEnable)
{
    DepthStencilState ds;
    ds.setDepthBufferEnableProperty(false);
    EXPECT_FALSE(ds.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, SetDepthBufferWriteEnable)
{
    DepthStencilState ds;
    ds.setDepthBufferWriteEnableProperty(false);
    EXPECT_FALSE(ds.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, SetDepthBufferFunction)
{
    DepthStencilState ds;
    ds.setDepthBufferFunctionProperty(CompareFunction::Less);
    EXPECT_EQ(ds.getDepthBufferFunctionProperty(), CompareFunction::Less);
}

TEST(DepthStencilStateTest, SetStencilEnable)
{
    DepthStencilState ds;
    ds.setStencilEnableProperty(true);
    EXPECT_TRUE(ds.getStencilEnableProperty());
}

TEST(DepthStencilStateTest, SetStencilFunction)
{
    DepthStencilState ds;
    ds.setStencilFunctionProperty(CompareFunction::Equal);
    EXPECT_EQ(ds.getStencilFunctionProperty(), CompareFunction::Equal);
}

TEST(DepthStencilStateTest, SetStencilPass)
{
    DepthStencilState ds;
    ds.setStencilPassProperty(StencilOperation::Replace);
    EXPECT_EQ(ds.getStencilPassProperty(), StencilOperation::Replace);
}

TEST(DepthStencilStateTest, SetReferenceStencil)
{
    DepthStencilState ds;
    ds.setReferenceStencilProperty(42);
    EXPECT_EQ(ds.getReferenceStencilProperty(), 42);
}

TEST(DepthStencilStateTest, SetTwoSidedStencilMode)
{
    DepthStencilState ds;
    ds.setTwoSidedStencilModeProperty(true);
    EXPECT_TRUE(ds.getTwoSidedStencilModeProperty());
}

// --- Preset Name (Task 311: FNA sets Name on every preset; CNA previously did not - same gap
// already found and fixed in SamplerState (Task 291) and BlendState (Task 301); tracked as the
// DepthStencilState portion of Task 866) ---

TEST(DepthStencilStateTest, DefaultName)
{
    EXPECT_EQ(DepthStencilState::Default.getNameProperty(), "DepthStencilState.Default");
}

TEST(DepthStencilStateTest, DepthReadName)
{
    EXPECT_EQ(DepthStencilState::DepthRead.getNameProperty(), "DepthStencilState.DepthRead");
}

TEST(DepthStencilStateTest, NoneName)
{
    EXPECT_EQ(DepthStencilState::None.getNameProperty(), "DepthStencilState.None");
}

TEST(DepthStencilStateTest, PresetToStringReturnsName)
{
    EXPECT_EQ(DepthStencilState::Default.ToString(), "DepthStencilState.Default");
}

TEST(DepthStencilStateTest, DefaultConstructedNameIsEmpty)
{
    DepthStencilState ds;
    EXPECT_TRUE(ds.getNameProperty().empty());
}

// Refresh DepthStencilState::set_* IL_0000-000d: ThrowIfBound, then store verbatim.
// Neither enum validation nor disposal validation occurs in an unbound setter.
TEST(DepthStencilStateTest, UnboundEnumSettersStoreNamedAndUnnamedValuesEvenAfterDispose)
{
    for (bool disposed : {false, true})
    {
        SCOPED_TRACE(disposed);
        DepthStencilState state;
        if (disposed) state.Dispose();
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("DepthBufferFunction");
            SCOPED_TRACE(ordinal);
            state.setDepthBufferFunctionProperty(static_cast<CompareFunction>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getDepthBufferFunctionProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("StencilFunction");
            SCOPED_TRACE(ordinal);
            state.setStencilFunctionProperty(static_cast<CompareFunction>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getStencilFunctionProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("CounterClockwiseStencilFunction");
            SCOPED_TRACE(ordinal);
            state.setCounterClockwiseStencilFunctionProperty(static_cast<CompareFunction>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getCounterClockwiseStencilFunctionProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("StencilFail");
            SCOPED_TRACE(ordinal);
            state.setStencilFailProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getStencilFailProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("StencilDepthBufferFail");
            SCOPED_TRACE(ordinal);
            state.setStencilDepthBufferFailProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getStencilDepthBufferFailProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("StencilPass");
            SCOPED_TRACE(ordinal);
            state.setStencilPassProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getStencilPassProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("CounterClockwiseStencilFail");
            SCOPED_TRACE(ordinal);
            state.setCounterClockwiseStencilFailProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getCounterClockwiseStencilFailProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("CounterClockwiseStencilDepthBufferFail");
            SCOPED_TRACE(ordinal);
            state.setCounterClockwiseStencilDepthBufferFailProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getCounterClockwiseStencilDepthBufferFailProperty()), ordinal);
        }
        for (int ordinal = -1; ordinal <= 8; ++ordinal)
        {
            SCOPED_TRACE("CounterClockwiseStencilPass");
            SCOPED_TRACE(ordinal);
            state.setCounterClockwiseStencilPassProperty(static_cast<StencilOperation>(ordinal));
            EXPECT_EQ(static_cast<int>(state.getCounterClockwiseStencilPassProperty()), ordinal);
        }
        EXPECT_EQ(state.getIsDisposedProperty(), disposed);
    }
}

// Refresh DepthStencilState::set_* IL_0000-000d stores each independent field.
TEST(DepthStencilStateTest, UnboundScalarSettersRemainIndependentEvenAfterDispose)
{
    for (bool disposed : {false, true})
    {
        SCOPED_TRACE(disposed);
        DepthStencilState state;
        if (disposed) state.Dispose();
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(true);
        state.setStencilEnableProperty(true);
        state.setTwoSidedStencilModeProperty(true);
        state.setStencilMaskProperty(0x1357);
        state.setStencilWriteMaskProperty(0x2468);
        state.setReferenceStencilProperty(256);
        EXPECT_FALSE(state.getDepthBufferEnableProperty());
        EXPECT_TRUE(state.getDepthBufferWriteEnableProperty());
        EXPECT_TRUE(state.getStencilEnableProperty());
        EXPECT_TRUE(state.getTwoSidedStencilModeProperty());
        EXPECT_EQ(state.getStencilMaskProperty(), 0x1357);
        EXPECT_EQ(state.getStencilWriteMaskProperty(), 0x2468);
        EXPECT_EQ(state.getReferenceStencilProperty(), 256);
        state.setStencilFailProperty(StencilOperation::Replace);
        state.setStencilDepthBufferFailProperty(StencilOperation::Invert);
        state.setStencilPassProperty(StencilOperation::Increment);
        state.setCounterClockwiseStencilFailProperty(StencilOperation::Zero);
        state.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Decrement);
        state.setCounterClockwiseStencilPassProperty(StencilOperation::IncrementSaturation);
        state.setDepthBufferFunctionProperty(CompareFunction::Less);
        state.setStencilFunctionProperty(CompareFunction::Equal);
        state.setCounterClockwiseStencilFunctionProperty(CompareFunction::Greater);
        EXPECT_EQ(state.getStencilFailProperty(), StencilOperation::Replace);
        EXPECT_EQ(state.getStencilDepthBufferFailProperty(), StencilOperation::Invert);
        EXPECT_EQ(state.getStencilPassProperty(), StencilOperation::Increment);
        EXPECT_EQ(state.getCounterClockwiseStencilFailProperty(), StencilOperation::Zero);
        EXPECT_EQ(state.getCounterClockwiseStencilDepthBufferFailProperty(), StencilOperation::Decrement);
        EXPECT_EQ(state.getCounterClockwiseStencilPassProperty(), StencilOperation::IncrementSaturation);
        EXPECT_EQ(state.getDepthBufferFunctionProperty(), CompareFunction::Less);
        EXPECT_EQ(state.getStencilFunctionProperty(), CompareFunction::Equal);
        EXPECT_EQ(state.getCounterClockwiseStencilFunctionProperty(), CompareFunction::Greater);
        for (int value : {-1, 0, 256})
        {
            state.setReferenceStencilProperty(value);
            EXPECT_EQ(state.getReferenceStencilProperty(), value);
        }
    }
}
