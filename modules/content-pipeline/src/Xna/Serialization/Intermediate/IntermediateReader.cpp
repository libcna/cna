// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateReader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentException.hpp"
#include "System/Xml/XmlException.hpp"
#include "System/Xml/XmlNodeType.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    namespace
    {
        using System::Xml::XmlNodeType;

        bool IsObjectSerializer(const ContentTypeSerializerBase& serializer)
        {
            return serializer.getTargetTypeProperty() == System::Type::From<System::Object>();
        }

        const char* NodeTypeName(XmlNodeType type)
        {
            switch (type)
            {
            case XmlNodeType::None: return "None";
            case XmlNodeType::Element: return "Element";
            case XmlNodeType::Attribute: return "Attribute";
            case XmlNodeType::Text: return "Text";
            case XmlNodeType::CDATA: return "CDATA";
            case XmlNodeType::EntityReference: return "EntityReference";
            case XmlNodeType::Entity: return "Entity";
            case XmlNodeType::ProcessingInstruction: return "ProcessingInstruction";
            case XmlNodeType::Comment: return "Comment";
            case XmlNodeType::Document: return "Document";
            case XmlNodeType::DocumentType: return "DocumentType";
            case XmlNodeType::DocumentFragment: return "DocumentFragment";
            case XmlNodeType::Notation: return "Notation";
            case XmlNodeType::Whitespace: return "Whitespace";
            case XmlNodeType::SignificantWhitespace: return "SignificantWhitespace";
            case XmlNodeType::EndElement: return "EndElement";
            case XmlNodeType::EndEntity: return "EndEntity";
            case XmlNodeType::XmlDeclaration: return "XmlDeclaration";
            }
            return "None";
        }

        /** @brief .NET's `Type.ToString()` spelling of a canonical name: `List`1[System.Int32]`. */
        std::string DotNetTypeString(const std::string& canonical)
        {
            const std::size_t open = canonical.find('[');
            if (open == std::string::npos || open + 1 < canonical.size() && canonical[open + 1] == ']')
            {
                return canonical;
            }
            std::string args = canonical.substr(open + 1, canonical.size() - open - 2);
            int depth = 0;
            int count = 1;
            std::string spelledArgs;
            std::string current;
            for (char c : args)
            {
                if (c == '[') { ++depth; }
                if (c == ']') { --depth; }
                if (c == ',' && depth == 0)
                {
                    spelledArgs += DotNetTypeString(current) + ",";
                    current.clear();
                    ++count;
                    continue;
                }
                current += c;
            }
            spelledArgs += DotNetTypeString(current);
            return canonical.substr(0, open) + "`" + std::to_string(count) + "[" + spelledArgs + "]";
        }

        bool LooksAbsolute(const std::string& path)
        {
            if (path.empty())
            {
                return false;
            }
            if (path[0] == '/' || path[0] == '\\')
            {
                return true;
            }
            return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
        }

        std::string WithForwardSlashes(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }
    }

    IntermediateReader::IntermediateReader(IntermediateSerializer& serializer, System::Xml::XmlReader& xml)
        : serializer_(serializer), xml_(xml)
    {
    }

    IntermediateSerializer& IntermediateReader::getSerializerProperty() const noexcept { return serializer_; }

    System::Xml::XmlReader& IntermediateReader::getXmlProperty() const noexcept { return xml_; }

    bool IntermediateReader::MoveToElement(const std::string& elementName)
    {
        if (currentElementEmpty_)
        {
            return false;
        }
        return xml_.IsStartElement(elementName);
    }

    System::Type IntermediateReader::ReadTypeName()
    {
        const std::string spelled = xml_.GetAttribute("Type");
        if (spelled.empty())
        {
            throw InvalidContentException("XML is missing a \"Type\" attribute.");
        }
        return serializer_.ResolveTypeName(spelled, xml_).getTargetTypeProperty();
    }

    ContentTypeSerializerBase& IntermediateReader::ResolveDeclared(const ContentSerializerAttribute& format,
                                                                   ContentTypeSerializerBase& declaredSerializer,
                                                                   bool rootAsset)
    {
        (void)format;
        (void)rootAsset;
        const std::string spelled = xml_.GetAttribute("Type");
        if (spelled.empty())
        {
            if (IsObjectSerializer(declaredSerializer))
            {
                throw InvalidContentException("XML is missing a \"Type\" attribute.");
            }
            if (declaredSerializer.IsAbstract())
            {
                throw InvalidContentException("Instances of abstract classes cannot be created.");
            }
            return declaredSerializer;
        }
        ContentTypeSerializerBase& resolved = serializer_.ResolveTypeName(spelled, xml_);
        if (&resolved == &declaredSerializer || IsObjectSerializer(declaredSerializer) ||
            resolved.TargetTypeName() == declaredSerializer.TargetTypeName() ||
            declaredSerializer.UnderlyingSerializer() == &resolved)
        {
            return resolved;
        }
        if (resolved.IsAbstract())
        {
            throw InvalidContentException("Instances of abstract classes cannot be created.");
        }
        bool derives = false;
        if (resolved.IsReferenceType() && declaredSerializer.IsReferenceType())
        {
            const ContentObject probe = resolved.CreateInstance();
            derives = !declaredSerializer.FromObject(resolved.AsObject(probe)).Empty();
        }
        if (!derives)
        {
            throw InvalidContentException("XML \"Type\" attribute is invalid. Expecting a subclass of " +
                                          DotNetTypeString(IntermediateSerializer::CanonicalTypeName(declaredSerializer.TargetTypeName())) +
                                          ", but XML contains " +
                                          DotNetTypeString(IntermediateSerializer::CanonicalTypeName(resolved.TargetTypeName())) + ".");
        }
        return resolved;
    }

    IntermediateReader::DepthGuard::DepthGuard(IntermediateReader& owner) : reader(owner)
    {
        if (++reader.depth_ > MaxNestingDepth)
        {
            --reader.depth_;
            throw InvalidContentException("XML nests deeper than the " + std::to_string(MaxNestingDepth) +
                                          " levels CNA reads. " + reader.Location());
        }
    }

    IntermediateReader::DepthGuard::~DepthGuard() { --reader.depth_; }

    ContentObject IntermediateReader::ReadObjectCore(const ContentSerializerAttribute& format,
                                                     ContentTypeSerializerBase& declaredSerializer,
                                                     const ContentObject& existingInstance)
    {
        const DepthGuard depth(*this);
        const std::string& name = format.getElementNameProperty();
        const bool flatten = format.getFlattenContentProperty();
        ContentTypeSerializerBase* serializer = &declaredSerializer;
        const bool savedEmpty = currentElementEmpty_;
        bool wasEmpty = false;
        if (!flatten)
        {
            if (!MoveToElement(name))
            {
                throw InvalidContentException("XML element \"" + name + "\" not found.");
            }
            if (xml_.GetAttribute("Null") == "true")
            {
                if (!format.getAllowNullProperty())
                {
                    throw InvalidContentException("XML element \"" + name + "\" is not allowed to be null.");
                }
                if (!declaredSerializer.IsNullable())
                {
                    throw InvalidContentException("XML element \"" + name + "\" is null, but " +
                                                  declaredSerializer.TargetTypeName() +
                                                  " is a value type and cannot be null.");
                }
                xml_.Skip();
                return declaredSerializer.NullObject();
            }
            serializer = &ResolveDeclared(format, declaredSerializer, false);
            wasEmpty = xml_.getIsEmptyElementProperty();
            xml_.ReadStartElement();
        }
        else
        {
            if (IsObjectSerializer(declaredSerializer))
            {
                throw InvalidContentException("XML is missing a \"Type\" attribute.");
            }
            if (declaredSerializer.IsAbstract())
            {
                throw InvalidContentException("Instances of abstract classes cannot be created.");
            }
        }
        currentElementEmpty_ = flatten ? savedEmpty : wasEmpty;
        ContentObject result;
        try
        {
            result = serializer->InvokeDeserialize(*this, format, existingInstance);
        }
        catch (...)
        {
            currentElementEmpty_ = savedEmpty;
            throw;
        }
        currentElementEmpty_ = savedEmpty;
        if (!flatten && !wasEmpty)
        {
            ReadEndElement();
        }
        return result;
    }

    ContentObject IntermediateReader::ReadRawObjectCore(const ContentSerializerAttribute& format,
                                                        ContentTypeSerializerBase& typeSerializer,
                                                        const ContentObject& existingInstance)
    {
        const DepthGuard depth(*this);
        const bool flatten = format.getFlattenContentProperty();
        const bool savedEmpty = currentElementEmpty_;
        bool wasEmpty = false;
        if (!flatten)
        {
            if (!MoveToElement(format.getElementNameProperty()))
            {
                throw InvalidContentException("XML element \"" + format.getElementNameProperty() + "\" not found.");
            }
            wasEmpty = xml_.getIsEmptyElementProperty();
            xml_.ReadStartElement();
        }
        currentElementEmpty_ = flatten ? savedEmpty : wasEmpty;
        ContentObject result;
        try
        {
            result = typeSerializer.InvokeDeserialize(*this, format, existingInstance);
        }
        catch (...)
        {
            currentElementEmpty_ = savedEmpty;
            throw;
        }
        currentElementEmpty_ = savedEmpty;
        if (!flatten && !wasEmpty)
        {
            ReadEndElement();
        }
        return result;
    }

    void IntermediateReader::ReadSharedResourceCore(const ContentSerializerAttribute& format,
                                                    ContentTypeSerializerBase& declaredSerializer,
                                                    std::function<void(const ContentObject&)> fixup)
    {
        std::string id;
        if (format.getFlattenContentProperty())
        {
            id = ReadContentText();
        }
        else
        {
            const std::string& name = format.getElementNameProperty();
            if (!MoveToElement(name))
            {
                throw InvalidContentException("XML element \"" + name + "\" not found.");
            }
            if (xml_.GetAttribute("Null") == "true")
            {
                xml_.Skip();
                return;
            }
            if (xml_.getIsEmptyElementProperty())
            {
                xml_.Read();
                return;
            }
            xml_.ReadStartElement();
            id = ReadElementText();
            ReadEndElement();
        }
        if (id.empty())
        {
            return;
        }
        sharedFixups_.push_back(SharedFixup{id, &declaredSerializer, std::move(fixup)});
    }

    void IntermediateReader::ReadExternalReferenceCore(const std::string& targetTypeName,
                                                       std::function<void(std::string)> setFilename)
    {
        if (!MoveToElement("Reference"))
        {
            return;
        }
        const bool empty = xml_.getIsEmptyElementProperty();
        xml_.ReadStartElement();
        std::string id;
        if (!empty)
        {
            id = ReadElementText();
            ReadEndElement();
        }
        if (id.empty())
        {
            return;
        }
        externalFixups_.push_back(ExternalFixup{id, targetTypeName, std::move(setFilename)});
    }

    std::string IntermediateReader::ReadText(const ContentSerializerAttribute& format)
    {
        return format.getFlattenContentProperty() ? ReadContentText() : ReadElementText();
    }

    std::string IntermediateReader::ReadElementText()
    {
        if (currentElementEmpty_)
        {
            return std::string();
        }
        std::string text;
        while (true)
        {
            switch (xml_.getNodeTypeProperty())
            {
            case XmlNodeType::Text:
            case XmlNodeType::CDATA:
            case XmlNodeType::Whitespace:
            case XmlNodeType::SignificantWhitespace:
                text += xml_.getValueProperty();
                xml_.Read();
                break;
            case XmlNodeType::Comment:
            case XmlNodeType::ProcessingInstruction:
                xml_.Read();
                break;
            case XmlNodeType::Element:
                throw System::Xml::XmlException(
                    "ReadElementContentAs() methods cannot be called on an element that has child elements. " +
                    Location());
            default:
                return text;
            }
        }
    }

    std::string IntermediateReader::ReadContentText()
    {
        if (currentElementEmpty_)
        {
            return std::string();
        }
        // .NET's ReadContentAsString refuses to start on an element and otherwise collects text up
        // to the next element or end tag.
        if (xml_.getNodeTypeProperty() == XmlNodeType::Element)
        {
            throw InvalidContentException(
                "The ReadContentAsString method is not supported on node type Element. If you want to read typed "
                "content of an element, use the ReadElementContentAs method. " +
                Location());
        }
        std::string text;
        while (true)
        {
            switch (xml_.getNodeTypeProperty())
            {
            case XmlNodeType::Text:
            case XmlNodeType::CDATA:
            case XmlNodeType::Whitespace:
            case XmlNodeType::SignificantWhitespace:
                text += xml_.getValueProperty();
                xml_.Read();
                break;
            case XmlNodeType::Comment:
            case XmlNodeType::ProcessingInstruction:
                xml_.Read();
                break;
            default:
                return text;
            }
        }
    }

    void IntermediateReader::ReadEndElement()
    {
        while (true)
        {
            switch (xml_.getNodeTypeProperty())
            {
            case XmlNodeType::Comment:
            case XmlNodeType::ProcessingInstruction:
            case XmlNodeType::Whitespace:
                xml_.Read();
                break;
            case XmlNodeType::EndElement:
                xml_.Read();
                return;
            default:
                ThrowInvalidNodeType();
            }
        }
    }

    void IntermediateReader::ThrowInvalidNodeType() const
    {
        throw System::Xml::XmlException(std::string("'") + NodeTypeName(xml_.getNodeTypeProperty()) +
                                        "' is an invalid XmlNodeType. " + Location());
    }

    bool IntermediateReader::CurrentElementIsEmpty() const noexcept { return currentElementEmpty_; }

    bool IntermediateReader::HasPendingSharedFixups() const noexcept { return !sharedFixups_.empty(); }

    bool IntermediateReader::HasPendingExternalFixups() const noexcept { return !externalFixups_.empty(); }

    std::vector<std::string> IntermediateReader::SplitTokens(std::string_view text)
    {
        std::vector<std::string> tokens;
        std::size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            {
                ++i;
            }
            const std::size_t start = i;
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            {
                ++i;
            }
            if (i > start)
            {
                tokens.emplace_back(text.substr(start, i - start));
            }
        }
        return tokens;
    }

    std::string IntermediateReader::Location() const
    {
        return "Line " + std::to_string(xml_.getLineNumberProperty()) + ", position " +
               std::to_string(xml_.getLinePositionProperty()) + ".";
    }

    ContentObject IntermediateReader::ReadRootAsset(ContentTypeSerializerBase& serializer)
    {
        while (true)
        {
            const XmlNodeType type = xml_.getNodeTypeProperty();
            if (type == XmlNodeType::DocumentType)
            {
                throw System::Xml::XmlException(
                    "For security reasons DTD is prohibited in this XML document. To enable DTD processing set the "
                    "DtdProcessing property on XmlReaderSettings to Parse and pass the settings into XmlReader.Create "
                    "method.");
            }
            if (type == XmlNodeType::Element)
            {
                break;
            }
            if (!xml_.Read())
            {
                throw InvalidContentException(
                    "XML is not in the XNA intermediate format. Missing XnaContent root element.");
            }
        }
        if (xml_.getNameProperty() != "XnaContent")
        {
            throw InvalidContentException("XML is not in the XNA intermediate format. Missing XnaContent root element.");
        }
        const bool rootEmpty = xml_.getIsEmptyElementProperty();
        xml_.ReadStartElement();
        if (rootEmpty)
        {
            throw InvalidContentException("XML element \"Asset\" not found.");
        }
        ContentSerializerAttribute format;
        format.setElementNameProperty("Asset");
        return ReadObjectCore(format, serializer, ContentObject{});
    }

    void IntermediateReader::ReadResources()
    {
        const bool empty = xml_.getIsEmptyElementProperty();
        xml_.ReadStartElement();
        if (empty)
        {
            return;
        }
        ContentSerializerAttribute format;
        format.setElementNameProperty("Resource");
        while (MoveToElement("Resource"))
        {
            const std::string id = xml_.GetAttribute("ID");
            if (id.empty())
            {
                throw InvalidContentException("XML attribute \"ID\" was not found.");
            }
            const std::string spelledType = xml_.GetAttribute("Type");
            if (spelledType.empty())
            {
                throw InvalidContentException("XML is missing a \"Type\" attribute.");
            }
            for (const auto& [existingId, resource] : sharedResources_)
            {
                if (existingId == id)
                {
                    throw InvalidContentException("Duplicate XML ID attribute \"" + id + "\".");
                }
            }
            ContentTypeSerializerBase& serializer = serializer_.ResolveTypeName(spelledType, xml_);
            if (serializer.IsAbstract())
            {
                throw InvalidContentException("Instances of abstract classes cannot be created.");
            }
            const bool wasEmpty = xml_.getIsEmptyElementProperty();
            xml_.ReadStartElement();
            const bool savedEmpty = currentElementEmpty_;
            currentElementEmpty_ = wasEmpty;
            ContentObject value;
            try
            {
                value = serializer.InvokeDeserialize(*this, format, ContentObject{});
            }
            catch (...)
            {
                currentElementEmpty_ = savedEmpty;
                throw;
            }
            currentElementEmpty_ = savedEmpty;
            if (!wasEmpty)
            {
                ReadEndElement();
            }
            sharedResources_.emplace_back(id, SharedResource{std::move(value), &serializer});
        }
        ReadEndElement();
    }

    void IntermediateReader::ReadExternalReferences()
    {
        const bool empty = xml_.getIsEmptyElementProperty();
        xml_.ReadStartElement();
        if (empty)
        {
            return;
        }
        while (MoveToElement("ExternalReference"))
        {
            const std::string id = xml_.GetAttribute("ID");
            if (id.empty())
            {
                throw InvalidContentException("XML attribute \"ID\" was not found.");
            }
            const std::string targetType = xml_.GetAttribute("TargetType");
            if (targetType.empty())
            {
                throw InvalidContentException("XML attribute \"TargetType\" was not found.");
            }
            if (!IntermediateSerializer::IsKnownTypeName(targetType))
            {
                throw System::ArgumentException("Cannot find type \"" + targetType + "\".");
            }
            const bool wasEmpty = xml_.getIsEmptyElementProperty();
            xml_.ReadStartElement();
            std::string filename;
            if (!wasEmpty)
            {
                filename = ReadElementText();
                ReadEndElement();
            }
            externalReferences_.emplace_back(id, ExternalEntry{targetType, std::move(filename)});
        }
        ReadEndElement();
    }

    void IntermediateReader::ExpectEndOfDocument()
    {
        while (true)
        {
            switch (xml_.getNodeTypeProperty())
            {
            case XmlNodeType::Comment:
            case XmlNodeType::ProcessingInstruction:
            case XmlNodeType::Whitespace:
                xml_.Read();
                break;
            case XmlNodeType::EndElement:
                xml_.Read();
                return;
            default:
                ThrowInvalidNodeType();
            }
        }
    }

    void IntermediateReader::ApplyFixups()
    {
        for (const SharedFixup& fixup : sharedFixups_)
        {
            SharedResource* resource = nullptr;
            for (auto& [id, candidate] : sharedResources_)
            {
                if (id == fixup.id)
                {
                    resource = &candidate;
                    break;
                }
            }
            if (resource == nullptr)
            {
                throw InvalidContentException("Missing shared resource \"" + fixup.id + "\".");
            }
            ContentTypeSerializerBase& declared = *fixup.declared;
            const bool sameType = resource->serializer == &declared || IsObjectSerializer(declared) ||
                                  resource->serializer->TargetTypeName() == declared.TargetTypeName();
            bool compatible = sameType;
            if (!compatible && resource->serializer->IsReferenceType() && declared.IsReferenceType())
            {
                compatible = !declared.FromObject(resource->serializer->AsObject(resource->value)).Empty();
            }
            if (!compatible)
            {
                throw InvalidContentException(
                    "XML specifies invalid type for shared resource. Expecting a subclass of \"" +
                    DotNetTypeString(IntermediateSerializer::CanonicalTypeName(declared.TargetTypeName())) +
                    "\", but XML contains \"" +
                    DotNetTypeString(IntermediateSerializer::CanonicalTypeName(resource->serializer->TargetTypeName())) +
                    "\".");
            }
            // A value-typed resource referenced through a reference wrapper is wrapped once, so every
            // reference to the same id shares one instance.
            if (declared.IsReferenceType() && !resource->serializer->IsReferenceType())
            {
                ContentObject wrapped = declared.WrapValue(resource->value);
                if (!wrapped.Empty())
                {
                    resource->value = std::move(wrapped);
                    resource->serializer = &declared;
                }
            }
            fixup.apply(resource->value);
        }
        const std::string& relocation = serializer_.getReferenceRelocationPathProperty();
        for (const ExternalFixup& fixup : externalFixups_)
        {
            const ExternalEntry* entry = nullptr;
            for (const auto& [id, candidate] : externalReferences_)
            {
                if (id == fixup.id)
                {
                    entry = &candidate;
                    break;
                }
            }
            if (entry == nullptr)
            {
                throw InvalidContentException("Missing external reference \"" + fixup.id + "\".");
            }
            if (entry->targetTypeName != fixup.targetTypeName)
            {
                throw InvalidContentException("XML specifies wrong type for external reference \"" + fixup.id + "\".");
            }
            std::string filename = entry->filename;
            if (!LooksAbsolute(filename))
            {
                if (filename.empty())
                {
                    // XNA's path helper passes its own message as the parameter name of an
                    // ArgumentNullException; the .NET Framework 4.0 text is reproduced as it stands.
                    throw System::ArgumentException(
                        "Value cannot be null.\nParameter name: Invalid filesystem location \"\".");
                }
                if (relocation.empty())
                {
                    throw System::ArgumentException("Invalid filesystem location \"" + filename + "\".");
                }
                const std::filesystem::path base = std::filesystem::path(WithForwardSlashes(relocation)).parent_path();
                filename = (base / WithForwardSlashes(filename)).lexically_normal().generic_string();
            }
            fixup.apply(std::move(filename));
        }
    }
}
