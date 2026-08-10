// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-190 -- every mipmapped colour attachment of a completed Vulkan MRT pass must
// regenerate its chain from that pass' level 0. The matrix deliberately distinguishes attachment
// 0 from the other attachments: an explicit Clear writes every slot, while a stock BasicEffect
// fragment output writes location 0 only. Thus the primary chain must contain the draw colour and
// the secondary chain must contain the clear colour; reversing the bindings reverses those roles.
//
// Every check runs inside one ordinary Game frame. The fixture never calls Present and verifies
// that the renderer records exactly the one submit/present performed by Game::EndDraw. Readback
// flushes producer work synchronously, but add no public frame or presentation.

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
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_VULKAN
#error "REMED-GFX-190's MRT mip-finalization matrix is Vulkan-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 16;
    constexpr int kLevelCount = 5;
    constexpr int kGuard = 7;
    constexpr int kSuffix = 11;
    const Color kSentinel(3, 5, 7, 9);

    bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& value)
    {
        return "(" + std::to_string(value.getRProperty()) + "," +
               std::to_string(value.getGProperty()) + "," +
               std::to_string(value.getBProperty()) + "," +
               std::to_string(value.getAProperty()) + ")";
    }

    int LevelDimension(int base, int level)
    {
        return std::max(1, base >> level);
    }
}

/** @brief Exercises Vulkan MRT finalization and guarded per-mip readback for REMED-GFX-190. */
class VulkanMrtMipFinalizationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> heldPrimary_;
    std::unique_ptr<RenderTarget2D> heldSecondary_;
    std::unique_ptr<RenderTarget2D> heldUnboundControl_;
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

    [[nodiscard]] std::unique_ptr<RenderTarget2D> Make2D(
        GraphicsDevice& device, bool mipmapped, int samples, const std::string& label)
    {
        Step(label + ": RenderTarget2D(16x16,mipMap=" + (mipmapped ? "true" : "false") +
             ",MSAA=" + std::to_string(samples) + ")");
        return std::make_unique<RenderTarget2D>(
            device, kSize, kSize, mipmapped, SurfaceFormat::Color, DepthFormat::None, samples,
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

    void RenderMrt(GraphicsDevice& device, RenderTarget2D& first, RenderTarget2D& second,
                   const Color& clearColor, const Color& primaryDrawColor,
                   const std::string& label)
    {
        ResetDrawState(device);
        std::vector<RenderTargetBinding> bindings;
        bindings.emplace_back(&first);
        bindings.emplace_back(&second);
        Step(label + ": SetRenderTargets({first,second}), Clear=" + ColorText(clearColor) +
             ", primary draw=" + ColorText(primaryDrawColor) + ", unbind");
        device.SetRenderTargets(bindings);
        device.Clear(clearColor);
        DrawFull(device, primaryDrawColor);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void RenderMrtCube(GraphicsDevice& device, RenderTarget2D& target2D,
                       RenderTargetCube& cube, CubeMapFace face, bool cubeFirst,
                       const Color& clearColor, const Color& primaryDrawColor,
                       const std::string& label)
    {
        ResetDrawState(device);
        std::vector<RenderTargetBinding> bindings;
        if (cubeFirst)
        {
            bindings.emplace_back(&cube, face);
            bindings.emplace_back(&target2D);
        }
        else
        {
            bindings.emplace_back(&target2D);
            bindings.emplace_back(&cube, face);
        }
        Step(label + ": bind " + (cubeFirst ? "{cube face,2D}" : "{2D,cube face}") +
             ", Clear=" + ColorText(clearColor) + ", primary draw=" +
             ColorText(primaryDrawColor) + ", unbind");
        device.SetRenderTargets(bindings);
        device.Clear(clearColor);
        DrawFull(device, primaryDrawColor);
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

    void ExpectFull(RenderTarget2D& target, int level, const Color& expected,
                    const std::string& label)
    {
        const int width = LevelDimension(target.getWidthProperty(), level);
        const int height = LevelDimension(target.getHeightProperty(), level);
        const int count = width * height;
        Step(label + ": full GetData(level=" + std::to_string(level) +
             ",startIndex=7,elementCount=" + std::to_string(count + 3) + ")");
        ExpectRead(
            [&](Color* data, int startIndex, int capacity) {
                target.GetData(level, nullptr, data, startIndex, capacity);
            },
            count, count + 3, expected, label);
    }

    void ExpectFull(RenderTargetCube& target, CubeMapFace face, int level,
                    const Color& expected, const std::string& label)
    {
        const int edge = LevelDimension(target.getSizeProperty(), level);
        const int count = edge * edge;
        Step(label + ": cube full GetData(level=" + std::to_string(level) +
             ",startIndex=7,elementCount=" + std::to_string(count + 3) + ")");
        ExpectRead(
            [&](Color* data, int startIndex, int capacity) {
                target.GetData(face, level, nullptr, data, startIndex, capacity);
            },
            count, count + 3, expected, label);
    }

    void ExpectRect(RenderTarget2D& target, int level, const Rectangle& rectangle,
                    const Color& expected, const std::string& label)
    {
        const int count = rectangle.Width * rectangle.Height;
        Step(label + ": rectangular GetData(level=" + std::to_string(level) + ",rect=(" +
             std::to_string(rectangle.X) + "," + std::to_string(rectangle.Y) + " " +
             std::to_string(rectangle.Width) + "x" + std::to_string(rectangle.Height) +
             "),startIndex=7,elementCount=" + std::to_string(count + 13) + ")");
        ExpectRead(
            [&](Color* data, int startIndex, int capacity) {
                target.GetData(level, &rectangle, data, startIndex, capacity);
            },
            count, count + 13, expected, label);
    }

    void ExpectChain(RenderTarget2D& target, const Color& expected, const std::string& label)
    {
        const int levels = target.getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
            ExpectFull(target, level, expected,
                       label + " level " + std::to_string(level) +
                       (level == levels - 1 ? " (final)" : ""));
    }

    void ExpectChain(RenderTargetCube& target, CubeMapFace face,
                     const Color& expected, const std::string& label)
    {
        const int levels = target.getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
            ExpectFull(target, face, level, expected,
                       label + " level " + std::to_string(level) +
                       (level == levels - 1 ? " (final)" : ""));
    }

    void ExpectInvalidThenValid(RenderTarget2D& target, const Color& validExpected,
                                const std::string& label)
    {
        const int invalidLevel = target.getLevelCountProperty();
        std::vector<Color> destination(23, kSentinel);
        bool outOfRange = false;
        Step(label + ": invalid GetData(level=LevelCount), then a valid final-level read");
        try
        {
            target.GetData(invalidLevel, nullptr, destination.data(), 4, 1);
        }
        catch (const std::out_of_range&)
        {
            outOfRange = true;
        }
        catch (const std::exception& e)
        {
            std::printf("        wrong exception: %s\n", e.what());
        }
        Check(outOfRange, label + ": level == LevelCount is rejected as out_of_range");
        bool untouched = true;
        for (const Color& value : destination)
            if (!Exact(value, kSentinel)) untouched = false;
        Check(untouched, label + ": invalid read leaves the whole destination untouched");
        ExpectFull(target, invalidLevel - 1, validExpected,
                   label + ": valid read after invalid request");
    }

    void LegBothMipmapped(bool multisampled, const std::string& id)
    {
        auto& device = getGraphicsDeviceProperty();
        const int samples = multisampled ? 4 : 0;
        auto a = Make2D(device, true, samples, id + " target A");
        auto b = Make2D(device, true, samples, id + " target B");
        Check(a->getLevelCountProperty() == kLevelCount,
              id + ": target A owns the full five-level chain");
        Check(b->getLevelCountProperty() == kLevelCount,
              id + ": target B owns the full five-level chain");
        if (multisampled)
        {
            Check(a->getMultiSampleCountProperty() > 1,
                  id + ": target A engaged real MSAA for resolve-before-mip coverage");
            Check(b->getMultiSampleCountProperty() == a->getMultiSampleCountProperty(),
                  id + ": both MRT attachments use the same applied sample count");
        }

        const Color clear1(17, 61, 149, 255);
        const Color draw1(233, 41, 19, 255);
        RenderMrt(device, *a, *b, clear1, draw1, id + " cycle 1 A-primary");
        ExpectChain(*a, draw1, id + " primary A");
        ExpectChain(*b, clear1, id + " secondary B");

        const Color clear2(29, 173, 83, 255);
        const Color draw2(211, 107, 37, 255);
        RenderMrt(device, *b, *a, clear2, draw2, id + " cycle 2 B-primary (reordered)");
        ExpectChain(*b, draw2, id + " reordered primary B");
        ExpectChain(*a, clear2, id + " reordered secondary A");
    }

    void LegMixedMipCounts()
    {
        auto& device = getGraphicsDeviceProperty();
        auto mip = Make2D(device, true, 0, "C mipmapped target");
        auto flat = Make2D(device, false, 0, "C non-mipmapped target");
        Check(mip->getLevelCountProperty() == kLevelCount,
              "C mipmapped attachment owns five levels");
        Check(flat->getLevelCountProperty() == 1,
              "C non-mipmapped attachment owns level 0 only");

        const Color clear1(43, 89, 167, 255);
        const Color draw1(239, 53, 101, 255);
        RenderMrt(device, *mip, *flat, clear1, draw1,
                  "C1 mipmapped primary, non-mipmapped secondary");
        ExpectChain(*mip, draw1, "C1 mipmapped primary");
        ExpectFull(*flat, 0, clear1, "C1 non-mipmapped secondary level 0");
        ExpectInvalidThenValid(*flat, clear1, "C1 non-mipmapped secondary");

        const Color clear2(71, 131, 47, 255);
        const Color draw2(193, 67, 227, 255);
        RenderMrt(device, *flat, *mip, clear2, draw2,
                  "C2 non-mipmapped primary, mipmapped secondary (reordered)");
        ExpectFull(*flat, 0, draw2, "C2 non-mipmapped primary level 0");
        ExpectChain(*mip, clear2, "C2 mipmapped secondary");
        ExpectRect(*mip, 1, Rectangle(1, 2, 3, 4), clear2,
                   "C3 secondary level-1 rectangle");
        ExpectRect(*mip, 2, Rectangle(1, 0, 2, 3), clear2,
                   "C4 secondary intermediate-level rectangle");
        ExpectInvalidThenValid(*mip, clear2, "C5 mipmapped secondary");
    }

    void LegRepeatedCycles()
    {
        auto& device = getGraphicsDeviceProperty();
        auto a = Make2D(device, true, 4, "D repeated target A");
        auto b = Make2D(device, true, 4, "D repeated target B");
        const std::array<Color, 6> clears = {
            Color(11, 71, 131, 255), Color(23, 83, 143, 255), Color(37, 97, 157, 255),
            Color(47, 109, 173, 255), Color(59, 127, 181, 255), Color(73, 139, 197, 255),
        };
        const std::array<Color, 6> draws = {
            Color(241, 31, 17, 255), Color(229, 43, 29, 255), Color(217, 59, 41, 255),
            Color(203, 71, 53, 255), Color(191, 89, 67, 255), Color(179, 101, 79, 255),
        };
        for (std::size_t cycle = 0; cycle < clears.size(); ++cycle)
        {
            const std::string label = "D cycle " + std::to_string(cycle);
            if ((cycle & 1u) == 0)
            {
                RenderMrt(device, *a, *b, clears[cycle], draws[cycle], label + " A-primary");
                ExpectFull(*a, 1, draws[cycle], label + " primary level 1");
                ExpectFull(*b, 1, clears[cycle], label + " secondary level 1");
            }
            else
            {
                RenderMrt(device, *b, *a, clears[cycle], draws[cycle], label + " B-primary");
                ExpectFull(*b, 1, draws[cycle], label + " primary level 1");
                ExpectFull(*a, 1, clears[cycle], label + " secondary level 1");
            }
        }
        ExpectChain(*b, draws.back(), "D final primary B after six cycles");
        ExpectChain(*a, clears.back(), "D final secondary A after six cycles");
    }

    void LegDisposal()
    {
        auto& device = getGraphicsDeviceProperty();
        auto survivorSecondary = Make2D(device, true, 0, "E1 secondary survivor");
        {
            auto dyingPrimary = Make2D(device, true, 0, "E1 dying primary");
            ResetDrawState(device);
            std::vector<RenderTargetBinding> bindings;
            bindings.emplace_back(dyingPrimary.get());
            bindings.emplace_back(survivorSecondary.get());
            const Color clear(31, 151, 79, 255);
            const Color draw(227, 47, 113, 255);
            Step("E1 bind {dying primary,surviving secondary}, produce, unbind, then dispose "
                 "primary before the deferred producer flushes");
            device.SetRenderTargets(bindings);
            device.Clear(clear);
            DrawFull(device, draw);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dyingPrimary->Dispose();
            dyingPrimary.reset();
            ExpectChain(*survivorSecondary, clear,
                        "E1 surviving secondary finalized after primary disposal");
        }
        survivorSecondary->Dispose();
        survivorSecondary.reset();
        Check(true, "E1 surviving secondary disposes cleanly after its full chain read");

        auto survivorPrimary = Make2D(device, true, 0, "E2 primary survivor");
        {
            auto dyingSecondary = Make2D(device, true, 0, "E2 dying secondary");
            ResetDrawState(device);
            std::vector<RenderTargetBinding> bindings;
            bindings.emplace_back(survivorPrimary.get());
            bindings.emplace_back(dyingSecondary.get());
            const Color clear(53, 137, 191, 255);
            const Color draw(199, 73, 29, 255);
            Step("E2 bind {surviving primary,dying secondary}, produce, unbind, then dispose "
                 "secondary before the deferred producer flushes");
            device.SetRenderTargets(bindings);
            device.Clear(clear);
            DrawFull(device, draw);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dyingSecondary->Dispose();
            dyingSecondary.reset();
            ExpectChain(*survivorPrimary, draw,
                        "E2 surviving primary finalized after secondary disposal");
        }
        survivorPrimary->Dispose();
        survivorPrimary.reset();
        Check(true, "E2 surviving primary disposes cleanly after its full chain read");
    }

    void LegCubeFaceAttachments()
    {
        auto& device = getGraphicsDeviceProperty();
        auto target2D = Make2D(device, true, 0, "F 2D target");
        auto cube = std::make_unique<RenderTargetCube>(
            device, kSize, true, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        Check(cube->getLevelCountProperty() == kLevelCount,
              "F cube target owns the full five-level chain");
        constexpr CubeMapFace kFace = CubeMapFace::NegativeZ;

        const Color clear1(41, 113, 179, 255);
        const Color draw1(251, 67, 23, 255);
        RenderMrtCube(device, *target2D, *cube, kFace, false, clear1, draw1,
                      "F1 2D primary, cube-face secondary");
        ExpectChain(*target2D, draw1, "F1 2D primary");
        ExpectChain(*cube, kFace, clear1, "F1 cube-face secondary");

        const Color clear2(83, 157, 61, 255);
        const Color draw2(223, 37, 137, 255);
        RenderMrtCube(device, *target2D, *cube, kFace, true, clear2, draw2,
                      "F2 cube-face primary, 2D secondary (reordered)");

        // REMED-GFX-194: the direct cube-face read must select this face's exact target pass from
        // the pending MRT proxy, including its reordered primary slot, and record that producer
        // before the copy. This used to return clear1 from the prior completed cycle.
        ExpectFull(*cube, kFace, 0, draw2,
                   "F2 direct cube read selects the pending reordered primary producer");
        ExpectChain(*target2D, clear2, "F2 2D secondary");
        ExpectChain(*cube, kFace, draw2,
                    "F2 cube-face primary chain after direct producer selection");
    }

    void LegDeviceTeardownAndUnboundControl()
    {
        auto& device = getGraphicsDeviceProperty();
        heldUnboundControl_ = Make2D(device, true, 0, "G unbound control");
        const Color untouchedColor(97, 43, 181, 255);
        ResetDrawState(device);
        device.SetRenderTarget(heldUnboundControl_.get());
        device.Clear(untouchedColor);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ExpectChain(*heldUnboundControl_, untouchedColor, "G unbound control before MRT work");

        heldPrimary_ = Make2D(device, true, 4, "G held primary");
        heldSecondary_ = Make2D(device, true, 4, "G held secondary");
        const Color clear(67, 149, 211, 255);
        const Color draw(237, 79, 31, 255);
        RenderMrt(device, *heldPrimary_, *heldSecondary_, clear, draw,
                  "G held MRT set before device teardown");
        ExpectChain(*heldPrimary_, draw, "G held primary");
        ExpectChain(*heldSecondary_, clear, "G held secondary");
        ExpectChain(*heldUnboundControl_, untouchedColor,
                    "G resource never bound in the MRT remains byte-exact");
        Check(true, "G three live mipmapped targets are retained for explicit device teardown");
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

            LegBothMipmapped(false, "A non-MSAA both-mipmapped");
            LegBothMipmapped(true, "B real-MSAA both-mipmapped");
            LegMixedMipCounts();
            LegRepeatedCycles();
            LegDisposal();
            LegCubeFaceAttachments();
            LegDeviceTeardownAndUnboundControl();
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("unexpected exception while running MRT matrix: ") + e.what());
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
            Check(messages.empty(), "the full MRT mip matrix emitted no Vulkan validation messages");
            Check(vk_->GetFrameSubmitCountEXT() == submitCountAtDraw_ + 1,
                  "the matrix used exactly the ordinary Game::EndDraw frame submit");
            Check(vk_->GetPresentCountEXT() == presentCountAtDraw_ + 1,
                  "the matrix used exactly the ordinary Game::EndDraw Present");
        }
        std::printf("[INFO] REMED-GFX-190 Vulkan MRT mip matrix: %d/%d checks passed\n",
                    passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
    }

public:
    /** @brief Creates the one-frame Vulkan MRT matrix with a real multisampled device. */
    VulkanMrtMipFinalizationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    /** @brief Returns zero only when every matrix and validation assertion passed. */
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    VulkanMrtMipFinalizationTest game;
    game.Run();
    int result = game.Result();
    std::printf("[STEP] explicit GraphicsDevice teardown while the final MRT resources remain live\n");
    std::fflush(stdout);
    try
    {
        game.Dispose();
        std::printf("[PASS] explicit device teardown completed; retained target destruction follows\n");
    }
    catch (const std::exception& e)
    {
        std::printf("[FAIL] explicit device teardown threw: %s\n", e.what());
        result = 1;
    }
    std::fflush(stdout);
    return result;
}
