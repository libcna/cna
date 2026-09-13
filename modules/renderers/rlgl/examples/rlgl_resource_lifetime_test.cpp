// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-054: native children are released before their device context, and
// delayed C++ destruction cannot delete recycled names in a later RLGL context.

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglResourceLifetime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "RlglResources.hpp"
#include "common/PixelTestGame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;
    using CNA::Internal::Renderers::IIndexBufferRenderer;
    using CNA::Internal::Renderers::IOcclusionQueryRenderer;
    using CNA::Internal::Renderers::IRenderTargetCubeRenderer;
    using CNA::Internal::Renderers::IRenderTargetRenderer;
    using CNA::Internal::Renderers::ISpriteBatchRenderer;
    using CNA::Internal::Renderers::ITextureCubeRenderer;
    using CNA::Internal::Renderers::ITextureRenderer;
    using CNA::Internal::Renderers::IVertexBufferRenderer;

#if defined(CNA_RLGL_COMPILED_EFFECTS)
    constexpr std::size_t kExpectedNativeChildren = 9;
#else
    constexpr std::size_t kExpectedNativeChildren = 8;
#endif

    struct NativeNames
    {
        unsigned int texture = 0;
        unsigned int cube = 0;
        unsigned int targetFramebuffer = 0;
        unsigned int targetTexture = 0;
        unsigned int targetCubeFramebuffer = 0;
        unsigned int targetCubeTexture = 0;
        unsigned int vertexBuffer = 0;
        unsigned int indexBuffer = 0;
    };

    struct ResourceStore
    {
        std::unique_ptr<ITextureRenderer> texture;
        std::unique_ptr<ITextureCubeRenderer> cube;
        std::unique_ptr<IRenderTargetRenderer> target;
        std::unique_ptr<IRenderTargetCubeRenderer> targetCube;
        std::unique_ptr<IVertexBufferRenderer> vertexBuffer;
        std::unique_ptr<IIndexBufferRenderer> indexBuffer;
        std::unique_ptr<ISpriteBatchRenderer> spriteBatch;
        std::unique_ptr<IOcclusionQueryRenderer> query;
        std::shared_ptr<Rlgl::RlglResourceLifetime> lifetime;
        NativeNames names;

        void DestroyObjects()
        {
            query.reset();
            spriteBatch.reset();
            indexBuffer.reset();
            vertexBuffer.reset();
            targetCube.reset();
            target.reset();
            cube.reset();
            texture.reset();
        }
    };

    [[nodiscard]] CNA::Internal::Graphics::ImageData SolidImage()
    {
        CNA::Internal::Graphics::ImageData image;
        image.width = 4;
        image.height = 4;
        image.surfaceFormat = static_cast<int>(SurfaceFormat::Color);
        image.pixels.resize(4u * 4u * 4u);
        for (std::size_t index = 0; index < image.pixels.size(); index += 4)
        {
            image.pixels[index + 0] = 41;
            image.pixels[index + 1] = 137;
            image.pixels[index + 2] = 229;
            image.pixels[index + 3] = 255;
        }
        return image;
    }

    void PopulateResources(Rlgl::RlglRenderer& renderer, ResourceStore& store)
    {
        store.lifetime = renderer.GetResourceLifetimeForTesting();
        store.texture = renderer.CreateTexture(SolidImage());
        store.cube = renderer.CreateTextureCube(
            4, false, static_cast<int>(SurfaceFormat::Color));
        store.target = renderer.CreateRenderTarget2D(4, 4, 3, true, false, 0);
        store.targetCube = renderer.CreateRenderTargetCube(4, 3, true, false, 0);
        store.vertexBuffer = renderer.CreateVertexBuffer(3);
        const std::array<float, 6> vertices{-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f};
        store.vertexBuffer->SetData(vertices.data(), 3, sizeof(float) * 2u);
        store.indexBuffer = renderer.CreateIndexBuffer16(3);
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        store.indexBuffer->SetData16(indices.data(), static_cast<int>(indices.size()));
        store.spriteBatch = renderer.CreateSpriteBatch();
        store.query = renderer.CreateOcclusionQuery();

        const std::array<std::uint8_t, 4 * 4 * 4> cubePixels = [] {
            std::array<std::uint8_t, 4 * 4 * 4> result{};
            for (std::size_t index = 0; index < result.size(); index += 4)
            {
                result[index + 0] = 83;
                result[index + 1] = 19;
                result[index + 2] = 171;
                result[index + 3] = 255;
            }
            return result;
        }();
        store.cube->SetData(0, 0, 0, 0, 4, 4, cubePixels.data(), cubePixels.size());
        store.query->Begin();

        store.names.texture = Rlgl::GetNativeTextureId(*store.texture);
        store.names.cube = Rlgl::GetTextureCubeResourceSnapshotForTesting(*store.cube).texture;
        const auto target = Rlgl::GetRenderTargetResourceSnapshotForTesting(*store.target);
        store.names.targetFramebuffer = target.framebuffer;
        store.names.targetTexture = target.colorTexture;
        const auto targetCube =
            Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(*store.targetCube);
        store.names.targetCubeFramebuffer = targetCube.framebuffer;
        store.names.targetCubeTexture = targetCube.colorTexture;
        store.names.vertexBuffer = Rlgl::GetNativeBufferId(*store.vertexBuffer);
        store.names.indexBuffer = Rlgl::GetNativeBufferId(*store.indexBuffer);
    }

    [[nodiscard]] bool AllNamesAreNonZero(const NativeNames& names)
    {
        return names.texture != 0 && names.cube != 0 &&
            names.targetFramebuffer != 0 && names.targetTexture != 0 &&
            names.targetCubeFramebuffer != 0 && names.targetCubeTexture != 0 &&
            names.vertexBuffer != 0 && names.indexBuffer != 0;
    }

    class AllocateOutlivingResources final : public CNA::Examples::PixelTestGame
    {
    public:
        explicit AllocateOutlivingResources(ResourceStore& store)
            : store_(store)
            , graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(48);
            graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(
                getGraphicsDeviceProperty().GetRenderer());
            PopulateResources(renderer, store_);
            const auto snapshot = store_.lifetime->GetSnapshotForTesting();
            Check(snapshot.active &&
                    snapshot.registeredResources == kExpectedNativeChildren &&
                    snapshot.releasedResources == 0 && snapshot.lateDisposals == 0,
                "every implemented native child registers with one live device lifetime");
            Check(AllNamesAreNonZero(store_.names),
                "every handle-owning resource has a native identity before device shutdown");
        }

    private:
        ResourceStore& store_;
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };

    class VerifyRecycledContext final : public CNA::Examples::PixelTestGame
    {
    public:
        explicit VerifyRecycledContext(ResourceStore& oldResources)
            : oldResources_(oldResources)
            , graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setPreferredBackBufferWidthProperty(64);
            graphics_->setPreferredBackBufferHeightProperty(48);
            graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());
            ResourceStore fresh;
            PopulateResources(renderer, fresh);

            Check(fresh.names.texture == oldResources_.names.texture,
                "the later GL context recycled the old Texture2D name");
            oldResources_.DestroyObjects();
            const auto oldSnapshot = oldResources_.lifetime->GetSnapshotForTesting();
            Check(!oldSnapshot.active && oldSnapshot.registeredResources == 0 &&
                    oldSnapshot.releasedResources == kExpectedNativeChildren &&
                    oldSnapshot.lateDisposals == kExpectedNativeChildren,
                "delayed destructors skip all native callbacks after device shutdown");

            std::array<std::uint8_t, 4 * 4 * 4> texturePixels{};
            const bool readTexture = fresh.texture->GetData(
                0, 0, 0, 4, 4, texturePixels.data(), texturePixels.size());
            Check(readTexture && texturePixels[0] == 41 && texturePixels[1] == 137 &&
                    texturePixels[2] == 229 && texturePixels[3] == 255,
                "delayed Texture2D destruction does not delete the recycled live name");

            std::array<std::uint8_t, 4 * 4 * 4> cubePixels{};
            const bool readCube = fresh.cube->GetData(
                0, 0, 0, 0, 4, 4, cubePixels.data(), cubePixels.size());
            Check(readCube && cubePixels[0] == 83 && cubePixels[1] == 19 &&
                    cubePixels[2] == 171 && cubePixels[3] == 255,
                "delayed cube destruction leaves the later cube resource intact");

            const auto vertex = Rlgl::GetBufferResourceSnapshotForTesting(*fresh.vertexBuffer);
            const auto index = Rlgl::GetBufferResourceSnapshotForTesting(*fresh.indexBuffer);
            Check(vertex.id == fresh.names.vertexBuffer && vertex.nativeBytes.size() == 24 &&
                    index.id == fresh.names.indexBuffer && index.nativeBytes.size() == 6,
                "delayed buffer destruction leaves later vertex and index storage intact");

            fresh.query->End();
            for (int attempt = 0; attempt < 10000 && !fresh.query->IsComplete(); ++attempt)
                std::this_thread::yield();
            Check(fresh.query->IsComplete() && fresh.query->PixelCount() == 0,
                "an active old query is closed before shutdown without corrupting a recycled query");

            fresh.target->BindAsRenderTarget();
            renderer.Clear(0.2f, 0.4f, 0.6f, 1.0f);
            fresh.target->UnbindAsRenderTarget();
            std::array<std::uint8_t, 4> targetPixel{};
            const bool readTarget = fresh.target->GetData(
                0, 1, 1, 1, 1, targetPixel.data(), targetPixel.size());
            Check(readTarget && targetPixel[0] == 51 && targetPixel[1] == 102 &&
                    targetPixel[2] == 153 && targetPixel[3] == 255,
                "delayed target destruction leaves the later framebuffer attachments intact");

            fresh.targetCube->BindAsRenderTargetFace(2);
            renderer.Clear(64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f);
            fresh.targetCube->UnbindAsRenderTarget();
            std::array<std::uint8_t, 4> targetCubePixel{};
            const bool readTargetCube = fresh.targetCube->GetData(
                2, 0, 1, 1, 1, 1, targetCubePixel.data(), targetCubePixel.size());
            Check(readTargetCube && targetCubePixel[0] == 64 && targetCubePixel[1] == 128 &&
                    targetCubePixel[2] == 192 && targetCubePixel[3] == 255,
                "delayed cube-target destruction leaves the later face attachments intact");

            device.Clear(Color::Black);
            fresh.spriteBatch->Begin();
            fresh.spriteBatch->Draw(*fresh.texture, 8.0f, 8.0f);
            fresh.spriteBatch->End();
            ExpectPixel(
                "later SpriteBatch pipeline remains usable after delayed destruction",
                Rectangle(9, 9, 1, 1), Color(41, 137, 229, 255));

            const auto freshLifetime = fresh.lifetime;
            fresh.DestroyObjects();
            const auto freshSnapshot = freshLifetime->GetSnapshotForTesting();
            Check(freshSnapshot.active && freshSnapshot.registeredResources == 0 &&
                    freshSnapshot.releasedResources == kExpectedNativeChildren &&
                    freshSnapshot.lateDisposals == 0,
                "ordinary resource disposal is idempotent and context-current while the device lives");

            device.Clear(Color(17, 91, 203, 255));
            ExpectPixel(
                "ordinary draw/readback remains usable after all disposal paths",
                Rectangle(32, 24, 1, 1), Color(17, 91, 203, 255));
        }

    private:
        ResourceStore& oldResources_;
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };

    void CheckOutsideGame(const bool condition, const char* label, int& failures)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    int failures = 0;
    ResourceStore oldResources;
    {
        AllocateOutlivingResources first(oldResources);
        first.Run();
        if (first.getResultProperty() != 0) ++failures;
    }

    const auto shutdown = oldResources.lifetime->GetSnapshotForTesting();
    oldResources.lifetime->Shutdown();
    const auto repeatedShutdown = oldResources.lifetime->GetSnapshotForTesting();
    CheckOutsideGame(
        !shutdown.active && shutdown.registeredResources == 0 &&
            shutdown.releasedResources == kExpectedNativeChildren && shutdown.lateDisposals == 0 &&
            repeatedShutdown.registeredResources == shutdown.registeredResources &&
            repeatedShutdown.releasedResources == shutdown.releasedResources &&
            repeatedShutdown.lateDisposals == shutdown.lateDisposals,
        "device shutdown releases every child before context close and is idempotent",
        failures);
    CheckOutsideGame(
        Rlgl::GetNativeTextureId(*oldResources.texture) == 0 &&
            Rlgl::GetTextureCubeResourceSnapshotForTesting(*oldResources.cube).texture == 0 &&
            Rlgl::GetRenderTargetResourceSnapshotForTesting(*oldResources.target).framebuffer == 0 &&
            Rlgl::GetRenderTargetCubeResourceSnapshotForTesting(
                *oldResources.targetCube).framebuffer == 0 &&
            Rlgl::GetNativeBufferId(*oldResources.vertexBuffer) == 0 &&
            Rlgl::GetNativeBufferId(*oldResources.indexBuffer) == 0,
        "outliving C++ resources retain no old-context native handles",
        failures);

    {
        VerifyRecycledContext second(oldResources);
        second.Run();
        if (second.getResultProperty() != 0) ++failures;
    }
    return failures == 0 ? 0 : 1;
}
