// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"

using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;

// --- Default constructor ---

TEST(RenderTargetBindingTest, DefaultRenderTargetNull)
{
    RenderTargetBinding rb;
    EXPECT_EQ(rb.getRenderTargetProperty(), nullptr);
}

TEST(RenderTargetBindingTest, DefaultArraySliceZero)
{
    RenderTargetBinding rb;
    EXPECT_EQ(rb.getArraySliceProperty(), 0);
}

TEST(RenderTargetBindingTest, DefaultCubeMapFacePositiveX)
{
    RenderTargetBinding rb;
    EXPECT_EQ(rb.getCubeMapFaceProperty(), CubeMapFace::PositiveX);
}

// --- Texture* constructor ---

TEST(RenderTargetBindingTest, CtorTextureStoresPointer)
{
    // Use a non-null sentinel address without dereferencing
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr);
    EXPECT_EQ(rb.getRenderTargetProperty(), ptr);
}

TEST(RenderTargetBindingTest, CtorTextureDefaultArraySlice)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr);
    EXPECT_EQ(rb.getArraySliceProperty(), 0);
}

TEST(RenderTargetBindingTest, CtorTextureExplicitArraySlice)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr, 2);
    EXPECT_EQ(rb.getArraySliceProperty(), 2);
}

// --- Cube face constructor ---

TEST(RenderTargetBindingTest, CtorCubeMapFace)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr, CubeMapFace::NegativeZ);
    EXPECT_EQ(rb.getCubeMapFaceProperty(), CubeMapFace::NegativeZ);
}

TEST(RenderTargetBindingTest, CtorCubeMapFaceStoresPointer)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x5678);
    RenderTargetBinding rb(ptr, CubeMapFace::PositiveY);
    EXPECT_EQ(rb.getRenderTargetProperty(), ptr);
}
