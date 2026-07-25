// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "System/ArgumentNullException.hpp"

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

TEST(RenderTargetBindingTest, CtorTextureNullThrowsArgumentNullException)
{
    EXPECT_THROW(RenderTargetBinding(nullptr), System::ArgumentNullException);
}

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

TEST(RenderTargetBindingTest, CtorCubeMapFaceNullThrowsArgumentNullException)
{
    EXPECT_THROW(
        RenderTargetBinding(nullptr, CubeMapFace::NegativeZ),
        System::ArgumentNullException);
}

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

TEST(RenderTargetBindingTest, CtorCubeMapFaceArraySliceIsZero)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr, CubeMapFace::NegativeX);
    EXPECT_EQ(rb.getArraySliceProperty(), 0);
}

// --- Texture* ctor: default cube face ---

TEST(RenderTargetBindingTest, CtorTextureDefaultCubeMapFaceIsPositiveX)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb(ptr);
    EXPECT_EQ(rb.getCubeMapFaceProperty(), CubeMapFace::PositiveX);
}

// --- All six CubeMapFace values round-trip correctly ---

TEST(RenderTargetBindingTest, AllCubeMapFacesRoundTrip)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    const CubeMapFace faces[] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };
    for (auto face : faces)
    {
        RenderTargetBinding rb(ptr, face);
        EXPECT_EQ(rb.getCubeMapFaceProperty(), face);
    }
}

// --- Distinct array slices produce distinct bindings (mip-level scenario) ---

TEST(RenderTargetBindingTest, DistinctArraySlicesAreDistinguishable)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rb0(ptr, 0);
    RenderTargetBinding rb1(ptr, 1);
    EXPECT_EQ(rb0.getArraySliceProperty(), 0);
    EXPECT_EQ(rb1.getArraySliceProperty(), 1);
    EXPECT_NE(rb0.getArraySliceProperty(), rb1.getArraySliceProperty());
}

// --- Distinct cube faces produce distinct bindings ---

TEST(RenderTargetBindingTest, DistinctCubeFacesAreDistinguishable)
{
    auto* ptr = reinterpret_cast<Microsoft::Xna::Framework::Graphics::Texture*>(0x1234);
    RenderTargetBinding rbPX(ptr, CubeMapFace::PositiveX);
    RenderTargetBinding rbNZ(ptr, CubeMapFace::NegativeZ);
    EXPECT_NE(rbPX.getCubeMapFaceProperty(), rbNZ.getCubeMapFaceProperty());
}
