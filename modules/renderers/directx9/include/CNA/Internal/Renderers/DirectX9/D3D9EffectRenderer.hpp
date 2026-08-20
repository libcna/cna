#pragma once

// plans/plan_dx9.md Phase D9-11 (D9-111): custom ShaderEffect for D3D9 -- runtime D3DCompile() of
// arbitrary HLSL vertex+fragment source, separate from the offline stock-shader pipeline (design
// decision 5), mirroring D3D11EffectRenderer's own established contract as closely as D3D9's
// fundamentally different constant-register model allows.
//
// Unlike D3D11EffectRenderer (a fixed-slot cbuffer convention works there -- HLSL cbuffer field
// offsets are caller-predictable), D3D9 constant REGISTERS are assigned by the compiler and can
// vary between shaders, so SetUniform*'s `name` parameter is NOT ignored here: every uniform set
// is looked up by name, per shader stage, in that stage's own real, compiler-assigned register
// table (D9-110's ParseConstantTableEXT()). A name absent from BOTH stages' tables is a silent
// no-op (the custom shader simply doesn't read that uniform) -- not an error, matching common
// shader-uniform-setter conventions across this project's other renderers.
//
// Compile target follows GraphicsProfile (D9-100): SM2.0 (vs_2_0/ps_2_0) under Reach, SM3.0
// (vs_3_0/ps_3_0) under HiDef -- selected at construction time, not per-CompileProgram() call.
//
// Scope boundary (honest, not a placeholder): CompileProgram()/Bind()/Unbind()/IsValid()/
// GetCompileError()/the uniform setters are real and independently GPU-tested here (compile a
// trivial custom vertex+pixel shader pair at runtime, draw a real triangle with it, read back the
// exact pixel color driven by a uniform -- D3D9ConstantTableTests.cpp's own sibling,
// D3D9EffectRendererTests.cpp). Unlike D3D11EffectRenderer, this class does NOT manage a vertex
// declaration -- D3D9's vertex declaration is a separate, decoupled device state (not baked into
// the shader like D3D11's InputLayout), and D9-112 (SpriteBatch::Begin(effect) wiring) is where the
// real vertex-declaration/binding integration happens, matching D3D9SpriteBatchRenderer's own
// existing stride-24 SpriteVertex contract. BindTexture() is not overridden (uses IEffectRenderer's
// own no-op default) -- texture unit 0 is bound by the caller, per IEffectRenderer's own doc
// comment.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantTable.hpp"

#include <d3d9.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::WRL::ComPtr;

    class D3D9EffectRenderer final : public IEffectRenderer
    {
    public:
        /// `hiDef` selects the vs_3_0/ps_3_0 compile target (GraphicsProfile::HiDef); false
        /// selects vs_2_0/ps_2_0 (GraphicsProfile::Reach, D9-100).
        D3D9EffectRenderer(IDirect3DDevice9* device, bool hiDef);

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override { return valid_; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;

    private:
        /// Looks `name` up in both stages' real register tables and uploads `data` (`registerCount`
        /// float4 registers' worth, i.e. `registerCount * 4` floats) to whichever stage(s)
        /// declare it. Silent no-op if neither stage's compiled shader reads this name.
        void UploadEXT(const char* name, const float* data, int registerCount);

        ComPtr<IDirect3DDevice9> device_;
        bool hiDef_;
        ComPtr<IDirect3DVertexShader9> vs_;
        ComPtr<IDirect3DPixelShader9> ps_;
        std::vector<D3D9ShaderConstantEXT> vsConstants_;
        std::vector<D3D9ShaderConstantEXT> psConstants_;
        std::string compileError_;
        bool valid_ = false;
    };
}
