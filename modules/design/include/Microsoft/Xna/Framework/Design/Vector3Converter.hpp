// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Vector3 values and exposes X, Y, and Z. */
class Vector3Converter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertFrom; using MathTypeConverter::ConvertTo; using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Vector3 converter. */ Vector3Converter();
    /** @brief Converts a supported source value to Vector3. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Vector3. */ [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Vector3 to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Vector3. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Vector3 from named component values. @param context Conversion context. @param propertyValues X, Y, and Z values. @return A boxed Vector3. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
