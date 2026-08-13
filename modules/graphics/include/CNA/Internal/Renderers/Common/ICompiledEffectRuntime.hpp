// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Texture;
}

namespace CNA::Internal::Renderers
{
    using EffectParameterClass =
        Microsoft::Xna::Framework::Graphics::EffectParameterClass;
    using EffectParameterType =
        Microsoft::Xna::Framework::Graphics::EffectParameterType;
    using Texture = Microsoft::Xna::Framework::Graphics::Texture;

    /**
     * @brief Renderer-neutral value reflected from a compiled XNA Effect Framework binary.
     *
     * `rawValue` is the native Effect Framework value storage, copied while the runtime owns a
     * valid parse tree. Numeric entries use four-byte cells and retain the register padding in
     * the binary; strings are additionally resolved into `stringValue`. Sampler and shader-object
     * parameters are not exposed in the public EffectParameterCollection.
     */
    struct CompiledEffectValueDescription
    {
        std::string name;
        std::string semantic;
        int rowCount = 0;
        int columnCount = 0;
        int elementCount = 0;
        EffectParameterClass parameterClass = EffectParameterClass::Scalar;
        EffectParameterType parameterType = EffectParameterType::Void;
        std::vector<std::uint8_t> rawValue;
        std::string stringValue;
    };

    /** @brief One reflected annotation and its immutable default value. */
    using CompiledEffectAnnotationDescription = CompiledEffectValueDescription;

    /** @brief One reflected public parameter. */
    struct CompiledEffectParameterDescription : CompiledEffectValueDescription
    {
        std::uint32_t runtimeIndex = 0;
        std::vector<CompiledEffectAnnotationDescription> annotations;
        std::vector<CompiledEffectParameterDescription> structureMembers;
    };

    /** @brief One pass in a compiled technique. */
    struct CompiledEffectPassDescription
    {
        std::string name;
        std::vector<CompiledEffectAnnotationDescription> annotations;
    };

    /** @brief One technique and its ordered pass list. */
    struct CompiledEffectTechniqueDescription
    {
        std::string name;
        std::vector<CompiledEffectAnnotationDescription> annotations;
        std::vector<CompiledEffectPassDescription> passes;
    };

    /** @brief Immutable public reflection for a compiled effect instance. */
    struct CompiledEffectDescription
    {
        std::vector<CompiledEffectParameterDescription> parameters;
        std::vector<CompiledEffectTechniqueDescription> techniques;
    };

    /**
     * @brief Device-bound implementation of one compiled XNA effect.
     *
     * The public graphics layer owns the XNA object graph and mutable values. A runtime owns only
     * renderer-native shader/effect resources and receives synchronized values immediately before
     * a pass is applied. No backend-specific type is allowed in this interface.
     */
    class ICompiledEffectRuntime
    {
    public:
        virtual ~ICompiledEffectRuntime() = default;

        /** @brief Creates a device-local independent copy, including current native values. */
        [[nodiscard]] virtual std::unique_ptr<ICompiledEffectRuntime> Clone() const = 0;

        /** @brief Returns the immutable reflection used to build the public Effect graph. */
        [[nodiscard]] virtual const CompiledEffectDescription& GetDescription() const = 0;

        /** @brief Selects a reflected technique by its stable zero-based index. */
        virtual void SetTechnique(std::uint32_t techniqueIndex) = 0;

        /** @brief Replaces one top-level parameter's padded raw value storage. */
        virtual void SetParameterValue(std::uint32_t runtimeIndex,
                                       const void* data,
                                       std::size_t dataBytes) = 0;

        /** @brief Associates a public texture parameter with its renderer-native sampler binding. */
        virtual void SetParameterTexture(std::uint32_t runtimeIndex, Texture* texture) = 0;

        /** @brief Applies the exact pass of the currently selected technique. */
        virtual void ApplyPass(std::uint32_t passIndex) = 0;
    };
}
