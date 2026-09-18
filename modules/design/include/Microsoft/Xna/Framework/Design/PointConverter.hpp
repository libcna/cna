// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Point values and exposes their X and Y components. */
class PointConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertFrom;
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Point converter. */
    PointConverter();
    /** @brief Converts a supported source value to Point. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Point. */
    [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Point to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Point. @param destinationType Destination type. @return The converted value. */
    [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Point from named component values. @param context Conversion context. @param propertyValues X and Y values. @return A boxed Point. */
    [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
};
}
