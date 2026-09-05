// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <typeinfo>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    namespace
    {
        bool IsObjectSerializer(const ContentTypeSerializerBase& serializer)
        {
            return serializer.getTargetTypeProperty() == System::Type::From<System::Object>();
        }

        std::string WithBackslashes(std::string path)
        {
            std::replace(path.begin(), path.end(), '/', '\\');
            return path;
        }

        std::string WithForwardSlashes(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
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
    }

    IntermediateWriter::IntermediateWriter(IntermediateSerializer& serializer, System::Xml::XmlWriter& xml)
        : serializer_(serializer), xml_(xml)
    {
    }

    IntermediateSerializer& IntermediateWriter::getSerializerProperty() const noexcept { return serializer_; }

    System::Xml::XmlWriter& IntermediateWriter::getXmlProperty() const noexcept { return xml_; }

    void IntermediateWriter::WriteTypeName(System::Type type)
    {
        xml_.WriteAttributeString("Type", serializer_.SpellTypeName(serializer_.GetTypeSerializer(type)));
    }

    ContentTypeSerializerBase& IntermediateWriter::ResolveDynamic(ContentObject& value,
                                                                  ContentTypeSerializerBase& declaredSerializer) const
    {
        ContentTypeSerializerBase* result = &declaredSerializer;
        if (IsObjectSerializer(declaredSerializer))
        {
            result = IntermediateSerializer::FindTypeSerializer(value);
            if (result == nullptr)
            {
                throw InvalidContentException("No type serializer is registered for '" + value.StableType() + "'.");
            }
        }
        if (result->IsReferenceType())
        {
            if (std::shared_ptr<System::Object> object = result->AsObject(value))
            {
                const System::Type dynamicType = System::Type::FromTypeInfo(typeid(*object));
                if (dynamicType != result->getTargetTypeProperty())
                {
                    ContentTypeSerializerBase* derived = IntermediateSerializer::FindTypeSerializer(dynamicType);
                    if (derived == nullptr)
                    {
                        throw InvalidContentException("The object of type '" + object->GetTypeName() +
                                                      "' has no registered type serializer; register the type before "
                                                      "serializing it through a reference to '" +
                                                      result->TargetTypeName() + "'.");
                    }
                    ContentObject reboxed = derived->FromObject(object);
                    if (!reboxed.Empty())
                    {
                        value = std::move(reboxed);
                        result = derived;
                    }
                }
            }
        }
        return *result;
    }

    void IntermediateWriter::WriteObjectCore(const ContentObject& value, const ContentSerializerAttribute& format,
                                             ContentTypeSerializerBase& declaredSerializer, bool forceTypeAttribute)
    {
        const std::string& name = format.getElementNameProperty();
        const bool flatten = format.getFlattenContentProperty();
        if (declaredSerializer.IsNull(value))
        {
            if (format.getOptionalProperty() || flatten)
            {
                return;
            }
            if (!format.getAllowNullProperty())
            {
                throw InvalidContentException("XML element \"" + name + "\" is not allowed to be null.");
            }
            xml_.WriteStartElement(name);
            xml_.WriteAttributeString("Null", "true");
            xml_.WriteEndElement();
            return;
        }
        ContentObject payload = value;
        ContentTypeSerializerBase& dynamic = ResolveDynamic(payload, declaredSerializer);
        if (format.getOptionalProperty() && dynamic.ObjectIsEmpty(payload))
        {
            return;
        }
        const bool writeType = forceTypeAttribute || IsObjectSerializer(declaredSerializer) ||
                               (declaredSerializer.IsReferenceType() && &dynamic != &declaredSerializer);
        if (flatten)
        {
            if (writeType)
            {
                throw InvalidContentException("XML element \"" + name +
                                              "\" is flattened, so its Type attribute has nowhere to go: the value's "
                                              "type '" + dynamic.TargetTypeName() + "' differs from the declared '" +
                                              declaredSerializer.TargetTypeName() + "'.");
            }
            dynamic.InvokeSerialize(*this, payload, format);
            return;
        }
        xml_.WriteStartElement(name);
        if (writeType)
        {
            xml_.WriteAttributeString("Type", serializer_.SpellTypeName(dynamic));
        }
        dynamic.InvokeSerialize(*this, payload, format);
        xml_.WriteEndElement();
    }

    void IntermediateWriter::WriteRawObjectCore(const ContentObject& value, const ContentSerializerAttribute& format,
                                                ContentTypeSerializerBase& typeSerializer)
    {
        const bool flatten = format.getFlattenContentProperty();
        if (!flatten)
        {
            xml_.WriteStartElement(format.getElementNameProperty());
        }
        typeSerializer.InvokeSerialize(*this, value, format);
        if (!flatten)
        {
            xml_.WriteEndElement();
        }
    }

    void IntermediateWriter::WriteSharedResourceCore(const ContentObject& value,
                                                     const ContentSerializerAttribute& format,
                                                     ContentTypeSerializerBase& declaredSerializer)
    {
        const bool flatten = format.getFlattenContentProperty();
        if (!flatten)
        {
            xml_.WriteStartElement(format.getElementNameProperty());
        }
        if (!declaredSerializer.IsNull(value))
        {
            ContentObject payload = value;
            ContentTypeSerializerBase& dynamic = ResolveDynamic(payload, declaredSerializer);
            const void* identity = dynamic.Identity(payload);
            std::string id;
            const auto found = identity != nullptr ? resourceIds_.find(identity) : resourceIds_.end();
            if (found != resourceIds_.end())
            {
                id = found->second;
            }
            else
            {
                id = "#Resource" + std::to_string(resources_.size() + 1);
                if (identity != nullptr)
                {
                    resourceIds_.emplace(identity, id);
                }
                resources_.push_back(QueuedResource{id, payload, &dynamic});
            }
            xml_.WriteString(id);
        }
        if (!flatten)
        {
            xml_.WriteEndElement();
        }
    }

    void IntermediateWriter::WriteExternalReferenceCore(const std::string& filename, const std::string& targetTypeName)
    {
        if (filename.empty())
        {
            return;
        }
        const std::pair<std::string, std::string> key(targetTypeName, filename);
        std::string id;
        const auto found = externalIds_.find(key);
        if (found != externalIds_.end())
        {
            id = found->second;
        }
        else
        {
            id = "#External" + std::to_string(externals_.size() + 1);
            externalIds_.emplace(key, id);
            externals_.push_back(QueuedExternal{id, targetTypeName, filename});
        }
        xml_.WriteElementString("Reference", id);
    }

    void IntermediateWriter::ScanForNamespaces(const ContentObject& value, ContentTypeSerializerBase& declaredSerializer)
    {
        std::set<const void*> visited;
        ContentTypeSerializerBase::ChildCallback visit;
        visit = [this, &visited, &visit](ContentTypeSerializerBase& declared, const ContentObject& child)
        {
            if (declared.IsNull(child))
            {
                return;
            }
            ContentObject payload = child;
            ContentTypeSerializerBase& dynamic = ResolveDynamic(payload, declared);
            if (IsObjectSerializer(declared) || &dynamic != &declared)
            {
                (void)serializer_.SpellTypeName(dynamic);
            }
            if (dynamic.IsReferenceType())
            {
                const void* identity = dynamic.Identity(payload);
                if (identity != nullptr && !visited.insert(identity).second)
                {
                    return;
                }
            }
            dynamic.InvokeScanChildren(serializer_, visit, payload);
        };
        ContentObject payload = value;
        ContentTypeSerializerBase& root = ResolveDynamic(payload, declaredSerializer);
        (void)serializer_.SpellTypeName(root);
        if (root.IsReferenceType())
        {
            if (const void* identity = root.Identity(payload))
            {
                visited.insert(identity);
            }
        }
        root.InvokeScanChildren(serializer_, visit, payload);
    }

    void IntermediateWriter::WriteRootAsset(const ContentObject& value, ContentTypeSerializerBase& serializer)
    {
        ContentSerializerAttribute format;
        format.setElementNameProperty("Asset");
        WriteObjectCore(value, format, serializer, true);
    }

    void IntermediateWriter::WriteResources()
    {
        if (resources_.empty())
        {
            return;
        }
        xml_.WriteStartElement("Resources");
        ContentSerializerAttribute format;
        format.setElementNameProperty("Resource");
        for (std::size_t i = 0; i < resources_.size(); ++i)
        {
            // Writing a resource may queue further resources, so the vector may grow: copy the entry.
            const QueuedResource resource = resources_[i];
            xml_.WriteStartElement("Resource");
            xml_.WriteAttributeString("ID", resource.id);
            xml_.WriteAttributeString("Type", serializer_.SpellTypeName(*resource.serializer));
            resource.serializer->InvokeSerialize(*this, resource.value, format);
            xml_.WriteEndElement();
        }
        xml_.WriteEndElement();
    }

    void IntermediateWriter::WriteExternalReferences()
    {
        if (externals_.empty())
        {
            return;
        }
        xml_.WriteStartElement("ExternalReferences");
        for (const QueuedExternal& external : externals_)
        {
            xml_.WriteStartElement("ExternalReference");
            xml_.WriteAttributeString("ID", external.id);
            xml_.WriteAttributeString("TargetType", external.targetTypeName);
            xml_.WriteString(RelativeFilename(external.filename));
            xml_.WriteEndElement();
        }
        xml_.WriteEndElement();
    }

    std::string IntermediateWriter::RelativeFilename(const std::string& filename) const
    {
        const std::string& relocation = serializer_.getReferenceRelocationPathProperty();
        if (relocation.empty() || !LooksAbsolute(filename) || !LooksAbsolute(relocation))
        {
            return WithBackslashes(filename);
        }
        const std::filesystem::path base = std::filesystem::path(WithForwardSlashes(relocation)).parent_path();
        const std::filesystem::path target(WithForwardSlashes(filename));
        if (base.root_name() != target.root_name())
        {
            return WithBackslashes(filename);
        }
        const std::filesystem::path relative = target.lexically_relative(base);
        if (relative.empty())
        {
            return WithBackslashes(filename);
        }
        return WithBackslashes(relative.generic_string());
    }
}
