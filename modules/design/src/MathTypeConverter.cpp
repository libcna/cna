// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"

namespace Microsoft::Xna::Framework::Design {

MathTypeConverter::MathTypeConverter() = default;

bool MathTypeConverter::CanConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Type& sourceType) const {
    if (supportStringConvert_ && System::ComponentModel::detail::ObjectText::IsStringType(sourceType)) {
        return true;
    }
    return System::ComponentModel::TypeConverter::CanConvertFrom(context, sourceType);
}

bool MathTypeConverter::CanConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Type& destinationType) const {
    if (supportStringConvert_ && destinationType == System::Type::From<std::string>()) return true;
    return System::ComponentModel::TypeConverter::CanConvertTo(context, destinationType);
}

bool MathTypeConverter::GetCreateInstanceSupported(
    System::ComponentModel::ITypeDescriptorContext* context) const {
    (void)context;
    return true;
}

System::ComponentModel::PropertyDescriptorCollection MathTypeConverter::GetProperties(
    System::ComponentModel::ITypeDescriptorContext* context,
    const std::any& value,
    const System::ComponentModel::AttributeCollection& attributes) const {
    (void)context;
    (void)value;
    (void)attributes;
    return propertyDescriptions_;
}

bool MathTypeConverter::GetPropertiesSupported(
    System::ComponentModel::ITypeDescriptorContext* context) const {
    (void)context;
    return true;
}

} // namespace Microsoft::Xna::Framework::Design
