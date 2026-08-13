// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <memory>

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Graphics::Effect;

    /** @brief FNA's general EffectReader for compiled Effect Framework bytecode. */
    class EffectReader final : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        EffectReader()
            : ContentTypeReader<std::shared_ptr<Effect>>(
                  "Microsoft.Xna.Framework.Graphics.Effect") {}

    protected:
        std::shared_ptr<Effect> Read(
            ContentReader& input,
            std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /** @brief Registers EffectReader under its canonical XNA/FNA reader name. */
    void RegisterEffectXnbReader();
}
