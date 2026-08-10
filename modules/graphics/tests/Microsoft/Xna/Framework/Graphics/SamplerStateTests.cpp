// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

// --- Default constructor ---

TEST(SamplerStateTest, DefaultAddressUWrap)
{
    SamplerState ss;
    EXPECT_EQ(ss.getAddressUProperty(), TextureAddressMode::Wrap);
}

TEST(SamplerStateTest, DefaultAddressVWrap)
{
    SamplerState ss;
    EXPECT_EQ(ss.getAddressVProperty(), TextureAddressMode::Wrap);
}

TEST(SamplerStateTest, DefaultAddressWWrap)
{
    SamplerState ss;
    EXPECT_EQ(ss.getAddressWProperty(), TextureAddressMode::Wrap);
}

TEST(SamplerStateTest, DefaultFilterLinear)
{
    SamplerState ss;
    EXPECT_EQ(ss.getFilterProperty(), TextureFilter::Linear);
}

TEST(SamplerStateTest, DefaultMaxAnisotropyFour)
{
    SamplerState ss;
    EXPECT_EQ(ss.getMaxAnisotropyProperty(), 4);
}

TEST(SamplerStateTest, DefaultMaxMipLevelZero)
{
    SamplerState ss;
    EXPECT_EQ(ss.getMaxMipLevelProperty(), 0);
}

TEST(SamplerStateTest, DefaultMipMapLodBiasZero)
{
    SamplerState ss;
    EXPECT_FLOAT_EQ(ss.getMipMapLevelOfDetailBiasProperty(), 0.0f);
}

// --- Predefined: LinearClamp ---

TEST(SamplerStateTest, LinearClampFilter)
{
    EXPECT_EQ(SamplerState::LinearClamp.getFilterProperty(), TextureFilter::Linear);
}

TEST(SamplerStateTest, LinearClampAddressU)
{
    EXPECT_EQ(SamplerState::LinearClamp.getAddressUProperty(), TextureAddressMode::Clamp);
}

TEST(SamplerStateTest, LinearClampAddressV)
{
    EXPECT_EQ(SamplerState::LinearClamp.getAddressVProperty(), TextureAddressMode::Clamp);
}

TEST(SamplerStateTest, LinearClampAddressW)
{
    EXPECT_EQ(SamplerState::LinearClamp.getAddressWProperty(), TextureAddressMode::Clamp);
}

// --- Predefined: LinearWrap ---

TEST(SamplerStateTest, LinearWrapFilter)
{
    EXPECT_EQ(SamplerState::LinearWrap.getFilterProperty(), TextureFilter::Linear);
}

TEST(SamplerStateTest, LinearWrapAddressU)
{
    EXPECT_EQ(SamplerState::LinearWrap.getAddressUProperty(), TextureAddressMode::Wrap);
}

TEST(SamplerStateTest, LinearWrapAddressV)
{
    EXPECT_EQ(SamplerState::LinearWrap.getAddressVProperty(), TextureAddressMode::Wrap);
}

// --- Predefined: PointClamp ---

TEST(SamplerStateTest, PointClampFilter)
{
    EXPECT_EQ(SamplerState::PointClamp.getFilterProperty(), TextureFilter::Point);
}

TEST(SamplerStateTest, PointClampAddressU)
{
    EXPECT_EQ(SamplerState::PointClamp.getAddressUProperty(), TextureAddressMode::Clamp);
}

TEST(SamplerStateTest, PointClampAddressV)
{
    EXPECT_EQ(SamplerState::PointClamp.getAddressVProperty(), TextureAddressMode::Clamp);
}

// --- Predefined: PointWrap ---

TEST(SamplerStateTest, PointWrapFilter)
{
    EXPECT_EQ(SamplerState::PointWrap.getFilterProperty(), TextureFilter::Point);
}

TEST(SamplerStateTest, PointWrapAddressU)
{
    EXPECT_EQ(SamplerState::PointWrap.getAddressUProperty(), TextureAddressMode::Wrap);
}

TEST(SamplerStateTest, PointWrapAddressV)
{
    EXPECT_EQ(SamplerState::PointWrap.getAddressVProperty(), TextureAddressMode::Wrap);
}

// --- Predefined: AnisotropicClamp ---

TEST(SamplerStateTest, AnisotropicClampFilter)
{
    EXPECT_EQ(SamplerState::AnisotropicClamp.getFilterProperty(), TextureFilter::Anisotropic);
}

TEST(SamplerStateTest, AnisotropicClampAddressU)
{
    EXPECT_EQ(SamplerState::AnisotropicClamp.getAddressUProperty(), TextureAddressMode::Clamp);
}

// --- Predefined: AnisotropicWrap ---

TEST(SamplerStateTest, AnisotropicWrapFilter)
{
    EXPECT_EQ(SamplerState::AnisotropicWrap.getFilterProperty(), TextureFilter::Anisotropic);
}

TEST(SamplerStateTest, AnisotropicWrapAddressU)
{
    EXPECT_EQ(SamplerState::AnisotropicWrap.getAddressUProperty(), TextureAddressMode::Wrap);
}

// --- Setters ---

TEST(SamplerStateTest, SetAddressU)
{
    SamplerState ss;
    ss.setAddressUProperty(TextureAddressMode::Mirror);
    EXPECT_EQ(ss.getAddressUProperty(), TextureAddressMode::Mirror);
}

TEST(SamplerStateTest, SetAddressV)
{
    SamplerState ss;
    ss.setAddressVProperty(TextureAddressMode::Clamp);
    EXPECT_EQ(ss.getAddressVProperty(), TextureAddressMode::Clamp);
}

TEST(SamplerStateTest, SetFilter)
{
    SamplerState ss;
    ss.setFilterProperty(TextureFilter::Point);
    EXPECT_EQ(ss.getFilterProperty(), TextureFilter::Point);
}

TEST(SamplerStateTest, SetMaxAnisotropy)
{
    SamplerState ss;
    ss.setMaxAnisotropyProperty(16);
    EXPECT_EQ(ss.getMaxAnisotropyProperty(), 16);
}

TEST(SamplerStateTest, SetMaxMipLevel)
{
    SamplerState ss;
    ss.setMaxMipLevelProperty(4);
    EXPECT_EQ(ss.getMaxMipLevelProperty(), 4);
}

TEST(SamplerStateTest, SetMipMapLodBias)
{
    SamplerState ss;
    ss.setMipMapLevelOfDetailBiasProperty(-0.5f);
    EXPECT_FLOAT_EQ(ss.getMipMapLevelOfDetailBiasProperty(), -0.5f);
}

// --- Preset Name (Task 291: FNA sets Name on every preset; CNA previously did not) ---

TEST(SamplerStateTest, AnisotropicClampName)
{
    EXPECT_EQ(SamplerState::AnisotropicClamp.getNameProperty(), "SamplerState.AnisotropicClamp");
}

TEST(SamplerStateTest, AnisotropicWrapName)
{
    EXPECT_EQ(SamplerState::AnisotropicWrap.getNameProperty(), "SamplerState.AnisotropicWrap");
}

TEST(SamplerStateTest, LinearClampName)
{
    EXPECT_EQ(SamplerState::LinearClamp.getNameProperty(), "SamplerState.LinearClamp");
}

TEST(SamplerStateTest, LinearWrapName)
{
    EXPECT_EQ(SamplerState::LinearWrap.getNameProperty(), "SamplerState.LinearWrap");
}

TEST(SamplerStateTest, PointClampName)
{
    EXPECT_EQ(SamplerState::PointClamp.getNameProperty(), "SamplerState.PointClamp");
}

TEST(SamplerStateTest, PointWrapName)
{
    EXPECT_EQ(SamplerState::PointWrap.getNameProperty(), "SamplerState.PointWrap");
}

TEST(SamplerStateTest, PresetToStringReturnsName)
{
    EXPECT_EQ(SamplerState::PointClamp.ToString(), "SamplerState.PointClamp");
}

TEST(SamplerStateTest, DefaultConstructedNameIsEmpty)
{
    SamplerState ss;
    EXPECT_TRUE(ss.getNameProperty().empty());
}
