#include "EasyGLGraphicsBackend.hpp"
#include <iostream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Graphics {

    // --- EasyGLTextureBackend ---

    EasyGLTextureBackend::EasyGLTextureBackend(const std::string& assetName) {
        (void)assetName;
        // TODO: Implement EasyGL texture loading
    }

    // --- EasyGLSpriteBatchBackend ---

    void EasyGLSpriteBatchBackend::Begin() {
        // TODO: Implement EasyGL SpriteBatch::Begin
    }

    void EasyGLSpriteBatchBackend::End() {
        // TODO: Implement EasyGL SpriteBatch::End
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y) {
        (void)texture; (void)x; (void)y;
        // TODO: Implement EasyGL SpriteBatch::Draw
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& sourceRectangle,
                                       const Rectangle& destinationRectangle,
                                       const Color& color) {
        (void)texture; (void)sourceRectangle; (void)destinationRectangle; (void)color;
        // TODO: Implement EasyGL SpriteBatch::Draw
    }

    void EasyGLSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& sourceRectangle,
                                       const Rectangle& destinationRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth) {
        (void)texture; (void)sourceRectangle; (void)destinationRectangle; (void)color;
        (void)rotation; (void)origin; (void)effects; (void)layerDepth;
        // TODO: Implement EasyGL SpriteBatch::Draw
    }

    // --- EasyGLGraphicsBackend ---

    EasyGLGraphicsBackend::EasyGLGraphicsBackend() {
        std::cout << "EasyGLGraphicsBackend initialized (Placeholder)" << std::endl;
    }

    void EasyGLGraphicsBackend::Clear(float r, float g, float b, float a) {
        (void)r; (void)g; (void)b; (void)a;
        // TODO: Implement EasyGL Clear
    }

    void EasyGLGraphicsBackend::Present() {
        // TODO: Implement EasyGL Present
    }

    void EasyGLGraphicsBackend::GetViewportSize(int& width, int& height) {
        width = 800; // Default placeholder
        height = 600;
    }

    std::unique_ptr<ITextureBackend> EasyGLGraphicsBackend::CreateTexture(const std::string& assetName) {
        return std::make_unique<EasyGLTextureBackend>(assetName);
    }

    std::unique_ptr<ISpriteBatchBackend> EasyGLGraphicsBackend::CreateSpriteBatch() {
        return std::make_unique<EasyGLSpriteBatchBackend>();
    }

    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend() {
        return std::make_unique<EasyGLGraphicsBackend>();
    }

}
