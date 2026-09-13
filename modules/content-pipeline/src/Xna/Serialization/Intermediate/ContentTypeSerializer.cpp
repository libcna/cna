// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializer.hpp"

#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeSerializerAttribute.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    ContentTypeSerializerAttribute::ContentTypeSerializerAttribute() = default;

    ContentTypeSerializerBase::ContentTypeSerializerBase(System::Type targetType) : targetType_(targetType) {}

    ContentTypeSerializerBase::ContentTypeSerializerBase(System::Type targetType, std::string xmlTypeName)
        : targetType_(targetType), xmlTypeName_(std::move(xmlTypeName))
    {
    }

    ContentTypeSerializerBase::ContentTypeSerializerBase(System::Type targetType, std::string xmlTypeName,
                                                         std::string targetTypeName)
        : targetType_(targetType), xmlTypeName_(std::move(xmlTypeName)), targetTypeName_(std::move(targetTypeName))
    {
    }

    bool ContentTypeSerializerBase::getCanDeserializeIntoExistingObjectProperty() const { return false; }

    System::Type ContentTypeSerializerBase::getTargetTypeProperty() const noexcept { return targetType_; }

    const std::string& ContentTypeSerializerBase::getXmlTypeNameProperty() const noexcept { return xmlTypeName_; }

    bool ContentTypeSerializerBase::ObjectIsEmpty(const ContentObject& value) const
    {
        (void)value;
        return false;
    }

    const std::string& ContentTypeSerializerBase::TargetTypeName() const noexcept { return targetTypeName_; }

    bool ContentTypeSerializerBase::IsReferenceType() const noexcept { return false; }

    bool ContentTypeSerializerBase::IsNullable() const noexcept { return false; }

    bool ContentTypeSerializerBase::IsAbstract() const noexcept { return false; }

    bool ContentTypeSerializerBase::IsNull(const ContentObject& value) const { return value.Empty(); }

    ContentObject ContentTypeSerializerBase::NullObject() const { return ContentObject{}; }

    ContentObject ContentTypeSerializerBase::CreateInstance() const
    {
        ThrowCannotCreateInstance(targetTypeName_, false);
    }

    std::shared_ptr<System::Object> ContentTypeSerializerBase::AsObject(const ContentObject& value) const
    {
        (void)value;
        return nullptr;
    }

    ContentObject ContentTypeSerializerBase::FromObject(const std::shared_ptr<System::Object>& value) const
    {
        (void)value;
        return ContentObject{};
    }

    System::Type ContentTypeSerializerBase::DynamicType(const ContentObject& value) const
    {
        (void)value;
        return targetType_;
    }

    std::size_t ContentTypeSerializerBase::PackedTokenCount() const noexcept { return 0; }

    std::string ContentTypeSerializerBase::FormatPacked(const ContentObject& value) const
    {
        (void)value;
        throw System::NotSupportedException("Values of type '" + targetTypeName_ + "' are not written as packed text.");
    }

    ContentObject ContentTypeSerializerBase::ParsePacked(std::span<const std::string> tokens) const
    {
        (void)tokens;
        throw System::NotSupportedException("Values of type '" + targetTypeName_ + "' are not read from packed text.");
    }

    std::type_index ContentTypeSerializerBase::CarrierType() const
    {
        return targetType_.getTypeInfo() != nullptr ? std::type_index(*targetType_.getTypeInfo())
                                                    : std::type_index(typeid(void));
    }

    const void* ContentTypeSerializerBase::Identity(const ContentObject& value) const
    {
        return AsObject(value).get();
    }

    const ContentTypeSerializerBase* ContentTypeSerializerBase::UnderlyingSerializer() const { return nullptr; }

    ContentObject ContentTypeSerializerBase::UnderlyingValue(const ContentObject& value) const { return value; }

    ContentObject ContentTypeSerializerBase::WrapValue(const ContentObject& value) const
    {
        (void)value;
        return ContentObject{};
    }

    std::string ContentTypeSerializerBase::CollectionItemName() const { return std::string(); }

    void ContentTypeSerializerBase::Initialize(IntermediateSerializer& serializer) { (void)serializer; }

    void ContentTypeSerializerBase::ScanChildren(IntermediateSerializer& serializer, const ChildCallback& callback,
                                                 const ContentObject& value)
    {
        (void)serializer;
        (void)callback;
        (void)value;
    }

    void ThrowCannotCreateInstance(const std::string& typeName, bool abstractType)
    {
        if (abstractType)
        {
            throw InvalidContentException("Instances of abstract classes cannot be created.");
        }
        throw InvalidContentException("Cannot create an instance of type '" + typeName +
                                      "': it has no default constructor.");
    }
}
