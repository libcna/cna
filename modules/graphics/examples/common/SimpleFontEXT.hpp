// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Graphics/SystemFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
namespace CNAExamplesEXT {
/** @brief Creates the shared CNA bitmap font. @param device Graphics owner. @return Font. */
inline std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> MakeSimpleFontEXT(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) {
    return CNA::Internal::Graphics::makeSystemFont(device);
}
}
