// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/EffectImporter.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::shared_ptr<Graphics::EffectContent> EffectImporter::Import(const std::string& filename,
                                                                    ContentImporterContext& context)
    {
        (void)context;
        std::ifstream file(filename, std::ios::binary);
        if (!file)
        {
            // XNA's message names the file twice, once as it was asked for and once as it looked
            // for it (measured, effectimporter/refusals).
            throw System::IO::FileNotFoundException("There was an error importing \"" + filename +
                                                    "\". The file could not be found. Looked for \"" +
                                                    filename + "\".");
        }
        // The text is taken as it is: an empty file and a binary one are both accepted, and an
        // `#include` is left for the compiler to resolve (measured, effectimporter/source and
        // effectimporter/include, which records no dependency).
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto content = std::make_shared<Graphics::EffectContent>();
        content->setEffectCodeProperty(std::move(source));
        content->setIdentityProperty(ContentIdentity(filename, std::string(XnaTypeName).substr(
                                                                   std::string(XnaTypeName).rfind('.') + 1)));
        return content;
    }

    ContentImporterAttribute EffectImporter::Attribute()
    {
        ContentImporterAttribute attribute(".fx");
        attribute.setDefaultProcessorProperty("EffectProcessor");
        attribute.setDisplayNameProperty("Effect - XNA Framework");
        return attribute;
    }

    const std::string& EffectImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
