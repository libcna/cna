// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/XmlImporter.hpp"

#include <filesystem>
#include <memory>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/Exception.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Xml/XmlReader.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    ContentObject XmlImporter::Import(const std::string& filename, ContentImporterContext& context)
    {
        (void)context;
        // A file that is not there is refused before the serializer sees it, with the runtime's
        // own exception rather than a content one (measured, importer_missing_file).
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw System::IO::FileNotFoundException("Could not find file '" + filename + "'.");
        }
        std::unique_ptr<System::Xml::XmlReader> reader;
        try
        {
            reader.reset(System::Xml::XmlReader::Create(filename));
        }
        catch (const System::Exception& error)
        {
            // A document the parser cannot read is refused as a deserialization failure, under the
            // same sentence the serializer's own failures carry (measured, importer_not_xml and
            // importer_empty_document; the parser's own wording differs from .NET's and that
            // divergence is recorded in docs/xna-intermediate-xml-format.md).
            throw InvalidContentException(
                "There was an error while deserializing intermediate XML. " + error.getMessageProperty(),
                std::current_exception());
        }
        catch (const std::exception& error)
        {
            throw InvalidContentException(
                std::string("There was an error while deserializing intermediate XML. ") + error.what(),
                std::current_exception());
        }
        if (reader == nullptr)
        {
            throw InvalidContentException(
                "There was an error while deserializing intermediate XML. Root element is missing.");
        }
        using Serialization::Intermediate::IntermediateSerializer;
        // The document's own `Type` attribute decides what is built, which is what the object
        // serializer is for; the importer itself knows no types.
        return IntermediateSerializer::DeserializeObject(
            *reader, IntermediateSerializer::TypeSerializerFor<System::Object>(), filename);
    }

    ContentImporterAttribute XmlImporter::Attribute()
    {
        // No default processor: XNA's own descriptor names none, so what the document builds is
        // passed on unprocessed unless the project says otherwise (measured through the inventory).
        ContentImporterAttribute attribute(".xml");
        attribute.setDisplayNameProperty("XML Content - XNA Framework");
        return attribute;
    }

    const std::string& XmlImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
