#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include <stdexcept>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "backends/graphics/common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    SpriteBatch::SpriteBatch(GraphicsDevice& graphicsDevice)
        : backend_(graphicsDevice.GetBackend().CreateSpriteBatch()),
          begun(false)
    {
    }

    SpriteBatch::SpriteBatch()
        : begun(false)
    {
    }

    SpriteBatch::~SpriteBatch() = default;

    void SpriteBatch::Begin()
    {
        if (backend_) {
            backend_->Begin();
            begun = true;
        }
    }

    void SpriteBatch::Begin(SpriteSortMode sprite_sort_mode, BlendState blend_state)
    {
        (void)sprite_sort_mode;
        (void)blend_state;
        Begin();
    }

    void SpriteBatch::End()
    {
        if (backend_) {
            backend_->End();
            begun = false;
        }
    }

    void SpriteBatch::Draw(const Texture2D& texture, float x, float y)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_) backend_->Draw(texture.GetBackend(), x, y);
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>::value_type& value,
                           const Rectangle& sourceRectangle,
                           const Rectangle& destinationRectangle,
                           Color color)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_) backend_->Draw(value.GetBackend(), sourceRectangle, destinationRectangle, color);
    }

    void SpriteBatch::Draw(const std::optional<Texture2D>& value,
                           const Rectangle& sourceRectangle,
                           const Rectangle& destinationRectangle,
                           Color color,
                           float rotation_rad,
                           Vector2 origin,
                           SpriteEffects effect,
                           float layerDepth)
    {
        if (!begun) throw std::runtime_error("SpriteBatch::Draw called before Begin().");
        if (backend_ && value.has_value()) {
            backend_->Draw(value->GetBackend(), sourceRectangle, destinationRectangle, color, rotation_rad, origin, effect, layerDepth);
        }
    }
}