// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    bool BindOrSkip(GraphicsDevice& device, RenderTarget2D& target)
    {
        try
        {
            device.SetRenderTarget(&target);
            return true;
        }
        catch (const System::NotSupportedException&)
        {
            return false;
        }
    }
}

TEST(TextureDataBindingContractTest, PixelBoundTexture2DRejectsWritesButAllowsReadback)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    const std::array<Color, 4> initial{Color::Red, Color::Green, Color::Blue, Color::White};
    const std::array<Color, 4> replacement{Color::Black, Color::Black, Color::Black, Color::Black};
    texture.SetData(initial.data(), static_cast<int>(initial.size()));
    device.getTexturesProperty()(0, &texture);

    EXPECT_THROW(texture.SetData(replacement.data(), static_cast<int>(replacement.size())),
                 System::InvalidOperationException);
    EXPECT_THROW(texture.SetData(0, nullptr, replacement.data(), 0,
                                 static_cast<int>(replacement.size())),
                 System::InvalidOperationException);

    std::array<Color, 4> actual{};
    EXPECT_NO_THROW(texture.GetData(actual.data(), static_cast<int>(actual.size())));
    EXPECT_EQ(actual, initial);

    device.getTexturesProperty()(0, nullptr);
    EXPECT_NO_THROW(texture.SetData(replacement.data(), static_cast<int>(replacement.size())));
}

TEST(TextureDataBindingContractTest, Texture2DResourceUsePrecedesLevelAndCopyWindowValidation)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    std::array<Color, 4> data{};
    device.getTexturesProperty()(0, &texture);

    try
    {
        texture.SetData(1, nullptr, data.data(), -1, 0);
        FAIL() << "expected the bound-resource failure";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
        EXPECT_NE(std::string(exception.what()).find("resource is in use"), std::string::npos);
    }
    catch (...)
    {
        FAIL() << "unexpected exception type";
    }
}

TEST(TextureDataBindingContractTest, VertexBoundTexture2DRejectsWrites)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    Texture2D texture(device, 2, 2, false, SurfaceFormat::Single);
    const std::array<float, 4> data{0.0f, 0.25f, 0.5f, 1.0f};
    texture.SetData(data.data(), static_cast<int>(data.size()));
    device.getVertexTexturesProperty()(0, &texture);

    EXPECT_THROW(texture.SetData(data.data(), static_cast<int>(data.size())),
                 System::InvalidOperationException);
}

TEST(TextureDataBindingContractTest, PixelBoundTextureCubeRejectsWrites)
{
    GraphicsDevice device;
    TextureCube texture(device, 2, false, SurfaceFormat::Color);
    const std::array<Color, 4> data{Color::Red, Color::Green, Color::Blue, Color::White};
    try
    {
        texture.SetData(CubeMapFace::PositiveX, data.data(), static_cast<int>(data.size()));
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer does not store TextureCube data";
    }
    device.getTexturesProperty()(0, &texture);

    EXPECT_THROW(texture.SetData(CubeMapFace::PositiveX, data.data(),
                                 static_cast<int>(data.size())),
                 System::InvalidOperationException);
}

TEST(TextureDataBindingContractTest, PixelBoundTexture3DRejectsWrites)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        GTEST_SKIP() << "this renderer does not support Texture3D";

    Texture3D texture(device, 2, 2, 2, false, SurfaceFormat::Color);
    const std::array<Color, 8> data{
        Color::Red, Color::Green, Color::Blue, Color::White,
        Color::Black, Color::Yellow, Color::Magenta, Color::CornflowerBlue};
    texture.SetData(data.data(), static_cast<int>(data.size()));
    device.getTexturesProperty()(0, &texture);

    EXPECT_THROW(texture.SetData(data.data(), static_cast<int>(data.size())),
                 System::InvalidOperationException);
}

TEST(TextureDataBindingContractTest, ActiveRenderTarget2DRejectsWritesAndReadback)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 2, 2);
    if (!BindOrSkip(device, target))
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";

    const std::array<Color, 4> data{Color::Red, Color::Green, Color::Blue, Color::White};
    std::array<Color, 4> destination{};
    EXPECT_THROW(target.SetData(data.data(), static_cast<int>(data.size())),
                 System::InvalidOperationException);
    EXPECT_THROW(target.GetData(destination.data(), static_cast<int>(destination.size())),
                 System::InvalidOperationException);

    device.SetRenderTarget(nullptr);
    EXPECT_NO_THROW(target.SetData(data.data(), static_cast<int>(data.size())));
    EXPECT_NO_THROW(target.GetData(destination.data(), static_cast<int>(destination.size())));
}

TEST(TextureDataBindingContractTest, ActiveRenderTarget2DPrecedesLevelAndCopyWindowValidation)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 2, 2);
    if (!BindOrSkip(device, target))
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";

    std::array<Color, 4> destination{};
    try
    {
        target.GetData(1, nullptr, destination.data(), -1, 0);
        FAIL() << "expected the active-render-target failure";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
        EXPECT_NE(std::string(exception.what()).find("render target"), std::string::npos);
    }
    catch (...)
    {
        FAIL() << "unexpected exception type";
    }
}

TEST(TextureDataBindingContractTest, ActiveRenderTargetCubeRejectsWritesAndReadback)
{
    GraphicsDevice device;
    std::unique_ptr<RenderTargetCube> target;
    try
    {
        target = std::make_unique<RenderTargetCube>(
            device, 2, false, SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(target.get(), CubeMapFace::PositiveX);
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "this renderer does not support RenderTargetCube";
    }

    const std::array<Color, 4> data{Color::Red, Color::Green, Color::Blue, Color::White};
    std::array<Color, 4> destination{};
    EXPECT_THROW(target->SetData(CubeMapFace::PositiveX, data.data(),
                                 static_cast<int>(data.size())),
                 System::InvalidOperationException);
    EXPECT_THROW(target->GetData(CubeMapFace::PositiveX, destination.data(),
                                 static_cast<int>(destination.size())),
                 System::InvalidOperationException);
}
