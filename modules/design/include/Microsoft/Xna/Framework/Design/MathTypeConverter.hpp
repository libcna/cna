// SPDX-License-Identifier: MS-PL

#pragma once

#include <algorithm>
#include <any>
#include <cctype>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "System/ArgumentException.hpp"
#include "System/ComponentModel/Design/Serialization/InstanceDescriptor.hpp"
#include "System/ComponentModel/ExpandableObjectConverter.hpp"
#include "System/ComponentModel/TypeDescriptor.hpp"
#include "System/ComponentModel/detail/ObjectText.hpp"
#include "System/Globalization/CultureInfo.hpp"

namespace Microsoft::Xna::Framework::Design {

/** @brief Provides common conversion behavior for XNA mathematical value types. */
class MathTypeConverter : public System::ComponentModel::ExpandableObjectConverter {
public:
    using System::ComponentModel::ExpandableObjectConverter::CanConvertFrom;
    using System::ComponentModel::ExpandableObjectConverter::CanConvertTo;
    using System::ComponentModel::ExpandableObjectConverter::GetCreateInstanceSupported;
    using System::ComponentModel::ExpandableObjectConverter::GetProperties;
    using System::ComponentModel::ExpandableObjectConverter::GetPropertiesSupported;

    /** @brief Initializes an expandable converter with string conversion enabled. */
    MathTypeConverter();

    /**
     * @brief Reports whether the converter accepts values of the specified source type.
     *
     * @param context The optional conversion context.
     * @param sourceType The source type.
     * @return @c true for strings when enabled, or when the base converter supports the type.
     */
    [[nodiscard]] bool CanConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                      const System::Type& sourceType) const override;

    /**
     * @brief Reports whether the converter can produce the specified destination type.
     *
     * @param context The optional conversion context.
     * @param destinationType The destination type.
     * @return @c true for InstanceDescriptor and base-supported destinations.
     */
    [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                    const System::Type& destinationType) const override;

    /**
     * @brief Reports that component edits recreate the immutable boxed value.
     *
     * @param context The optional conversion context.
     * @return Always @c true.
     */
    [[nodiscard]] bool GetCreateInstanceSupported(
        System::ComponentModel::ITypeDescriptorContext* context) const override;

    /**
     * @brief Returns the component descriptors exposed by the concrete converter.
     *
     * @param context The optional conversion context.
     * @param value The value being described.
     * @param attributes The requested descriptor attributes.
     * @return The converter's stable ordered property collection.
     */
    [[nodiscard]] System::ComponentModel::PropertyDescriptorCollection GetProperties(
        System::ComponentModel::ITypeDescriptorContext* context,
        const std::any& value,
        const System::ComponentModel::AttributeCollection& attributes) const override;

    /**
     * @brief Reports that the converter exposes component properties.
     *
     * @param context The optional conversion context.
     * @return Always @c true.
     */
    [[nodiscard]] bool GetPropertiesSupported(
        System::ComponentModel::ITypeDescriptorContext* context) const override;

protected:
    System::ComponentModel::PropertyDescriptorCollection propertyDescriptions_;
    bool supportStringConvert_ = true;

    template<class TValue>
    [[nodiscard]] static std::optional<std::vector<TValue>> ConvertToValues(
        System::ComponentModel::ITypeDescriptorContext* context,
        const System::Globalization::CultureInfo* culture,
        const std::any& value,
        std::size_t expectedCount,
        std::span<const std::string> names) {
        const auto textValue = System::ComponentModel::detail::ObjectText::AsString(value);
        if (!textValue.has_value()) return std::nullopt;
        if (culture == nullptr) {
            culture = &System::Globalization::CultureInfo::getCurrentCultureProperty();
        }

        std::string text = *textValue;
        const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
        const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
        text = first < last ? std::string(first, last) : std::string{};

        const std::string separator = culture->getTextInfoProperty().getListSeparatorProperty();
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (;;) {
            const std::size_t split = text.find(separator, start);
            if (split == std::string::npos) {
                parts.push_back(text.substr(start));
                break;
            }
            parts.push_back(text.substr(start, split - start));
            start = split + separator.size();
        }
        try {
            const auto converter = System::ComponentModel::TypeDescriptor::GetConverter(System::Type::From<TValue>());
            std::vector<TValue> result;
            result.reserve(parts.size());
            for (const std::string& part : parts) {
                result.push_back(std::any_cast<TValue>(converter->ConvertFromString(context, culture, part)));
            }
            if (result.size() == expectedCount) return result;
        } catch (...) {
            std::string message = "The value '" + *textValue + "' could not be converted";
            if (!names.empty()) {
                message += " to components ";
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (i != 0) message += ", ";
                    message += names[i];
                }
            }
            message += ".";
            throw System::ArgumentException(message, std::current_exception());
        }

        throw System::ArgumentException("The value must contain " + std::to_string(expectedCount) +
                                        " components.");
    }

    template<class TValue, std::size_t TExtent>
    [[nodiscard]] static std::string ConvertFromValues(
        System::ComponentModel::ITypeDescriptorContext* context,
        const System::Globalization::CultureInfo* culture,
        std::span<const TValue, TExtent> values) {
        if (culture == nullptr) {
            culture = &System::Globalization::CultureInfo::getCurrentCultureProperty();
        }
        const auto converter = System::ComponentModel::TypeDescriptor::GetConverter(System::Type::From<TValue>());
        const std::string separator = culture->getTextInfoProperty().getListSeparatorProperty() + " ";
        std::string result;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) result += separator;
            result += converter->ConvertToString(context, culture, std::any(values[i]));
        }
        return result;
    }
};

} // namespace Microsoft::Xna::Framework::Design
