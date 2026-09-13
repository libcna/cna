// SPDX-License-Identifier: MS-PL
// DX-243 keeps this windowless smoke test focused on D3D12-native integration that the
// renderer-neutral parity corpus cannot observe: command submission, resource-state tracking,
// native object-cache identity, and device recreation. Public pixels, resources, effects, queries,
// presentation, and state semantics are covered by independent DirectX parity fixtures.

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12ResourceStateTracker.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RootSignatureCache.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;
using CNA::Internal::Renderers::DirectX12::D3D12PipelineStateCache;
using CNA::Internal::Renderers::DirectX12::D3D12PipelineStateDesc;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
using CNA::Internal::Renderers::DirectX12::D3D12ResourceStateTracker;
using CNA::Internal::Renderers::DirectX12::D3D12RootSignatureCache;

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, const char* label)
    {
        ++g_checks;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            ++g_failures;
    }

    bool CopyRoundTrip(DirectX12Renderer& renderer)
    {
        constexpr std::size_t kByteCount = 256;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = kByteCount;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            return false;
        hr = renderer.GetDeviceEXT()->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(readback.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            return false;

        std::array<unsigned char, kByteCount> expected{};
        for (std::size_t i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<unsigned char>((i * 37u + 11u) & 0xFFu);

        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        hr = upload->Map(0, &noRead, &mapped);
        if (FAILED(hr))
            return false;
        std::memcpy(mapped, expected.data(), expected.size());
        const D3D12_RANGE written{0, expected.size()};
        upload->Unmap(0, &written);

        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* commandList = renderer.GetCommandListEXT();
        if (allocator == nullptr || commandList == nullptr || FAILED(allocator->Reset()) ||
            FAILED(commandList->Reset(allocator, nullptr)))
            return false;
        commandList->CopyBufferRegion(readback.Get(), 0, upload.Get(), 0, expected.size());
        if (FAILED(commandList->Close()))
            return false;
        renderer.ExecuteCommandListAndWaitEXT(commandList);

        const D3D12_RANGE readRange{0, expected.size()};
        hr = readback->Map(0, &readRange, &mapped);
        if (FAILED(hr))
            return false;
        const bool matches = std::memcmp(mapped, expected.data(), expected.size()) == 0;
        readback->Unmap(0, &noRead);
        return matches;
    }
}

int main()
{
    GraphicsRendererCreateArgs args;
    args.virtualWidth = 64;
    args.virtualHeight = 64;
    DirectX12Renderer renderer(args);

    // Family A: windowless device and real command submission/fence completion.
    Check(renderer.GetDeviceEXT() != nullptr, "A1: native D3D12 device exists");
    Check(renderer.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0,
          "A2: negotiated feature level is 11_0 or higher");
    Check(renderer.GetCommandQueueEXT() != nullptr && renderer.GetCommandAllocatorEXT(0) != nullptr &&
              renderer.GetCommandListEXT() != nullptr && renderer.GetFenceEXT() != nullptr,
          "A3: queue, allocator, command list, and fence exist");
    Check(!renderer.IsSwapChainAvailableEXT() && renderer.GetSwapChainEXT() == nullptr,
          "A4: windowless construction does not create a swap chain");
    Check(CopyRoundTrip(renderer),
          "A5: recorded upload-to-readback copy executes and returns exact bytes");
    Check(renderer.GetFenceEXT()->GetCompletedValue() > 0,
          "A6: command submission advances the completed fence value");

    const std::uint64_t frame0 = renderer.SignalAndWaitForFrameEXT(0);
    const std::uint64_t frame1 = renderer.SignalAndWaitForFrameEXT(1);
    const std::uint64_t frame0Again = renderer.SignalAndWaitForFrameEXT(0);
    Check(frame1 > frame0 && frame0Again > frame1,
          "A7: per-frame fence values increase monotonically");
    Check(renderer.GetFenceEXT()->GetCompletedValue() >= frame0,
          "A8: reusing a frame slot waits for its prior fence value");

    // Family B: resource-state tracker transitions, redundant-barrier suppression, and lifetime.
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bufferDescription{};
    bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDescription.Width = 256;
    bufferDescription.Height = 1;
    bufferDescription.DepthOrArraySize = 1;
    bufferDescription.MipLevels = 1;
    bufferDescription.Format = DXGI_FORMAT_UNKNOWN;
    bufferDescription.SampleDesc.Count = 1;
    bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDescription,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    Check(SUCCEEDED(hr) && resource != nullptr, "B1: native default-heap resource exists");

    D3D12ResourceStateTracker tracker;
    tracker.TrackResource(resource.Get(), D3D12_RESOURCE_STATE_COMMON);
    Check(tracker.GetTrackedCountEXT() == 1 &&
              tracker.GetTrackedStateEXT(resource.Get()) == D3D12_RESOURCE_STATE_COMMON,
          "B2: tracker records the resource's initial state");

    ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
    ID3D12GraphicsCommandList* commandList = renderer.GetCommandListEXT();
    allocator->Reset();
    commandList->Reset(allocator, nullptr);
    Check(tracker.TransitionTo(commandList, resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST),
          "B3: changing state emits a transition barrier");
    Check(tracker.GetTrackedStateEXT(resource.Get()) == D3D12_RESOURCE_STATE_COPY_DEST,
          "B4: emitted transition updates tracked state");
    Check(!tracker.TransitionTo(commandList, resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST),
          "B5: repeated state suppresses a redundant barrier");
    Check(tracker.TransitionTo(commandList, resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ),
          "B6: a second distinct state emits another barrier");
    commandList->Close();
    bool transitionSubmitSucceeded = true;
    try
    {
        renderer.ExecuteCommandListAndWaitEXT(commandList);
    }
    catch (const std::exception&)
    {
        transitionSubmitSucceeded = false;
    }
    Check(transitionSubmitSucceeded, "B7: recorded transition barriers submit successfully");
    tracker.UntrackResource(resource.Get());
    Check(!tracker.IsTrackedEXT(resource.Get()) && tracker.GetTrackedCountEXT() == 0,
          "B8: released resource is removed from tracker state");
    bool unknownStateThrew = false;
    try
    {
        (void) tracker.GetTrackedStateEXT(resource.Get());
    }
    catch (const std::runtime_error&)
    {
        unknownStateThrew = true;
    }
    Check(unknownStateThrew, "B9: querying an untracked resource throws");

    // Family C: root-signature and PSO cache keys create identity hits and discriminating misses.
    {
        D3D12RootSignatureCache rootSignatures;
        auto rootA = rootSignatures.GetOrCreate(renderer.GetDeviceEXT(), 2, 0, 0);
        auto rootAAgain = rootSignatures.GetOrCreate(renderer.GetDeviceEXT(), 2, 0, 0);
        auto rootB = rootSignatures.GetOrCreate(renderer.GetDeviceEXT(), 2, 1, 1);
        Check(rootA != nullptr && rootA.Get() == rootAAgain.Get(),
              "C1: identical root-signature shape returns the same native object");
        Check(rootB != nullptr && rootB.Get() != rootA.Get() &&
                  rootSignatures.GetCacheSizeEXT() == 2,
              "C2: different root-signature shape returns a distinct object");

        D3D12PipelineStateCache pipelineStates;
        D3D12PipelineStateDesc description;
        description.variant = D3DShaderVariant::Colored3d;
        description.strideInBytes = 16;
        auto pipelineA = pipelineStates.GetOrCreate(renderer.GetDeviceEXT(), rootA.Get(), description);
        auto pipelineAAgain = pipelineStates.GetOrCreate(renderer.GetDeviceEXT(), rootA.Get(), description);
        D3D12PipelineStateDesc differentDescription = description;
        differentDescription.cullMode = 0;
        auto pipelineB = pipelineStates.GetOrCreate(
            renderer.GetDeviceEXT(), rootA.Get(), differentDescription);
        Check(pipelineA != nullptr && pipelineA.Get() == pipelineAAgain.Get(),
              "C3: identical PSO descriptor returns the same native object");
        Check(pipelineB != nullptr && pipelineB.Get() != pipelineA.Get() &&
                  pipelineStates.GetCacheSizeEXT() == 2,
              "C4: changed PSO field returns a distinct native object");
    }

    // Family D: complete device teardown/recreation clears old-device tracking and leaves a
    // functional command path. The marker makes this discriminate recreation from a no-op.
    renderer.GetResourceStateTrackerEXT().TrackResource(
        resource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(renderer.GetResourceStateTrackerEXT().IsTrackedEXT(resource.Get()),
          "D1: old-device tracker marker exists before recreation");
    bool recreationSucceeded = true;
    try
    {
        renderer.RecreateDeviceEXT();
    }
    catch (const std::exception&)
    {
        recreationSucceeded = false;
    }
    Check(recreationSucceeded, "D2: device recreation completes without throwing");
    Check(renderer.GetDeviceEXT() != nullptr && renderer.GetCommandQueueEXT() != nullptr &&
              renderer.GetCommandAllocatorEXT(0) != nullptr && renderer.GetCommandListEXT() != nullptr &&
              renderer.GetFenceEXT() != nullptr &&
              !renderer.GetResourceStateTrackerEXT().IsTrackedEXT(resource.Get()),
          "D3: recreated objects exist and old-device tracker marker was cleared");
    Check(CopyRoundTrip(renderer),
          "D4: recreated device executes a fresh exact-byte command round-trip");

    std::printf("\n=== %d/%d PASS ===\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
