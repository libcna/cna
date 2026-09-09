// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderPackageEXT.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace CNA::Graphics
{
    namespace
    {
        [[nodiscard]] bool IsValidStage(const CNA::ShaderStageEXT stage) noexcept
        {
            return stage == CNA::ShaderStageEXT::Vertex
                || stage == CNA::ShaderStageEXT::Fragment
                || stage == CNA::ShaderStageEXT::Compute;
        }

        [[nodiscard]] bool IsValidBindingType(const ShaderBindingTypeEXT type) noexcept
        {
            switch (type)
            {
                case ShaderBindingTypeEXT::SampledTexture2D:
                case ShaderBindingTypeEXT::SampledTextureCube:
                case ShaderBindingTypeEXT::SampledTexture3D:
                case ShaderBindingTypeEXT::SampledTexture2DArray:
                case ShaderBindingTypeEXT::StorageBuffer:
                case ShaderBindingTypeEXT::StorageTexture2D:
                    return true;
                case ShaderBindingTypeEXT::Count:
                    return false;
            }
            return false;
        }
    }

    ShaderBindingRequirementEXT::ShaderBindingRequirementEXT(
        std::string name, const int binding, const ShaderBindingTypeEXT type,
        const CNA::ShaderStageEXT stage)
        : name_(std::move(name))
        , binding_(binding)
        , type_(type)
        , stage_(stage)
    {
        if (name_.empty())
            throw std::invalid_argument("ShaderBindingRequirementEXT: name must not be empty");
        if (binding_ < 0)
            throw std::invalid_argument(
                "ShaderBindingRequirementEXT: binding must not be negative");
        if (!IsValidBindingType(type_))
            throw std::invalid_argument(
                "ShaderBindingRequirementEXT: binding type is not recognized");
        if (!IsValidStage(stage_))
            throw std::invalid_argument(
                "ShaderBindingRequirementEXT: shader stage is not recognized");
    }

    const std::string& ShaderBindingRequirementEXT::getName() const noexcept { return name_; }

    int ShaderBindingRequirementEXT::getBinding() const noexcept { return binding_; }

    ShaderBindingTypeEXT ShaderBindingRequirementEXT::getType() const noexcept { return type_; }

    CNA::ShaderStageEXT ShaderBindingRequirementEXT::getStage() const noexcept { return stage_; }

    ShaderPackageEXT::ShaderPackageEXT(
        std::vector<ShaderCodeEXT> variants,
        std::vector<CNA::ShaderStageEXT> requiredStages,
        std::vector<ShaderBindingRequirementEXT> bindingRequirements)
        : variants_(std::move(variants))
        , requiredStages_(std::move(requiredStages))
        , bindingRequirements_(std::move(bindingRequirements))
    {
        if (variants_.empty())
            throw std::invalid_argument("ShaderPackageEXT: at least one code variant is required");
        if (requiredStages_.empty())
            throw std::invalid_argument("ShaderPackageEXT: at least one shader stage is required");

        for (std::size_t index = 0; index < requiredStages_.size(); ++index)
        {
            const auto stage = requiredStages_[index];
            if (!IsValidStage(stage))
                throw std::invalid_argument("ShaderPackageEXT: required stage is not recognized");
            if (std::find(requiredStages_.begin(), requiredStages_.begin() + index, stage)
                != requiredStages_.begin() + index)
            {
                throw std::invalid_argument("ShaderPackageEXT: required stages must be unique");
            }
            if (std::none_of(
                    variants_.begin(), variants_.end(),
                    [stage](const ShaderCodeEXT& code) { return code.getStage() == stage; }))
            {
                throw std::invalid_argument(
                    "ShaderPackageEXT: every required stage needs at least one code variant");
            }
        }

        for (const auto& variant : variants_)
        {
            if (!requiresStage(variant.getStage()))
                throw std::invalid_argument(
                    "ShaderPackageEXT: a code variant uses a stage the package does not require");
        }

        for (std::size_t index = 0; index < bindingRequirements_.size(); ++index)
        {
            const auto& binding = bindingRequirements_[index];
            if (!requiresStage(binding.getStage()))
                throw std::invalid_argument(
                    "ShaderPackageEXT: a binding uses a stage the package does not require");
            for (std::size_t earlierIndex = 0; earlierIndex < index; ++earlierIndex)
            {
                const auto& earlier = bindingRequirements_[earlierIndex];
                if (earlier.getBinding() != binding.getBinding()) continue;
                if (earlier.getStage() == binding.getStage())
                    throw std::invalid_argument(
                        "ShaderPackageEXT: a stage cannot declare one binding twice");
                if (earlier.getName() != binding.getName() || earlier.getType() != binding.getType())
                    throw std::invalid_argument(
                        "ShaderPackageEXT: a shared binding must keep one name and resource type");
            }
        }
    }

    const std::vector<ShaderCodeEXT>& ShaderPackageEXT::getVariants() const noexcept
    {
        return variants_;
    }

    const std::vector<CNA::ShaderStageEXT>& ShaderPackageEXT::getRequiredStages() const noexcept
    {
        return requiredStages_;
    }

    const std::vector<ShaderBindingRequirementEXT>&
        ShaderPackageEXT::getBindingRequirements() const noexcept
    {
        return bindingRequirements_;
    }

    bool ShaderPackageEXT::requiresStage(const CNA::ShaderStageEXT stage) const noexcept
    {
        return std::find(requiredStages_.begin(), requiredStages_.end(), stage)
            != requiredStages_.end();
    }
}

#endif // CNA_CNAEXT
