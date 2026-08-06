// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Magnum/MagnumEffectBackend.hpp"

#include "CNA/Internal/Backends/Magnum/MagnumRenderTargets.hpp"
#include "CNA/Internal/Backends/Magnum/MagnumTextures.hpp"

#include <Magnum/GL/AbstractShaderProgram.h>

#include <string>

namespace CNA::Internal::Backends::Magnum
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

    bool MagnumEffectBackend::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        const bool ok = program_.CompileAndLink(vertSrc, fragSrc);
        compileError_ = ok ? std::string{} : program_.GetLog();
        return ok;
    }

    void MagnumEffectBackend::Bind()
    {
        if (!program_.IsValid())
            return;
        // Magnum's setUniform() makes the program current itself (through direct state access when
        // the driver has it, and an explicit bind otherwise), so there is no separate "use" step:
        // pushing the pending uniforms IS what binding means here.
        UploadRenderTargetFlips();
    }

    void MagnumEffectBackend::Unbind()
    {
        // Nothing to release: GL has no "unbind program" that is meaningful here -- the next
        // Bind()/stock-program selection makes its own program current, and leaving this one bound
        // in the meantime cannot affect a draw that has not selected a program yet.
    }

    bool MagnumEffectBackend::IsValid() const
    {
        return program_.IsValid();
    }

    std::string MagnumEffectBackend::GetCompileError() const
    {
        return compileError_;
    }

    void MagnumEffectBackend::SetUniformFloat(const char* name, float value)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetFloat(program_.LocationOf(name), value);
    }

    void MagnumEffectBackend::SetUniformInt(const char* name, int value)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetInt(program_.LocationOf(name), value);
    }

    void MagnumEffectBackend::SetUniformVec2(const char* name, float x, float y)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector2(program_.LocationOf(name), Mg::Vector2{x, y});
    }

    void MagnumEffectBackend::SetUniformVec3(const char* name, float x, float y, float z)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector3(program_.LocationOf(name), Mg::Vector3{x, y, z});
    }

    void MagnumEffectBackend::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector4(program_.LocationOf(name), Mg::Vector4{x, y, z, w});
    }

    void MagnumEffectBackend::SetUniformMat4(const char* name, const float* matrix)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetMatrix4(program_.LocationOf(name), matrix);
    }

    void MagnumEffectBackend::SetUniformFloatArray(const char* name, const float* values, int count)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetFloatArray(program_.LocationOf(name), values, count);
    }

    void MagnumEffectBackend::SetUniformVec2Array(const char* name, const float* values, int count)
    {
        if (!program_.IsValid() || name == nullptr) return;
        program_.SetVector2Array(program_.LocationOf(name), values, count);
    }

    void MagnumEffectBackend::BindTexture(int unit, ITextureBackend* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* plain = dynamic_cast<MagnumTextureBackend*>(texture))
            plain->GetTexture().bind(unit);
        else if (auto* target = dynamic_cast<MagnumRenderTargetBackend*>(texture))
            target->GetColorTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
        renderTargetFlips_[static_cast<std::size_t>(unit)] =
            SampledRowOrderIsBottomUp(texture) ? 1.0f : 0.0f;
        UploadRenderTargetFlips();
    }

    void MagnumEffectBackend::BindTextureCube(int unit, ITextureCubeBackend* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* cube = dynamic_cast<MagnumTextureCubeBackend*>(texture))
            cube->GetTexture().bind(unit);
        else if (auto* target = dynamic_cast<MagnumRenderTargetCubeBackend*>(texture))
            target->GetTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
    }

    void MagnumEffectBackend::BindTexture3D(int unit, ITexture3DBackend* texture)
    {
        if (!program_.IsValid() || texture == nullptr || unit < 0 || unit > 3)
            return;

        if (auto* volume = dynamic_cast<MagnumTexture3DBackend*>(texture))
            volume->GetTexture().bind(unit);
        else
            texture->BindGL();

        program_.SetInt(program_.LocationOf(SamplerNameForUnit(unit)), unit);
    }

    void MagnumEffectBackend::UploadRenderTargetFlips()
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
