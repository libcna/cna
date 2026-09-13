// SPDX-License-Identifier: MS-PL
//
// Renderer-neutral public replacement for the portable half of REMED-GFX-164. Reading a
// multisampled render target while it is still active may either return the exact current image or
// reject the operation without touching the destination. Silent stale resolve data is never valid,
// and either outcome must leave the active render-target set usable by subsequent commands.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;
    constexpr int kGuard = 5;
    const Color kSentinel(61, 7, 149, 203);
    const Color kFirst(37, 113, 229, 73);
    const Color kSecond(205, 31, 77, 93);

    bool Same(const Color& lhs, const Color& rhs)
    {
        return lhs.getRProperty() == rhs.getRProperty() &&
               lhs.getGProperty() == rhs.getGProperty() &&
               lhs.getBProperty() == rhs.getBProperty() &&
               lhs.getAProperty() == rhs.getAProperty();
    }

    class ActiveMsaaReadbackTest final : public Game
    {
    public:
        ActiveMsaaReadbackTest()
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(32);
            graphics_->setPreferredBackBufferHeightProperty(32);
        }

        [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }

    protected:
        void Draw(const GameTime&) override
        {
            if (ran_) return;
            ran_ = true;

            auto& device = getGraphicsDeviceProperty();
            RunSingleTarget(device);
            RunMrt(device);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::printf("Result: %d/%d PASS\n", passed_, passed_ + failed_);
            Exit();
        }

    private:
        enum class ActiveReadOutcome
        {
            Exact,
            Rejected,
            Invalid,
        };

        std::unique_ptr<GraphicsDeviceManager> graphics_;
        bool ran_ = false;
        int passed_ = 0;
        int failed_ = 0;

        void Check(bool condition, const std::string& label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
            if (condition) ++passed_; else ++failed_;
        }

        static std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& device)
        {
            return std::make_unique<RenderTarget2D>(
                device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 4,
                RenderTargetUsage::PreserveContents);
        }

        ActiveReadOutcome ReadActiveRectangle(RenderTarget2D& target, const Color& expected,
                                              const std::string& label)
        {
            const Rectangle rect(2, 1, 3, 5);
            const int count = rect.Width * rect.Height;
            std::vector<Color> pixels(static_cast<std::size_t>(kGuard + count + kGuard), kSentinel);

            bool rejected = false;
            bool otherException = false;
            try
            {
                target.GetData(0, &rect, pixels.data(), kGuard, count);
            }
            catch (const System::NotSupportedException&)
            {
                rejected = true;
            }
            catch (...)
            {
                otherException = true;
            }

            bool guardsExact = true;
            bool windowExact = true;
            bool untouched = true;
            int exactPixels = 0;
            int sentinelPixels = 0;
            int transparentPixels = 0;
            for (int i = 0; i < kGuard; ++i)
            {
                guardsExact = guardsExact && Same(pixels[static_cast<std::size_t>(i)], kSentinel);
                guardsExact = guardsExact &&
                    Same(pixels[static_cast<std::size_t>(kGuard + count + i)], kSentinel);
            }
            for (int i = 0; i < count; ++i)
            {
                const Color& pixel = pixels[static_cast<std::size_t>(kGuard + i)];
                windowExact = windowExact && Same(pixel, expected);
                untouched = untouched && Same(pixel, kSentinel);
                if (Same(pixel, expected)) ++exactPixels;
                if (Same(pixel, kSentinel)) ++sentinelPixels;
                if (Same(pixel, Color::Transparent)) ++transparentPixels;
            }

            const bool exact = !rejected && !otherException && guardsExact && windowExact;
            const bool honestRejection = rejected && !otherException && guardsExact && untouched;
            const std::string facts =
                " [outcome=" + std::string(exact ? "exact" : honestRejection ? "rejected" : "invalid") +
                " exact=" + std::to_string(exactPixels) + "/" + std::to_string(count) +
                " sentinel=" + std::to_string(sentinelPixels) +
                " transparent=" + std::to_string(transparentPixels) +
                " guards=" + (guardsExact ? "exact" : "modified") +
                " otherException=" + (otherException ? "yes" : "no") + "]";
            Check(exact || honestRejection,
                  label + " returns exact RGBA or rejects with the guarded destination untouched" +
                      facts);
            if (exact) return ActiveReadOutcome::Exact;
            if (honestRejection) return ActiveReadOutcome::Rejected;
            return ActiveReadOutcome::Invalid;
        }

        void CheckUniform(RenderTarget2D& target, const Color& expected, const std::string& label)
        {
            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, kSentinel);
            bool read = true;
            try
            {
                target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
            }
            catch (...)
            {
                read = false;
            }
            bool exact = read;
            for (const Color& pixel : pixels) exact = exact && Same(pixel, expected);
            Check(exact, label);
        }

        void RunSingleTarget(GraphicsDevice& device)
        {
            auto target = MakeTarget(device);
            Check(target->getMultiSampleCountProperty() > 1,
                  "single target applies a genuinely multisampled attachment");

            device.SetRenderTarget(target.get());
            device.Clear(kFirst);
            const ActiveReadOutcome first = ReadActiveRectangle(
                *target, kFirst, "single-target active MSAA read");
            device.Clear(kSecond);
            const ActiveReadOutcome second = ReadActiveRectangle(
                *target, kSecond, "repeated active MSAA read after continued rendering");
            Check(first != ActiveReadOutcome::Invalid && first == second,
                  "single-target active read outcome is stable across continued rendering");

            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckUniform(*target, kSecond,
                         "single target remains writable and resolves the post-read clear");
        }

        void RunMrt(GraphicsDevice& device)
        {
            auto first = MakeTarget(device);
            auto second = MakeTarget(device);
            Check(first->getMultiSampleCountProperty() > 1 &&
                      second->getMultiSampleCountProperty() > 1,
                  "MRT pair applies genuinely multisampled attachments");

            device.SetRenderTargets({RenderTargetBinding(first.get()),
                                     RenderTargetBinding(second.get())});
            device.Clear(kFirst);
            const ActiveReadOutcome firstOutcome = ReadActiveRectangle(
                *first, kFirst, "active MRT slot 0 MSAA read");
            const ActiveReadOutcome secondOutcome = ReadActiveRectangle(
                *second, kFirst, "active MRT slot 1 MSAA read");
            Check(firstOutcome != ActiveReadOutcome::Invalid && firstOutcome == secondOutcome,
                  "both active MRT slots use the same honest readback contract");

            device.Clear(kSecond);
            device.SetRenderTargets({});
            CheckUniform(*first, kSecond,
                         "active read preserves MRT slot 0 for subsequent rendering");
            CheckUniform(*second, kSecond,
                         "active read preserves MRT slot 1 for subsequent rendering");
        }
    };
}

int main()
{
    ActiveMsaaReadbackTest game;
    game.Run();
    return game.Result();
}
