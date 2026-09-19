// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Point values and exposes their X and Y components. */
class PointConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::CanConvertTo;
    using MathTypeConverter::ConvertFrom;
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Point converter. */
    PointConverter();
    /** @brief Reports whether a Point can be converted to the destination type. @param context Conversion context. @param destinationType Destination type. @return @c true for InstanceDescriptor and base-supported destinations. */
    [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Type& destinationType) const override { return destinationType == System::Type::From<System::ComponentModel::Design::Serialization::InstanceDescriptor>() || MathTypeConverter::CanConvertTo(context, destinationType); }
    /** @brief Converts a supported source value to Point. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Point. */
    [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Point to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Point. @param destinationType Destination type. @return The converted value. */
    [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Point from named component values. @param context Conversion context. @param propertyValues X and Y values. @return A boxed Point. */
    [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
};
}
