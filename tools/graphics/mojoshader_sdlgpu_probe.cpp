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

#include "mojoshader.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
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
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cna_mojoshader_sdlgpu_probe <file.fxb>...\n");
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
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, /*debug_mode=*/false, /*name=*/nullptr);
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
    for (int i = 1; i < argc; ++i)
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

        failures += ExercisePasses(argv[i], context, effect);
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
