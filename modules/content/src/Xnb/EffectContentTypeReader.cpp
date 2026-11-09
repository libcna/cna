// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/EffectContentTypeReader.hpp"
#include "CNA/Internal/ContentManagerAccess.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    namespace
    {
        constexpr std::int32_t kMaximumEffectBytecodeBytes = 64 * 1024 * 1024;
    }

    std::shared_ptr<Effect> EffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void) existingInstance;
        const std::string context = "'" + input.getAssetNameProperty() + "': EffectReader";
        if (input.getContentManagerProperty() == nullptr)
        {
            throw ContentLoadException(
                context + " has no ContentManager/GraphicsDevice for the compiled effect.");
        }

        // Refresh EffectReader.Read IL_0001-001b has no catch. ReadAsset alone
        // wraps IOException; constructor/device exceptions keep their identity.
        const std::int32_t length = input.ReadInt32();
        if (length < 0 || length > kMaximumEffectBytecodeBytes)
        {
            throw ContentLoadException(
                context + " declares an invalid bytecode length (" +
                std::to_string(length) + ").");
        }
        const std::vector<std::uint8_t> bytes =
            input.ReadBytesExactOrThrow(length, "EffectReader");
        auto effect = std::make_shared<Effect>(
            CNA::Internal::ContentManagerAccess::GraphicsDeviceFromContentReader(input), bytes);
        effect->setNameProperty(input.getAssetNameProperty());
        return effect;
    }

    void RegisterEffectXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.EffectReader",
            [] { return std::make_unique<EffectReader>(); });
    }
}
