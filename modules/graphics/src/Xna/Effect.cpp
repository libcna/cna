// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>

namespace
{
    constexpr std::size_t kMaximumCompiledEffectBytes = 64u * 1024u * 1024u;
    constexpr std::uint32_t kEffectFrameworkToken = 0xFEFF0901u;
    constexpr std::uint32_t kXna4EffectWrapperToken = 0xBCF00BCFu;
    constexpr std::uint32_t kMaximumReflectedParameters = 16u * 1024u;
    constexpr std::uint32_t kMaximumReflectedTechniques = 4u * 1024u;
    constexpr std::uint32_t kMaximumEffectObjects = 64u * 1024u;

    std::uint32_t ReadUInt32LittleEndian(
        const std::vector<SharpRuntime::bytecs>& bytes, std::size_t offset)
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    /**
     * Performs only the bounded container checks needed before calling MojoShader. In
     * particular, this mirrors its XNA 4 wrapper seek but rejects the unsigned underflow that a
     * wrapper offset below eight would otherwise trigger in older MojoShader revisions.
     */
    bool HasStructurallyValidEffectFrameworkHeader(
        const std::vector<SharpRuntime::bytecs>& bytes)
    {
        if (bytes.size() < 24) return false;

        std::size_t tokenOffset = 0;
        const std::uint32_t outerToken = ReadUInt32LittleEndian(bytes, 0);
        if (outerToken == kXna4EffectWrapperToken)
        {
            const std::uint32_t wrappedOffset = ReadUInt32LittleEndian(bytes, 4);
            if (wrappedOffset < 8 || wrappedOffset > bytes.size() - 8) return false;
            tokenOffset = wrappedOffset;
        }
        else if (outerToken != kEffectFrameworkToken)
        {
            return false;
        }

        if (ReadUInt32LittleEndian(bytes, tokenOffset) != kEffectFrameworkToken) return false;
        const std::size_t effectBase = tokenOffset + 8;
        const std::uint32_t structureOffset =
            ReadUInt32LittleEndian(bytes, tokenOffset + 4);
        if (structureOffset > bytes.size() - effectBase) return false;
        const std::size_t structure = effectBase + structureOffset;
        if (bytes.size() - structure < 16) return false;

        // The MojoShader parser allocates these top-level tables before it can diagnose deeper
        // malformed data. Conservative, format-independent ceilings prevent multiplication
        // overflow and unreasonable allocations without constraining real-world XNA effects.
        const std::uint32_t parameterCount = ReadUInt32LittleEndian(bytes, structure);
        const std::uint32_t techniqueCount = ReadUInt32LittleEndian(bytes, structure + 4);
        const std::uint32_t objectCount = ReadUInt32LittleEndian(bytes, structure + 12);
        return parameterCount <= kMaximumReflectedParameters &&
               techniqueCount > 0 && techniqueCount <= kMaximumReflectedTechniques &&
               objectCount <= kMaximumEffectObjects;
    }

    Microsoft::Xna::Framework::Graphics::EffectAnnotation MakeAnnotation(
        const CNA::Internal::Renderers::CompiledEffectAnnotationDescription& description)
    {
        std::vector<float> data((description.rawValue.size() + sizeof(float) - 1) /
                                sizeof(float));
        if (!description.rawValue.empty())
        {
            std::memcpy(data.data(), description.rawValue.data(), description.rawValue.size());
        }
        return Microsoft::Xna::Framework::Graphics::EffectAnnotation(
            description.name, description.semantic,
            description.rowCount, description.columnCount,
            description.parameterClass, description.parameterType,
            std::move(data), description.stringValue);
    }
}

namespace Microsoft::Xna::Framework::Graphics
{
    Effect::Effect(GraphicsDevice& device)
        : GraphicsResource(&device)
        , device_(&device)
    {
        techniques_.Add(EffectTechnique(this, "Default"));
        currentTechnique_ = &techniques_[0];
    }

    Effect::Effect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& effectCode)
        : GraphicsResource(&device)
        , device_(&device)
    {
        if (effectCode.empty())
        {
            throw System::ArgumentException(
                "Compiled effect bytecode must not be empty.", "effectCode");
        }
        if (effectCode.size() > kMaximumCompiledEffectBytes)
        {
            throw System::ArgumentException(
                "Compiled effect bytecode exceeds CNA's 64 MiB safety limit.", "effectCode");
        }
        if (effectCode.size() >= 4 && effectCode[0] == 'M' && effectCode[1] == 'G' &&
            effectCode[2] == 'F' && effectCode[3] == 'X')
        {
            throw System::NotSupportedException(
                "The supplied bytes are a MonoGame MGFX effect. Effect(GraphicsDevice, byte[]) "
                "currently accepts XNA/FNA Direct3D 9 Effect Framework bytecode (.fxb/XNB), "
                "not the distinct MGFX container format.");
        }
        if (!HasStructurallyValidEffectFrameworkHeader(effectCode))
        {
            throw System::ArgumentException(
                "Compiled effect bytecode does not contain a structurally valid XNA Direct3D 9 "
                "Effect Framework header.", "effectCode");
        }
        if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        {
            throw System::NotSupportedException(
                "The active graphics renderer does not support compiled XNA/FNA Effect "
                "Framework bytecode (GraphicsCapability::CompiledEffects is false).");
        }

        compiledRuntime_ = device.GetRenderer().CreateCompiledEffect(
            reinterpret_cast<const std::uint8_t*>(effectCode.data()), effectCode.size());
        if (!compiledRuntime_)
        {
            throw System::NotSupportedException(
                "The active graphics renderer advertised compiled effects but failed to create "
                "a compiled-effect runtime.");
        }
        BuildCompiledObjectGraph();
    }

    Effect::Effect(GraphicsDevice& device,
                   std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime,
                   const Effect* cloneSource)
        : GraphicsResource(&device)
        , device_(&device)
        , compiledRuntime_(std::move(runtime))
    {
        if (!compiledRuntime_)
        {
            throw System::InvalidOperationException(
                "Cannot construct a compiled Effect clone without a renderer runtime.");
        }
        BuildCompiledObjectGraph();

        if (cloneSource != nullptr)
        {
            const int count = std::min(parameters_.getCountProperty(),
                                       cloneSource->parameters_.getCountProperty());
            for (int i = 0; i < count; ++i)
            {
                parameters_[i].CopyMutableValueFromInternal(cloneSource->parameters_[i]);
            }

            for (int i = 0; i < cloneSource->techniques_.getCountProperty(); ++i)
            {
                if (&cloneSource->techniques_[i] == cloneSource->currentTechnique_ &&
                    i < techniques_.getCountProperty())
                {
                    setCurrentTechniqueProperty(&techniques_[i]);
                    break;
                }
            }
        }
    }

    Effect::~Effect()
    {
        Dispose(false);
    }

    void Effect::Dispose(bool disposing)
    {
        if (!isDisposed_)
        {
            compiledRuntime_.reset();
        }
        GraphicsResource::Dispose(disposing);
    }

    GraphicsDevice& Effect::getGraphicsDeviceInternal() const { return *device_; }

    EffectTechnique* Effect::getCurrentTechniqueProperty() const { return currentTechnique_; }

    void Effect::setCurrentTechniqueProperty(EffectTechnique* value)
    {
        currentTechnique_ = value;
        if (compiledRuntime_ && value != nullptr)
        {
            for (int i = 0; i < techniques_.getCountProperty(); ++i)
            {
                if (&techniques_[i] == value)
                {
                    compiledRuntime_->SetTechnique(value->getIndexInternal());
                    break;
                }
            }
        }
    }

    EffectParameterCollection& Effect::getParametersProperty() { return parameters_; }
    const EffectParameterCollection& Effect::getParametersProperty() const { return parameters_; }

    EffectTechniqueCollection& Effect::getTechniquesProperty() { return techniques_; }
    const EffectTechniqueCollection& Effect::getTechniquesProperty() const { return techniques_; }

    void Effect::Apply()
    {
        ApplyPassInternal(0);
    }

    void Effect::ApplyPassInternal(std::uint32_t passIndex)
    {
        if (isDisposed_)
            throw System::ObjectDisposedException(getNameProperty());
        OnApply();

        if (compiledRuntime_)
        {
            if (currentTechnique_ == nullptr)
            {
                throw System::InvalidOperationException(
                    "A compiled effect cannot be applied with a null CurrentTechnique.");
            }
            const auto& passes = currentTechnique_->getPassesProperty();
            if (passIndex >= static_cast<std::uint32_t>(passes.getCountProperty()))
            {
                throw System::InvalidOperationException(
                    "The compiled effect pass index is outside the current technique.");
            }
            SyncCompiledParameters();
            compiledRuntime_->SetTechnique(currentTechnique_->getIndexInternal());
            compiledRuntime_->ApplyPass(passIndex);
        }
        if (device_) device_->SetCurrentEffect(this);
    }

    void Effect::FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const
    {
        params.compiledEffectRuntime = compiledRuntime_.get();
    }

    const std::string& Effect::GetVertexSource() const
    {
        static const std::string empty;
        return empty;
    }

    const std::string& Effect::GetFragmentSource() const
    {
        static const std::string empty;
        return empty;
    }

    CNA::Internal::Renderers::IEffectRenderer* Effect::GetEffectRendererPtr() const
    {
        return nullptr;
    }

    Effect* Effect::Clone()
    {
        if (!compiledRuntime_)
        {
            return new Effect(*device_);
        }
        return new Effect(*device_, compiledRuntime_->Clone(), this);
    }

    void Effect::OnApply()
    {
    }

    EffectParameter Effect::BuildCompiledParameter(
        const CNA::Internal::Renderers::CompiledEffectParameterDescription& description)
    {
        auto storage = std::make_shared<EffectParameter::CompiledStorage>();
        storage->bytes = description.rawValue;
        storage->stringValue = description.stringValue;
        storage->runtimeIndex = description.runtimeIndex;

        std::function<EffectParameter(
            const CNA::Internal::Renderers::CompiledEffectParameterDescription&,
            std::size_t, std::size_t, bool)> build;

        build = [&](const CNA::Internal::Renderers::CompiledEffectParameterDescription& desc,
                    std::size_t offset, std::size_t size, bool includeArrayElements)
        {
            const std::size_t available = offset <= storage->bytes.size()
                ? storage->bytes.size() - offset : 0;
            EffectParameter parameter(
                desc.name, desc.semantic, desc.rowCount, desc.columnCount,
                includeArrayElements ? desc.elementCount : 0,
                desc.parameterClass, desc.parameterType, storage,
                std::min(offset, storage->bytes.size()), std::min(size, available));

            for (const auto& annotation : desc.annotations)
            {
                parameter.annotations_.Add(MakeAnnotation(annotation));
            }

            std::size_t memberOffset = offset;
            for (const auto& member : desc.structureMembers)
            {
                const std::size_t memberElements = static_cast<std::size_t>(
                    std::max(member.elementCount, 1));
                const std::size_t memberCells = static_cast<std::size_t>(
                    std::max(member.rowCount, 1)) * static_cast<std::size_t>(
                    std::max(member.columnCount, 1)) * memberElements;
                const std::size_t memberBytes = memberCells <=
                        std::numeric_limits<std::size_t>::max() / 4
                    ? memberCells * 4 : 0;
                parameter.members_->Add(build(member, memberOffset, memberBytes, true));
                memberOffset += memberBytes;
            }

            if (includeArrayElements && desc.elementCount > 0)
            {
                const std::size_t elementStride = static_cast<std::size_t>(
                    std::max(desc.rowCount, 1)) * 16;
                for (int i = 0; i < desc.elementCount; ++i)
                {
                    auto elementDescription = desc;
                    elementDescription.name.clear();
                    elementDescription.semantic.clear();
                    elementDescription.elementCount = 0;
                    elementDescription.annotations.clear();
                    const std::size_t elementOffset = offset +
                        static_cast<std::size_t>(i) * elementStride;
                    parameter.elements_->Add(build(elementDescription, elementOffset,
                                                   elementStride, false));
                }
            }
            return parameter;
        };

        return build(description, 0, storage->bytes.size(), true);
    }

    void Effect::BuildCompiledObjectGraph()
    {
        const auto& description = compiledRuntime_->GetDescription();
        if (description.techniques.empty())
        {
            throw System::ArgumentException(
                "The compiled effect declares no techniques.", "effectCode");
        }

        for (const auto& parameter : description.parameters)
        {
            parameters_.Add(BuildCompiledParameter(parameter));
        }

        for (std::size_t techniqueIndex = 0;
             techniqueIndex < description.techniques.size(); ++techniqueIndex)
        {
            const auto& reflectedTechnique = description.techniques[techniqueIndex];
            EffectTechnique technique(this, reflectedTechnique.name,
                                      static_cast<std::uint32_t>(techniqueIndex), false);
            for (const auto& annotation : reflectedTechnique.annotations)
            {
                technique.getAnnotationsProperty().Add(MakeAnnotation(annotation));
            }
            for (std::size_t passIndex = 0;
                 passIndex < reflectedTechnique.passes.size(); ++passIndex)
            {
                const auto& reflectedPass = reflectedTechnique.passes[passIndex];
                EffectPass pass(this, reflectedPass.name, technique.getIdInternal(),
                                static_cast<std::uint32_t>(passIndex));
                for (const auto& annotation : reflectedPass.annotations)
                {
                    pass.getAnnotationsProperty().Add(MakeAnnotation(annotation));
                }
                technique.getPassesProperty().Add(std::move(pass));
            }
            techniques_.Add(std::move(technique));
        }

        currentTechnique_ = &techniques_[0];
        compiledRuntime_->SetTechnique(0);
    }

    void Effect::SyncCompiledParameters()
    {
        for (auto& parameter : parameters_)
        {
            if (!parameter.IsDirtyInternal()) continue;

            switch (parameter.getParameterTypeProperty())
            {
                case EffectParameterType::Texture:
                case EffectParameterType::Texture1D:
                case EffectParameterType::Texture2D:
                case EffectParameterType::Texture3D:
                case EffectParameterType::TextureCube:
                    compiledRuntime_->SetParameterTexture(
                        parameter.GetRuntimeIndexInternal(), parameter.GetTextureInternal());
                    break;
                default:
                    compiledRuntime_->SetParameterValue(
                        parameter.GetRuntimeIndexInternal(), parameter.GetRawValueInternal(),
                        parameter.GetRawValueSizeInternal());
                    break;
            }
            parameter.MarkCleanInternal();
        }
    }

    const std::string& Effect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.Effect";
        return name;
    }
}
