#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a font texture used to draw text with SpriteBatch.
    class SpriteFont
    {
    public:
        [[nodiscard]] int getLineSpasingProperty() const;
        void setLineSpacingProperty(int value);

        [[nodiscard]] float getSpacingProperty() const;
        void setSpacingProperty(float value);

        /// Measures the size of a string when drawn with this font.
        [[nodiscard]] Vector2 MeasureString(const std::string& text) const;

    private:
        int lineSpacing_ = 0;
        float spacing_   = 0.0f;
    };
}
