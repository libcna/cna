// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderPackageEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>
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
                case ShaderBindingTypeEXT::ConstantBuffer:
                    return true;
                case ShaderBindingTypeEXT::Count:
                    return false;
            }
            return false;
        }

        constexpr std::array ShaderLanguagePreference = {
            CNA::ShaderLanguageEXT::SpirV,
            CNA::ShaderLanguageEXT::Dxil,
            CNA::ShaderLanguageEXT::GlslDesktop,
            CNA::ShaderLanguageEXT::GlslEs,
            CNA::ShaderLanguageEXT::GlslVulkan,
            CNA::ShaderLanguageEXT::Hlsl,
            CNA::ShaderLanguageEXT::Msl,
            CNA::ShaderLanguageEXT::Wgsl
        };

        [[nodiscard]] std::string_view LanguageName(
            const CNA::ShaderLanguageEXT language) noexcept
        {
            switch (language)
            {
                case CNA::ShaderLanguageEXT::Unknown: return "Unknown";
                case CNA::ShaderLanguageEXT::GlslDesktop: return "GlslDesktop";
                case CNA::ShaderLanguageEXT::GlslEs: return "GlslEs";
                case CNA::ShaderLanguageEXT::GlslVulkan: return "GlslVulkan";
                case CNA::ShaderLanguageEXT::Hlsl: return "Hlsl";
                case CNA::ShaderLanguageEXT::Msl: return "Msl";
                case CNA::ShaderLanguageEXT::Wgsl: return "Wgsl";
                case CNA::ShaderLanguageEXT::SpirV: return "SpirV";
                case CNA::ShaderLanguageEXT::Dxil: return "Dxil";
                case CNA::ShaderLanguageEXT::Count: return "Count";
            }
            return "InvalidLanguage";
        }

        [[nodiscard]] std::string_view StageName(const CNA::ShaderStageEXT stage) noexcept
        {
            switch (stage)
            {
                case CNA::ShaderStageEXT::Unknown: return "Unknown";
                case CNA::ShaderStageEXT::Vertex: return "Vertex";
                case CNA::ShaderStageEXT::Fragment: return "Fragment";
                case CNA::ShaderStageEXT::Compute: return "Compute";
                case CNA::ShaderStageEXT::Count: return "Count";
            }
            return "InvalidStage";
        }

        [[nodiscard]] bool HasLanguage(
            const std::vector<ShaderCodeEXT>& variants,
            const CNA::ShaderLanguageEXT language) noexcept
        {
            return std::any_of(
                variants.begin(), variants.end(),
                [language](const ShaderCodeEXT& code) {
                    return code.getLanguage() == language;
                });
        }

        void AddFailure(std::vector<std::string>& failures, std::string failure)
        {
            if (std::find(failures.begin(), failures.end(), failure) == failures.end())
                failures.push_back(std::move(failure));
        }

        void AddBindingRequirementFailures(
            const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            const std::vector<ShaderBindingRequirementEXT>& bindings,
            std::vector<std::string>& failures)
        {
            for (const auto& binding : bindings)
            {
                const std::string prefix = "binding '" + binding.getName() + "' ("
                    + std::string(StageName(binding.getStage())) + " "
                    + std::to_string(binding.getBinding()) + ") ";
                switch (binding.getType())
                {
                    case ShaderBindingTypeEXT::SampledTexture2D:
                    case ShaderBindingTypeEXT::SampledTextureCube:
                        break;
                    case ShaderBindingTypeEXT::SampledTexture3D:
                        if (!device.SupportsRendererFeatureEXT(
                                CNA::RendererFeature::Texture3DSampling))
                            AddFailure(failures, prefix + "requires Texture3DSampling");
                        break;
                    case ShaderBindingTypeEXT::SampledTexture2DArray:
                    {
                        const auto limit = device.GetRendererLimitEXT(
                            CNA::RendererLimit::MaxTextureArrayLayers);
                        if (!limit.known || limit.value == 0)
                            AddFailure(failures, prefix + "requires sampled texture arrays");
                        break;
                    }
                    case ShaderBindingTypeEXT::StorageBuffer:
                        if (binding.getStage() == CNA::ShaderStageEXT::Compute
                            && !device.SupportsCapability(
                                CNA::GraphicsCapability::ComputeShaders))
                            AddFailure(failures, prefix + "requires ComputeShaders");
                        if (binding.getStage() == CNA::ShaderStageEXT::Vertex)
                        {
                            const auto limit = device.GetRendererLimitEXT(
                                CNA::RendererLimit::MaxVertexShaderStorageBlocks);
                            if (!limit.known || limit.value == 0
                                || static_cast<std::uint64_t>(binding.getBinding()) >= limit.value)
                                AddFailure(
                                    failures, prefix + "exceeds vertex storage-buffer bindings");
                        }
                        break;
                    case ShaderBindingTypeEXT::StorageTexture2D:
                        if (binding.getStage() == CNA::ShaderStageEXT::Compute
                            && !device.SupportsRendererFeatureEXT(
                                CNA::RendererFeature::ComputeImageBinding))
                            AddFailure(failures, prefix + "requires ComputeImageBinding");
                        break;
                    case ShaderBindingTypeEXT::ConstantBuffer:
                    {
                        const auto limit = device.GetRendererLimitEXT(
                            CNA::RendererLimit::MaxUniformBufferBytes);
                        if (!limit.known || limit.value == 0)
                            AddFailure(failures, prefix + "requires constant buffers");
                        if (binding.getStage() != CNA::ShaderStageEXT::Compute)
                            AddFailure(
                                failures, prefix +
                                "has no portable non-compute binding route yet");
                        else if (!device.SupportsCapability(
                                     CNA::GraphicsCapability::ComputeShaders))
                            AddFailure(failures, prefix + "requires ComputeShaders");
                        break;
                    }
                    case ShaderBindingTypeEXT::Count:
                        break;
                }
            }
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

    ShaderPackageSelectionEXT::ShaderPackageSelectionEXT(
        const CNA::ShaderLanguageEXT language, std::vector<ShaderCodeEXT> code,
        std::string diagnostic)
        : language_(language)
        , code_(std::move(code))
        , diagnostic_(std::move(diagnostic))
    {
    }

    bool ShaderPackageSelectionEXT::isUsable() const noexcept
    {
        return language_ != CNA::ShaderLanguageEXT::Unknown && !code_.empty();
    }

    CNA::ShaderLanguageEXT ShaderPackageSelectionEXT::getLanguage() const noexcept
    {
        return language_;
    }

    const std::vector<ShaderCodeEXT>& ShaderPackageSelectionEXT::getCode() const noexcept
    {
        return code_;
    }

    const ShaderCodeEXT* ShaderPackageSelectionEXT::findStage(
        const CNA::ShaderStageEXT stage) const noexcept
    {
        const auto found = std::find_if(
            code_.begin(), code_.end(),
            [stage](const ShaderCodeEXT& code) { return code.getStage() == stage; });
        return found == code_.end() ? nullptr : &*found;
    }

    const std::string& ShaderPackageSelectionEXT::getDiagnostic() const noexcept
    {
        return diagnostic_;
    }

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

    ShaderPackageSelectionEXT ShaderPackageEXT::selectFor(
        const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const
    {
        std::vector<std::string> packageFailures;
        const bool needsGraphics = requiresStage(CNA::ShaderStageEXT::Vertex)
            || requiresStage(CNA::ShaderStageEXT::Fragment);
        if (needsGraphics
            && !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
            AddFailure(packageFailures, "graphics stages require CustomEffects");
        if (requiresStage(CNA::ShaderStageEXT::Compute)
            && !device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            AddFailure(packageFailures, "the compute stage requires ComputeShaders");
        AddBindingRequirementFailures(device, bindingRequirements_, packageFailures);

        std::vector<std::string> considered;
        for (const auto language : ShaderLanguagePreference)
        {
            if (!HasLanguage(variants_, language)) continue;

            std::ostringstream description;
            description << LanguageName(language) << " {";
            bool firstVariant = true;
            for (const auto& variant : variants_)
            {
                if (variant.getLanguage() != language) continue;
                if (!firstVariant) description << ", ";
                firstVariant = false;
                description << StageName(variant.getStage()) << "='"
                            << (variant.getSourceLabel().empty()
                                    ? std::string("<unnamed>")
                                    : variant.getSourceLabel())
                            << "'";
            }
            description << "}";

            std::vector<std::string> failures = packageFailures;
            std::vector<ShaderCodeEXT> selected;
            selected.reserve(requiredStages_.size());
            for (const auto stage : requiredStages_)
            {
                std::vector<const ShaderCodeEXT*> matches;
                for (const auto& variant : variants_)
                {
                    if (variant.getLanguage() == language && variant.getStage() == stage)
                        matches.push_back(&variant);
                }
                if (matches.empty())
                {
                    AddFailure(
                        failures, "missing required " + std::string(StageName(stage)) + " code");
                    continue;
                }
                if (matches.size() > 1)
                {
                    AddFailure(
                        failures, "ambiguous duplicate " + std::string(StageName(stage))
                            + " code");
                    continue;
                }
                if (!device.SupportsShaderLanguageEXT(language, stage))
                {
                    AddFailure(
                        failures, "renderer rejects " + std::string(LanguageName(language)) + "/"
                            + std::string(StageName(stage)));
                    continue;
                }
                selected.push_back(*matches.front());
            }

            if (failures.empty())
            {
                description << ": selected";
                return ShaderPackageSelectionEXT(
                    language, std::move(selected), description.str());
            }

            description << ": rejected (";
            for (std::size_t index = 0; index < failures.size(); ++index)
            {
                if (index != 0) description << "; ";
                description << failures[index];
            }
            description << ")";
            considered.push_back(description.str());
        }

        std::ostringstream diagnostic;
        diagnostic << "ShaderPackageEXT: no usable shader variant. Considered: ";
        for (std::size_t index = 0; index < considered.size(); ++index)
        {
            if (index != 0) diagnostic << "; ";
            diagnostic << considered[index];
        }
        return ShaderPackageSelectionEXT(
            CNA::ShaderLanguageEXT::Unknown, {}, diagnostic.str());
    }
}

#endif // CNA_CNAEXT

#ifdef CNA_CNAEXT

namespace Microsoft::Xna::Framework::Graphics
{
    namespace
    {
        [[nodiscard]] std::string PortablePayloadBytes(
            const CNA::Graphics::ShaderCodeEXT& code)
        {
            if (code.isText()) return code.getText();
            const auto& bytes = code.getBytes();
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    }

    ShaderEffect::ShaderEffect(
        GraphicsDevice& device, const CNA::Graphics::ShaderCodeEXT& vertexCode,
        const CNA::Graphics::ShaderCodeEXT& fragmentCode)
        : ShaderEffect(device, PreparePortablePayload(device, vertexCode, fragmentCode))
    {
    }

    ShaderEffect::ShaderEffect(
        GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package)
        : ShaderEffect(device, PreparePortablePayload(device, package))
    {
    }

    ShaderEffect::ShaderEffect(
        GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package,
        const std::string& fallbackVertexSource,
        const std::string& fallbackFragmentSource)
        : ShaderEffect(
            device, PreparePortablePayloadOrFallback(
                device, package, fallbackVertexSource, fallbackFragmentSource))
    {
    }

    ShaderEffect::ShaderEffect(GraphicsDevice& device, PreparedPortablePayload payload)
        : ShaderEffect(device, payload.vertexSource, payload.fragmentSource)
    {
        selectedVertexLabelEXT_ = std::move(payload.vertexLabel);
        selectedFragmentLabelEXT_ = std::move(payload.fragmentLabel);
        selectedShaderLanguageEXT_ = payload.language;
    }

    ShaderEffect::PreparedPortablePayload ShaderEffect::PreparePortablePayload(
        GraphicsDevice& device, const CNA::Graphics::ShaderCodeEXT& vertexCode,
        const CNA::Graphics::ShaderCodeEXT& fragmentCode)
    {
        if (vertexCode.getStage() != CNA::ShaderStageEXT::Vertex
            || fragmentCode.getStage() != CNA::ShaderStageEXT::Fragment)
            throw std::invalid_argument(
                "ShaderEffect: explicit code must be ordered Vertex, Fragment");
        if (vertexCode.getLanguage() != fragmentCode.getLanguage())
            throw std::invalid_argument(
                "ShaderEffect: vertex and fragment code must use the same language");
        return PreparePortablePayload(
            device,
            CNA::Graphics::ShaderPackageEXT(
                {vertexCode, fragmentCode},
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment}));
    }

    ShaderEffect::PreparedPortablePayload ShaderEffect::PreparePortablePayload(
        GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package)
    {
        if (package.getRequiredStages().size() != 2
            || !package.requiresStage(CNA::ShaderStageEXT::Vertex)
            || !package.requiresStage(CNA::ShaderStageEXT::Fragment))
        {
            throw std::invalid_argument(
                "ShaderEffect: package must require exactly Vertex and Fragment");
        }
        const CNA::Graphics::ShaderPackageSelectionEXT selection = package.selectFor(device);
        if (!selection.isUsable())
        {
            std::vector<CNA::ShaderDiagnosticEXT> diagnostics;
            diagnostics.reserve(package.getVariants().size());
            for (const auto& variant : package.getVariants())
                diagnostics.emplace_back(
                    CNA::ShaderDiagnosticSeverityEXT::Error, variant.getStage(),
                    variant.getSourceLabel(), 0, 0, selection.getDiagnostic());
            throw CNA::ShaderCompilationExceptionEXT(std::move(diagnostics));
        }
        const auto* vertex = selection.findStage(CNA::ShaderStageEXT::Vertex);
        const auto* fragment = selection.findStage(CNA::ShaderStageEXT::Fragment);
        if (vertex == nullptr || fragment == nullptr)
            throw std::logic_error(
                "ShaderEffect: usable selection has no complete graphics payload pair");
        if (vertex->getEntryPoint() != "main" || fragment->getEntryPoint() != "main")
            throw std::invalid_argument(
                "ShaderEffect: the existing renderer path requires entry point 'main'");
        return PreparedPortablePayload{
            PortablePayloadBytes(*vertex), PortablePayloadBytes(*fragment),
            vertex->getSourceLabel(), fragment->getSourceLabel(),
            selection.getLanguage()};
    }

    ShaderEffect::PreparedPortablePayload ShaderEffect::PreparePortablePayloadOrFallback(
        GraphicsDevice& device, const CNA::Graphics::ShaderPackageEXT& package,
        const std::string& fallbackVertexSource,
        const std::string& fallbackFragmentSource)
    {
        if (package.getRequiredStages().size() != 2
            || !package.requiresStage(CNA::ShaderStageEXT::Vertex)
            || !package.requiresStage(CNA::ShaderStageEXT::Fragment))
        {
            throw std::invalid_argument(
                "ShaderEffect: package must require exactly Vertex and Fragment");
        }
        if (!package.selectFor(device).isUsable())
        {
            return PreparedPortablePayload{
                fallbackVertexSource, fallbackFragmentSource, {}, {},
                CNA::ShaderLanguageEXT::Unknown};
        }
        return PreparePortablePayload(device, package);
    }

}

#endif // CNA_CNAEXT
