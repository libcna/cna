// SPDX-License-Identifier: MS-PL
#pragma once

#include <easygl/Texture.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace CNA::Internal::Renderers::EasyGL
{
    /** @brief Texture-binding proofs for one renderer context, serialized by its context lease. */
    class EasyGLTextureBindingState final
    {
    public:
        /**
         * @brief Selects a unit and binds unless its target already provably holds the name.
         * @param unit Native texture unit, including the Texture0 enum base.
         * @param target Binding target (2D, cube or 3D).
         * @param name GL texture name; zero is a known unbound target.
         */
        void Bind(::easygl::TextureUnit unit, ::easygl::TextureTarget target, unsigned int name);
        /** @brief Forgets all proofs after an unobserved native mutation or context loss. */
        void Invalidate() noexcept;
        /**
         * @brief Forgets a name before deletion or handle abandonment, including all aliases.
         * @param name Old GL texture name.
         */
        void Forget(unsigned int name) noexcept;
        /** @brief Invalidates proofs and counts one bind sent outside Bind. */
        void UntrackedBindSent() noexcept;
        /**
         * @brief Gets the number of texture binds actually sent since construction.
         * @return Cumulative bind count, retained across context recovery.
         */
        [[nodiscard]] std::uint64_t BindCount() const noexcept;

    private:
        std::array<std::array<std::optional<unsigned int>, 3>, 20> bindings_{};
        std::uint64_t bindCount_ = 0;
    };

    /**
     * @brief CNA-owned texture that observes binding mutations without changing easy-gl.
     *
     * Image/parameter/FBO APIs otherwise keep easy-gl's contract. A missing record uses the
     * uncached path. Native mutations through a base reference require explicit invalidation.
     */
    class EasyGLTrackedTexture final : public ::easygl::Texture
    {
    public:
        /**
         * @brief Creates an empty texture wrapper with optional context binding tracking.
         * @param bindings Owning renderer's record, independent of the recovery registry.
         */
        explicit EasyGLTrackedTexture(std::shared_ptr<EasyGLTextureBindingState> bindings = {});
        /** @brief Forgets the native name before the GL texture is deleted. */
        ~EasyGLTrackedTexture();
        /**
         * @brief Transfers a texture and its binding record.
         * @param other Texture to empty.
         */
        EasyGLTrackedTexture(EasyGLTrackedTexture&& other) noexcept;
        /**
         * @brief Deletes the current texture and transfers another texture and its record.
         * @param other Texture to empty.
         * @return This wrapper.
         */
        EasyGLTrackedTexture& operator=(EasyGLTrackedTexture&& other) noexcept;
        /**
         * @brief Associates an empty texture with its renderer's binding record.
         * @param bindings Context record to use for future operations.
         */
        void SetBindingState(std::shared_ptr<EasyGLTextureBindingState> bindings);
        /**
         * @brief Gets this texture's context record.
         * @return Shared context record, or null for an uncached texture.
         */
        [[nodiscard]] std::shared_ptr<EasyGLTextureBindingState> BindingState() const;
        /**
         * @brief Selects unit zero and binds this texture to its target.
         * @param target Texture binding target.
         */
        void bind(::easygl::TextureTarget target = ::easygl::TextureTarget::Texture2D) const;
        /**
         * @brief Selects a unit and binds this texture to its target.
         * @param unit Native texture unit.
         * @param target Texture binding target.
         */
        void active_bind(::easygl::TextureUnit unit, ::easygl::TextureTarget target) const;
        using ::easygl::Texture::set_image_2d;
        /**
         * @brief Uploads RGBA bytes using easy-gl's internally binding overload.
         * @param target Texture image target.
         * @param level Mip level.
         * @param width Image width.
         * @param height Image height.
         * @param data Image bytes, or null to allocate.
         */
        void set_image_2d(::easygl::TextureTarget target, int level, int width, int height,
                          const void* data);
        /** @brief Forgets all aliases before deleting the native texture. */
        void destroy() noexcept;
        /** @brief Forgets all aliases and abandons the handle without a GL call. */
        void reset_handle_no_gl() noexcept;

    private:
        std::shared_ptr<EasyGLTextureBindingState> bindings_;
    };
}
