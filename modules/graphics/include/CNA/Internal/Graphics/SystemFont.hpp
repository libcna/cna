// SPDX-License-Identifier: MS-PL
#pragma once
#include <memory>
namespace Microsoft::Xna::Framework::Graphics {class GraphicsDevice;class SpriteFont;}
namespace CNA::Internal::Graphics {
/** @brief Creates the original CNA bitmap system-overlay font.
 * @param device Graphics resource owner. @return Printable ASCII font with fallback glyph. */
std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> makeSystemFont(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
}
