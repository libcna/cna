// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Magnum/MagnumEffectRenderer.hpp"

#include "CNA/Internal/Renderers/Magnum/MagnumRenderTargets.hpp"
#include "CNA/Internal/Renderers/Magnum/MagnumTextures.hpp"

#include <Magnum/GL/AbstractShaderProgram.h>

#include <string>

namespace CNA::Internal::Renderers::Magnum
{
    namespace
    {
        /// Sampler names an effect's GLSL may declare, indexed by texture unit.
        const char* SamplerNameForUnit(int unit)
        {
            switch (unit)
            {
                case 0:  return "texture1";
                case 1:  return "texture2";
                case 2:  return "texture3";
                default: return "texture4";
            }
        }
    }

    bool MagnumEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        const bool ok = program_.CompileAndLink(vertSrc, fragSrc);
        compileError_ = ok ? std::string{} : program_.GetLog();
        return ok;
    }

    void MagnumEffectRenderer::Bind()
    {
        if (!program_.IsValid())
            return;
        // Magnum's setUniform() makes the program current itself (through direct state access when
        // the driver has it, and an explicit bind otherwise), so there is no separate "use" step:
        // pushing the pending uniforms IS what binding means here.
        UploadRenderTargetFlips();
    }

    void MagnumEffectRenderer::Unbind()
    {
        // Nothing to release: GL has no "unbind program" that is meaningful here -- the next
        // Bind()/stock-program selection makes its own program current, and leaving this one bound
        // in the meantime cannot affect a draw that has not selected a program yet.
    }

    bool MagnumEffectRenderer::IsValid() const
    {
        return program_.IsValid();
    }

    std::string MagnumEffectRenderer::GetCompileError() const
    {
        return compileError_;
    }

    void MagnumEffectRenderer::SetUniformFloat(const char* name, float value)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetFloat(program_.LocationOf(name), value);
    }

    void MagnumEffectRenderer::SetUniformInt(const char* name, int value)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetInt(program_.LocationOf(name), value);
    }

    void MagnumEffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector2(program_.LocationOf(name), Mg::Vector2{x, y});
    }

    void MagnumEffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector3(program_.LocationOf(name), Mg::Vector3{x, y, z});
    }

    void MagnumEffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector4(program_.LocationOf(name), Mg::Vector4{x, y, z, w});
    }

    void MagnumEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetMatrix4(program_.LocationOf(name), matrix);
    }

    void MagnumEffectRenderer::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetFloatArray(program_.LocationOf(name), values, count);
    }

    void MagnumEffectRenderer::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector2Array(program_.LocationOf(name), values, count);
    }

    void MagnumEffectRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* plain = dynamic_cast<MagnumTextureRenderer*>(texture))
            plain->GetTexture().bind(unit);
        else if (auto* target = dynamic_cast<MagnumRenderTargetRenderer*>(texture))
            target->GetColorTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
        renderTargetFlips_[static_cast<std::size_t>(unit)] =
            SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
        UploadRenderTargetFlips();
    }

    void MagnumEffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* cube = dynamic_cast<MagnumTextureCubeRenderer*>(texture))
            cube->GetTexture().bind(unit);
        else if (auto* target = dynamic_cast<MagnumRenderTargetCubeRenderer*>(texture))
            target->GetTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
    }

    void MagnumEffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* volume = dynamic_cast<MagnumTexture3DRenderer*>(texture))
            volume->GetTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
    }

    void MagnumEffectRenderer::UploadRenderTargetFlips()
    {
        // Opt-in: an effect that never samples a render target, or that handles the orientation
        // itself, simply does not declare uRtFlipV and the location resolves to -1.
        const int location = program_.LocationOf("uRtFlipV");
        if (location < 0)
            return;
        program_.SetVector4(location, Mg::Vector4{
            renderTargetFlips_[0], renderTargetFlips_[1],
            renderTargetFlips_[2], renderTargetFlips_[3]});
    }
}
