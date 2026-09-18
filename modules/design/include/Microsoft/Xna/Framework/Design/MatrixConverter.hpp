// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Matrix values and exposes Translation plus all sixteen row-major elements. */
class MatrixConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Matrix converter. */ MatrixConverter();
    /** @brief Converts a Matrix to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Matrix. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Matrix from all sixteen named elements. @param context Conversion context. @param propertyValues Matrix element values. @return A boxed Matrix. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
