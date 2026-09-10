// SPDX-License-Identifier: MS-PL
// SOFTWARE-119: renderer-neutral RenderTargetCube sampling contract.
//
// Every face is rendered blue through the public target-binding path. After unbinding, the same
// object is consumed as TextureCube by EnvironmentMapEffect. Readback must find the blue result;
// this catches a renderer that owns renderable face storage but exposes a different/stale resource
// to the texture path. Face isolation, orientation, mip generation and MSAA are asserted more
// strongly by the companion shared cube-target contracts.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cstdlib>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
    constexpr int kTolerance = 2;

#if defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
#else
    constexpr const char* kRendererName = "renderer";
#endif

    bool MatchesBlue(const Color& color)
    {
        return std::abs(static_cast<int>(color.getRProperty())) <= kTolerance &&
               std::abs(static_cast<int>(color.getGProperty())) <= kTolerance &&
               std::abs(static_cast<int>(color.getBProperty()) - 255) <= kTolerance &&
               std::abs(static_cast<int>(color.getAProperty()) - 255) <= kTolerance;
    }
}

class RenderTargetCubeSampleTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        const Color blue(0, 0, 255, 255);
        const Color white(255, 255, 255, 255);
        RenderTargetCube target(
            device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
        constexpr std::array<CubeMapFace, 6> faces = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
        {
            device.SetRenderTarget(&target, face);
            device.Clear(blue);
        }
        device.SetRenderTargets({});

        Texture2D whiteTexture(device, 1, 1);
        whiteTexture.SetData(&white, 1);
        device.Clear(Color(0, 0, 0, 255));

        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), normal, Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.0f), normal, Vector2(1.0f, 1.0f)},
        };

        EnvironmentMapEffect effect(device);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setTextureProperty(&whiteTexture);
        effect.setEnvironmentMapProperty(&target);
        effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Viewport& viewport = device.getViewportProperty();
        const Rectangle center(
            viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        Color actual(0, 0, 0, 0);
        device.GetBackBufferData(&center, &actual, 0, 1);
        if (MatchesBlue(actual))
        {
            std::printf("[PASS] %s RenderTargetCube samples rendered storage (%u,%u,%u,%u)\n",
                        kRendererName, actual.getRProperty(), actual.getGProperty(),
                        actual.getBProperty(), actual.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] %s RenderTargetCube sample=(%u,%u,%u,%u), expected blue +/- %d\n",
                        kRendererName, actual.getRProperty(), actual.getGProperty(),
                        actual.getBProperty(), actual.getAProperty(), kTolerance);
        }
        Exit();
    }

public:
    RenderTargetCubeSampleTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    RenderTargetCubeSampleTest game;
    game.Run();
    return game.Result();
}
