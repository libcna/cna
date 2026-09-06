// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Represents a processor parameter: one public configurable property of a content
     *        processor, as the component scanner reports it.
     *
     * XNA fills these by reflecting a processor's public properties. C++ has no reflection, so a
     * processor declares its parameters through @ref ProcessorParameterBindings and this class is
     * the read-only view the scanner and the `.contentproj` reader consume.
     */
    class ProcessorParameter
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ProcessorParameter";

        /**
         * @brief Initializes a parameter description.
         *
         * @param propertyName Name of the property.
         * @param propertyType .NET full name of the property's type.
         * @param displayName Display name, or empty to use the property name.
         * @param description Description shown to users, or empty.
         * @param defaultValue Boxed default value; empty when the property has none.
         * @param possibleEnumValues Enumerator names when the type is an enum, otherwise empty.
         */
        CNAEXT ProcessorParameter(std::string propertyName, std::string propertyType,
                                  std::string displayName, std::string description,
                                  ContentObject defaultValue,
                                  std::vector<std::string> possibleEnumValues);

        /**
         * @brief Gets the default value of the parameter.
         *
         * @return The boxed default, or an empty object when none.
         */
        [[nodiscard]] const ContentObject& getDefaultValueProperty() const noexcept;

        /**
         * @brief Gets the description of the parameter.
         *
         * @return The description, or empty.
         */
        [[nodiscard]] const std::string& getDescriptionProperty() const noexcept;

        /**
         * @brief Gets the display name of the parameter.
         *
         * @return The display name; the property name when none was declared.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const noexcept;

        /**
         * @brief Gets whether the parameter is an enumeration.
         *
         * @return True when `PossibleEnumValues` lists the allowed spellings.
         */
        [[nodiscard]] bool getIsEnumProperty() const noexcept;

        /**
         * @brief Gets the possible enumerator names of an enum parameter.
         *
         * @return The names, in declaration order; empty for a non-enum parameter.
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<std::string>
        getPossibleEnumValuesProperty() const;

        /**
         * @brief Gets the name of the property the parameter sets.
         *
         * @return The property name.
         */
        [[nodiscard]] const std::string& getPropertyNameProperty() const noexcept;

        /**
         * @brief Gets the .NET full name of the property's type.
         *
         * @return For example `System.Boolean` or `Microsoft.Xna.Framework.Color`.
         */
        [[nodiscard]] const std::string& getPropertyTypeProperty() const noexcept;

        /**
         * @brief Compares the description: name, type, display name, description and enum
         *        spellings. The boxed default is not part of the comparison.
         *
         * @param other Parameter to compare.
         * @return True when both describe the same property the same way.
         */
        CNAEXT [[nodiscard]] bool operator==(const ProcessorParameter& other) const noexcept;

    private:
        std::string propertyName_;
        std::string propertyType_;
        std::string displayName_;
        std::string description_;
        ContentObject defaultValue_;
        std::vector<std::string> possibleEnumValues_;
    };

    /**
     * @brief Represents a collection of supported processor parameters.
     *
     * Read-only, as in XNA; a `ProcessorParameterBindings` builds one.
     */
    class ProcessorParameterCollection final
        : public System::Collections::ObjectModel::ReadOnlyCollection<ProcessorParameter>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ProcessorParameterCollection";

        /** @brief Initializes an empty collection. */
        ProcessorParameterCollection() = default;

        /**
         * @brief Initializes a collection over the given parameters.
         *
         * @param parameters The parameters, in declaration order.
         */
        CNAEXT explicit ProcessorParameterCollection(std::vector<ProcessorParameter> parameters);

        /**
         * @brief Finds a parameter by property name.
         *
         * @param propertyName The property name, matched exactly.
         * @return The parameter, or null when none has that name.
         */
        CNAEXT [[nodiscard]] const ProcessorParameter* Find(const std::string& propertyName) const noexcept;
    };

    /**
     * @brief One parameter's typed accessors on a concrete processor: how a text or boxed value
     *        reaches the property, and how its current value is read back.
     *
     * Not an XNA type: the C++ replacement for the property reflection XNA performs
     * (docs/xna-content-pipeline-compat-api.md §5).
     *
     * @tparam TProcessor The processor class the bindings belong to.
     */
    template<typename TProcessor>
    struct CNAEXT ProcessorParameterBinding
    {
        /** @brief The scanner-visible description. */
        ProcessorParameter parameter;

        /** @brief Assigns a value spelled as text (the `.contentproj` form). */
        std::function<void(TProcessor&, const std::string&)> assignText;

        /** @brief Assigns a boxed value (the `OpaqueDataDictionary` form). */
        std::function<void(TProcessor&, const ContentObject&)> assignObject;

        /** @brief Reads the current value back, boxed. */
        std::function<ContentObject(const TProcessor&)> read;
    };

    /**
     * @brief Parses the text spelling of a parameter value of type @p T.
     *
     * Booleans accept `true`/`false` case-insensitively (the .NET `Boolean.Parse` spellings);
     * numbers use invariant culture; `Color` accepts `R, G, B, A` or `{R:.. G:.. B:.. A:..}`;
     * vectors accept comma-separated components; strings are taken verbatim.
     *
     * @tparam T The property type.
     * @param text The spelling.
     * @return The parsed value.
     * @throws System::FormatException when the text does not spell a @p T.
     */
    template<typename T>
    [[nodiscard]] CNAEXT T ParseProcessorParameterText(const std::string& text);

    /**
     * @brief Converts a boxed value to @p T: the exact type, or a string box parsed as text, or
     *        a canonical numeric box widened/narrowed exactly.
     *
     * @tparam T The property type.
     * @param value The boxed value.
     * @param propertyName Property name used in diagnostics.
     * @return The converted value.
     * @throws System::InvalidCastException when no conversion applies.
     */
    template<typename T>
    [[nodiscard]] CNAEXT T ConvertProcessorParameterObject(const ContentObject& value,
                                                           const std::string& propertyName);

    /// The property types a parameter may have; each has its parser and converter defined in
    /// ProcessorParameter.cpp. Any other type fails to link, which is the intended diagnostic.
#define CNA_XNA_PROCESSOR_PARAMETER_TYPE(T)                                                       \
    template<> [[nodiscard]] CNAEXT T ParseProcessorParameterText<T>(const std::string& text);    \
    template<> [[nodiscard]] CNAEXT T ConvertProcessorParameterObject<T>(const ContentObject& value, \
                                                                        const std::string& propertyName)
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(bool);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(std::int32_t);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(std::uint32_t);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(std::int64_t);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(float);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(double);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(std::string);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(char16_t);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(Microsoft::Xna::Framework::Color);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(Microsoft::Xna::Framework::Vector2);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(Microsoft::Xna::Framework::Vector3);
    CNA_XNA_PROCESSOR_PARAMETER_TYPE(Microsoft::Xna::Framework::Vector4);
#undef CNA_XNA_PROCESSOR_PARAMETER_TYPE

    /**
     * @brief Collects a processor's parameter bindings; a processor class declares them in a
     *        `static void DescribeParameters(ProcessorParameterBindings<Self>&)`.
     *
     * @tparam TProcessor The processor class.
     */
    template<typename TProcessor>
    class CNAEXT ProcessorParameterBindings
    {
    public:
        /**
         * @brief Declares one non-enum parameter.
         *
         * @tparam T The property type.
         * @param propertyName The property name (the `ProcessorParameters_<Name>` spelling).
         * @param getter Member getter, `T (TProcessor::*)() const`.
         * @param setter Member setter, `void (TProcessor::*)(T)` or `(const T&)`.
         * @param displayName Display name, or empty to reuse the property name.
         * @param description Description, or empty.
         */
        template<typename T, typename Getter, typename Setter>
        void Add(std::string propertyName, Getter getter, Setter setter,
                 std::string displayName = {}, std::string description = {})
        {
            const TProcessor prototype{};
            ContentObject defaultValue = Box<T>(static_cast<T>((prototype.*getter)()));
            ProcessorParameterBinding<TProcessor> binding{
                ProcessorParameter(propertyName, ContentTypeName<T>::Name(),
                                   displayName.empty() ? propertyName : std::move(displayName),
                                   std::move(description), std::move(defaultValue), {}),
                [setter](TProcessor& target, const std::string& text) { (target.*setter)(ParseProcessorParameterText<T>(text)); },
                [setter, propertyName](TProcessor& target, const ContentObject& value) { (target.*setter)(ConvertProcessorParameterObject<T>(value, propertyName)); },
                [getter](const TProcessor& target) { return Box<T>(static_cast<T>((target.*getter)())); },
            };
            bindings_.push_back(std::move(binding));
        }

        /**
         * @brief Declares one enum parameter with its spellings.
         *
         * @tparam TEnum The enum type; its `ContentTypeName` must be declared.
         * @param propertyName The property name.
         * @param getter Member getter.
         * @param setter Member setter.
         * @param names Enumerator spellings paired with values, in declaration order.
         * @param displayName Display name, or empty to reuse the property name.
         * @param description Description, or empty.
         */
        template<typename TEnum, typename Getter, typename Setter>
        void AddEnum(std::string propertyName, Getter getter, Setter setter,
                     std::vector<std::pair<std::string, TEnum>> names,
                     std::string displayName = {}, std::string description = {})
        {
            static_assert(std::is_enum_v<TEnum>, "AddEnum needs an enum property type.");
            const TProcessor prototype{};
            std::vector<std::string> spellings;
            for (const auto& entry : names) { spellings.push_back(entry.first); }
            const TEnum current = (prototype.*getter)();
            ProcessorParameterBinding<TProcessor> binding{
                ProcessorParameter(propertyName, ContentTypeName<TEnum>::Name(),
                                   displayName.empty() ? propertyName : std::move(displayName),
                                   std::move(description), Box<std::string>(EnumName(names, current, propertyName)), spellings),
                [setter, names, propertyName](TProcessor& target, const std::string& text) { (target.*setter)(EnumValue(names, text, propertyName)); },
                [setter, names, propertyName](TProcessor& target, const ContentObject& value) {
                    if (Holds<TEnum>(value)) { (target.*setter)(Unbox<TEnum>(value)); return; }
                    (target.*setter)(EnumValue(names, ConvertProcessorParameterObject<std::string>(value, propertyName), propertyName));
                },
                [getter, names, propertyName](const TProcessor& target) { return Box<std::string>(EnumName(names, (target.*getter)(), propertyName)); },
            };
            bindings_.push_back(std::move(binding));
        }

        /**
         * @brief Returns the declared bindings in declaration order.
         *
         * @return The bindings.
         */
        [[nodiscard]] const std::vector<ProcessorParameterBinding<TProcessor>>& Bindings() const noexcept
        {
            return bindings_;
        }

        /**
         * @brief Finds a binding by property name.
         *
         * @param propertyName The property name, matched exactly.
         * @return The binding, or null.
         */
        [[nodiscard]] const ProcessorParameterBinding<TProcessor>* Find(const std::string& propertyName) const noexcept
        {
            for (const auto& binding : bindings_)
            {
                if (binding.parameter.getPropertyNameProperty() == propertyName) { return &binding; }
            }
            return nullptr;
        }

        /**
         * @brief Builds the scanner-visible collection.
         *
         * @return The parameters, in declaration order.
         */
        [[nodiscard]] ProcessorParameterCollection ToCollection() const
        {
            std::vector<ProcessorParameter> parameters;
            for (const auto& binding : bindings_) { parameters.push_back(binding.parameter); }
            return ProcessorParameterCollection(std::move(parameters));
        }

    private:
        template<typename TEnum>
        static std::string EnumName(const std::vector<std::pair<std::string, TEnum>>& names, TEnum value,
                                    const std::string& propertyName)
        {
            for (const auto& entry : names) { if (entry.second == value) { return entry.first; } }
            throw System::InvalidCastException("Processor parameter '" + propertyName +
                                               "' holds an enum value with no declared spelling.");
        }

        template<typename TEnum>
        static TEnum EnumValue(const std::vector<std::pair<std::string, TEnum>>& names, const std::string& text,
                               const std::string& propertyName)
        {
            for (const auto& entry : names) { if (entry.first == text) { return entry.second; } }
            std::string allowed;
            for (const auto& entry : names) { allowed += (allowed.empty() ? "" : ", ") + entry.first; }
            throw System::InvalidCastException("'" + text + "' is not a value of processor parameter '" +
                                               propertyName + "'; expected one of: " + allowed + ".");
        }

        std::vector<ProcessorParameterBinding<TProcessor>> bindings_;
    };

    /**
     * @brief The spellings an enumeration already declares, in the shape `AddEnum` takes.
     *
     * The names come from the enumeration's own `ContentEnumNames` declaration -- the one the
     * intermediate serializer reads -- rather than from a second list written beside each
     * processor, which is a list that can silently disagree with the first.
     *
     * @tparam TEnum An enumeration declared with `CNA_XNA_CONTENT_ENUM`.
     * @return Name/value pairs in the declaration's own order.
     */
    template<typename TEnum>
    [[nodiscard]] CNAEXT std::vector<std::pair<std::string, TEnum>> DeclaredEnumSpellings()
    {
        std::vector<std::pair<std::string, TEnum>> spellings;
        for (const auto& entry : Serialization::Intermediate::ContentEnumNames<TEnum>::Names)
        {
            spellings.emplace_back(std::string(entry.second), entry.first);
        }
        return spellings;
    }

    /**
     * @brief Detects whether a processor class declares `DescribeParameters`.
     *
     * @tparam TProcessor The processor class.
     */
    template<typename TProcessor>
    concept DescribesProcessorParameters = requires(ProcessorParameterBindings<TProcessor>& bindings) {
        { TProcessor::DescribeParameters(bindings) };
    };

    /**
     * @brief Returns the bindings a processor class declares, or an empty set.
     *
     * @tparam TProcessor The processor class.
     * @return The bindings.
     */
    template<typename TProcessor>
    [[nodiscard]] CNAEXT ProcessorParameterBindings<TProcessor> DescribeProcessorParameters()
    {
        ProcessorParameterBindings<TProcessor> bindings;
        if constexpr (DescribesProcessorParameters<TProcessor>)
        {
            TProcessor::DescribeParameters(bindings);
        }
        return bindings;
    }
}
