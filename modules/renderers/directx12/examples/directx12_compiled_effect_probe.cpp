// SPDX-License-Identifier: MS-PL
// plans/plan_fx.md FX-134: executable D3D12 compiled-effect existence gate.

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include "mojoshader.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using Microsoft::WRL::ComPtr;

namespace
{
    struct ProbeShader
    {
        const MOJOSHADER_parseData* parseData = nullptr;
        ComPtr<ID3DBlob> bytecode;
        int refCount = 1;
    };

    struct ProbeContext
    {
        static constexpr int kFloat4Registers = 256;
        static constexpr int kInt4Registers = 16;
        static constexpr int kBoolRegisters = 16;

        ProbeShader* boundVertex = nullptr;
        ProbeShader* boundPixel = nullptr;
        std::array<float, kFloat4Registers * 4> vsFloat{};
        std::array<int, kInt4Registers * 4> vsInt{};
        std::array<unsigned char, kBoolRegisters> vsBool{};
        std::array<float, kFloat4Registers * 4> psFloat{};
        std::array<int, kInt4Registers * 4> psInt{};
        std::array<unsigned char, kBoolRegisters> psBool{};
        std::string error;
    };

    std::string BlobText(ID3DBlob* blob)
    {
        return blob != nullptr
            ? std::string(static_cast<const char*>(blob->GetBufferPointer()),
                          blob->GetBufferSize())
            : std::string{};
    }

    void* MOJOSHADERCALL CompileShader(
        const void* context, const char* mainFunction, const unsigned char* tokens,
        unsigned int tokenBytes, const MOJOSHADER_swizzle* swizzles,
        unsigned int swizzleCount, const MOJOSHADER_samplerMap* samplerMap,
        unsigned int samplerMapCount)
    {
        auto* probe = static_cast<ProbeContext*>(const_cast<void*>(context));
        const MOJOSHADER_parseData* parsed = MOJOSHADER_parse(
            MOJOSHADER_PROFILE_HLSL, mainFunction, tokens, tokenBytes, swizzles,
            swizzleCount, samplerMap, samplerMapCount, nullptr, nullptr, nullptr);
        if (parsed == nullptr)
        {
            probe->error = "MOJOSHADER_parse returned no HLSL result.";
            return nullptr;
        }
        if (parsed->error_count > 0 || parsed->output == nullptr)
        {
            probe->error = parsed->error_count > 0 && parsed->errors != nullptr &&
                                   parsed->errors[0].error != nullptr
                ? parsed->errors[0].error
                : "MojoShader produced no HLSL output.";
            MOJOSHADER_freeParseData(parsed);
            return nullptr;
        }

        const char* target = parsed->shader_type == MOJOSHADER_TYPE_VERTEX
            ? "vs_4_0" : parsed->shader_type == MOJOSHADER_TYPE_PIXEL ? "ps_4_0" : nullptr;
        if (target == nullptr)
        {
            probe->error = "MojoShader produced an unsupported shader stage.";
            MOJOSHADER_freeParseData(parsed);
            return nullptr;
        }

        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> diagnostics;
        const HRESULT hr = D3DCompile(
            parsed->output, static_cast<SIZE_T>(parsed->output_len), parsed->mainfn,
            nullptr, nullptr, parsed->mainfn, target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
            bytecode.GetAddressOf(), diagnostics.GetAddressOf());
        if (FAILED(hr))
        {
            probe->error = BlobText(diagnostics.Get());
            if (probe->error.empty()) probe->error = "D3DCompile rejected MojoShader HLSL.";
            MOJOSHADER_freeParseData(parsed);
            return nullptr;
        }

        auto* shader = new ProbeShader{};
        shader->parseData = parsed;
        shader->bytecode = std::move(bytecode);
        return shader;
    }

    void MOJOSHADERCALL AddRefShader(void* shader)
    {
        if (shader != nullptr) ++static_cast<ProbeShader*>(shader)->refCount;
    }

    void MOJOSHADERCALL DeleteShader(const void* context, void* shader)
    {
        if (shader == nullptr) return;
        auto* probeShader = static_cast<ProbeShader*>(shader);
        if (--probeShader->refCount > 0) return;
        auto* probe = static_cast<ProbeContext*>(const_cast<void*>(context));
        if (probe != nullptr)
        {
            if (probe->boundVertex == probeShader) probe->boundVertex = nullptr;
            if (probe->boundPixel == probeShader) probe->boundPixel = nullptr;
        }
        MOJOSHADER_freeParseData(probeShader->parseData);
        delete probeShader;
    }

    MOJOSHADER_parseData* MOJOSHADERCALL GetParseData(void* shader)
    {
        return shader != nullptr
            ? const_cast<MOJOSHADER_parseData*>(static_cast<ProbeShader*>(shader)->parseData)
            : nullptr;
    }

    void MOJOSHADERCALL BindShaders(const void* context, void* vertex, void* pixel)
    {
        auto* probe = static_cast<ProbeContext*>(const_cast<void*>(context));
        probe->boundVertex = static_cast<ProbeShader*>(vertex);
        probe->boundPixel = static_cast<ProbeShader*>(pixel);
    }

    void MOJOSHADERCALL GetBoundShaders(const void* context, void** vertex, void** pixel)
    {
        const auto* probe = static_cast<const ProbeContext*>(context);
        if (vertex != nullptr) *vertex = probe->boundVertex;
        if (pixel != nullptr) *pixel = probe->boundPixel;
    }

    void MOJOSHADERCALL MapUniforms(
        const void* context, float** vsFloat, int** vsInt, unsigned char** vsBool,
        float** psFloat, int** psInt, unsigned char** psBool)
    {
        auto* probe = static_cast<ProbeContext*>(const_cast<void*>(context));
        *vsFloat = probe->vsFloat.data();
        *vsInt = probe->vsInt.data();
        *vsBool = probe->vsBool.data();
        *psFloat = probe->psFloat.data();
        *psInt = probe->psInt.data();
        *psBool = probe->psBool.data();
    }

    void MOJOSHADERCALL UnmapUniforms(const void*) {}

    const char* MOJOSHADERCALL GetError(const void* context)
    {
        return static_cast<const ProbeContext*>(context)->error.c_str();
    }

    MOJOSHADER_effectShaderContext MakeBackend(ProbeContext* context)
    {
        MOJOSHADER_effectShaderContext backend{};
        backend.compileShader = CompileShader;
        backend.shaderAddRef = AddRefShader;
        backend.deleteShader = DeleteShader;
        backend.getParseData = GetParseData;
        backend.bindShaders = BindShaders;
        backend.getBoundShaders = GetBoundShaders;
        backend.mapUniformBufferMemory = MapUniforms;
        backend.unmapUniformBufferMemory = UnmapUniforms;
        backend.getError = GetError;
        backend.shaderContext = context;
        return backend;
    }

    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> result(length > 0 ? static_cast<std::size_t>(length) : 0);
        if (!result.empty() &&
            std::fread(result.data(), 1, result.size(), file) != result.size())
            result.clear();
        std::fclose(file);
        return result;
    }

    UINT Align(UINT value, UINT alignment)
    {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    ComPtr<ID3D12Resource> CreateUploadBuffer(
        ID3D12Device* device, const void* bytes, std::size_t byteCount,
        std::size_t allocationBytes = 0)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = std::max<std::size_t>(byteCount, allocationBytes);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(result.GetAddressOf()))))
            return {};
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(result->Map(0, &noRead, &mapped))) return {};
        if (byteCount > 0) std::memcpy(mapped, bytes, byteCount);
        const D3D12_RANGE written{0, byteCount};
        result->Unmap(0, &written);
        return result;
    }

    int FloatRegisterCount(const MOJOSHADER_parseData* parsed)
    {
        int result = 0;
        for (int index = 0; index < parsed->uniform_count; ++index)
        {
            const MOJOSHADER_uniform& uniform = parsed->uniforms[index];
            if (uniform.type != MOJOSHADER_UNIFORM_FLOAT) continue;
            result = std::max(result, uniform.index +
                (uniform.array_count > 0 ? uniform.array_count : 1));
        }
        return result;
    }

    bool Near(unsigned char actual, unsigned char expected)
    {
        return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 2;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: cna_probe_directx12_compiled_effect <file.fxb>\n");
        return 2;
    }
    const std::vector<unsigned char> effectBytes = ReadFile(argv[1]);
    if (effectBytes.empty())
    {
        std::fprintf(stderr, "FX-134 gate: could not read %s\n", argv[1]);
        return 1;
    }

    ProbeContext context;
    MOJOSHADER_effectShaderContext backend = MakeBackend(&context);
    MOJOSHADER_effect* effect = const_cast<MOJOSHADER_effect*>(MOJOSHADER_compileEffect(
        effectBytes.data(), static_cast<unsigned int>(effectBytes.size()),
        nullptr, 0, nullptr, 0, &backend));
    if (effect == nullptr || effect->error_count > 0)
    {
        const char* parserError = effect != nullptr && effect->errors != nullptr &&
                                          effect->errors[0].error != nullptr
            ? effect->errors[0].error : context.error.c_str();
        std::fprintf(stderr, "FX-134 gate: effect compilation failed: %s\n", parserError);
        return 1;
    }
    if (effect->technique_count < 1 || effect->techniques[0].pass_count < 2)
    {
        std::fprintf(stderr, "FX-134 gate: committed fixture has no technique 0 pass 1\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    MOJOSHADER_effectSetTechnique(effect, &effect->techniques[0]);
    MOJOSHADER_effectStateChanges stateChanges{};
    unsigned int passCount = 0;
    MOJOSHADER_effectBegin(effect, &passCount, 0, &stateChanges);
    MOJOSHADER_effectBeginPass(effect, 1);
    ProbeShader* vertexShader = context.boundVertex;
    ProbeShader* pixelShader = context.boundPixel;
    if (vertexShader == nullptr || pixelShader == nullptr)
    {
        std::fprintf(stderr, "FX-134 gate: pass did not bind a complete shader pair\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    GraphicsRendererCreateArgs args;
    args.virtualWidth = 64;
    args.virtualHeight = 64;
    DirectX12Renderer renderer(args);
    ID3D12Device* device = renderer.GetDeviceEXT();

    D3D12_ROOT_PARAMETER rootParameters[2]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 2;
    rootDescription.pParameters = rootParameters;
    rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serializedRoot;
    ComPtr<ID3DBlob> rootErrors;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRoot.GetAddressOf(), rootErrors.GetAddressOf());
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FX-134 gate: root signature serialization failed: %s\n",
                     BlobText(rootErrors.Get()).c_str());
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }
    ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(
        0, serializedRoot->GetBufferPointer(), serializedRoot->GetBufferSize(),
        IID_PPV_ARGS(rootSignature.GetAddressOf()));
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FX-134 gate: D3D12 rejected the root signature\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    D3D12_INPUT_ELEMENT_DESC inputElements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescription{};
    pipelineDescription.pRootSignature = rootSignature.Get();
    pipelineDescription.VS = {vertexShader->bytecode->GetBufferPointer(),
                              vertexShader->bytecode->GetBufferSize()};
    pipelineDescription.PS = {pixelShader->bytecode->GetBufferPointer(),
                              pixelShader->bytecode->GetBufferSize()};
    pipelineDescription.InputLayout = {inputElements, 2};
    pipelineDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDescription.SampleMask = std::numeric_limits<UINT>::max();
    pipelineDescription.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipelineDescription.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDescription.RasterizerState.DepthClipEnable = TRUE;
    pipelineDescription.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineDescription.DepthStencilState.DepthEnable = FALSE;
    pipelineDescription.DepthStencilState.StencilEnable = FALSE;
    pipelineDescription.NumRenderTargets = 1;
    pipelineDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineDescription.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    HRESULT hrPipeline = device->CreateGraphicsPipelineState(
        &pipelineDescription, IID_PPV_ARGS(pipeline.GetAddressOf()));
    if (FAILED(hrPipeline))
    {
        std::fprintf(stderr, "FX-134 gate: D3D12 rejected the MojoShader shader pair\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    struct Vertex { float x, y, z, u, v; };
    const std::array<Vertex, 6> vertices = {{
        {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, -1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    }};
    const ComPtr<ID3D12Resource> vertexBuffer =
        CreateUploadBuffer(device, vertices.data(), sizeof(vertices));
    const int vsRegisters = FloatRegisterCount(vertexShader->parseData);
    const int psRegisters = FloatRegisterCount(pixelShader->parseData);
    const ComPtr<ID3D12Resource> vsConstants = CreateUploadBuffer(
        device, context.vsFloat.data(), static_cast<std::size_t>(vsRegisters) * 16u, 256);
    const ComPtr<ID3D12Resource> psConstants = CreateUploadBuffer(
        device, context.psFloat.data(), static_cast<std::size_t>(psRegisters) * 16u, 256);
    if (!vertexBuffer || !vsConstants || !psConstants)
    {
        std::fprintf(stderr, "FX-134 gate: upload resource creation failed\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    D3D12_RESOURCE_DESC targetDescription{};
    targetDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    targetDescription.Width = 64;
    targetDescription.Height = 64;
    targetDescription.DepthOrArraySize = 1;
    targetDescription.MipLevels = 1;
    targetDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDescription.SampleDesc.Count = 1;
    targetDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    targetDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = targetDescription.Format;
    clearValue.Color[3] = 1.0f;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> target;
    hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &targetDescription,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
        IID_PPV_ARGS(target.GetAddressOf()));
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FX-134 gate: render target creation failed\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDescription.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    hr = device->CreateDescriptorHeap(
        &rtvHeapDescription, IID_PPV_ARGS(rtvHeap.GetAddressOf()));
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FX-134 gate: RTV heap creation failed\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, rtv);

    const UINT rowPitch = Align(64u * 4u, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    D3D12_RESOURCE_DESC readbackDescription{};
    readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDescription.Width = static_cast<UINT64>(rowPitch) * 64u;
    readbackDescription.Height = 1;
    readbackDescription.DepthOrArraySize = 1;
    readbackDescription.MipLevels = 1;
    readbackDescription.SampleDesc.Count = 1;
    readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;
    hr = device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
    if (FAILED(hr))
    {
        std::fprintf(stderr, "FX-134 gate: readback resource creation failed\n");
        MOJOSHADER_deleteEffect(effect);
        return 1;
    }

    ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
    ID3D12GraphicsCommandList* commandList = renderer.GetCommandListEXT();
    allocator->Reset();
    commandList->Reset(allocator, pipeline.Get());
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, 64, 64};
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    const D3D12_VERTEX_BUFFER_VIEW vertexView{
        vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(vertices)), sizeof(Vertex)};
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexView);
    commandList->SetGraphicsRootConstantBufferView(0, vsConstants->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, psConstants->GetGPUVirtualAddress());
    commandList->DrawInstanced(6, 1, 0, 0);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = targetDescription.Format;
    destination.PlacedFootprint.Footprint.Width = 64;
    destination.PlacedFootprint.Footprint.Height = 64;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = rowPitch;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    commandList->Close();
    renderer.ExecuteCommandListAndWaitEXT(commandList);

    void* mapped = nullptr;
    const std::size_t centerOffset = static_cast<std::size_t>(32) * rowPitch + 32u * 4u;
    const D3D12_RANGE readRange{centerOffset, centerOffset + 4};
    readback->Map(0, &readRange, &mapped);
    const auto* pixel = static_cast<const unsigned char*>(mapped) + centerOffset;
    const std::array<unsigned char, 4> actual = {pixel[0], pixel[1], pixel[2], pixel[3]};
    const D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);

    MOJOSHADER_effectEndPass(effect);
    MOJOSHADER_effectEnd(effect);
    std::printf("FX-134 gate: HLSL -> DXBC accepted (VS %zu bytes, PS %zu bytes)\n",
                vertexShader->bytecode->GetBufferSize(), pixelShader->bytecode->GetBufferSize());
    std::printf("FX-134 gate: center pixel = (%u,%u,%u,%u), expected ~= (20,41,61,82)\n",
                actual[0], actual[1], actual[2], actual[3]);
    const bool pixelMatches = Near(actual[0], 20) && Near(actual[1], 41) &&
                              Near(actual[2], 61) && Near(actual[3], 82);
    MOJOSHADER_deleteEffect(effect);
    if (!pixelMatches)
    {
        std::fprintf(stderr, "FX-134 gate: offscreen draw/readback pixel mismatch\n");
        return 1;
    }
    std::printf("FX-134 gate: PASS\n");
    return 0;
}
