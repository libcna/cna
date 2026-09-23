// SPDX-License-Identifier: MS-PL
//
// plans/plan_sdlgpu_modern_graphics.md SMG-0040: no storage buffer's native SDL_GPUBuffer may be
// alive when the renderer destroys its SDL_GPUDevice, whatever order the owners go in.
//
// The defect this pins: the renderer's own draw-binding state (BindStorageBufferForDrawEXT) holds
// a reference to every buffer it publishes. As a member, that reference was dropped only after
// SDL_DestroyGPUDevice, when the buffer's destructor found no device and skipped its release --
// Vulkan's VUID-vkDestroyDevice-device-05137, 51 times across the storage-buffer suites. A record
// held past the renderer by anyone else had the same fate, and addressed a destroyed renderer.
//
// The renderer-level cases count native acquisitions and releases through the renderer's resource
// hook, and check that every buffer release precedes the device's, so they are exact in any build.
// The public case (CNA_CNAEXT) lets StorageBuffer wrappers outlive their GraphicsDevice; its native
// proof is the Debug build's validation layer, which the ctest entry turns into a failure.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no GPU display.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuModern.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#if defined(CNA_CNAEXT)
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "System/ObjectDisposedException.hpp"
#endif

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    // CNA::Graphics::StorageBufferUsage / StorageBufferCpuAccess, as the renderer receives them.
    constexpr std::uint32_t kStorage = 1u << 0;
    constexpr std::uint32_t kTransferSource = 1u << 1;
    constexpr std::uint32_t kTransferDestination = 1u << 2;
    constexpr std::uint32_t kIndirectArguments = 1u << 3;
    constexpr std::uint32_t kConstant = 1u << 6;
    constexpr std::uint32_t kCpuRead = 1u << 0;
    constexpr std::uint32_t kCpuWrite = 1u << 1;

    int checks = 0;
    int failures = 0;

    void Check(const bool ok, const std::string& label)
    {
        ++checks;
        if (!ok) ++failures;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
    }

    /// Records every storage-buffer and device edge, in order.
    struct Events
    {
        int buffersAcquired = 0;
        int buffersReleased = 0;
        int deviceReleases = 0;
        /// Buffer releases that came after the device was already released.
        int releasesAfterDevice = 0;

        static void OnResource(void* context, const SdlGpuResourceKindEXT resource,
                               const SdlGpuResourceEventEXT event) noexcept
        {
            auto& self = *static_cast<Events*>(context);
            if (resource == SdlGpuResourceKindEXT::Device &&
                event == SdlGpuResourceEventEXT::Released)
                ++self.deviceReleases;
            if (resource != SdlGpuResourceKindEXT::StorageBuffer) return;
            if (event == SdlGpuResourceEventEXT::Acquired)
            {
                ++self.buffersAcquired;
                return;
            }
            ++self.buffersReleased;
            if (self.deviceReleases > 0) ++self.releasesAfterDevice;
        }
    };

    std::unique_ptr<SdlGpuRenderer> MakeRenderer(SDL_Window* window, Events& events)
    {
        SdlGpuTestHooksEXT hooks{};
        hooks.context = &events;
        hooks.resourceEvent = &Events::OnResource;
        return std::make_unique<SdlGpuRenderer>(
            window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0, hooks);
    }

    /// The shape StorageBuffer gives a record: shared, so the renderer's bindings can hold it.
    std::shared_ptr<IStorageBufferRenderer> MakeBuffer(
        SdlGpuRenderer& renderer, const std::size_t bytes, const std::uint32_t usage,
        const std::uint32_t cpuAccess)
    {
        return std::shared_ptr<IStorageBufferRenderer>(
            renderer.CreateStorageBufferEXT(bytes, usage, cpuAccess));
    }

    void Upload(IStorageBufferRenderer& buffer, const std::uint32_t seed)
    {
        const std::uint32_t words[4] = {seed, seed + 1, seed + 2, seed + 3};
        (void) buffer.SetDataRangeEXT(0, words, sizeof(words));
    }

    void NormalOrder(SDL_Window* window)
    {
        Events events;
        {
            auto renderer = MakeRenderer(window, events);
            {
                auto buffer = MakeBuffer(*renderer, 64, kStorage, kCpuRead | kCpuWrite);
                Upload(*buffer, 1);
                std::uint32_t back[4] = {};
                Check(buffer->GetDataRangeEXT(0, back, sizeof(back)) && back[3] == 4,
                      "normal order: an uploaded buffer reads back before teardown");
            }
            Check(events.buffersReleased == 1,
                  "normal order: dropping the only owner releases the buffer at once");
        }
        Check(events.buffersAcquired == 1 && events.buffersReleased == 1 &&
                  events.releasesAfterDevice == 0 && events.deviceReleases == 1,
              "normal order: one buffer acquired, released once, before the device");
    }

    /// The 51-message defect: the caller lets go, the renderer's binding is the last owner.
    void BindingIsTheLastOwner(SDL_Window* window)
    {
        Events events;
        {
            auto renderer = MakeRenderer(window, events);
            {
                auto lights = MakeBuffer(*renderer, 256, kStorage, kCpuWrite);
                auto clusters = MakeBuffer(*renderer, 512, kStorage, kCpuWrite);
                auto indices = MakeBuffer(*renderer, 128, kStorage, kCpuWrite);
                Upload(*lights, 10);
                Upload(*clusters, 20);
                Upload(*indices, 30);
                renderer->BindStorageBufferForDrawEXT(0, *lights);
                renderer->BindStorageBufferForDrawEXT(1, *clusters);
                renderer->BindStorageBufferForDrawEXT(2, *indices);
            }
            Check(events.buffersReleased == 0,
                  "binding keep-alive: bound buffers survive their caller, as a draw needs");
        }
        Check(events.buffersAcquired == 3 && events.buffersReleased == 3,
              "binding keep-alive: all three bound buffers are released at teardown");
        Check(events.releasesAfterDevice == 0,
              "binding keep-alive: every release precedes SDL_DestroyGPUDevice");
    }

    /// Records held past the renderer: one of each role this layer has.
    void RecordsOutliveTheRenderer(SDL_Window* window)
    {
        Events events;
        std::vector<std::shared_ptr<IStorageBufferRenderer>> survivors;
        {
            auto renderer = MakeRenderer(window, events);
            auto readWrite = MakeBuffer(*renderer, 64, kStorage | kTransferSource, kCpuRead | kCpuWrite);
            auto readOnly = MakeBuffer(*renderer, 64, kStorage | kTransferDestination, kCpuRead);
            auto constant = MakeBuffer(*renderer, 64, kConstant, kCpuWrite);
            auto indirect = MakeBuffer(*renderer, 64, kIndirectArguments | kStorage, kCpuWrite);
            auto bound = MakeBuffer(*renderer, 64, kStorage, kCpuWrite);
            Upload(*readWrite, 100);
            Upload(*constant, 200);
            Upload(*indirect, 300);
            Upload(*bound, 400);
            Check(readWrite->CopyToEXT(*readOnly, 0, 0, 16),
                  "outlives renderer: a GPU copy between two survivors succeeds before teardown");
            renderer->BindStorageBufferForDrawEXT(0, *bound);
            survivors = {readWrite, readOnly, constant, indirect, bound};
        }
        Check(events.buffersAcquired == 5 && events.buffersReleased == 5,
              "outlives renderer: all five native buffers are released at teardown");
        Check(events.releasesAfterDevice == 0,
              "outlives renderer: every release precedes SDL_DestroyGPUDevice");

        bool allRefuse = true;
        bool allDetached = true;
        for (const auto& record : survivors)
        {
            const std::uint32_t word = 7;
            std::uint32_t out = 0;
            allRefuse = allRefuse && !record->SetDataRangeEXT(0, &word, sizeof(word)) &&
                        !record->GetDataRangeEXT(0, &out, sizeof(out));
            allDetached = allDetached &&
                          static_cast<SdlGpuStorageBufferRenderer*>(record.get())->Buffer() == nullptr;
        }
        Check(allRefuse, "outlives renderer: a detached record refuses upload and readback");
        Check(allDetached, "outlives renderer: a detached record owns no native buffer");
        Check(!survivors[0]->CopyToEXT(*survivors[1], 0, 0, 4),
              "outlives renderer: a detached record refuses a GPU copy");

        survivors.clear();   // destructors after the renderer: must touch neither SDL nor it
        Check(events.buffersReleased == 5,
              "outlives renderer: destroying the survivors later releases nothing twice");
    }

    /// Registry bookkeeping: survivors on both sides of one that was destroyed early.
    void RegistryRemoval(SDL_Window* window)
    {
        Events events;
        std::vector<std::shared_ptr<IStorageBufferRenderer>> survivors;
        {
            auto renderer = MakeRenderer(window, events);
            std::vector<std::shared_ptr<IStorageBufferRenderer>> buffers;
            for (int i = 0; i < 6; ++i)
                buffers.push_back(MakeBuffer(*renderer, 32, kStorage, kCpuWrite));
            buffers[0].reset();
            buffers[3].reset();
            buffers[5].reset();
            Check(events.buffersReleased == 3,
                  "registry: three buffers destroyed early release at once");
            survivors = {buffers[1], buffers[2], buffers[4]};
        }
        Check(events.buffersAcquired == 6 && events.buffersReleased == 6 &&
                  events.releasesAfterDevice == 0,
              "registry: the three survivors are released exactly once, before the device");
        survivors.clear();
        Check(events.buffersReleased == 6, "registry: no survivor releases a second time");
    }

#if defined(CNA_CNAEXT)
    using Microsoft::Xna::Framework::Game;
    using Microsoft::Xna::Framework::GameTime;
    using Microsoft::Xna::Framework::GraphicsDeviceManager;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using CNA::Graphics::StorageBuffer;
    using CNA::Graphics::StorageBufferCpuAccess;
    using CNA::Graphics::StorageBufferDescriptor;
    using CNA::Graphics::StorageBufferUsage;

    /// Wrappers the game creates and main() destroys after the game -- and its device -- are gone.
    std::vector<std::unique_ptr<StorageBuffer>> outlivingWrappers;
    bool publicSkipped = false;

    class OutlivesDeviceGame final : public Game
    {
        std::unique_ptr<GraphicsDeviceManager> gdm_;
        int frame_ = 0;

    protected:
        void Draw(const GameTime&) override
        {
            if (frame_++ < 1) return;
            GraphicsDevice& device = getGraphicsDeviceProperty();
            if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders) ||
                !device.SupportsCapability(CNA::GraphicsCapability::IndirectDraw))
            {
                publicSkipped = true;
                Exit();
                return;
            }
            const auto make = [&](const StorageBufferUsage usage,
                                  const StorageBufferCpuAccess access) {
                return std::make_unique<StorageBuffer>(
                    device, StorageBufferDescriptor(64, usage, access));
            };
            const std::uint32_t words[4] = {1, 2, 3, 4};

            // Normal order, bound: destroyed inside the frame while the renderer's binding holds it.
            {
                auto local = make(StorageBufferUsage::Storage, StorageBufferCpuAccess::Write);
                local->setBytes(words, sizeof(words));
                device.GetRenderer().BindStorageBufferForDrawEXT(3, *local->getRendererEXT());
            }

            auto uploaded = make(StorageBufferUsage::Storage | StorageBufferUsage::TransferSource,
                                 StorageBufferCpuAccess::Read | StorageBufferCpuAccess::Write);
            uploaded->setBytes(words, sizeof(words));
            auto bound = make(StorageBufferUsage::Storage, StorageBufferCpuAccess::Write);
            bound->setBytes(words, sizeof(words));
            device.GetRenderer().BindStorageBufferForDrawEXT(0, *bound->getRendererEXT());
            auto constant = make(StorageBufferUsage::Constant, StorageBufferCpuAccess::Write);
            constant->setBytes(words, sizeof(words));
            auto indirect = make(StorageBufferUsage::IndirectArguments | StorageBufferUsage::Storage,
                                 StorageBufferCpuAccess::Write);
            indirect->setBytes(words, sizeof(words));
            outlivingWrappers.push_back(std::move(uploaded));
            outlivingWrappers.push_back(std::move(bound));
            outlivingWrappers.push_back(std::move(constant));
            outlivingWrappers.push_back(std::move(indirect));
            Exit();
        }

    public:
        OutlivesDeviceGame()
        {
            gdm_ = std::make_unique<GraphicsDeviceManager>(this);
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            gdm_->setPreferredBackBufferWidthProperty(64);
            gdm_->setPreferredBackBufferHeightProperty(64);
        }
    };

    void WrappersOutliveTheGraphicsDevice()
    {
        {
            OutlivesDeviceGame game;
            game.Run();
        }
        if (publicSkipped)
        {
            std::printf("[INFO] public case skipped: no storage buffers on this device\n");
            return;
        }
        Check(outlivingWrappers.size() == 4,
              "public: four StorageBuffer wrappers outlive their GraphicsDevice");
        bool disposed = true;
        bool refused = true;
        for (const auto& wrapper : outlivingWrappers)
        {
            disposed = disposed && wrapper->getIsDisposedProperty();
            try
            {
                const std::uint32_t word = 9;
                wrapper->setBytes(&word, sizeof(word));
                refused = false;
            }
            catch (const System::ObjectDisposedException&)
            {
            }
        }
        Check(disposed, "public: device destruction disposed every surviving wrapper");
        Check(refused, "public: a surviving wrapper refuses use with ObjectDisposedException");
        outlivingWrappers.clear();
        Check(true, "public: destroying the wrappers after the device is safe");
    }
#endif
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow("CNA SMG-0040 storage-buffer lifetime", 64, 64, SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    NormalOrder(window);
    BindingIsTheLastOwner(window);
    RecordsOutliveTheRenderer(window);
    RegistryRemoval(window);

    SDL_DestroyWindow(window);
    SDL_Quit();

#if defined(CNA_CNAEXT)
    WrappersOutliveTheGraphicsDevice();
#endif

    std::printf("=== %d/%d PASS ===\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
