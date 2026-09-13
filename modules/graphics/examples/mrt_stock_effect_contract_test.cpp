// SPDX-License-Identifier: MS-PL
// SOFTWARE-120: renderer-neutral classic XNA multiple-render-target contract.
//
// BasicEffect emits COLOR0 only, exactly like FNA's stock shader. The first attachment therefore
// receives fragment output while higher attachments retain their explicit clear colour. Clear,
// resolve, mip generation and target transitions still apply to every bound attachment. This is
// intentionally independent of CNAEXT ShaderEffect and does not fabricate extra CPU outputs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsCapability.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    constexpr int kTolerance = 2;
    const Color kClear(11, 37, 83, 149);
    const Color kSource(221, 143, 67, 199);

    bool Near(const Color& actual, const Color& expected, int tolerance = kTolerance)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
               std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
               std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance &&
               std::abs(actual.getAProperty() - expected.getAProperty()) <= tolerance;
    }

    std::string Describe(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + "," +
               std::to_string(color.getGProperty()) + "," +
               std::to_string(color.getBProperty()) + "," +
               std::to_string(color.getAProperty()) + ")";
    }
}

class MrtStockEffectContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition)
            ++passed_;
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(
        DepthFormat depth = DepthFormat::None, int samples = 0, bool mipMap = false,
        RenderTargetUsage usage = RenderTargetUsage::PreserveContents)
    {
        return std::make_unique<RenderTarget2D>(
            getGraphicsDeviceProperty(), kSize, kSize, mipMap, SurfaceFormat::Color,
            depth, samples, usage);
    }

    Color ReadCenter(RenderTarget2D& target, int level = 0)
    {
        const int dimension = std::max(1, kSize >> level);
        const Rectangle center(dimension / 2, dimension / 2, 1, 1);
        Color result(0, 0, 0, 0);
        target.GetData(level, &center, &result, 0, 1);
        return result;
    }

    Color ReadCubeCenter(RenderTargetCube& target, CubeMapFace face, int level = 0)
    {
        const int dimension = std::max(1, kSize >> level);
        const Rectangle center(dimension / 2, dimension / 2, 1, 1);
        Color result(0, 0, 0, 0);
        target.GetData(face, level, &center, &result, 0, 1);
        return result;
    }

    static std::vector<RenderTargetBinding> Bindings(
        const std::vector<RenderTarget2D*>& targets)
    {
        std::vector<RenderTargetBinding> result;
        result.reserve(targets.size());
        for (RenderTarget2D* target : targets)
            result.emplace_back(target);
        return result;
    }

    void DrawFullScreen(const Color& color, float z = 0.5f)
    {
        auto& device = getGraphicsDeviceProperty();
        BasicEffect effect(device);
        effect.setVertexColorEnabledProperty(true);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, z), color},
            {Vector3(-1.0f, -1.0f, z), color},
            {Vector3( 1.0f, -1.0f, z), color},
            {Vector3(-1.0f,  1.0f, z), color},
            {Vector3( 1.0f, -1.0f, z), color},
            {Vector3( 1.0f,  1.0f, z), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void TestFourTargetsAndStockOutput()
    {
        auto& device = getGraphicsDeviceProperty();
        std::array<std::unique_ptr<RenderTarget2D>, 4> owned = {
            MakeTarget(), MakeTarget(), MakeTarget(), MakeTarget()
        };
        std::vector<RenderTarget2D*> targets = {
            owned[0].get(), owned[1].get(), owned[2].get(), owned[3].get()
        };
        device.SetRenderTargets(Bindings(targets));
        const auto current = device.GetRenderTargets();
        Check(current.size() == 4 &&
                  current[0].getRenderTargetProperty() == owned[0].get() &&
                  current[3].getRenderTargetProperty() == owned[3].get(),
              "four ordered public bindings remain observable while active");
        device.Clear(kClear);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawFullScreen(kSource);
        device.SetRenderTargets({});

        Check(Near(ReadCenter(*owned[0]), kSource),
              "BasicEffect COLOR0 reaches MRT attachment 0");
        for (int slot = 1; slot < 4; ++slot)
        {
            const Color actual = ReadCenter(*owned[static_cast<std::size_t>(slot)]);
            Check(Near(actual, kClear),
                  "single-output stock effect leaves cleared attachment " +
                      std::to_string(slot) + " unchanged: " + Describe(actual));
        }
    }

    void TestWriteMasksAndDiscardClear()
    {
        auto& device = getGraphicsDeviceProperty();
        auto first = MakeTarget();
        auto second = MakeTarget();
        device.SetRenderTargets(Bindings({first.get(), second.get()}));
        device.Clear(kClear);
        BlendState masks = BlendState::Opaque;
        masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        masks.setColorWriteChannels1Property(ColorWriteChannels::Green);
        device.setBlendStateProperty(masks);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawFullScreen(kSource);
        device.SetRenderTargets({});

        const Color expectedFirst(
            kSource.getRProperty(), kClear.getGProperty(),
            kClear.getBProperty(), kClear.getAProperty());
        Check(Near(ReadCenter(*first), expectedFirst),
              "ColorWriteChannels0 masks the real COLOR0 output");
        Check(Near(ReadCenter(*second), kClear),
              "ColorWriteChannels1 does not redirect COLOR0 into attachment 1");

        auto discard0 = MakeTarget(
            DepthFormat::None, 0, false, RenderTargetUsage::DiscardContents);
        auto discard1 = MakeTarget(
            DepthFormat::None, 0, false, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(discard0.get());
        device.Clear(Color::Red);
        device.SetRenderTarget(discard1.get());
        device.Clear(Color::Blue);
        device.SetRenderTargets({});
        device.SetRenderTargets(Bindings({discard0.get(), discard1.get()}));
        device.SetRenderTargets({});
        const Color discarded(0, 0, 0, 255);
        Check(Near(ReadCenter(*discard0), discarded) &&
                  Near(ReadCenter(*discard1), discarded),
              "DiscardContents clears every active MRT color attachment");
    }

    void TestDepthOwnership()
    {
        auto& device = getGraphicsDeviceProperty();
        auto depth0 = MakeTarget(DepthFormat::Depth24);
        auto color1 = MakeTarget();
        device.SetRenderTargets(Bindings({depth0.get(), color1.get()}));
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        const Color nearColor(35, 210, 75, 255);
        const Color farColor(225, 45, 55, 255);
        DrawFullScreen(nearColor, 0.2f);
        DrawFullScreen(farColor, 0.8f);
        device.SetRenderTargets({});
        Check(Near(ReadCenter(*depth0), nearColor) && Near(ReadCenter(*color1), kClear),
              "slot zero owns depth while higher stock-output slots remain unchanged");

        auto noDepth0 = MakeTarget();
        auto unusedDepth1 = MakeTarget(DepthFormat::Depth24);
        device.SetRenderTargets(Bindings({noDepth0.get(), unusedDepth1.get()}));
        device.Clear(kClear);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawFullScreen(nearColor, 0.2f);
        DrawFullScreen(farColor, 0.8f);
        device.SetRenderTargets({});
        Check(Near(ReadCenter(*noDepth0), farColor) &&
                  Near(ReadCenter(*unusedDepth1), kClear),
              "a depthless first slot does not borrow a later attachment's depth buffer");
    }

    void TestMsaaAndMips()
    {
        auto& device = getGraphicsDeviceProperty();
        auto msaa0 = MakeTarget(DepthFormat::None, 4);
        auto msaa1 = MakeTarget(DepthFormat::None, 4);
        device.SetRenderTargets(Bindings({msaa0.get(), msaa1.get()}));
        device.Clear(Color(0, 0, 0, 0));
        BlendState oneSample = BlendState::Opaque;
        oneSample.setMultiSampleMaskProperty(1);
        device.setBlendStateProperty(oneSample);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawFullScreen(Color::White);
        device.SetRenderTargets({});
        const Color quarter(64, 64, 64, 64);
        const Color transparent(0, 0, 0, 0);
        Check(msaa0->getMultiSampleCountProperty() == 4 &&
                  Near(ReadCenter(*msaa0), quarter),
              "each active MRT target keeps a real 4x plane and slot 0 resolves one sample");
        Check(Near(ReadCenter(*msaa1), transparent),
              "the unwritten 4x attachment resolves its explicit clear colour");

        auto mip0 = MakeTarget(DepthFormat::None, 0, true);
        auto mip1 = MakeTarget(DepthFormat::None, 0, true);
        device.SetRenderTargets(Bindings({mip0.get(), mip1.get()}));
        device.Clear(kClear);
        device.setBlendStateProperty(BlendState::Opaque);
        DrawFullScreen(kSource);
        device.SetRenderTargets({});
        Check(Near(ReadCenter(*mip0, 1), kSource),
              "MRT attachment 0 generates its mip chain from COLOR0 output");
        Check(Near(ReadCenter(*mip1, 1), kClear),
              "higher MRT attachments generate mips from their own clear storage");
    }

    void TestCubeMembers()
    {
        auto& device = getGraphicsDeviceProperty();
        auto first = MakeTarget();
        auto fourth = MakeTarget();
        RenderTargetCube cube(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTargetCube secondCube(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(first.get()),
            RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::PositiveX),
            RenderTargetBinding(static_cast<Texture*>(&secondCube), CubeMapFace::NegativeX),
            RenderTargetBinding(fourth.get()),
        });
        device.Clear(kClear);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawFullScreen(kSource);
        device.SetRenderTargets({});
        Check(Near(ReadCenter(*first), kSource) &&
                  Near(ReadCubeCenter(cube, CubeMapFace::PositiveX), kClear) &&
                  Near(ReadCubeCenter(secondCube, CubeMapFace::NegativeX), kClear) &&
                  Near(ReadCenter(*fourth), kClear),
              "2D plus faces of two cube resources remain four ordered attachments");

        device.SetRenderTargets({
            RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::PositiveY),
            RenderTargetBinding(first.get()),
        });
        const Color secondClear(17, 61, 109, 233);
        const Color cubeOutput(43, 207, 151, 255);
        device.Clear(secondClear);
        DrawFullScreen(cubeOutput);
        device.SetRenderTargets({});
        Check(Near(ReadCubeCenter(cube, CubeMapFace::PositiveY), cubeOutput) &&
                  Near(ReadCenter(*first), secondClear),
              "a cube face in slot zero receives COLOR0 and owns the set ordering");

        auto msaaSecond = MakeTarget(DepthFormat::None, 4);
        RenderTargetCube msaaCube(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::None, 4,
            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(static_cast<Texture*>(&msaaCube), CubeMapFace::PositiveZ),
            RenderTargetBinding(msaaSecond.get()),
        });
        device.Clear(Color(0, 0, 0, 0));
        BlendState oneSample = BlendState::Opaque;
        oneSample.setMultiSampleMaskProperty(1);
        device.setBlendStateProperty(oneSample);
        DrawFullScreen(Color::White);
        device.SetRenderTargets({});
        Check(msaaCube.getMultiSampleCountProperty() == 4 &&
                  Near(ReadCubeCenter(msaaCube, CubeMapFace::PositiveZ), Color(64, 64, 64, 64)),
              "a multisampled cube MRT slot resolves its selected face");
        Check(Near(ReadCenter(*msaaSecond), Color(0, 0, 0, 0)),
              "an unwritten multisampled 2D peer resolves independently of the cube face");

        auto mipSecond = MakeTarget(DepthFormat::None, 0, true);
        RenderTargetCube mipCube(
            device, kSize, true, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(static_cast<Texture*>(&mipCube), CubeMapFace::NegativeZ),
            RenderTargetBinding(mipSecond.get()),
        });
        device.Clear(kClear);
        device.setBlendStateProperty(BlendState::Opaque);
        DrawFullScreen(kSource);
        device.SetRenderTargets({});
        Check(Near(ReadCubeCenter(mipCube, CubeMapFace::NegativeZ, 1), kSource) &&
                  Near(ReadCenter(*mipSecond, 1), kClear),
              "cube and 2D MRT members generate independent mip chains");

        auto depthPeer = MakeTarget();
        RenderTargetCube depthCube(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(static_cast<Texture*>(&depthCube), CubeMapFace::NegativeY),
            RenderTargetBinding(depthPeer.get()),
        });
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClear, 1.0f, 0);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        const Color nearColor(57, 193, 89, 255);
        DrawFullScreen(nearColor, 0.2f);
        DrawFullScreen(Color::Red, 0.8f);
        device.SetRenderTargets({});
        Check(Near(ReadCubeCenter(depthCube, CubeMapFace::NegativeY), nearColor) &&
                  Near(ReadCenter(*depthPeer), kClear),
              "a cube face in slot zero owns depth for the MRT set");

        auto survivor = MakeTarget(DepthFormat::None, 0, true);
        auto dyingCube = std::make_unique<RenderTargetCube>(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({
            RenderTargetBinding(
                static_cast<Texture*>(dyingCube.get()), CubeMapFace::PositiveX),
            RenderTargetBinding(survivor.get()),
        });
        device.Clear(kClear);
        dyingCube.reset();
        device.SetRenderTargets({});
        Check(Near(ReadCenter(*survivor), kClear) &&
                  Near(ReadCenter(*survivor, 1), kClear),
              "destroying a bound cube detaches its MRT slots and still finalizes live peers");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        Check(device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets),
              "MultipleRenderTargets capability is backed by the public bind path");
        TestFourTargetsAndStockOutput();
        TestWriteMasksAndDiscardClear();
        TestDepthOwnership();
        TestMsaaAndMips();
        TestCubeMembers();

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    MrtStockEffectContractTest()
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
    MrtStockEffectContractTest game;
    game.Run();
    return game.Result();
}
