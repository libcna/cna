// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/XnaComponentNames.hpp"

#include <map>

namespace Microsoft::Xna::Framework::Content::Pipeline::Tasks
{
    namespace
    {
        /** @brief The class name without the namespace a project may have written in front of it. */
        [[nodiscard]] std::string Bare(const std::string& xnaName)
        {
            const std::size_t dot = xnaName.rfind('.');
            return dot == std::string::npos ? xnaName : xnaName.substr(dot + 1u);
        }

        [[nodiscard]] const std::map<std::string, XnaComponentMapping>& Importers()
        {
            static const std::map<std::string, XnaComponentMapping> map = {
                {"TextureImporter", {"CNA.ImageImporter", {}, ""}},
                {"FontDescriptionImporter", {"CNA.FontDescriptionImporter", {}, ""}},
                {"WavImporter", {"CNA.WavImporter", {}, ""}},
                {"Mp3Importer", {"CNA.SongImporter", {}, ""}},
                {"WmaImporter", {"CNA.SongImporter", {}, ""}},
                {"WmvImporter", {"CNA.VideoImporter", {}, ""}},
                {"EffectImporter", {"CNA.EffectSourceImporter", {}, ""}},
                {"XmlImporter",
                 {"", {},
                  "XmlImporter builds whatever the document's Asset Type names, which the "
                  "canonical build graph selects from the source itself; name no importer for an "
                  ".xml asset and it is read the same way."}},
                {"FbxImporter", {"CNA.FbxImporter", {}, ""}},
                {"XImporter", {"CNA.XImporter", {}, ""}},
            };
            return map;
        }

        [[nodiscard]] const std::map<std::string, XnaComponentMapping>& Processors()
        {
            // The three texture processors are one canonical processor with three sets of
            // defaults; the values are the measured ones (parity report, processor-default table).
            static const std::map<std::string, XnaComponentMapping> map = {
                {"TextureProcessor",
                 {"CNA.TextureProcessor",
                  {{"generateMipmaps", "false"}, {"textureFormat", "Color"}},
                  ""}},
                {"SpriteTextureProcessor",
                 {.canonicalName = "CNA.TextureProcessor",
                  .defaults = {{"generateMipmaps", "false"}, {"textureFormat", "Color"}},
                  .obsoleteMessage =
                      "SpriteTextureProcessor is obsolete, and may be removed in a future "
                      "version. TextureProcessor (Texture - XNA Framework) should be used "
                      "instead."}},
                {"ModelTextureProcessor",
                 {.canonicalName = "CNA.TextureProcessor",
                  .defaults = {{"generateMipmaps", "true"}, {"textureFormat", "DxtCompressed"}},
                  .obsoleteMessage =
                      "ModelTextureProcessor is obsolete, and may be removed in a future "
                      "version. TextureProcessor (Texture - XNA Framework) should be used "
                      "instead."}},
                {"FontDescriptionProcessor", {"CNA.FontDescriptionProcessor", {}, ""}},
                // The XNA model processor, not the glTF one: an XNA project's models are
                // `.x` and `.fbx` files, and those import to XNA's own scene graph. The
                // canonical `CNA.ModelProcessor` takes an imported glTF document, which no
                // XNA source produces.
                {"ModelProcessor", {"CNA.XnaModelProcessor", {}, ""}},
                {"SoundEffectProcessor", {"CNA.SoundEffectProcessor", {}, ""}},
                {"SongProcessor", {"CNA.SongProcessor", {}, ""}},
                {"VideoProcessor", {"CNA.VideoProcessor", {}, ""}},
                {"EffectProcessor", {"CNA.EffectSourceProcessor", {}, ""}},
                {"PassThroughProcessor",
                 {"", {},
                  "PassThroughProcessor writes its input unchanged, which is what the canonical "
                  "graph does for a source with no processor; name none and the asset passes "
                  "through."}},
                {"FontTextureProcessor",
                 {"", {},
                  "the sprite-font-from-texture route is not implemented in this build; a "
                  ".spritefont source and FontDescriptionProcessor are."}},
                {"MaterialProcessor",
                 {"", {},
                  "MaterialProcessor is reached through ModelProcessor rather than named on an "
                  "asset of its own."}},
            };
            return map;
        }

        [[nodiscard]] XnaComponentMapping Look(const std::map<std::string, XnaComponentMapping>& map,
                                               const std::string& xnaName, const char* kind)
        {
            const auto found = map.find(Bare(xnaName));
            if (found != map.end())
            {
                XnaComponentMapping mapping = found->second;
                mapping.known = true;
                return mapping;
            }
            return {"", {},
                    std::string("'") + xnaName + "' is not a built-in XNA " + kind +
                        ". A custom one is registered in code with RegisterXna" +
                        (std::string(kind) == "importer" ? "Importer" : "Processor") +
                        "() before the build runs; C++ has no assembly to load it from."};
        }

        [[nodiscard]] std::vector<std::string> NamesOf(
            const std::map<std::string, XnaComponentMapping>& map)
        {
            std::vector<std::string> names;
            for (const auto& [name, mapping] : map)
            {
                if (!mapping.canonicalName.empty())
                {
                    names.push_back(name);
                }
            }
            return names;
        }
    }

    XnaComponentMapping MapXnaImporterName(const std::string& xnaName)
    {
        return Look(Importers(), xnaName, "importer");
    }

    XnaComponentMapping MapXnaProcessorName(const std::string& xnaName)
    {
        return Look(Processors(), xnaName, "processor");
    }

    std::vector<std::string> KnownXnaImporterNames() { return NamesOf(Importers()); }

    std::vector<std::string> KnownXnaProcessorNames() { return NamesOf(Processors()); }
}
