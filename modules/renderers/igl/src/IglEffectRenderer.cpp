// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"

#include <igl/Device.h>
#include <igl/Shader.h>
#include <igl/ShaderCreator.h>

#include <cstring>
#include <string>

namespace CNA::Internal::Renderers::Igl
{
    IglEffectRenderer::IglEffectRenderer(IglRenderer& owner)
        : owner_(owner), programId_(owner.NextProgramIdEXT())
    {
    }

    bool IglEffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        stages_.reset();
        compileError_.clear();

        if (vertSrc.empty() || fragSrc.empty())
        {
            compileError_ = "IGL renderer: a ShaderEffect needs both a vertex and a fragment shader";
            return false;
        }

        const IglShaderSources sources =
            AdaptCustomShaderSources(vertSrc, fragSrc, owner_.IsVulkanBackend());

        igl::Result result;
        stages_ = igl::ShaderStagesCreator::fromModuleStringInput(
            owner_.GetDevice(), sources.vertex.c_str(), "main", "CNA ShaderEffect VS",
            sources.fragment.c_str(), "main", "CNA ShaderEffect FS", &result);

        if (!stages_ || !result.isOk())
        {
            stages_.reset();
            compileError_ = result.message.empty()
                                ? std::string("IGL renderer: ShaderEffect compilation failed")
                                : result.message;
            return false;
        }

        return true;
    }

    void IglEffectRenderer::Bind()
    {
        owner_.boundCustomEffect_ = this;
    }

    void IglEffectRenderer::Unbind()
    {
        if (owner_.boundCustomEffect_ == this)
            owner_.boundCustomEffect_ = nullptr;
    }

    void IglEffectRenderer::RecordUniform(const char* name, const igl::UniformType type,
                                          const int numElements, const float* values,
                                          const int floatCount)
    {
        if (name == nullptr || values == nullptr || floatCount <= 0)
            return;

        UniformValue value;
        value.type = type;
        value.numElements = numElements;
        value.data.assign(values, values + floatCount);
        uniforms_[name] = std::move(value);
    }

    void IglEffectRenderer::SetUniformFloat(const char* name, const float value)
    {
        RecordUniform(name, igl::UniformType::Float, 1, &value, 1);
    }

    void IglEffectRenderer::SetUniformInt(const char* name, const int value)
    {
        // The value is stored bit-exact rather than converted: an int uniform's bytes must reach
        // glUniform1iv unchanged, and a float round-trip would quietly alter large magnitudes.
        float storage = 0.0f;
        std::memcpy(&storage, &value, sizeof(float));
        RecordUniform(name, igl::UniformType::Int, 1, &storage, 1);
    }

    void IglEffectRenderer::SetUniformVec2(const char* name, const float x, const float y)
    {
        const float values[2] = {x, y};
        RecordUniform(name, igl::UniformType::Float2, 1, values, 2);
    }

    void IglEffectRenderer::SetUniformVec3(const char* name, const float x, const float y,
                                           const float z)
    {
        const float values[3] = {x, y, z};
        RecordUniform(name, igl::UniformType::Float3, 1, values, 3);
    }

    void IglEffectRenderer::SetUniformVec4(const char* name, const float x, const float y,
                                           const float z, const float w)
    {
        const float values[4] = {x, y, z, w};
        RecordUniform(name, igl::UniformType::Float4, 1, values, 4);
    }

    void IglEffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        RecordUniform(name, igl::UniformType::Mat4x4, 1, matrix, 16);
    }

    void IglEffectRenderer::SetUniformFloatArray(const char* name, const float* values,
                                                  const int count)
    {
        RecordUniform(name, igl::UniformType::Float, count, values, count);
    }

    void IglEffectRenderer::SetUniformVec2Array(const char* name, const float* values,
                                                 const int count)
    {
        RecordUniform(name, igl::UniformType::Float2, count, values, count * 2);
    }

    void IglEffectRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(igl::IGL_TEXTURE_SAMPLERS_MAX))
            return;

        if (texture == nullptr)
        {
            boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
            return;
        }

        if (auto* iglTexture = dynamic_cast<IglTextureRenderer*>(texture))
        {
            boundTextures_[static_cast<std::size_t>(unit)] = iglTexture->GetIglTexture().get();
            return;
        }
        if (auto* target = dynamic_cast<IglRenderTargetRenderer*>(texture))
        {
            boundTextures_[static_cast<std::size_t>(unit)] = target->GetColorTexture().get();
            return;
        }

        CNA::Logger::Warn(
            "IGL renderer: ShaderEffect::SetTexture was given a texture this renderer does not own; "
            "the sampler unit was left unbound.",
            CNA::LogCategory::RENDER);
        boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
    }

    void IglEffectRenderer::BindTextureCube(const int unit, ITextureCubeRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(igl::IGL_TEXTURE_SAMPLERS_MAX))
            return;

        if (texture == nullptr)
        {
            boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
            return;
        }

        if (auto* cube = dynamic_cast<IglTextureCubeRenderer*>(texture))
        {
            boundTextures_[static_cast<std::size_t>(unit)] = cube->GetIglTexture().get();
            return;
        }
        if (auto* renderCube = dynamic_cast<IglRenderTargetCubeRenderer*>(texture))
        {
            boundTextures_[static_cast<std::size_t>(unit)] = renderCube->GetIglTexture().get();
            return;
        }

        boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
    }

    void IglEffectRenderer::BindTexture3D(const int unit, ITexture3DRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(igl::IGL_TEXTURE_SAMPLERS_MAX))
            return;

        if (texture == nullptr)
        {
            boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
            return;
        }

        if (auto* volume = dynamic_cast<IglTexture3DRenderer*>(texture))
        {
            boundTextures_[static_cast<std::size_t>(unit)] = volume->GetIglTexture().get();
            return;
        }

        boundTextures_[static_cast<std::size_t>(unit)] = nullptr;
    }
}
