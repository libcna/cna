// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Rectangle values and exposes X, Y, Width, and Height. */
class RectangleConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::CanConvertTo;
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Rectangle converter. */
    RectangleConverter();
    /** @brief Reports whether a Rectangle can be converted to the destination type. @param context Conversion context. @param destinationType Destination type. @return @c true for InstanceDescriptor and base-supported destinations. */
    [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Type& destinationType) const override { return destinationType == System::Type::From<System::ComponentModel::Design::Serialization::InstanceDescriptor>() || MathTypeConverter::CanConvertTo(context, destinationType); }
    /** @brief Converts a Rectangle to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Rectangle. @param destinationType Destination type. @return The converted value. */
    [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Rectangle from named component values. @param context Conversion context. @param propertyValues X, Y, Width, and Height. @return A boxed Rectangle. */
    [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
};
}
