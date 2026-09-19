// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts BoundingSphere values and exposes Center and Radius. */
class BoundingSphereConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::CanConvertTo;
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the BoundingSphere converter. */ BoundingSphereConverter();
    /** @brief Reports whether a BoundingSphere can be converted to the destination type. @param context Conversion context. @param destinationType Destination type. @return @c true for InstanceDescriptor and base-supported destinations. */ [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Type& destinationType) const override { return destinationType == System::Type::From<System::ComponentModel::Design::Serialization::InstanceDescriptor>() || MathTypeConverter::CanConvertTo(context, destinationType); }
    /** @brief Converts a BoundingSphere to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed BoundingSphere. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a BoundingSphere from Center and Radius. @param context Conversion context. @param propertyValues Center and Radius values. @return A boxed BoundingSphere. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
