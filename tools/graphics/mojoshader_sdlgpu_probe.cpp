// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-061 existence gate: prove that the pinned MojoShader's SDL_GPU adapter turns a
// committed compiled effect into shaders SDL_GPU accepts, against a device this machine can
// actually create -- before any CNA renderer code is written for it.
//
// The reason to spend a probe on this rather than start implementing: CNA's SDL_GPU renderer
// already builds its pipelines from SPIR-V (SDL_GPU_SHADERFORMAT_SPIRV), and MojoShader has both a
// SPIR-V profile and an SDL_GPU adapter, so the pairing *looks* obvious on paper. What is not
// obvious from paper is whether the adapter links a program from a real effect's shader pair, what
// it needs from the device, and how much of the uniform plumbing it owns versus leaves to the
// caller. Those answers decide the size of FX-061.
//
// This probe deliberately links only MojoShader and SDL3 -- no FNA3D and no CNA. If it needed
// FNA3D to work, FX-061 would not be independent of it and the whole plan would change.
//
// Usage: cna_mojoshader_sdlgpu_probe <file.fxb>...
//
// plan_fx.md FX-071 golden-pixel investigation (2026-08-16): a second mode,
// `cna_mojoshader_sdlgpu_probe --render <file.fxb>`, goes one step further than the reflection
// exercise below -- it links a real SDL_GPU pipeline from the effect's own compiled shaders,
// draws a full-screen quad with it into an offscreen render target, and reads the pixels back.
// Nothing in this dependency chain had ever exercised MojoShader's SPIR-V Effect Framework
// translation all the way to an actual GPU-rendered pixel before this: FNA3D's own compiled-effect
// tests use its OpenGL/D3D11 drivers, never SPIR-V, and CNA's own stock SDL_GPU shaders are
// hand-written SPIR-V that never goes through MOJOSHADER_compileEffect at all. This mode is what
// isolated the FX-071 all-black golden-pixel result to two register-count/float-count unit
// mismatches in CNA's own MojoShader Effect-parser-robustness patch (see
// cmake/patches/mojoshader-6333f74-effect-parser-robustness.patch) rather than a CNA renderer bug
// or an upstream MojoShader defect -- with zero CNA renderer code anywhere in this path.
//
// PROBE_TECHNIQUE / PROBE_PASS environment variables select a technique/pass other than 0/0, which
// is how the two register-count bugs above were told apart: MainPixelShader's struct-driven
// preshader (technique 0 pass 0) and FlatPixelShader's array-driven preshader (technique 0 pass 1,
// or technique 1 pass 0) failed for two different reasons that only showed up in different passes.

#include "mojoshader.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    std::vector<unsigned char> ReadFile(const char* path)
    {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) return {};
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::vector<unsigned char> bytes(size > 0 ? static_cast<std::size_t>(size) : 0u);
        if (!bytes.empty() && std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
        {
            bytes.clear();
        }
        std::fclose(file);
        return bytes;
    }

    /// Builds the backend MojoShader requires, wired entirely to its own SDL_GPU adapter.
    MOJOSHADER_effectShaderContext MakeSdlBackend(MOJOSHADER_sdlContext* context)
    {
        MOJOSHADER_effectShaderContext backend{};
        backend.shaderContext = context;
        backend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_sdlCompileShader;
        backend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_sdlShaderAddRef;
        backend.deleteShader = (MOJOSHADER_deleteShaderFunc) MOJOSHADER_sdlDeleteShader;
        backend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_sdlGetShaderParseData;
        backend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_sdlBindShaders;
        backend.getBoundShaders = (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_sdlGetBoundShaderData;
        backend.mapUniformBufferMemory =
            (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_sdlMapUniformBufferMemory;
        backend.unmapUniformBufferMemory =
            (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_sdlUnmapUniformBufferMemory;
        backend.getError = (MOJOSHADER_getErrorFunc) MOJOSHADER_sdlGetError;
        return backend;
    }

    /// Walks one effect's passes, binding each pass's shader pair the way a renderer would.
    int ExercisePasses(const char* path, MOJOSHADER_sdlContext* context,
                       const MOJOSHADER_effect* effect)
    {
        int boundPasses = 0;
        for (int t = 0; t < effect->technique_count; ++t)
        {
            const MOJOSHADER_effectTechnique& technique = effect->techniques[t];
            for (unsigned int p = 0; p < technique.pass_count; ++p)
            {
                MOJOSHADER_effectStateChanges changes{};
                MOJOSHADER_effect* mutableEffect = const_cast<MOJOSHADER_effect*>(effect);
                MOJOSHADER_effectSetTechnique(mutableEffect, &technique);
                unsigned int passCount = 0;
                MOJOSHADER_effectBegin(mutableEffect, &passCount, /*saveShaderState=*/0, &changes);
                MOJOSHADER_effectBeginPass(mutableEffect, p);

                // What a renderer needs at this point: the shader pair the pass selected, linked
                // into something the graphics API can bind.
                MOJOSHADER_sdlShaderData* vertex = nullptr;
                MOJOSHADER_sdlShaderData* pixel = nullptr;
                MOJOSHADER_sdlGetBoundShaderData(context, &vertex, &pixel);
                if (vertex == nullptr || pixel == nullptr)
                {
                    std::printf("%s: technique %d pass %u bound no shader pair\n", path, t, p);
                    MOJOSHADER_effectEndPass(mutableEffect);
                    MOJOSHADER_effectEnd(mutableEffect);
                    return 1;
                }

                const MOJOSHADER_parseData* vertexData =
                    MOJOSHADER_sdlGetShaderParseData(vertex);
                const MOJOSHADER_parseData* pixelData = MOJOSHADER_sdlGetShaderParseData(pixel);
                std::printf("%s: technique %d pass %u -- vertex %s (%d uniforms, %d samplers), "
                            "pixel %s (%d uniforms, %d samplers)\n",
                            path, t, p,
                            vertexData != nullptr ? vertexData->profile : "<none>",
                            vertexData != nullptr ? vertexData->uniform_count : -1,
                            vertexData != nullptr ? vertexData->sampler_count : -1,
                            pixelData != nullptr ? pixelData->profile : "<none>",
                            pixelData != nullptr ? pixelData->uniform_count : -1,
                            pixelData != nullptr ? pixelData->sampler_count : -1);

                ++boundPasses;
                MOJOSHADER_effectEndPass(mutableEffect);
                MOJOSHADER_effectEnd(mutableEffect);
            }
        }

        if (boundPasses == 0)
        {
            std::printf("%s: no pass bound anything\n", path);
            return 1;
        }
        return 0;
    }

    /// plan_fx.md FX-071 golden-pixel investigation: links the selected technique/pass's shaders
    /// (PROBE_TECHNIQUE/PROBE_PASS env vars, default 0/0), draws a full-screen quad shaped for
    /// CnaConformanceEffect.fx's MainVertexShader (float3 POSITION0, float2 TEXCOORD0) into an
    /// offscreen R8G8B8A8_UNORM target, and reports whether any non-black pixel reached it. No CNA
    /// code anywhere in this path -- SDL_GPU and MojoShader only.
    int RenderAndReadback(const char* path, SDL_GPUDevice* device, MOJOSHADER_sdlContext* context,
                          MOJOSHADER_effect* effect)
    {
        int techniqueIndex = 0;
        int passIndex = 0;
        if (const char* t = SDL_getenv("PROBE_TECHNIQUE")) techniqueIndex = std::atoi(t);
        if (const char* p = SDL_getenv("PROBE_PASS")) passIndex = std::atoi(p);

        if (effect->technique_count <= techniqueIndex ||
            effect->techniques[techniqueIndex].pass_count <= static_cast<unsigned int>(passIndex))
        {
            std::printf("%s: no technique %d pass %d to render\n", path, techniqueIndex, passIndex);
            return 1;
        }

        MOJOSHADER_effectStateChanges changes{};
        MOJOSHADER_effectSetTechnique(effect, &effect->techniques[techniqueIndex]);
        unsigned int passCount = 0;
        MOJOSHADER_effectBegin(effect, &passCount, /*saveShaderState=*/0, &changes);
        MOJOSHADER_effectBeginPass(effect, passIndex);

        MOJOSHADER_sdlShaderData* vertexData = nullptr;
        MOJOSHADER_sdlShaderData* pixelData = nullptr;
        MOJOSHADER_sdlGetBoundShaderData(context, &vertexData, &pixelData);
        if (vertexData == nullptr || pixelData == nullptr)
        {
            std::printf("%s: --render: technique %d pass %d bound no shader pair\n", path,
                        techniqueIndex, passIndex);
            return 1;
        }

        MOJOSHADER_vertexAttribute attrs[2]{};
        attrs[0].usage = MOJOSHADER_USAGE_POSITION;
        attrs[0].usageIndex = 0;
        attrs[0].vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3;
        attrs[1].usage = MOJOSHADER_USAGE_TEXCOORD;
        attrs[1].usageIndex = 0;
        attrs[1].vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2;

        MOJOSHADER_sdlProgram* program = MOJOSHADER_sdlLinkProgram(context, attrs, 2);
        if (program == nullptr)
        {
            std::printf("%s: --render: MOJOSHADER_sdlLinkProgram failed: %s\n",
                        path, MOJOSHADER_sdlGetError(context));
            return 1;
        }

        SDL_GPUShader* vertexShader = nullptr;
        SDL_GPUShader* pixelShader = nullptr;
        MOJOSHADER_sdlGetShaders(context, &vertexShader, &pixelShader);
        if (vertexShader == nullptr || pixelShader == nullptr)
        {
            std::printf("%s: --render: linked program has no native shader modules\n", path);
            return 1;
        }

        // Full-screen quad, winding and geometry matching CNA's own golden-pixel test attempt.
        struct Vertex { float x, y, z, u, v; };
        const Vertex vertices[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
            { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
        };
        const Uint32 vertexBytes = static_cast<Uint32>(sizeof(vertices));

        SDL_GPUBufferCreateInfo vbInfo{};
        vbInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vbInfo.size = vertexBytes;
        SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(device, &vbInfo);
        if (vertexBuffer == nullptr)
        {
            std::printf("%s: --render: SDL_CreateGPUBuffer(vertex) failed: %s\n", path, SDL_GetError());
            return 1;
        }

        // 1x1 white texture for FxSampler.
        SDL_GPUTextureCreateInfo texInfo{};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texInfo.width = 1;
        texInfo.height = 1;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = 1;
        texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* whiteTexture = SDL_CreateGPUTexture(device, &texInfo);
        if (whiteTexture == nullptr)
        {
            std::printf("%s: --render: SDL_CreateGPUTexture(white) failed: %s\n", path, SDL_GetError());
            return 1;
        }

        // Offscreen color render target.
        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        constexpr Uint32 kWidth = 64, kHeight = 64;
        colorInfo.width = kWidth;
        colorInfo.height = kHeight;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* colorTarget = SDL_CreateGPUTexture(device, &colorInfo);
        if (colorTarget == nullptr)
        {
            std::printf("%s: --render: SDL_CreateGPUTexture(color target) failed: %s\n", path, SDL_GetError());
            return 1;
        }

        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &samplerInfo);
        if (sampler == nullptr)
        {
            std::printf("%s: --render: SDL_CreateGPUSampler failed: %s\n", path, SDL_GetError());
            return 1;
        }

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(Vertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute sdlAttrs[2]{};
        sdlAttrs[0].location = 0;
        sdlAttrs[0].buffer_slot = 0;
        sdlAttrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        sdlAttrs[0].offset = 0;
        sdlAttrs[1].location = 1;
        sdlAttrs[1].buffer_slot = 0;
        sdlAttrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        sdlAttrs[1].offset = 12;

        SDL_GPUColorTargetDescription colorDesc{};
        colorDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = vertexShader;
        pipelineInfo.fragment_shader = pixelShader;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = sdlAttrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 2;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipelineInfo.target_info.color_target_descriptions = &colorDesc;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = false;

        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
        if (pipeline == nullptr)
        {
            std::printf("%s: --render: SDL_CreateGPUGraphicsPipeline failed: %s\n", path, SDL_GetError());
            return 1;
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            std::printf("%s: --render: SDL_AcquireGPUCommandBuffer failed: %s\n", path, SDL_GetError());
            return 1;
        }

        // Upload the vertex buffer and the white texture.
        {
            SDL_GPUTransferBufferCreateInfo tInfo{};
            tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tInfo.size = vertexBytes + 4;
            SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &tInfo);
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            std::memcpy(mapped, vertices, vertexBytes);
            const unsigned char white[4] = {255, 255, 255, 255};
            std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes, white, 4);
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTransferBufferLocation vbSrc{};
            vbSrc.transfer_buffer = transfer;
            SDL_GPUBufferRegion vbDest{};
            vbDest.buffer = vertexBuffer;
            vbDest.size = vertexBytes;
            SDL_UploadToGPUBuffer(copyPass, &vbSrc, &vbDest, false);

            SDL_GPUTextureTransferInfo texSrc{};
            texSrc.transfer_buffer = transfer;
            texSrc.offset = vertexBytes;
            SDL_GPUTextureRegion texDest{};
            texDest.texture = whiteTexture;
            texDest.w = 1;
            texDest.h = 1;
            texDest.d = 1;
            SDL_UploadToGPUTexture(copyPass, &texSrc, &texDest, false);
            SDL_EndGPUCopyPass(copyPass);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }

        MOJOSHADER_sdlUpdateUniformBuffers(context, cmd);

        SDL_GPUColorTargetInfo colorTargetInfo{};
        colorTargetInfo.texture = colorTarget;
        colorTargetInfo.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTargetInfo, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_GPUBufferBinding vbBinding{};
        vbBinding.buffer = vertexBuffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);
        // Matches the real MojoShader quirk CNA's own draw route already works around: a shader
        // with zero reflected samplers still gets num_samplers=1 at compile time (maxSamplerIndex
        // starts at 0, not -1), so SDL_GPU's validator expects a vertex sampler binding here even
        // though MainVertexShader never samples anything.
        SDL_GPUTextureSamplerBinding vertexDummyBinding{};
        vertexDummyBinding.texture = whiteTexture;
        vertexDummyBinding.sampler = sampler;
        SDL_BindGPUVertexSamplers(pass, 0, &vertexDummyBinding, 1);
        SDL_GPUTextureSamplerBinding samplerBinding{};
        samplerBinding.texture = whiteTexture;
        samplerBinding.sampler = sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
        SDL_EndGPURenderPass(pass);

        // Read the color target back.
        SDL_GPUTransferBufferCreateInfo downloadInfo{};
        downloadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        downloadInfo.size = kWidth * kHeight * 4;
        SDL_GPUTransferBuffer* downloadBuffer = SDL_CreateGPUTransferBuffer(device, &downloadInfo);
        SDL_GPUCopyPass* downloadPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = colorTarget;
        region.w = kWidth;
        region.h = kHeight;
        region.d = 1;
        SDL_GPUTextureTransferInfo downloadDest{};
        downloadDest.transfer_buffer = downloadBuffer;
        downloadDest.pixels_per_row = kWidth;
        downloadDest.rows_per_layer = kHeight;
        SDL_DownloadFromGPUTexture(downloadPass, &region, &downloadDest);
        SDL_EndGPUCopyPass(downloadPass);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            std::printf("%s: --render: SDL_SubmitGPUCommandBufferAndAcquireFence failed: %s\n",
                        path, SDL_GetError());
            return 1;
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        const auto* pixels = static_cast<const unsigned char*>(
            SDL_MapGPUTransferBuffer(device, downloadBuffer, false));
        int nonBlack = 0;
        int alphaZero = 0, alphaFull = 0, alphaOther = 0;
        unsigned char sample[4] = {0, 0, 0, 0};
        for (Uint32 i = 0; i < kWidth * kHeight; ++i)
        {
            const unsigned char* p = pixels + i * 4;
            if (p[0] != 0 || p[1] != 0 || p[2] != 0)
            {
                ++nonBlack;
                sample[0] = p[0]; sample[1] = p[1]; sample[2] = p[2]; sample[3] = p[3];
            }
            if (p[3] == 0) ++alphaZero;
            else if (p[3] == 255) ++alphaFull;
            else ++alphaOther;
        }
        std::printf("%s: --render: alphaZero=%d alphaFull=%d alphaOther=%d (of %u pixels)\n",
                    path, alphaZero, alphaFull, alphaOther, kWidth * kHeight);
        const unsigned char* centerPixel = pixels + (static_cast<std::size_t>(kHeight / 2) * kWidth + kWidth / 2) * 4;
        std::printf("%s: --render: %ux%u target, nonBlack=%d, sample=(%d,%d,%d,%d), "
                    "center=(%d,%d,%d,%d)\n",
                    path, kWidth, kHeight, nonBlack, sample[0], sample[1], sample[2], sample[3],
                    centerPixel[0], centerPixel[1], centerPixel[2], centerPixel[3]);
        SDL_UnmapGPUTransferBuffer(device, downloadBuffer);

        return nonBlack > 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_sdlgpu_probe [--render] <file.fxb>...\n");
        return 2;
    }

    bool renderMode = false;
    int firstFile = 1;
    if (std::strcmp(argv[1], "--render") == 0)
    {
        renderMode = true;
        firstFile = 2;
    }
    if (firstFile >= argc)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_sdlgpu_probe [--render] <file.fxb>...\n");
        return 2;
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    // The shader formats MojoShader's adapter reports are the ones it can actually produce, so
    // asking the device for exactly those is what keeps this an honest gate rather than a
    // hard-coded assumption about SPIR-V.
    const SDL_GPUShaderFormat formats = MOJOSHADER_sdlGetShaderFormats();
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, /*debug_mode=*/true, /*name=*/nullptr);
    if (device == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 4;
    }
    std::printf("SDL_GPU driver: %s, MojoShader shader formats: 0x%08X\n",
                SDL_GetGPUDeviceDriver(device), (unsigned int) formats);

    MOJOSHADER_sdlContext* context =
        MOJOSHADER_sdlCreateContext(device, nullptr, nullptr, nullptr);
    if (context == nullptr)
    {
        std::fprintf(stderr, "MOJOSHADER_sdlCreateContext failed: %s\n",
                     MOJOSHADER_sdlGetError(nullptr));
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 5;
    }

    int failures = 0;
    for (int i = firstFile; i < argc; ++i)
    {
        const std::vector<unsigned char> bytes = ReadFile(argv[i]);
        if (bytes.empty())
        {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            ++failures;
            continue;
        }

        MOJOSHADER_effectShaderContext backend = MakeSdlBackend(context);
        MOJOSHADER_effect* effect = MOJOSHADER_compileEffect(
            bytes.data(), static_cast<unsigned int>(bytes.size()),
            nullptr, 0, nullptr, 0, &backend);

        if (effect == nullptr || effect->error_count > 0)
        {
            if (effect != nullptr)
            {
                for (int e = 0; e < effect->error_count; ++e)
                {
                    std::printf("%s: error: %s\n", argv[i],
                                effect->errors[e].error != nullptr ? effect->errors[e].error
                                                                   : "<null>");
                }
            }
            else
            {
                std::printf("%s: compileEffect returned nothing\n", argv[i]);
            }
            ++failures;
            MOJOSHADER_deleteEffect(effect);
            continue;
        }

        failures += renderMode ? RenderAndReadback(argv[i], device, context, effect)
                               : ExercisePasses(argv[i], context, effect);
        MOJOSHADER_deleteEffect(effect);
    }

    MOJOSHADER_sdlDestroyContext(context);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    std::printf("%s\n", failures == 0
        ? "MojoShader's SDL_GPU adapter binds committed effects with no FNA3D linked."
        : "one or more effects failed");
    return failures == 0 ? 0 : 1;
}
