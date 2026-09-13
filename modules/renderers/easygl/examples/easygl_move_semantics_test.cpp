// SPDX-License-Identifier: MS-PL
// SOFTWARE-288: Moved C++ resources must transfer device ownership and live bindings.
//
// After std::move(src) into dst:
//   - src.HasRenderer() == false  (handle ownership transferred)
//   - dst.HasRenderer() == true
// When both go out of scope: only dst frees the handle (no double-free).
//
// Covered types: VertexBuffer, IndexBuffer, Texture2D, Texture3D, TextureCube and
// RenderTarget2D (move-construct + move-assign where publicly supported).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdio>
#include <memory>
#include <utility>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MoveSemanticsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── VertexBuffer move-construct ────────────────────────────────────
        {
            VertexBuffer src(dev, 8);
            check(src.HasRenderer(), "VB move-construct: src.HasRenderer true before move");

            VertexBuffer dst(std::move(src));

            check(!src.HasRenderer(), "VB move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "VB move-construct: dst.HasRenderer true after move");
            // Both go out of scope here — only dst frees the handle (no double-free)
        }

        // ── VertexBuffer move-assign ───────────────────────────────────────
        {
            VertexBuffer src(dev, 4);
            VertexBuffer dst(dev, 4);    // dst already holds a handle (handle1)
            check(src.HasRenderer(), "VB move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "VB move-assign: dst.HasRenderer true before move");

            dst = std::move(src);        // handle1 freed, src's handle transferred to dst

            check(!src.HasRenderer(), "VB move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "VB move-assign: dst.HasRenderer true after move");
        }

        // ── IndexBuffer move-construct ─────────────────────────────────────
        {
            IndexBuffer src(dev, 12);
            check(src.HasRenderer(), "IB move-construct: src.HasRenderer true before move");

            IndexBuffer dst(std::move(src));

            check(!src.HasRenderer(), "IB move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "IB move-construct: dst.HasRenderer true after move");
        }

        // ── IndexBuffer move-assign ────────────────────────────────────────
        {
            IndexBuffer src(dev, 6);
            IndexBuffer dst(dev, 6);
            check(src.HasRenderer(), "IB move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "IB move-assign: dst.HasRenderer true before move");

            dst = std::move(src);

            check(!src.HasRenderer(), "IB move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "IB move-assign: dst.HasRenderer true after move");
        }

        // ── Texture2D move-construct ───────────────────────────────────────
        {
            Texture2D src(dev, 4, 4);
            check(src.HasRenderer(), "Tex2D move-construct: src.HasRenderer true before move");

            Texture2D dst(std::move(src));

            check(!src.HasRenderer(), "Tex2D move-construct: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "Tex2D move-construct: dst.HasRenderer true after move");
        }

        // ── Texture2D move-assign ──────────────────────────────────────────
        {
            Texture2D src(dev, 2, 2);
            Texture2D dst(dev, 2, 2);
            check(src.HasRenderer(), "Tex2D move-assign: src.HasRenderer true before move");
            check(dst.HasRenderer(), "Tex2D move-assign: dst.HasRenderer true before move");

            dst = std::move(src);

            check(!src.HasRenderer(), "Tex2D move-assign: src.HasRenderer false after move");
            check(dst.HasRenderer(),  "Tex2D move-assign: dst.HasRenderer true after move");
        }

        // The handle-only checks above use source and destination in one scope, so reverse C++
        // destruction order accidentally keeps the moved-to handle alive until both wrappers have
        // gone away. A real move crosses a scope: device tracking and every raw public binding must
        // follow the destination, not the source address that is about to die.
        const std::size_t baselineResources = dev.GetTrackedResourceCount();

        std::unique_ptr<VertexBuffer> movedVb;
        {
            VertexBuffer source(dev, 4);
            dev.SetVertexBuffer(&source);
            movedVb = std::make_unique<VertexBuffer>(std::move(source));
            check(dev.GetVertexBuffer() == movedVb.get(),
                  "VB move-construct: live binding follows destination immediately");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1,
              "VB move-construct: destination remains device-tracked after source dies");
        check(dev.GetVertexBuffer() == movedVb.get(),
              "VB move-construct: destination remains bound after source dies");
        movedVb.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.GetVertexBuffer() == nullptr,
              "VB move-construct: destination destruction unregisters and unbinds once");

        std::unique_ptr<IndexBuffer> movedIb;
        {
            IndexBuffer source(dev, 4);
            dev.SetIndexBuffer(&source);
            movedIb = std::make_unique<IndexBuffer>(std::move(source));
            check(dev.GetIndexBuffer() == movedIb.get(),
                  "IB move-construct: live binding follows destination immediately");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1,
              "IB move-construct: destination remains device-tracked after source dies");
        check(dev.GetIndexBuffer() == movedIb.get(),
              "IB move-construct: destination remains bound after source dies");
        movedIb.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.GetIndexBuffer() == nullptr,
              "IB move-construct: destination destruction unregisters and unbinds once");

        std::unique_ptr<Texture2D> movedTexture;
        {
            Texture2D source(dev, 2, 2);
            dev.getTexturesProperty()(0, &source);
            movedTexture = std::make_unique<Texture2D>(std::move(source));
            check(dev.getTexturesProperty()[0] == movedTexture.get(),
                  "Texture2D move-construct: sampler binding follows destination immediately");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1,
              "Texture2D move-construct: destination remains device-tracked after source dies");
        check(dev.getTexturesProperty()[0] == movedTexture.get(),
              "Texture2D move-construct: destination remains sampled after source dies");
        movedTexture.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[0] == nullptr,
              "Texture2D move-construct: destination destruction unregisters and unbinds once");

        // Move-assignment starts with two tracked wrappers. The overwritten destination and moved-
        // from source must coalesce into exactly one live resource entry, and a source binding must
        // follow the destination across the source's lifetime boundary.
        auto assignedVb = std::make_unique<VertexBuffer>(dev, 3);
        {
            VertexBuffer source(dev, 5);
            dev.SetVertexBuffers({VertexBufferBinding(&source, 1, 0)});
            *assignedVb = std::move(source);
            const auto bindings = dev.GetVertexBuffers();
            check(bindings.size() == 1 &&
                      bindings[0].getVertexBufferProperty() == assignedVb.get() &&
                      bindings[0].getVertexOffsetProperty() == 1,
                  "VB move-assign: live multi-stream binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.GetVertexBuffer() == assignedVb.get(),
              "VB move-assign: one tracked bound destination survives source destruction");
        assignedVb.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.GetVertexBuffer() == nullptr,
              "VB move-assign: destination destruction unregisters and unbinds once");

        auto assignedIb = std::make_unique<IndexBuffer>(dev, 3);
        {
            IndexBuffer source(dev, 5);
            dev.SetIndexBuffer(&source);
            *assignedIb = std::move(source);
            check(dev.GetIndexBuffer() == assignedIb.get(),
                  "IB move-assign: live binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.GetIndexBuffer() == assignedIb.get(),
              "IB move-assign: one tracked bound destination survives source destruction");
        assignedIb.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.GetIndexBuffer() == nullptr,
              "IB move-assign: destination destruction unregisters and unbinds once");

        auto assignedTexture = std::make_unique<Texture2D>(
            dev, 1, 1, false, SurfaceFormat::Single);
        {
            Texture2D source(dev, 2, 2, false, SurfaceFormat::Single);
            dev.getTexturesProperty()(2, &source);
            dev.getVertexTexturesProperty()(1, &source);
            *assignedTexture = std::move(source);
            check(dev.getTexturesProperty()[2] == assignedTexture.get() &&
                      dev.getVertexTexturesProperty()[1] == assignedTexture.get(),
                  "Texture2D move-assign: pixel and vertex bindings follow destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.getTexturesProperty()[2] == assignedTexture.get() &&
                  dev.getVertexTexturesProperty()[1] == assignedTexture.get(),
              "Texture2D move-assign: one tracked sampled destination survives source destruction");
        assignedTexture.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[2] == nullptr &&
                  dev.getVertexTexturesProperty()[1] == nullptr,
              "Texture2D move-assign: destination destruction unregisters and unbinds once");

        std::unique_ptr<Texture3D> movedVolume;
        {
            Texture3D source(dev, 2, 2, 2, false, SurfaceFormat::Color);
            dev.getTexturesProperty()(3, &source);
            movedVolume = std::make_unique<Texture3D>(std::move(source));
            check(dev.getTexturesProperty()[3] == movedVolume.get(),
                  "Texture3D move-construct: sampler binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.getTexturesProperty()[3] == movedVolume.get(),
              "Texture3D move-construct: tracked sampled destination survives source destruction");
        movedVolume.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[3] == nullptr,
              "Texture3D move-construct: destination destruction unregisters and unbinds once");

        auto assignedVolume = std::make_unique<Texture3D>(
            dev, 1, 1, 1, false, SurfaceFormat::Color);
        {
            Texture3D source(dev, 2, 2, 2, false, SurfaceFormat::Color);
            dev.getTexturesProperty()(5, &source);
            *assignedVolume = std::move(source);
            check(dev.getTexturesProperty()[5] == assignedVolume.get(),
                  "Texture3D move-assign: sampler binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.getTexturesProperty()[5] == assignedVolume.get(),
              "Texture3D move-assign: one tracked sampled destination survives source destruction");
        assignedVolume.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[5] == nullptr,
              "Texture3D move-assign: destination destruction unregisters and unbinds once");

        std::unique_ptr<TextureCube> movedCube;
        {
            TextureCube source(dev, 2, false, SurfaceFormat::Color);
            dev.getTexturesProperty()(4, &source);
            movedCube = std::make_unique<TextureCube>(std::move(source));
            check(dev.getTexturesProperty()[4] == movedCube.get(),
                  "TextureCube move-construct: sampler binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.getTexturesProperty()[4] == movedCube.get(),
              "TextureCube move-construct: tracked sampled destination survives source destruction");
        movedCube.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[4] == nullptr,
              "TextureCube move-construct: destination destruction unregisters and unbinds once");

        auto assignedCube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        {
            TextureCube source(dev, 2, false, SurfaceFormat::Color);
            dev.getTexturesProperty()(6, &source);
            *assignedCube = std::move(source);
            check(dev.getTexturesProperty()[6] == assignedCube.get(),
                  "TextureCube move-assign: sampler binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.getTexturesProperty()[6] == assignedCube.get(),
              "TextureCube move-assign: one tracked sampled destination survives source destruction");
        assignedCube.reset();
        check(dev.GetTrackedResourceCount() == baselineResources &&
                  dev.getTexturesProperty()[6] == nullptr,
              "TextureCube move-assign: destination destruction unregisters and unbinds once");

        std::unique_ptr<RenderTarget2D> movedTarget;
        {
            RenderTarget2D source(dev, 2, 2);
            dev.SetRenderTarget(&source);
            movedTarget = std::make_unique<RenderTarget2D>(std::move(source));
            const auto targets = dev.GetRenderTargets();
            check(targets.size() == 1 &&
                      targets[0].getRenderTargetProperty() == movedTarget.get(),
                  "RenderTarget2D move-construct: live target binding follows destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.GetRenderTargets()[0].getRenderTargetProperty() == movedTarget.get(),
              "RenderTarget2D move-construct: tracked bound destination survives source destruction");
        dev.SetRenderTarget(nullptr);
        movedTarget.reset();
        check(dev.GetTrackedResourceCount() == baselineResources,
              "RenderTarget2D move-construct: unbound destination unregisters once");

        auto assignedTarget = std::make_unique<RenderTarget2D>(dev, 1, 1);
        {
            RenderTarget2D source(dev, 2, 2);
            dev.SetRenderTarget(&source);
            *assignedTarget = std::move(source);
            const auto targets = dev.GetRenderTargets();
            check(targets.size() == 1 &&
                      targets[0].getRenderTargetProperty() == assignedTarget.get() &&
                      assignedTarget->getWidthProperty() == 2,
                  "RenderTarget2D move-assign: live target binding and properties follow destination");
        }
        check(dev.GetTrackedResourceCount() == baselineResources + 1 &&
                  dev.GetRenderTargets()[0].getRenderTargetProperty() == assignedTarget.get(),
              "RenderTarget2D move-assign: one tracked bound destination survives source destruction");
        dev.SetRenderTarget(nullptr);
        assignedTarget.reset();
        check(dev.GetTrackedResourceCount() == baselineResources,
              "RenderTarget2D move-assign: unbound destination unregisters once");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    MoveSemanticsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    MoveSemanticsTest game;
    game.Run();
    return game.getResult();
}
