// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"

#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::string OpaqueDataDictionary::GetContentAsXml() const
    {
        throw System::NotSupportedException(
            "OpaqueDataDictionary::GetContentAsXml() requires the intermediate serializer "
            "(plans/plan_xnapipeline_parity.md XNAPP-072), which does not exist yet.");
    }

    System::Type OpaqueDataDictionary::getDefaultSerializerTypeProperty() const
    {
        return System::Type::From<System::Object>();
    }

    void OpaqueDataDictionary::AddItem(const std::string& key, const ContentObject& value)
    {
        if (value.Empty())
        {
            throw System::ArgumentException("Opaque data values must not be null.", "value");
        }
        NamedValueDictionary<ContentObject>::AddItem(key, value);
    }

    void OpaqueDataDictionary::ClearItems()
    {
        NamedValueDictionary<ContentObject>::ClearItems();
    }

    bool OpaqueDataDictionary::RemoveItem(const std::string& key)
    {
        return NamedValueDictionary<ContentObject>::RemoveItem(key);
    }

    void OpaqueDataDictionary::SetItem(const std::string& key, const ContentObject& value)
    {
        if (value.Empty())
        {
            throw System::ArgumentException("Opaque data values must not be null.", "value");
        }
        NamedValueDictionary<ContentObject>::SetItem(key, value);
    }
}
