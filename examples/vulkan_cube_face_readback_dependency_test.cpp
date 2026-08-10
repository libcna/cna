// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-194 -- a direct RenderTargetCube face read must flush the exact pending producer.
// The producer identity is the target's immutable face pass retained at one public attachment
// slot by one MRT binding-cycle proxy. The matrix keeps resource, face, mip, slot and cycle
// independently observable with distinct colours and immediate guarded reads.
//
// The whole matrix runs inside one ordinary Game frame. It calls neither Present nor a retry;
// readback closes only the pending off-screen producer work needed by the requested face.

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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
#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_VULKAN
#error "REMED-GFX-194's cube-face dependency matrix is Vulkan-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 16;
    constexpr int kLevelCount = 5;
    constexpr int kGuard = 9;
    constexpr int kSuffix = 13;
    const Color kSentinel(3, 5, 7, 11);
    constexpr std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    int LevelDimension(int level)
    {
        return std::max(1, kSize >> level);
    }

    std::string FaceName(CubeMapFace face)
    {
        static constexpr std::array<const char*, 6> names = {
            "+X(face 0)", "-X(face 1)", "+Y(face 2)",
            "-Y(face 3)", "+Z(face 4)", "-Z(face 5)",
        };
        return names[static_cast<std::size_t>(face)];
    }

    std::string ColorText(const Color& value)
    {
        return "(" + std::to_string(value.getRProperty()) + "," +
               std::to_string(value.getGProperty()) + "," +
               std::to_string(value.getBProperty()) + "," +
               std::to_string(value.getAProperty()) + ")";
    }
}

/** @brief Exercises exact Vulkan cube-face readback dependency selection for REMED-GFX-194. */
class VulkanCubeFaceReadbackDependencyTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTargetCube> heldCubeA_;
    std::unique_ptr<RenderTargetCube> heldCubeB_;
    std::unique_ptr<RenderTarget2D> heldCompanion_;
    VulkanRenderer* vk_ = nullptr;
    uint64_t presentCountAtDraw_ = 0;
    uint64_t submitCountAtDraw_ = 0;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition) ++passed_;
    }

    void Step(const std::string& label) const
    {
        std::printf("[STEP] %s\n", label.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] std::unique_ptr<RenderTargetCube> MakeCube(
        GraphicsDevice& device, const std::string& label)
    {
        Step(label + ": RenderTargetCube(16,mipMap=true,Depth=None,MSAA=0)");
        auto cube = std::make_unique<RenderTargetCube>(
            device, kSize, true, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        Check(cube->getLevelCountProperty() == kLevelCount,
              label + ": owns the full five-level chain");
        return cube;
    }

    [[nodiscard]] std::unique_ptr<RenderTarget2D> MakeCompanion(
        GraphicsDevice& device, const std::string& label)
    {
        Step(label + ": RenderTarget2D(16x16,mipMap=true,Depth=None,MSAA=0)");
        return std::make_unique<RenderTarget2D>(
            device, kSize, kSize, true, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
    }

    void ResetDrawState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        device.setDepthStencilStateProperty(DepthStencilState::None);
    }

    void DrawFull(GraphicsDevice& device, const Color& color)
    {
        BasicEffect effect(device);
        effect.setTextureEnabledProperty(false);
        effect.setLightingEnabledProperty(false);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionColor vertices[6] = {
            { Vector3(-1.f,  1.f, 0.f), color },
            { Vector3( 1.f, -1.f, 0.f), color },
            { Vector3(-1.f, -1.f, 0.f), color },
            { Vector3(-1.f,  1.f, 0.f), color },
            { Vector3( 1.f,  1.f, 0.f), color },
            { Vector3( 1.f, -1.f, 0.f), color },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void ProduceDirect(GraphicsDevice& device, RenderTargetCube& cube, CubeMapFace face,
                       const Color& clear, const Color& draw, const std::string& label)
    {
        ResetDrawState(device);
        Step(label + ": direct bind " + FaceName(face) + ", clear " + ColorText(clear) +
             ", draw " + ColorText(draw) + ", unbind");
        device.SetRenderTarget(&cube, face);
        device.Clear(clear);
        DrawFull(device, draw);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void ProduceMrt(GraphicsDevice& device, RenderTargetCube& cube, CubeMapFace face,
                    RenderTarget2D& companion, bool cubePrimary,
                    const Color& clear, const Color& draw, const std::string& label)
    {
        ResetDrawState(device);
        std::vector<RenderTargetBinding> bindings;
        if (cubePrimary)
        {
            bindings.emplace_back(&cube, face);
            bindings.emplace_back(&companion);
        }
        else
        {
            bindings.emplace_back(&companion);
            bindings.emplace_back(&cube, face);
        }
        Step(label + ": bind " + (cubePrimary ? "{cube,2D}" : "{2D,cube}") +
             " at " + FaceName(face) + ", clear " + ColorText(clear) + ", draw " +
             ColorText(draw) + ", unbind");
        device.SetRenderTargets(bindings);
        device.Clear(clear);
        DrawFull(device, draw);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void ProduceTwoCubes(GraphicsDevice& device,
                         RenderTargetCube& first, CubeMapFace firstFace,
                         RenderTargetCube& second, CubeMapFace secondFace,
                         const Color& clear, const Color& draw, const std::string& label)
    {
        ResetDrawState(device);
        std::vector<RenderTargetBinding> bindings;
        bindings.emplace_back(&first, firstFace);
        bindings.emplace_back(&second, secondFace);
        Step(label + ": bind {first " + FaceName(firstFace) + ",second " +
             FaceName(secondFace) + "}, clear " + ColorText(clear) + ", draw " +
             ColorText(draw) + ", unbind");
        device.SetRenderTargets(bindings);
        device.Clear(clear);
        DrawFull(device, draw);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    template<typename Reader>
    void ExpectRead(Reader&& reader, int requestedCount, int capacity,
                    const Color& expected, const std::string& label)
    {
        std::vector<Color> destination(
            static_cast<std::size_t>(kGuard + capacity + kSuffix), kSentinel);
        reader(destination.data(), kGuard, capacity);

        int wrong = 0;
        std::string firstWrong;
        for (int i = 0; i < requestedCount; ++i)
        {
            const Color& got = destination[static_cast<std::size_t>(kGuard + i)];
            if (!Exact(got, expected))
            {
                if (firstWrong.empty())
                    firstWrong = "index " + std::to_string(i) + " got " + ColorText(got) +
                                 " expected " + ColorText(expected);
                ++wrong;
            }
        }
        if (!firstWrong.empty()) std::printf("        %s\n", firstWrong.c_str());
        Check(wrong == 0, label + ": all " + std::to_string(requestedCount) +
                          " requested texels equal " + ColorText(expected));

        bool prefix = true;
        for (int i = 0; i < kGuard; ++i)
            if (!Exact(destination[static_cast<std::size_t>(i)], kSentinel)) prefix = false;
        Check(prefix, label + ": destination prefix before startIndex is protected");

        bool suffix = true;
        for (int i = requestedCount; i < capacity + kSuffix; ++i)
            if (!Exact(destination[static_cast<std::size_t>(kGuard + i)], kSentinel)) suffix = false;
        Check(suffix, label + ": surplus capacity and protected suffix are untouched");
    }

    void ExpectFull(RenderTargetCube& cube, CubeMapFace face, int level,
                    const Color& expected, const std::string& label)
    {
        const int edge = LevelDimension(level);
        const int count = edge * edge;
        Step(label + ": immediate full GetData(" + FaceName(face) + ",level=" +
             std::to_string(level) + ",startIndex=9,capacity=" +
             std::to_string(count + 5) + ")");
        ExpectRead(
            [&](Color* data, int startIndex, int capacity) {
                cube.GetData(face, level, nullptr, data, startIndex, capacity);
            },
            count, count + 5, expected, label);
    }

    void ExpectRect(RenderTargetCube& cube, CubeMapFace face, int level,
                    const Rectangle& rectangle, const Color& expected,
                    const std::string& label)
    {
        const int count = rectangle.Width * rectangle.Height;
        Step(label + ": immediate rectangular GetData(" + FaceName(face) + ",level=" +
             std::to_string(level) + ",rect=" + std::to_string(rectangle.X) + "," +
             std::to_string(rectangle.Y) + " " + std::to_string(rectangle.Width) + "x" +
             std::to_string(rectangle.Height) + ",startIndex=9)");
        ExpectRead(
            [&](Color* data, int startIndex, int capacity) {
                cube.GetData(face, level, &rectangle, data, startIndex, capacity);
            },
            count, count + 7, expected, label);
    }

    void ExpectMrtMatchSlots(const std::vector<uint32_t>& expectedSlots,
                             const std::string& label)
    {
        const auto& matches = vk_->GetLastMrtReadbackMatchesEXT();
        bool exact = matches.size() == expectedSlots.size();
        bool strictlyIncreasingCycles = true;
        for (std::size_t i = 0; i < matches.size(); ++i)
        {
            if (i < expectedSlots.size() && matches[i].second != expectedSlots[i]) exact = false;
            if (i > 0 && matches[i - 1].first >= matches[i].first)
                strictlyIncreasingCycles = false;
        }
        Check(exact, label + ": exact pending MRT match count and public slot sequence");
        Check(strictlyIncreasingCycles,
              label + ": matched binding cycles are distinct and remain in public order");
    }

    void LegAllSixFacesDirect()
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(device, "A direct cube");
        const std::array<Color, 6> draws = {
            Color(233, 31, 17, 255), Color(211, 53, 29, 255),
            Color(191, 71, 43, 255), Color(173, 89, 61, 255),
            Color(151, 107, 79, 255), Color(137, 127, 97, 255),
        };
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            const Color clear(17 + static_cast<int>(i) * 7,
                              47 + static_cast<int>(i) * 9,
                              101 + static_cast<int>(i) * 11, 255);
            const std::string label = "A" + std::to_string(i) + " direct " + FaceName(kFaces[i]);
            ProduceDirect(device, *cube, kFaces[i], clear, draws[i], label);
            ExpectFull(*cube, kFaces[i], 0, draws[i], label + " level 0");
            ExpectMrtMatchSlots({}, label + " direct read");
            ExpectRect(*cube, kFaces[i], 2, Rectangle(1, 1, 2, 2), draws[i],
                       label + " lower-mip rectangle");
        }
    }

    void LegAllSixFacesMrtSlots()
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(device, "B MRT cube");
        auto companion = MakeCompanion(device, "B MRT companion");
        const std::array<Color, 6> clears = {
            Color(19, 61, 149, 255), Color(31, 79, 163, 255),
            Color(43, 97, 179, 255), Color(59, 113, 193, 255),
            Color(71, 131, 211, 255), Color(83, 151, 227, 255),
        };
        const std::array<Color, 6> draws = {
            Color(241, 43, 23, 255), Color(229, 67, 37, 255),
            Color(217, 89, 53, 255), Color(199, 109, 71, 255),
            Color(181, 127, 89, 255), Color(163, 149, 107, 255),
        };
        for (std::size_t i = 0; i < kFaces.size(); ++i)
        {
            const bool primary = (i & 1u) == 0;
            const std::string label = "B" + std::to_string(i) + " " + FaceName(kFaces[i]) +
                                      (primary ? " primary slot 0" : " secondary slot 1");
            ProduceMrt(device, *cube, kFaces[i], *companion, primary,
                       clears[i], draws[i], label);
            const Color expected = primary ? draws[i] : clears[i];
            ExpectFull(*cube, kFaces[i], 0, expected,
                       label + " direct read immediately after producer");
            ExpectMrtMatchSlots({ primary ? 0u : 1u }, label + " dependency identity");
            ExpectRect(*cube, kFaces[i], 1, Rectangle(2, 3, 4, 3), expected,
                       label + " lower-mip guarded rectangle");
        }
    }

    void LegTwoTargetsAndRepeatedABA()
    {
        auto& device = getGraphicsDeviceProperty();
        auto a = MakeCube(device, "C cube target A");
        auto b = MakeCube(device, "C cube target B");

        const Color clearA(29, 83, 151, 255);
        const Color drawA(239, 47, 31, 255);
        ProduceTwoCubes(device, *a, CubeMapFace::PositiveX,
                       *b, CubeMapFace::NegativeX, clearA, drawA,
                       "C1 cycle A: A.face0 primary, B.face1 secondary");

        const Color clearB(53, 127, 79, 255);
        const Color drawB(211, 73, 167, 255);
        ProduceTwoCubes(device, *b, CubeMapFace::PositiveY,
                       *a, CubeMapFace::NegativeY, clearB, drawB,
                       "C2 cycle B: B.face2 primary, A.face3 secondary");

        const Color clearA2(97, 157, 41, 255);
        const Color drawA2(197, 101, 23, 255);
        ProduceTwoCubes(device, *a, CubeMapFace::PositiveX,
                       *b, CubeMapFace::PositiveZ, clearA2, drawA2,
                       "C3 cycle A again: A.face0 primary, B.face4 secondary");

        // This first read must match both A.face0 producer proxies (C1/C3), preserve their public
        // cycle order, and leave C2 independently selectable. Parent-cube or face-agnostic lookup
        // cannot distinguish this A/B/A sequence.
        ExpectFull(*a, CubeMapFace::PositiveX, 0, drawA2,
                   "C4 A/B/A exact latest A.face0 level 0");
        ExpectMrtMatchSlots({ 0u, 0u },
                            "C4 matches only the two A.face0 binding cycles, not parent-cube C2");
        ExpectRect(*a, CubeMapFace::PositiveX, 2, Rectangle(1, 0, 3, 2), drawA2,
                   "C5 A/B/A exact latest A.face0 lower mip");

        ExpectFull(*b, CubeMapFace::PositiveY, 0, drawB,
                   "C6 independently pending B.face2 primary");
        ExpectMrtMatchSlots({ 0u }, "C6 selects the independently pending B binding cycle");
        ExpectRect(*b, CubeMapFace::PositiveY, 1, Rectangle(1, 2, 5, 3), drawB,
                   "C7 independently pending B.face2 lower mip");
        ExpectFull(*b, CubeMapFace::NegativeX, 1, clearA,
                   "C8 cycle A secondary B.face1 retains its exact producer");
        ExpectFull(*a, CubeMapFace::NegativeY, 1, clearB,
                   "C9 cycle B secondary A.face3 retains its exact producer");
        ExpectFull(*b, CubeMapFace::PositiveZ, 1, clearA2,
                   "C10 cycle A-again secondary B.face4 retains its exact producer");
    }

    void LegDisposalBeforeRead()
    {
        auto& device = getGraphicsDeviceProperty();
        auto cube = MakeCube(device, "D disposal cube");
        auto companion = MakeCompanion(device, "D disposable companion");
        const Color clear(67, 139, 211, 255);
        const Color draw(227, 59, 113, 255);
        ProduceMrt(device, *cube, CubeMapFace::NegativeZ, *companion, true,
                   clear, draw, "D1 cube primary with disposable secondary");
        companion->Dispose();
        companion.reset();
        Step("D2 disposed the other MRT attachment before direct cube read");
        ExpectFull(*cube, CubeMapFace::NegativeZ, 0, draw,
                   "D3 exact cube producer survives companion disposal");
        ExpectMrtMatchSlots({ 0u }, "D3 disposed-companion producer identity");
        ExpectRect(*cube, CubeMapFace::NegativeZ, 2, Rectangle(0, 1, 3, 2), draw,
                   "D4 exact lower-mip producer survives companion disposal");
        cube->Dispose();
        cube.reset();
        Check(true, "D5 cube disposal after the completed readback is clean");
    }

    void LegLiveResourcesAtDeviceTeardown()
    {
        auto& device = getGraphicsDeviceProperty();
        heldCubeA_ = MakeCube(device, "E held cube A");
        heldCubeB_ = MakeCube(device, "E held cube B");
        heldCompanion_ = MakeCompanion(device, "E held companion");

        const Color clearA(41, 101, 181, 255);
        const Color drawA(251, 71, 29, 255);
        ProduceMrt(device, *heldCubeA_, CubeMapFace::PositiveX, *heldCompanion_, false,
                   clearA, drawA, "E1 held cube A secondary slot");
        ExpectFull(*heldCubeA_, CubeMapFace::PositiveX, 1, clearA,
                   "E2 held cube A immediate lower-mip read");
        ExpectMrtMatchSlots({ 1u }, "E2 held cube A secondary-slot identity");

        const Color clearB(89, 163, 47, 255);
        const Color drawB(193, 37, 149, 255);
        ProduceMrt(device, *heldCubeB_, CubeMapFace::NegativeY, *heldCompanion_, true,
                   clearB, drawB, "E3 held cube B primary slot");
        ExpectFull(*heldCubeB_, CubeMapFace::NegativeY, 0, drawB,
                   "E4 held cube B immediate level-0 read");
        ExpectMrtMatchSlots({ 0u }, "E4 held cube B primary-slot identity");
        Check(true, "E5 two cubes and their companion remain live for explicit device teardown");
    }

protected:
    void Draw(const GameTime&) override
    {
        try
        {
            vk_ = dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
            Check(vk_ != nullptr, "Vulkan renderer is reachable from GraphicsDevice");
            if (!vk_)
            {
                Exit();
                return;
            }
            presentCountAtDraw_ = vk_->GetPresentCountEXT();
            submitCountAtDraw_ = vk_->GetFrameSubmitCountEXT();

            LegAllSixFacesDirect();
            LegAllSixFacesMrtSlots();
            LegTwoTargetsAndRepeatedABA();
            LegDisposalBeforeRead();
            LegLiveResourcesAtDeviceTeardown();
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("unexpected exception in cube dependency matrix: ") + e.what());
            try { getGraphicsDeviceProperty().SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); }
            catch (...) {}
        }
        Exit();
    }

    void EndRun() override
    {
        if (vk_)
        {
            Check(VulkanRenderer::IsValidationActiveEXT(),
                  "VK_LAYER_KHRONOS_validation is active");
            const auto& messages = vk_->GetValidationMessagesEXT();
            if (!messages.empty())
                std::printf("        first validation message: %.400s\n", messages.front().c_str());
            Check(messages.empty(),
                  "the complete cube-face dependency matrix emitted no Vulkan validation messages");
            Check(vk_->GetFrameSubmitCountEXT() == submitCountAtDraw_ + 1,
                  "the matrix used exactly the ordinary Game::EndDraw frame submit");
            Check(vk_->GetPresentCountEXT() == presentCountAtDraw_ + 1,
                  "the matrix used exactly the ordinary Game::EndDraw Present");
        }
        std::printf("[INFO] REMED-GFX-194 Vulkan cube-face dependency matrix: %d/%d checks passed\n",
                    passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
    }

public:
    /** @brief Creates the one-frame Vulkan cube-face dependency matrix. */
    VulkanCubeFaceReadbackDependencyTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    /** @brief Returns zero only when every dependency and validation assertion passed. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    VulkanCubeFaceReadbackDependencyTest game;
    game.Run();
    int result = game.Result();
    std::printf("[STEP] explicit GraphicsDevice teardown while both final cube targets remain live\n");
    std::fflush(stdout);
    try
    {
        game.Dispose();
        std::printf("[PASS] explicit device teardown completed; retained cube destruction follows\n");
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] explicit device teardown threw: %s\n", e.what());
        result = 1;
    }
    std::fflush(stdout);
    return result;
}
