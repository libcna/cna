// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md DX-14-compile: standalone offline HLSL->DXBC compiler tool.
//
// D3DCompile() only exists at runtime, in d3dcompiler.dll -- MinGW ships the header/import-lib
// for *linking* only, not a native Linux implementation (design decision 5). So this cannot be a
// native Linux/Python tool shelling out to fxc: it is a small Windows .exe, cross-built via this
// project's existing MinGW-w64 toolchain and run through scripts/run-wine-dxvk.sh (DX-3), exactly
// like the DX-1 spike and the DirectX11_Smoke/DirectX11_Common CTests already do.
//
// Usage: hlsl_compiler_tool.exe <input.hlsl> <entry_point> <target_profile> <output.dxbc>
// Exit code 0 = compiled OK, 1 = compile error (message printed to stderr), 2 = usage/IO error.

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using Microsoft::WRL::ComPtr;

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: %s <input.hlsl> <entry_point> <target_profile> <output.dxbc>\n", argv[0]);
        return 2;
    }

    const std::string inputPath = argv[1];
    const std::string entryPoint = argv[2];
    const std::string targetProfile = argv[3];
    const std::string outputPath = argv[4];

    std::ifstream in(inputPath, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "error: cannot open input file '%s'\n", inputPath.c_str());
        return 2;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;

    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    const HRESULT hr = D3DCompile(
        source.data(), source.size(),
        inputPath.c_str(),
        nullptr, nullptr,
        entryPoint.c_str(), targetProfile.c_str(),
        flags, 0,
        code.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(hr))
    {
        std::fprintf(stderr, "D3DCompile FAILED (hr=0x%08lx) for '%s' [%s/%s]\n",
                     static_cast<unsigned long>(hr), inputPath.c_str(), entryPoint.c_str(), targetProfile.c_str());
        if (errors)
        {
            std::fprintf(stderr, "%.*s\n", static_cast<int>(errors->GetBufferSize()),
                         static_cast<const char*>(errors->GetBufferPointer()));
        }
        return 1;
    }

    if (errors)
    {
        // Warnings without a hard failure -- still surface them.
        std::fprintf(stderr, "D3DCompile warnings for '%s' [%s/%s]:\n%.*s\n",
                     inputPath.c_str(), entryPoint.c_str(), targetProfile.c_str(),
                     static_cast<int>(errors->GetBufferSize()),
                     static_cast<const char*>(errors->GetBufferPointer()));
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out)
    {
        std::fprintf(stderr, "error: cannot open output file '%s'\n", outputPath.c_str());
        return 2;
    }
    out.write(static_cast<const char*>(code->GetBufferPointer()),
              static_cast<std::streamsize>(code->GetBufferSize()));

    std::printf("OK: %s [%s/%s] -> %s (%zu bytes)\n", inputPath.c_str(), entryPoint.c_str(),
                targetProfile.c_str(), outputPath.c_str(), code->GetBufferSize());
    return 0;
}
