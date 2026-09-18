// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts BoundingBox values and exposes Min and Max. */
class BoundingBoxConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the BoundingBox converter. */ BoundingBoxConverter();
    /** @brief Converts a BoundingBox to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed BoundingBox. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a BoundingBox from Min and Max. @param context Conversion context. @param propertyValues Min and Max values. @return A boxed BoundingBox. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
