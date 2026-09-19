// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Color values and exposes R, G, B, and A byte components. */
class ColorConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::CanConvertTo;
    using MathTypeConverter::ConvertFrom; using MathTypeConverter::ConvertTo; using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Color converter. */ ColorConverter();
    /** @brief Reports whether a Color can be converted to the destination type. @param context Conversion context. @param destinationType Destination type. @return @c true for InstanceDescriptor and base-supported destinations. */ [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Type& destinationType) const override { return destinationType == System::Type::From<System::ComponentModel::Design::Serialization::InstanceDescriptor>() || MathTypeConverter::CanConvertTo(context, destinationType); }
    /** @brief Converts a supported source value to Color. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Color. */ [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Color to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Color. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Color from R, G, B, and A. @param context Conversion context. @param propertyValues Byte component values. @return A boxed Color. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
