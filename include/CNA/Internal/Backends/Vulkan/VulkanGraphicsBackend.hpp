#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::Vulkan {

    class VulkanTextureBackend : public ITextureBackend {
    public:
        explicit VulkanTextureBackend(const ImageData& data);
        ~VulkanTextureBackend() override;

        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

    private:
        int width;
        int height;
        // TODO: Add Vulkan Image/ImageView/Sampler handles
    };

    class VulkanSpriteBatchBackend : public ISpriteBatchBackend {
    public:
        VulkanSpriteBatchBackend();
        ~VulkanSpriteBatchBackend() override;

        void Begin() override;
        void End() override;
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        // TODO: Add Vulkan Pipeline, Descriptor Sets, Buffers for quad rendering
    };

    class VulkanGraphicsBackend : public IGraphicsBackend {
    public:
        explicit VulkanGraphicsBackend(SDL_Window* window);
        ~VulkanGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

    private:
        SDL_Window* window;
        // TODO: Add Vulkan Instance, Physical Device, Device, Surface, Swapchain, RenderPass, CommandPool, etc.
        
        void InitVulkan();
        void CleanupVulkan();
    };

}
