// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Design/Registration.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
namespace Microsoft::Xna::Framework::Design {
/** @brief Converts Ray values and exposes Position and Direction. */
class RayConverter final : public MathTypeConverter {
public:
    using MathTypeConverter::CanConvertFrom;
    using MathTypeConverter::ConvertFrom;
    using MathTypeConverter::CanConvertTo;
    using MathTypeConverter::ConvertTo;
    using MathTypeConverter::CreateInstance;
    /** @brief Initializes the Ray converter. */ RayConverter();
    /**
     * @brief Converts a supported source value to Ray.
     *
     * Documented XNA override. It forwards to the base converter unchanged, exactly as the
     * Microsoft implementation does; Ray has no string form, so the inherited
     * behaviour is what rejects one.
     *
     * @param context Conversion context.
     * @param culture Conversion culture.
     * @param value Source value.
     * @return The converted value produced by the base converter.
     */
    [[nodiscard]] std::any ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Globalization::CultureInfo* culture,
                                       const std::any& value) const override
    {
        return MathTypeConverter::ConvertFrom(context, culture, value);
    }
    /** @brief Reports whether a Ray can be converted to the destination type. @param context Conversion context. @param destinationType Destination type. @return @c true for InstanceDescriptor and base-supported destinations. */ [[nodiscard]] bool CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Type& destinationType) const override { return destinationType == System::Type::From<System::ComponentModel::Design::Serialization::InstanceDescriptor>() || MathTypeConverter::CanConvertTo(context, destinationType); }
    /** @brief Converts a Ray to a supported destination. @param context Conversion context. @param culture Conversion culture. @param value Boxed Ray. @param destinationType Destination type. @return The converted value. */ [[nodiscard]] std::any ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const override;
    /** @brief Recreates a Ray from Position and Direction. @param context Conversion context. @param propertyValues Position and Direction values. @return A boxed Ray. */ [[nodiscard]] std::any CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& propertyValues) const override;
}; }
