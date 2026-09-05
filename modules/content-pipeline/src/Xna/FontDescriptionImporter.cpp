// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/FontDescriptionImporter.hpp"

#include <filesystem>
#include <memory>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Xml/XmlReader.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<Graphics::FontDescription> FontDescriptionImporter::Import(const std::string& filename,
                                                                               ContentImporterContext& context)
    {
        (void)context;
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
        catch (const System::Exception& parse)
        {
            throw InvalidContentException(
                "There was an error while deserializing intermediate XML. " + parse.getMessageProperty(),
                std::current_exception());
        }
        if (reader == nullptr)
        {
            throw InvalidContentException(
                "There was an error while deserializing intermediate XML. Root element is missing.");
        }
        using Serialization::Intermediate::IntermediateSerializer;
        std::shared_ptr<Graphics::FontDescription> font =
            IntermediateSerializer::Deserialize<Graphics::FontDescription>(*reader, filename);
        if (font != nullptr)
        {
            // The identity names the file and this importer, which is what a processor's
            // diagnostics quote (measured, fontimporter/full).
            const std::string tool(XnaTypeName);
            font->setIdentityProperty(ContentIdentity(filename, tool.substr(tool.rfind('.') + 1)));
        }
        return font;
    }

    ContentImporterAttribute FontDescriptionImporter::Attribute()
    {
        ContentImporterAttribute attribute(".spritefont");
        attribute.setDefaultProcessorProperty("FontDescriptionProcessor");
        attribute.setDisplayNameProperty("Sprite Font Description");
        return attribute;
    }

    const std::string& FontDescriptionImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
