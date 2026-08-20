// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-7 (D9-72): disassembles a compiled D3D9 shader (via D3DDisassemble(), the
// real Microsoft d3dcompiler_47.dll's own disassembler) and prints the result to stdout. Used by
// extract_shader_registers.py to read the compiler's own authoritative "// Registers:" comment
// block -- the real, per-entry-point register/size table, which can genuinely differ from what a
// constant's declared HLSL type alone would imply (see extract_shader_registers.py's own header
// comment for the specific case that proved this).
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <vector>

using Microsoft::WRL::ComPtr;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <bin>\n", argv[0]);
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "error: cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    ComPtr<ID3DBlob> disasm;
    const HRESULT hr = D3DDisassemble(data.data(), data.size(), 0, nullptr, disasm.GetAddressOf());
    if (FAILED(hr))
    {
        std::fprintf(stderr, "D3DDisassemble failed hr=0x%08lx\n", static_cast<unsigned long>(hr));
        return 1;
    }

    std::fwrite(disasm->GetBufferPointer(), 1, disasm->GetBufferSize(), stdout);
    return 0;
}
