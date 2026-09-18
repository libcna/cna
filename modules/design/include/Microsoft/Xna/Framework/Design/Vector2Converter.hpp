// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Vector2 values and exposes X and Y. */
class Vector2Converter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertFrom; using MathTypeConverter::ConvertTo; using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Vector2 converter. */ Vector2Converter();
    /** @brief Converts a supported source value to Vector2. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Vector2. */ [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Vector2 to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Vector2. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Vector2 from named component values. @param context Conversion context. @param propertyValues X and Y values. @return A boxed Vector2. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
