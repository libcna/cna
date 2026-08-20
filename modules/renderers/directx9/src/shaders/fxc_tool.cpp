// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-7 (D9-71): compiles one entry point of a vendored XNA Stock Effect .fx to
// real D3D9 bytecode via the actual Microsoft d3dcompiler_47.dll (design decision 5 -- no native
// Linux D3DCompile() implementation exists, so this cross-compiles with MinGW-w64 and runs under
// Wine). Proven in Phase D9-0 (66/66 entry points compiled, 0 failures) -- moved here unchanged
// from dx9-spike/fxc_tool.cpp, not rewritten. Its own ID3DInclude handler resolves
// Macros.fxh/Common.fxh/Lighting.fxh/Structures.fxh relative to the .fx file's own directory,
// which src/CNA/Internal/Renderers/D3DCommon/shaders/hlsl_compiler_tool.cpp does not need (no
// XNA-style #include tree there).
//
// usage: fxc_tool.exe <file.fx> <entry> <target> <out.bin> [--strip]
// --strip is a dead option -- Phase D9-0 found the real fxc silently ignores the Effect-framework
// tail (VertexShader/PixelShader arrays, Technique) at /T vs_2_0/ps_2_0, so it is never used by
// compile_shaders_sm2.py; kept only so this file stays otherwise identical to the proven spike.
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

static std::string gDir;

// Resolves #include "Macros.fxh" etc. relative to the .fx file's own directory.
struct Inc : public ID3DInclude {
    std::vector<std::string*> live;
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* out, UINT* bytes) override {
        std::ifstream f(gDir + "/" + name, std::ios::binary);
        if (!f) return E_FAIL;
        std::ostringstream ss; ss << f.rdbuf();
        auto* s = new std::string(ss.str());
        live.push_back(s);
        *out = s->data(); *bytes = (UINT)s->size();
        return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage: %s <file.fx> <entry> <target> <out> [--strip]\n", argv[0]); return 2; }
    const std::string path = argv[1], entry = argv[2], target = argv[3], out = argv[4];
    const bool strip = (argc > 5 && std::string(argv[5]) == "--strip");

    auto slash = path.find_last_of("/\\");
    gDir = (slash == std::string::npos) ? "." : path.substr(0, slash);

    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "error: cannot open %s\n", path.c_str()); return 2; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string src = ss.str();

    if (strip) {
        // Cut the D3D9 Effect-framework tail (VertexShader/PixelShader arrays, VSIndices/PSIndices,
        // ShaderIndex, Technique) -- fx-only syntax that a /T vs_2_0 compile cannot parse.
        // Microsoft's source file itself is never modified; this happens only in the compile step.
        size_t cut = std::string::npos;
        for (const char* marker : {"\nVertexShader ", "\nPixelShader ", "\nint VSIndices", "\nint PSIndices",
                                   "\nint ShaderIndex", "\nTechnique", "\ntechnique"}) {
            size_t p = src.find(marker);
            if (p != std::string::npos && p < cut) cut = p;
        }
        if (cut != std::string::npos) src.resize(cut);
    }

    Inc inc;
    ComPtr<ID3DBlob> code, errs;
    const HRESULT hr = D3DCompile(src.data(), src.size(), path.c_str(), nullptr, &inc,
                                  entry.c_str(), target.c_str(),
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                  code.GetAddressOf(), errs.GetAddressOf());
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAIL %s [%s/%s] hr=0x%08lx\n%.*s\n", path.c_str(), entry.c_str(), target.c_str(),
                     (unsigned long)hr,
                     errs ? (int)errs->GetBufferSize() : 0,
                     errs ? (const char*)errs->GetBufferPointer() : "");
        return 1;
    }
    std::ofstream o(out, std::ios::binary);
    o.write((const char*)code->GetBufferPointer(), (std::streamsize)code->GetBufferSize());
    std::printf("OK %s %s %s %zu\n", path.c_str(), entry.c_str(), target.c_str(), code->GetBufferSize());
    return 0;
}
