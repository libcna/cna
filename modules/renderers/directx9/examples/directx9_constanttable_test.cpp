// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-11 (D9-110): validates D3D9ConstantTable's real CTAB parser against real
// Microsoft compiler output, per this task's own requirement: "Unit-test against a shader with
// known constants before wiring it to anything."
//
// This binary compiles a small, known HLSL vertex shader (3 named float constants of different
// sizes) via the REAL D3DCompile() -- the exact call this project's own fxc_tool.cpp already
// proved works in this environment -- then runs ParseConstantTableEXT() on the resulting
// bytecode. Rather than trusting the parser's own output blindly (a binary-format parser can be
// silently, plausibly wrong if an offset-base assumption is off), this ALSO calls D3DDisassemble()
// on the same bytecode and parses its own authoritative "// Registers:" comment block -- the exact
// same regex this project's own extract_shader_registers.py (D9-72) already uses to build the
// checked-in stock-effect register tables -- and cross-checks every entry from BOTH sources
// matches exactly. A wrong offset assumption in the binary parser would very likely either produce
// garbage (non-matching/unreadable) names or wrong register numbers here, not a subtly-plausible
// wrong answer.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantTable.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace CNA::Internal::Renderers::DirectX9;

static int passCount = 0;
static int totalCount = 0;

static void check(bool ok, const char* label)
{
    ++totalCount;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (ok) ++passCount;
}

namespace
{
    // A small, self-contained shader (no #include tree) with 3 named constants that get
    // meaningfully different register counts: a float4x4 (4 registers), a float4 (1 register),
    // and a plain scalar (1 register, D3D9 always allocates a full c-register per named constant
    // even for a scalar). All 3 are genuinely read, so the compiler cannot optimize any of them
    // away.
    const char* kTestShaderSrc = R"(
float4x4 WorldViewProj;
float4 TintColor;
float SomeScalar;

struct VSInput { float4 Position : POSITION0; };
struct VSOutput { float4 Position : POSITION0; float4 Color : COLOR0; };

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(input.Position, WorldViewProj);
    output.Color = TintColor * SomeScalar;
    return output;
}
)";

    struct DisasmConstant
    {
        std::string name;
        int registerIndex;
        int registerCount;
    };

    // Mirrors extract_shader_registers.py's own REGISTER_LINE_RE / parse_registers() exactly, so
    // this test's oracle is the same proven logic the checked-in stock-effect tables were built
    // with, not a reimplementation that could itself be subtly wrong in a different way.
    std::vector<DisasmConstant> ParseDisassemblyRegisters(const std::string& disassembly)
    {
        std::vector<DisasmConstant> result;
        static const std::regex lineRe(R"(^//\s+(\S+)\s+c(\d+)\s+(\d+)\s*$)");
        bool inTable = false;
        std::istringstream stream(disassembly);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "// Registers:")
            {
                inTable = true;
                continue;
            }
            if (!inTable) continue;
            if (line.rfind("//   ---", 0) == 0) continue;
            if (line == "//" || line.empty())
            {
                if (!result.empty()) break;
                continue;
            }
            std::smatch m;
            if (std::regex_match(line, m, lineRe))
                result.push_back({m[1].str(), std::stoi(m[2].str()), std::stoi(m[3].str())});
        }
        return result;
    }

    bool CompileTestShader(std::vector<uint8_t>& outBytecode, std::string& outError)
    {
        ComPtr<ID3DBlob> code, errs;
        const HRESULT hr = D3DCompile(kTestShaderSrc, std::strlen(kTestShaderSrc), "constanttable_test.hlsl",
                                      nullptr, nullptr, "VSMain", "vs_3_0",
                                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                      code.GetAddressOf(), errs.GetAddressOf());
        if (FAILED(hr))
        {
            outError = errs ? std::string(static_cast<const char*>(errs->GetBufferPointer()), errs->GetBufferSize())
                             : "(no error blob)";
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
        outBytecode.assign(bytes, bytes + code->GetBufferSize());
        return true;
    }

    bool DisassembleTestShader(const std::vector<uint8_t>& bytecode, std::string& outText)
    {
        ComPtr<ID3DBlob> disasm;
        const HRESULT hr = D3DDisassemble(bytecode.data(), bytecode.size(), 0, nullptr, disasm.GetAddressOf());
        if (FAILED(hr)) return false;
        outText.assign(static_cast<const char*>(disasm->GetBufferPointer()), disasm->GetBufferSize());
        return true;
    }

    const D3D9ShaderConstantEXT* Find(const std::vector<D3D9ShaderConstantEXT>& v, const std::string& name)
    {
        auto it = std::find_if(v.begin(), v.end(), [&](const D3D9ShaderConstantEXT& c) { return c.name == name; });
        return it == v.end() ? nullptr : &*it;
    }
}

int main()
{
    std::vector<uint8_t> bytecode;
    std::string error;
    if (!CompileTestShader(bytecode, error))
    {
        std::printf("FATAL: D3DCompile failed for the test shader:\n%s\n", error.c_str());
        return 1;
    }
    check(!bytecode.empty(), "test shader compiled to a non-empty bytecode blob");

    std::string disassembly;
    const bool disassembled = DisassembleTestShader(bytecode, disassembly);
    check(disassembled, "D3DDisassemble() succeeded on the same bytecode (independent oracle)");

    const std::vector<DisasmConstant> oracle = disassembled ? ParseDisassemblyRegisters(disassembly)
                                                              : std::vector<DisasmConstant>{};
    check(oracle.size() == 3, "the disassembler's own \"// Registers:\" block lists exactly 3 constants");

    const std::vector<D3D9ShaderConstantEXT> parsed = ParseConstantTableEXT(bytecode.data(), bytecode.size());
    check(parsed.size() == 3, "ParseConstantTableEXT() found exactly 3 named constants");

    // Cross-check every oracle entry against the CTAB parser's own output -- the real test.
    bool allCrossChecked = !oracle.empty();
    for (const auto& o : oracle)
    {
        const D3D9ShaderConstantEXT* p = Find(parsed, o.name);
        const bool matches = p != nullptr &&
                              p->registerIndex == o.registerIndex &&
                              p->registerCount == o.registerCount &&
                              p->registerSet == D3D9RegisterSetEXT::Float4;
        if (!matches) allCrossChecked = false;
        char label[256];
        std::snprintf(label, sizeof(label),
                      "CTAB parser matches disassembler oracle for '%s' (c%d, count %d)",
                      o.name.c_str(), o.registerIndex, o.registerCount);
        check(matches, label);
    }
    check(allCrossChecked, "every disassembler-reported constant was found, at the right register, by the CTAB parser");

    // Direct sanity checks on the specific constants this shader is known to declare.
    const D3D9ShaderConstantEXT* wvp = Find(parsed, "WorldViewProj");
    check(wvp != nullptr && wvp->registerCount == 4,
          "WorldViewProj (float4x4) occupies 4 registers");
    const D3D9ShaderConstantEXT* tint = Find(parsed, "TintColor");
    check(tint != nullptr && tint->registerCount == 1,
          "TintColor (float4) occupies 1 register");
    const D3D9ShaderConstantEXT* scalar = Find(parsed, "SomeScalar");
    check(scalar != nullptr && scalar->registerCount == 1,
          "SomeScalar (float) occupies 1 register (D3D9 allocates a full c-register per named constant)");

    // Edge cases: no crash on absent/malformed CTAB data.
    {
        const std::vector<D3D9ShaderConstantEXT> empty = ParseConstantTableEXT(nullptr, 0);
        check(empty.empty(), "null bytecode returns an empty vector, does not crash");
    }
    {
        const uint8_t garbage[8] = {0xFE, 0xFF, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
        const std::vector<D3D9ShaderConstantEXT> noCtab = ParseConstantTableEXT(garbage, sizeof(garbage));
        check(noCtab.empty(), "bytecode with no CTAB comment (or a truncated/malformed one) returns empty, does not crash");
    }
    {
        // A version token immediately followed by D3DSIO_END -- a minimal, well-formed-but-empty
        // shader with genuinely no comment at all.
        const uint8_t minimal[8] = {0x00, 0x03, 0xFE, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
        const std::vector<D3D9ShaderConstantEXT> noConstants = ParseConstantTableEXT(minimal, sizeof(minimal));
        check(noConstants.empty(), "a well-formed shader stream with no CTAB comment at all returns empty");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
