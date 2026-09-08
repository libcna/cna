// SPDX-License-Identifier: MS-PL
// SOFTWARE-179: renderer-independent GraphicsProfile resource ceilings.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

TEST(GraphicsProfileResourceCeilingTest, ReachRejectsOversizedTexturesVolumeTexturesAndMrt)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);

    EXPECT_THROW((void)Texture2D(device, 2049, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)TextureCube(device, 513, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)Texture3D(device, 1, 1, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);

    RenderTarget2D first(device, 1, 1);
    RenderTarget2D second(device, 1, 1);
    EXPECT_THROW(
        device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)}),
        System::NotSupportedException);
}

TEST(GraphicsProfileResourceCeilingTest, HiDefPermitsVolumeAndMrtButEnforcesItsOwnLimits)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    Texture2D texture(device, 1, 1, false, SurfaceFormat::Color);
    TextureCube cube(device, 1, false, SurfaceFormat::Color);
    Texture3D volume(device, 1, 1, 1, false, SurfaceFormat::Color);
    EXPECT_THROW((void)Texture2D(device, 4097, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)TextureCube(device, 4097, false, SurfaceFormat::Color),
                 System::NotSupportedException);
    EXPECT_THROW((void)Texture3D(device, 257, 1, 1, false, SurfaceFormat::Color),
                 System::NotSupportedException);

    RenderTarget2D first(device, 1, 1);
    RenderTarget2D second(device, 1, 1);
    EXPECT_NO_THROW(
        device.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)}));
    device.SetRenderTargets({});
}
