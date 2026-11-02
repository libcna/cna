// SPDX-License-Identifier: MS-PL
#include "CNA/C/shader.h"
#include "CNA/ShaderDiagnosticEXT.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include <cstring>
#include <thread>
static_assert(CNA_SHADER_LANGUAGE_UNKNOWN_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Unknown));
static_assert(CNA_SHADER_LANGUAGE_GLSL_DESKTOP_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::GlslDesktop));
static_assert(CNA_SHADER_LANGUAGE_GLSL_ES_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::GlslEs));
static_assert(CNA_SHADER_LANGUAGE_GLSL_VULKAN_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::GlslVulkan));
static_assert(CNA_SHADER_LANGUAGE_HLSL_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Hlsl));
static_assert(CNA_SHADER_LANGUAGE_MSL_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Msl));
static_assert(CNA_SHADER_LANGUAGE_WGSL_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Wgsl));
static_assert(CNA_SHADER_LANGUAGE_SPIRV_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::SpirV));
static_assert(CNA_SHADER_LANGUAGE_DXIL_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Dxil));
static_assert(CNA_SHADER_LANGUAGE_COUNT_EXT == static_cast<uint32_t>(CNA::ShaderLanguageEXT::Count));
static_assert(CNA_SHADER_STAGE_UNKNOWN_EXT == static_cast<uint32_t>(CNA::ShaderStageEXT::Unknown));
static_assert(CNA_SHADER_STAGE_VERTEX_EXT == static_cast<uint32_t>(CNA::ShaderStageEXT::Vertex));
static_assert(CNA_SHADER_STAGE_FRAGMENT_EXT == static_cast<uint32_t>(CNA::ShaderStageEXT::Fragment));
static_assert(CNA_SHADER_STAGE_COUNT_EXT == static_cast<uint32_t>(CNA::ShaderStageEXT::Count));
static_assert(CNA_SHADER_DIAGNOSTIC_SEVERITY_INFORMATION_EXT == static_cast<uint32_t>(CNA::ShaderDiagnosticSeverityEXT::Information));
static_assert(CNA_SHADER_DIAGNOSTIC_SEVERITY_WARNING_EXT == static_cast<uint32_t>(CNA::ShaderDiagnosticSeverityEXT::Warning));
static_assert(CNA_SHADER_DIAGNOSTIC_SEVERITY_ERROR_EXT == static_cast<uint32_t>(CNA::ShaderDiagnosticSeverityEXT::Error));
static_assert(CNA_SHADER_DIAGNOSTIC_SEVERITY_COUNT_EXT == static_cast<uint32_t>(CNA::ShaderDiagnosticSeverityEXT::Count));
#ifdef CNA_CNAEXT
static_assert(CNA_SHADER_BINDING_SAMPLED_TEXTURE2D_EXT == static_cast<uint32_t>(CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D));
static_assert(CNA_SHADER_BINDING_SAMPLED_TEXTURE_CUBE_EXT == static_cast<uint32_t>(CNA::Graphics::ShaderBindingTypeEXT::SampledTextureCube));
static_assert(CNA_SHADER_BINDING_SAMPLED_TEXTURE3D_EXT == static_cast<uint32_t>(CNA::Graphics::ShaderBindingTypeEXT::SampledTexture3D));
static_assert(CNA_SHADER_BINDING_COUNT_EXT == static_cast<uint32_t>(CNA::Graphics::ShaderBindingTypeEXT::Count));
#endif
extern "C" int cna_test_shader_worker(int (*function)(void))
{
    int result = 1;
    std::thread worker([&] { result = function(); }); worker.join(); return result;
}
extern "C" uint64_t cna_test_shader_parse(const char* log, uint32_t stage, uint64_t index,
    CNA_ShaderDiagnosticInfoEXT* info, char* label, char* message)
{
    const auto values = CNA::ShaderDiagnosticEXT::parseCompilerLog(log,
        static_cast<CNA::ShaderStageEXT>(stage), "fixture");
    if (index < values.size()) {
        const auto& d = values[static_cast<size_t>(index)];
        *info = {sizeof(*info), 1, static_cast<uint32_t>(d.getSeverity()),
            static_cast<uint32_t>(d.getStage()), d.getLine(), d.getColumn()};
        std::memcpy(label, d.getSourceLabel().c_str(), d.getSourceLabel().size() + 1);
        std::memcpy(message, d.getMessage().c_str(), d.getMessage().size() + 1);
    }
    return values.size();
}
extern "C" CNA_Bool cna_test_shader_descriptor(uint32_t language)
{
    return CNA::UsesDescriptorBindingContractEXT(static_cast<CNA::ShaderLanguageEXT>(language)) ? CNA_TRUE : CNA_FALSE;
}
extern "C" int cna_test_shader_code_native(uint32_t language, uint32_t stage,
    const char* payload, uint64_t bytes, CNA_ShaderCodeInfoEXT* info)
{
#ifdef CNA_CNAEXT
    using CNA::Graphics::ShaderCodeEXT;
    ShaderCodeEXT code = language >= 7 ? ShaderCodeEXT(static_cast<CNA::ShaderLanguageEXT>(language),
        static_cast<CNA::ShaderStageEXT>(stage), "main", "code", std::vector<uint8_t>(payload, payload + bytes)) :
        ShaderCodeEXT(static_cast<CNA::ShaderLanguageEXT>(language), static_cast<CNA::ShaderStageEXT>(stage),
            "main", "code", std::string(payload, static_cast<size_t>(bytes)));
    *info = {sizeof(*info), 1, static_cast<uint32_t>(code.getLanguage()), static_cast<uint32_t>(code.getStage()),
        code.getPayloadByteSize(), code.isText() ? CNA_TRUE : CNA_FALSE, {0}};
    return code.isBinary() != code.isText();
#else
    (void)language; (void)stage; (void)payload; (void)bytes; (void)info;
    return 0;
#endif
}

#include "ShaderSmokePackage.generated.inc"

extern "C" const uint8_t* cna_test_shader_spirv(uint32_t stage, uint64_t* byte_count)
{
    const auto littleEndianBytes = [](const auto& words) {
        std::vector<uint8_t> bytes;
        bytes.reserve(sizeof(words));
        for (const uint32_t word : words)
            for (unsigned byte = 0; byte < 4; ++byte)
                bytes.push_back(static_cast<uint8_t>(word >> (byte * 8)));
        return bytes;
    };
    static const auto vertex = littleEndianBytes(CNA::CApiTest::ShaderSmoke::kVertexSpirV);
    static const auto fragment = littleEndianBytes(CNA::CApiTest::ShaderSmoke::kFragmentSpirV);
    const auto& bytes = stage == CNA_SHADER_STAGE_VERTEX_EXT ? vertex : fragment;
    *byte_count = static_cast<uint64_t>(bytes.size());
    return bytes.data();
}
