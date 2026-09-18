// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Quaternion values and exposes X, Y, Z, and W. */
class QuaternionConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertFrom; using MathTypeConverter::ConvertTo; using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Quaternion converter. */ QuaternionConverter();
    /** @brief Converts a supported source value to Quaternion. @param context Conversion context. @param culture Conversion culture. @param value Source value. @return A boxed Quaternion. */ [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value) const override;
    /** @brief Converts a Quaternion to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Quaternion. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Quaternion from named component values. @param context Conversion context. @param propertyValues X, Y, Z, and W values. @return A boxed Quaternion. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
