// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/VertexChannelSerializers.hpp"

#include <mutex>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics::detail
{
    namespace
    {
        struct FactoryRegistry
        {
            std::mutex mutex;
            std::map<std::string, VertexChannelFactory::Factory> factories;
        };

        FactoryRegistry& TheRegistry()
        {
            static FactoryRegistry registry;
            return registry;
        }
    }

    void VertexChannelFactory::Register(System::Type elementType, Factory factory)
    {
        FactoryRegistry& registry = TheRegistry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        registry.factories[elementType.getFullNameProperty()] = std::move(factory);
    }

    std::shared_ptr<VertexChannelBase> VertexChannelFactory::Create(System::Type elementType,
                                                                   const std::string& name)
    {
        RegisterBuiltIns();
        FactoryRegistry& registry = TheRegistry();
        Factory factory;
        {
            const std::lock_guard<std::mutex> lock(registry.mutex);
            const auto found = registry.factories.find(elementType.getFullNameProperty());
            if (found != registry.factories.end())
            {
                factory = found->second;
            }
        }
        if (!factory)
        {
            throw InvalidContentException("Cannot create a vertex channel of element type \"" +
                                          elementType.getFullNameProperty() + "\".");
        }
        return factory(name);
    }

    void VertexChannelFactory::RegisterBuiltIns()
    {
        static const bool once = []
        {
            // The element types the intermediate serializer can write. XNA accepts every type its
            // VectorConverter knows; CNA's intermediate serializer has no packed-vector text form,
            // so a channel of one of those has no serialized form here either -- recorded rather
            // than faked.
            Register<SharpRuntime::intcs>();
            Register<float>();
            Register<Vector2>();
            Register<Vector3>();
            Register<Vector4>();
            Register<Color>();
            return true;
        }();
        (void)once;
    }

    void VertexChannelCollectionSerializer::Serialize(Serialization::Intermediate::IntermediateWriter& output,
                                                      const std::shared_ptr<VertexChannelCollection>& value,
                                                      const Content::ContentSerializerAttribute& format)
    {
        (void)format;
        System::Xml::XmlWriter& xml = output.getXmlProperty();
        for (const std::shared_ptr<VertexChannelBase>& channel : *value)
        {
            Serialization::Intermediate::ContentTypeSerializerBase& element =
                output.getSerializerProperty().GetTypeSerializer(channel->getElementTypeProperty());
            xml.WriteStartElement("VertexChannel");
            xml.WriteAttributeString("Name", channel->getNameProperty());
            xml.WriteAttributeString("ElementType", output.getSerializerProperty().SpellTypeName(element));
            const std::string text = channel->PackedContent();
            if (!text.empty())
            {
                xml.WriteString(text);
            }
            xml.WriteEndElement();
        }
    }

    std::shared_ptr<VertexChannelCollection> VertexChannelCollectionSerializer::Deserialize(
        Serialization::Intermediate::IntermediateReader& input, const Content::ContentSerializerAttribute& format,
        std::shared_ptr<VertexChannelCollection> existingInstance)
    {
        (void)format;
        if (existingInstance == nullptr)
        {
            existingInstance = std::make_shared<VertexChannelCollection>();
        }
        while (input.MoveToElement("VertexChannel"))
        {
            const std::string name = input.getXmlProperty().GetAttribute("Name");
            const std::string spelledType = input.getXmlProperty().GetAttribute("ElementType");
            if (spelledType.empty())
            {
                throw InvalidContentException("XML attribute \"ElementType\" was not found.");
            }
            Serialization::Intermediate::ContentTypeSerializerBase& element =
                input.getSerializerProperty().ResolveTypeName(spelledType, input.getXmlProperty());
            const std::shared_ptr<VertexChannelBase> channel =
                VertexChannelFactory::Create(element.getTargetTypeProperty(), name);
            // The element's own traversal, which the framework does for a member it reads itself:
            // step into the start tag, take the text, step out again.
            const bool empty = input.getXmlProperty().getIsEmptyElementProperty();
            input.getXmlProperty().ReadStartElement();
            if (!empty)
            {
                channel->SetPackedContent(
                    Serialization::Intermediate::IntermediateReader::SplitTokens(input.ReadContentText()));
                input.ReadEndElement();
            }
            existingInstance->InsertChannel(existingInstance->getCountProperty(), channel);
        }
        return existingInstance;
    }

    void VertexChannelCollectionSerializer::ScanChildren(
        Serialization::Intermediate::IntermediateSerializer& serializer, const ChildCallback& callback,
        const std::shared_ptr<VertexChannelCollection>& value)
    {
        (void)callback;
        // The element type is written as an attribute, so its namespace alias has to be declared
        // even though no child object is written through the ordinary path.
        for (const std::shared_ptr<VertexChannelBase>& channel : *value)
        {
            (void)serializer.SpellTypeName(serializer.GetTypeSerializer(channel->getElementTypeProperty()));
        }
    }
}
