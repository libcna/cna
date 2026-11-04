// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/ArgumentNullException.hpp"
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
    // The XNA in-use guard no longer applies after unbinding; verify the upload resumes.
    texture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, replacement);
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
    if (device.GetRenderer().ClassifySurfaceFormatEXT(
            static_cast<int>(SurfaceFormat::Single)) !=
        CNA::Internal::Renderers::RendererFormatVerdict::Supported)
        GTEST_SKIP() << "the active renderer cannot create a vertex-sampleable 2D texture";
    Texture2D texture(device, 2, 2, false, SurfaceFormat::Single);
    const std::array<float, 4> data{0.0f, 0.25f, 0.5f, 1.0f};
    texture.SetData(data.data(), static_cast<int>(data.size()));
    device.getVertexTexturesProperty()(0, &texture);

    const std::array<float, 4> replacement{-1.0f, -2.0f, -3.0f, -4.0f};
    EXPECT_THROW(texture.SetData(replacement.data(), static_cast<int>(replacement.size())),
                 System::InvalidOperationException);

    // Texture2D.CopyData IL_0035-00df refuses only writes; a bound texture is still readable, and
    // the refused upload changed nothing.
    std::array<float, 4> actual{};
    texture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, data);
    device.getVertexTexturesProperty()(0, nullptr);
    texture.SetData(replacement.data(), static_cast<int>(replacement.size()));
    texture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, replacement);
}

// XNA walks EVERY slot of both collections before a write: Textures for i < _maxTextures
// (Texture2D.CopyData IL_003c-0081) and VertexTextures for i < _maxTextures (IL_008e-00d3), with
// _maxTextures 16 for pixel samplers and 4 for HiDef vertex samplers. A texture bound only in the
// LAST slot of either collection is therefore still "in use" (0x80004004 -> InvalidOperation-
// Exception(ResourceInUse), Helpers.GetExceptionFromResult IL_0247-0251) -- an off-by-one scan
// that stops one slot early would let the write through.
TEST(TextureDataBindingContractTest, TheLastPixelAndVertexSlotsStillRejectWrites)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);

    Texture2D colorTexture(device, 2, 2);
    const std::array<Color, 4> initial{Color::Red, Color::Green, Color::Blue, Color::White};
    const std::array<Color, 4> replacement{Color(1, 2, 3, 4), Color(5, 6, 7, 8),
                                            Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
    colorTexture.SetData(initial.data(), static_cast<int>(initial.size()));
    device.getTexturesProperty()(15, &colorTexture);
    try
    {
        colorTexture.SetData(replacement.data(), static_cast<int>(replacement.size()));
        ADD_FAILURE() << "a texture bound in pixel slot 15 accepted a write";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
        EXPECT_NE(std::string(exception.what()).find("resource is in use"), std::string::npos);
    }
    std::array<Color, 4> actual{};
    colorTexture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, initial);
    device.getTexturesProperty()(15, nullptr);

    if (device.GetRenderer().ClassifySurfaceFormatEXT(
            static_cast<int>(SurfaceFormat::Single)) !=
        CNA::Internal::Renderers::RendererFormatVerdict::Supported)
        GTEST_SKIP() << "the active renderer cannot create a vertex-sampleable 2D texture";
    Texture2D floatTexture(device, 2, 2, false, SurfaceFormat::Single);
    const std::array<float, 4> floats{0.5f, 1.5f, 2.5f, 3.5f};
    floatTexture.SetData(floats.data(), static_cast<int>(floats.size()));
    device.getVertexTexturesProperty()(3, &floatTexture);
    const std::array<float, 4> otherFloats{9.0f, 8.0f, 7.0f, 6.0f};
    try
    {
        floatTexture.SetData(otherFloats.data(), static_cast<int>(otherFloats.size()));
        ADD_FAILURE() << "a texture bound in vertex slot 3 accepted a write";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
        EXPECT_NE(std::string(exception.what()).find("resource is in use"), std::string::npos);
    }
    std::array<float, 4> actualFloats{};
    floatTexture.GetData(actualFloats.data(), static_cast<int>(actualFloats.size()));
    EXPECT_EQ(actualFloats, floats);
    device.getVertexTexturesProperty()(3, nullptr);
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

    const std::array<Color, 4> replacement{Color(1,2,3,4), Color(5,6,7,8),
                                            Color(9,10,11,12), Color(13,14,15,16)};
    EXPECT_THROW(texture.SetData(CubeMapFace::PositiveX, replacement.data(),
                                 static_cast<int>(data.size())),
                 System::InvalidOperationException);
    EXPECT_THROW(texture.SetData(CubeMapFace::PositiveX, 1, nullptr,
                                 data.data(), -1, 0),
                 System::InvalidOperationException);
    EXPECT_THROW(texture.SetData(CubeMapFace::PositiveX, 1, nullptr,
                                 static_cast<const Color*>(nullptr), -1, 0),
                 System::ArgumentNullException);
    // XNA resource-in-use validation precedes upload. Readback may observe the old data.
    std::array<Color, 4> actual{};
    texture.GetData(CubeMapFace::PositiveX, actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, data);
    device.getTexturesProperty()(0, nullptr);
    texture.SetData(CubeMapFace::PositiveX, replacement.data(), static_cast<int>(replacement.size()));
    texture.GetData(CubeMapFace::PositiveX, actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, replacement);
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

    const std::array<Color, 8> replacement{
        Color(1,2,3,4), Color(5,6,7,8), Color(9,10,11,12), Color(13,14,15,16),
        Color(17,18,19,20), Color(21,22,23,24), Color(25,26,27,28), Color(29,30,31,32)};
    EXPECT_THROW(texture.SetData(replacement.data(), static_cast<int>(replacement.size())),
                 System::InvalidOperationException);
    // XNA resource-in-use validation precedes upload, and unbinding restores writability.
    std::array<Color, 8> actual{};
    texture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, data);
    device.getTexturesProperty()(0, nullptr);
    texture.SetData(replacement.data(), static_cast<int>(replacement.size()));
    texture.GetData(actual.data(), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, replacement);
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
    // Once resolved, the target is an ordinary texture again: XNA's CopyData pool-DEFAULT route
    // (Texture2D.CopyData IL_0187-0302) stages the upload through a system-memory copy, so the
    // data a later GetData reads back is exactly what SetData wrote.
    EXPECT_NO_THROW(target.SetData(data.data(), static_cast<int>(data.size())));
    try
    {
        target.GetData(destination.data(), static_cast<int>(destination.size()));
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "the active renderer validates render-target use but has no readback path";
    }
    EXPECT_EQ(destination, data);
}

// RRC-047: XNA refuses every data read from an active render target; this was held strictly only
// by an EasyGL-specific program while the shared pixel program accepted the exact content too. The
// refusal is the shared layer's, so it is asserted here for every GetData overload, with the
// destination untouched, and for a target bound as the SECOND slot of a multiple-target set.
TEST(TextureDataBindingContractTest, ActiveRenderTarget2DRefusesEveryReadOverloadAndKeepsTheDestination)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    RenderTarget2D target(device, 2, 2);
    if (!BindOrSkip(device, target))
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";

    const Color sentinel(0xCD, 0xCD, 0xCD, 0xCD);
    const auto refusesUntouched = [&](const char* overload, const auto& read) {
        std::array<Color, 4> destination{};
        destination.fill(sentinel);
        try
        {
            read(destination.data());
            ADD_FAILURE() << overload << ": the read of an active render target was accepted";
        }
        catch (const System::InvalidOperationException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException)) << overload;
        }
        catch (const std::exception& exception)
        {
            ADD_FAILURE() << overload << ": threw " << typeid(exception).name() << ": "
                          << exception.what();
        }
        for (const Color& pixel : destination)
            EXPECT_EQ(pixel, sentinel) << overload << ": the destination was written";
    };

    const Microsoft::Xna::Framework::Rectangle whole(0, 0, 2, 2);
    refusesUntouched("GetData(data, count)", [&](Color* d) { target.GetData(d, 4); });
    refusesUntouched("GetData(data, start, count)", [&](Color* d) { target.GetData(d, 0, 4); });
    refusesUntouched("GetData(level, null rect, ...)",
                     [&](Color* d) { target.GetData(0, nullptr, d, 0, 4); });
    refusesUntouched("GetData(level, rect, ...)", [&](Color* d) { target.GetData(0, &whole, d, 0, 4); });

    // Bound as the second of two targets: still active, still refused.
    RenderTarget2D first(device, 2, 2);
    try
    {
        device.SetRenderTargets({Microsoft::Xna::Framework::Graphics::RenderTargetBinding(&first),
                                 Microsoft::Xna::Framework::Graphics::RenderTargetBinding(&target)});
    }
    catch (const System::NotSupportedException&)
    {
        device.SetRenderTarget(nullptr);
        return; // a renderer without multiple render targets has asserted all it can above
    }
    refusesUntouched("GetData while bound as the second target", [&](Color* d) { target.GetData(d, 4); });
    device.SetRenderTarget(nullptr);
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
    EXPECT_THROW(target->GetData(CubeMapFace::PositiveX, 1, nullptr,
                                 destination.data(), -1, 0),
                 System::InvalidOperationException);
    EXPECT_THROW(target->GetData(CubeMapFace::PositiveX, 1, nullptr,
                                 static_cast<Color*>(nullptr), -1, 0),
                 System::ArgumentNullException);
}
