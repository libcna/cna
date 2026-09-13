// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"

#include <memory>

#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentException.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::string OpaqueDataDictionary::GetContentAsXml() const
    {
        if (getCountProperty() == 0)
        {
            return std::string();
        }
        System::Xml::XmlWriterSettings settings;
        settings.Indent = false;
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        Serialization::Intermediate::IntermediateSerializer::Serialize<OpaqueDataDictionary>(*writer, *this, std::string());
        std::string text = writer->ToString();
        // XNA builds this text through a StringWriter, whose declaration names utf-16.
        const std::string utf8 = "encoding=\"utf-8\"";
        const std::size_t declaration = text.find(utf8);
        if (declaration != std::string::npos && declaration < text.find("?>"))
        {
            text.replace(declaration, utf8.size(), "encoding=\"utf-16\"");
        }
        return text;
    }

    System::Type OpaqueDataDictionary::getDefaultSerializerTypeProperty() const
    {
        // Measured (tests/reference/xna40/intermediate/opaque_data_dictionary.xml): a string entry is
        // written without a Type attribute, every other value with one.
        return System::Type::From<std::string>();
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
