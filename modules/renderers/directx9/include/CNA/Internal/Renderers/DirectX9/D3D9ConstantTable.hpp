#pragma once

// plans/plan_dx9.md Phase D9-11 (D9-110): parses the CTAB (constant table) comment Microsoft's real
// compiler embeds in compiled D3D9 shader bytecode, returning the name -> register mapping for
// every named constant. Design decision 9: this project does not link D3DX (ID3DXConstantTable's
// own home -- a deprecated d3dx9_43.dll redistributable this project will not ship), so this reads
// the documented D3DXSHADER_CONSTANTTABLE binary layout directly instead of calling that helper
// API. Needed because, unlike D3D11's own D3D11EffectRenderer (a fixed-slot cbuffer convention
// works there since HLSL cbuffer field offsets are caller-predictable), D3D9 constant REGISTERS
// are assigned by the compiler itself and can vary between shaders -- a real post-compile
// name -> register lookup is the only reliable way to know where a named uniform actually landed.
//
// Token-stream layout reference (D3D9 shader bytecode): a comment token has its low 16 bits equal
// to 0xFFFE, with bits 16-30 giving the comment's payload size in DWORDs. This project's own
// compare_against_fxb.py (D9-73) already walks this exact token stream, DWORD by DWORD, to strip
// comments from 66 real compiled shaders and get an exact 61/66 byte-identical match against
// Microsoft's own shipped bytecode -- that already-proven walking strategy is mirrored here
// (advance one DWORD at a time; only comment tokens need their own length decoded, every other
// token is stepped over one DWORD at a time without needing a full instruction-length decoder).
//
// The CTAB payload itself begins with the 'CTAB' FourCC, followed by the documented
// D3DXSHADER_CONSTANTTABLE header (all offset fields relative to the 'CTAB' FourCC's own byte
// position) and a D3DXSHADER_CONSTANTINFO[] array. See D3D9ConstantTable.cpp for the exact layout
// this parser assumes -- validated empirically against real d3dcompiler_47.dll output in
// D3D9ConstantTableTests.cpp, not merely assumed from memory of the documented format.

#include <cstddef>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    /// Which D3D9 constant register file a shader constant lives in
    /// (mirrors the real D3DXREGISTER_SET enum values found in the CTAB data).
    enum class D3D9RegisterSetEXT
    {
        Bool,
        Int4,
        Float4,
        Sampler,
        Unknown,
    };

    /// One named constant's real, compiler-assigned register location.
    struct D3D9ShaderConstantEXT
    {
        std::string name;
        D3D9RegisterSetEXT registerSet = D3D9RegisterSetEXT::Unknown;
        int registerIndex = 0;
        int registerCount = 0;
    };

    /// Parses `bytecode` (as returned by D3DCompile()) and returns every named constant the CTAB
    /// comment describes. Returns an empty vector if no CTAB comment is present -- not itself an
    /// error (e.g. a shader compiled with debug info stripped).
    std::vector<D3D9ShaderConstantEXT> ParseConstantTableEXT(const void* bytecode, size_t byteLength);
}
