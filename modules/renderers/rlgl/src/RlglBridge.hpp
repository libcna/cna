// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::Rlgl::Bridge
{
    /** @brief Native GL sampler values exposed only to focused renderer validation. */
    struct SamplerSnapshot
    {
        int minFilter = 0;
        int magFilter = 0;
        int wrapS = 0;
        int wrapT = 0;
        int wrapR = 0;
        int compareMode = 0;
        float minLod = 0.0f;
        float maxLod = 0.0f;
        float lodBias = 0.0f;
        float anisotropy = 1.0f;
    };

    enum ClearPlane : unsigned int
    {
        /** @brief Selects the active framebuffer's color planes. */
        ColorPlane = 1u << 0u,
        /** @brief Selects the active framebuffer's depth plane. */
        DepthPlane = 1u << 1u,
        /** @brief Selects the active framebuffer's stencil plane. */
        StencilPlane = 1u << 2u,
    };

    /**
     * @brief Loads rlgl's GL dispatch and creates its default resources.
     * @param loader CNA platform entry-point loader.
     * @param width Initial drawable width.
     * @param height Initial drawable height.
     * @return The driver OpenGL version string.
     */
    [[nodiscard]] std::string Initialize(
        CNA::Platform::GlProcAddressLoader loader, int width, int height);

    /** @brief Releases rlgl's default resources while its context is current. */
    void Shutdown() noexcept;

    /**
     * @brief Updates rlgl's physical framebuffer bookkeeping.
     * @param width Drawable width.
     * @param height Drawable height.
     */
    void SetFramebufferSize(int width, int height);

    /**
     * @brief Clears selected framebuffer planes while preserving their write masks.
     * @param planes Bitwise ClearPlane selection.
     * @param r Red clear component.
     * @param g Green clear component.
     * @param b Blue clear component.
     * @param a Alpha clear component.
     * @param depth Depth clear value.
     * @param stencil Stencil clear value.
     */
    void Clear(unsigned int planes, float r, float g, float b, float a,
               float depth, int stencil);

    /**
     * @brief Enables or disables depth testing through rlgl.
     * @param enabled True to enable the test.
     */
    void SetDepthTestEnabled(bool enabled);

    /**
     * @brief Enables or disables color blending through rlgl.
     * @param enabled True to enable blending.
     */
    void SetBlendEnabled(bool enabled);

    /**
     * @brief Enables or disables depth writes through rlgl.
     * @param enabled True to enable depth writes.
     */
    void SetDepthWriteEnabled(bool enabled);

    /**
     * @brief Sets the GL viewport and depth range.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param minDepth Minimum depth value.
     * @param maxDepth Maximum depth value.
     */
    void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth);

    /**
     * @brief Sets the GL scissor rectangle.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     */
    void SetScissor(int x, int y, int width, int height);

    /**
     * @brief Reads an RGBA8 backbuffer rectangle and normalizes it to top-left row order.
     * @param x Left edge in top-left-origin coordinates.
     * @param y Top edge in top-left-origin coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param framebufferHeight Current physical framebuffer height.
     * @param pixels Destination holding at least width * height * 4 bytes.
     */
    void ReadBackbuffer(
        int x, int y, int width, int height, int framebufferHeight, unsigned char* pixels);

    /**
     * @brief Returns the current context's maximum two-dimensional texture edge.
     * @return The value reported by `GL_MAX_TEXTURE_SIZE`.
     */
    [[nodiscard]] int GetMaxTextureSize();

    /**
     * @brief Creates an RGBA8 texture through rlgl and allocates its declared mip chain.
     * @param width Level-zero width.
     * @param height Level-zero height.
     * @param mipLevels Number of mip levels to allocate.
     * @param pixels Tightly packed level-zero RGBA8 bytes.
     * @return The non-zero GL texture name owned by rlgl.
     */
    [[nodiscard]] unsigned int CreateTexture2DRgba8(
        int width, int height, int mipLevels, const std::uint8_t* pixels);

    /**
     * @brief Releases a texture through rlgl while the device is live.
     * @param id Texture name, or zero.
     */
    void DestroyTexture2D(unsigned int id) noexcept;

    /**
     * @brief Replaces one complete RGBA8 mip level.
     * @param id Texture name.
     * @param level Mip level.
     * @param width Level width.
     * @param height Level height.
     * @param pixels Tightly packed RGBA8 bytes.
     */
    void UpdateTexture2DRgba8(
        unsigned int id, int level, int width, int height, const std::uint8_t* pixels);

    /**
     * @brief Reads an RGBA8 rectangle from one texture mip level.
     * @param id Texture name.
     * @param level Mip level.
     * @param levelWidth Complete level width.
     * @param levelHeight Complete level height.
     * @param x Rectangle left edge.
     * @param y Rectangle top edge in upload-memory order.
     * @param width Rectangle width.
     * @param height Rectangle height.
     * @param pixels Destination holding width * height * 4 bytes.
     */
    void ReadTexture2DRgba8(
        unsigned int id, int level, int levelWidth, int levelHeight,
        int x, int y, int width, int height, std::uint8_t* pixels);

    /**
     * @brief Binds a two-dimensional texture through rlgl.
     * @param id Texture name, or zero to unbind.
     * @param unit Texture unit.
     */
    void BindTexture2D(unsigned int id, int unit);

    /**
     * @brief Returns the two-dimensional texture bound to a unit for focused validation.
     * @param unit Texture unit.
     * @return The current GL texture name.
     */
    [[nodiscard]] unsigned int GetBoundTexture2DForTesting(int unit);

    /**
     * @brief Returns the number of fragment texture units available to XNA samplers.
     * @return The live `GL_MAX_TEXTURE_IMAGE_UNITS` value.
     */
    [[nodiscard]] int GetMaxSamplerSlots();

    /**
     * @brief Returns the driver anisotropy ceiling found by rlgl's extension probe.
     * @return Maximum supported anisotropy, or one when the extension is unavailable.
     */
    [[nodiscard]] float GetMaxSamplerAnisotropy();

    /**
     * @brief Creates one GL 3.3 sampler object for a CNA texture slot.
     * @return A non-zero sampler name.
     */
    [[nodiscard]] unsigned int CreateSampler();

    /**
     * @brief Releases sampler objects while the rlgl device is live.
     * @param samplers Contiguous sampler names.
     * @param count Number of names in the array.
     */
    void DestroySamplers(const unsigned int* samplers, std::size_t count) noexcept;

    /**
     * @brief Applies one complete XNA sampler description and binds it to a texture unit.
     * @param sampler Sampler name.
     * @param slot Texture unit.
     * @param filter Raw `TextureFilter` ordinal.
     * @param addressU Raw U `TextureAddressMode` ordinal.
     * @param addressV Raw V `TextureAddressMode` ordinal.
     * @param addressW Raw W `TextureAddressMode` ordinal.
     * @param maxAnisotropy Requested anisotropy.
     * @param maxMipLevel Most detailed mip level the sampler may select.
     * @param lodBias Mipmap level-of-detail bias.
     */
    void ApplySampler(
        unsigned int sampler, int slot, int filter,
        int addressU, int addressV, int addressW,
        int maxAnisotropy, int maxMipLevel, float lodBias);

    /**
     * @brief Reads a sampler object's native state for focused validation.
     * @param sampler Sampler name.
     * @return Complete state snapshot.
     */
    [[nodiscard]] SamplerSnapshot GetSamplerSnapshotForTesting(unsigned int sampler);

    /**
     * @brief Returns the sampler object bound to a texture unit for focused validation.
     * @param slot Texture unit.
     * @return Bound sampler name, or zero.
     */
    [[nodiscard]] unsigned int GetBoundSamplerForTesting(int slot);

    /**
     * @brief Draws the unit-zero texture with a constant coordinate through rlgl's test batch.
     * @param u Horizontal texture coordinate.
     * @param v Vertical texture coordinate.
     * @param width Backbuffer width.
     * @param height Backbuffer height.
     */
    void DrawBoundTextureSampleForTesting(float u, float v, int width, int height);

    /** @brief Restores the platform default framebuffer as the active draw target. */
    void BindDefaultFramebuffer();
}
