// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-025: what `AcquireThreadContextLeaseEXT()` means on Vulkan, and
// what it deliberately does NOT mean.
//
// The contract
// ------------
// `IGraphicsRenderer::AcquireThreadContextLeaseEXT` returns "a lifetime token, or null when this
// renderer needs no explicit context lease". EasyGL returns a real token: it takes a
// `std::recursive_mutex` and makes its GL context current on the calling thread, because a GL
// context is thread-affine and a `ContentReader` constructed on another thread cannot touch the
// GPU without one. `ContentReader`'s constructor holds such a lease for its whole lifetime
// (`ContentReader.cpp:136`), and `GraphicsDeviceManager::BeginDraw` holds one for the whole frame
// (`GraphicsDeviceManager.cpp:354`) -- which is why EasyGL's mutex has to be recursive.
//
// Vulkan has no thread-affine context, so it takes the null default. This file pins that answer
// and, more importantly, pins its SCOPE.
//
// What the null does not promise
// ------------------------------
// Null means "no context to lease". It does not mean "safe to call from any thread": this
// renderer contains no synchronization at all -- zero `std::mutex`, `std::lock_guard`,
// `std::scoped_lock` or `std::atomic` across its whole implementation, measured 2026-09-05. A
// game that drives it from two threads races regardless of the lease, and the lease is not the
// mechanism that would fix that. VULKAN-025's own text forbids the alternative -- adding a mutex
// here would serialize lease holders while every other entry point stayed unguarded, which is a
// thread-safety guarantee the renderer does not have.
//
// Four legs:
//   A  both `RendererThreadContextLeaseRelease` enumerators return null. Testing one would leave
//      the other free to diverge.
//   B  nesting is safe. The framework already nests -- a ContentReader lease inside the frame
//      lease -- and on a renderer that took a non-recursive lock this would deadlock. A hang is
//      caught by the ctest TIMEOUT rather than by an assertion, which is the only way to observe
//      a deadlock from inside the process that deadlocked.
//   C  the scenario the contract exists for: a GPU resource is created while a lease is held and
//      is then usable. This is what a ContentReader does inside a frame.
//   D  the control. The texture created under C actually renders, so leg C cannot pass by
//      creating something unusable.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
using CNA::Internal::Renderers::RendererThreadContextLeaseRelease;

namespace {

class ThreadContextLeaseTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    bool done_     = false;
    int  failures_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        sb_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        if (renderer == nullptr) {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }

        // ---- leg A: both enumerators ---------------------------------------------
        {
            auto restore = renderer->AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
            check(restore == nullptr,
                  "A1 RestorePreviousBinding yields no token -- there is no thread-affine "
                  "context to bind, so there is nothing for a token to own");
        }
        {
            auto release = renderer->AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::ReleaseRendererBinding);
            check(release == nullptr,
                  "A2 ReleaseRendererBinding yields no token either");
        }

        // ---- leg B: nesting, which the framework already does ---------------------
        // This Draw already runs inside GraphicsDeviceManager's frame lease. Taking two more
        // here reproduces a ContentReader opened during a frame. A renderer holding a
        // non-recursive lock would stop here and the ctest TIMEOUT would report it.
        // Taken through the renderer's own virtual: GraphicsDevice's wrapper is private
        // to the framework, and the virtual is what this row is about anyway.
        {
            auto outer = renderer->AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
            auto inner = renderer->AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
            check(outer == nullptr && inner == nullptr,
                  "B  two nested leases inside the frame lease both yield null and neither "
                  "blocks");
        }

        // ---- leg C: a GPU resource created while a lease is held ------------------
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);

        Texture2D made;
        {
            auto lease = renderer->AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease::RestorePreviousBinding);
            const std::vector<std::uint8_t> blue = { 0, 0, 255, 255 };
            made = Texture2D::CreateFromPixels(device, 1, 1, blue);
        }
        check(made.getWidthProperty() == 1 && made.getHeightProperty() == 1,
              "C  a Texture2D created while a lease was held exists (" +
                  std::to_string(made.getWidthProperty()) + "x" +
                  std::to_string(made.getHeightProperty()) + ")");

        // ---- leg D: and it renders ------------------------------------------------
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(made, Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        const Rectangle centre(W / 2, H / 2, 1, 1);
        Color px(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &px, 0, 1);
        check(px.getBProperty() >= 200 && px.getRProperty() <= 60,
              "D  and that texture renders: centre=(" + std::to_string(px.getRProperty()) + "," +
                  std::to_string(px.getGProperty()) + "," + std::to_string(px.getBProperty()) +
                  ") -- so leg C did not pass by creating something unusable");

        Exit();
    }

public:
    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    ThreadContextLeaseTest game;
    game.Run();
    return game.getResult();
}
