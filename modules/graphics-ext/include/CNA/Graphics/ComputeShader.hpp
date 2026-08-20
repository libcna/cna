// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
}
namespace CNA::Internal::Renderers { class IComputeShaderRenderer; }

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    class StorageBuffer;

    /**
     * @brief One compute program, and the dispatches of it.
     *
     * plans/plan_modern.md `MOD-1521`. The engine-layer face of `IComputeShaderRenderer`: it owns the
     * compiled program, validates a dispatch against the device's real limits before submitting
     * it, and inserts the barrier a caller most often forgets.
     *
     * ```
     * CNA::Graphics::ComputeShader doubler(device, source);
     * doubler.bindStorageBuffer(0, buffer);
     * doubler.setUniform("uCount", 1024);
     * doubler.dispatch(1024 / 64);          // the shader declares local_size_x = 64
     * ```
     *
     * On a renderer without compute the constructor throws rather than producing an object whose
     * every method silently does nothing -- a dispatch that quietly did not happen is the hardest
     * kind of bug to see.
     */
    class ComputeShader
    {
    public:
        /**
         * @brief Compiles a compute program.
         *
         * @param device The device to compile on.
         * @param source The compute-shader source, in the renderer's own language.
         * @throws System::NotSupportedException If the renderer has no compute support; the
         *         message names the renderer.
         * @throws std::runtime_error If the program did not compile; the message carries the
         *         compiler log.
         */
        ComputeShader(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      const std::string& source);

        /** @brief Releases the program. */
        ~ComputeShader();

        ComputeShader(const ComputeShader&)            = delete;
        ComputeShader& operator=(const ComputeShader&) = delete;

        /**
         * @brief Sets an integer uniform.
         *
         * @param name  The uniform's name.
         * @param value The value.
         */
        void setUniform(const std::string& name, int value);

        /**
         * @brief Sets a float uniform.
         *
         * @param name  The uniform's name.
         * @param value The value.
         */
        void setUniform(const std::string& name, float value);

        /**
         * @brief Binds a storage buffer to one of the program's binding points.
         *
         * @param binding The binding index the shader declares; must not be negative.
         * @param buffer  The buffer.
         * @throws std::invalid_argument If @p binding is negative.
         */
        void bindStorageBuffer(int binding, StorageBuffer& buffer);

        /**
         * @brief Binds a texture the shader will sample, and sets its sampler uniform.
         *
         * plans/plan_modern.md `MOD-1552`. Sampling, unlike an image binding, needs nothing special of
         * the texture, so this is the route that works on every context with compute at all.
         *
         * @param unit        The texture unit to bind to; must not be negative.
         * @param samplerName The `sampler2D` uniform's name, which is set to @p unit.
         * @param texture     The texture.
         * @throws std::invalid_argument If @p unit is negative.
         */
        void bindTexture(int unit, const std::string& samplerName,
                         Microsoft::Xna::Framework::Graphics::Texture2D& texture);

        /**
         * @brief Returns whether this device can bind a `Texture2D` as a compute image at all.
         *
         * plans/plan_modern.md `MOD-1514`. Distinct from having compute: GL ES 3.1 requires an immutable
         * texture for an image binding and CNA allocates its textures mutably, so an ES context
         * with full compute support still answers false here. Where it does, route compute output
         * through a @ref StorageBuffer instead.
         *
         * @return True when @ref bindImage will work.
         */
        [[nodiscard]] bool isImageBindingSupported() const;

        /**
         * @brief Binds a texture as an image the shader can read or write.
         *
         * @param unit    The image unit the shader declares; must not be negative.
         * @param texture The texture.
         * @param access  How the shader will use it.
         * @throws std::invalid_argument If @p unit is negative.
         * @throws System::NotSupportedException If @ref isImageBindingSupported is false -- a
         *         binding the driver would reject is refused here, where the reason can be said.
         */
        void bindImage(int unit, Microsoft::Xna::Framework::Graphics::Texture2D& texture,
                       CNA::GraphicsImageAccess access);

        /**
         * @brief Runs the program over a grid of work groups.
         *
         * plans/plan_modern.md `MOD-1523`: the counts are checked against the device's real limits
         * *before* submission, so an over-large dispatch is an exception naming the axis and the
         * limit rather than a driver error, a lost context, or -- worst of all -- silence.
         *
         * @param groupsX Work groups on x; must be positive.
         * @param groupsY Work groups on y; must be positive.
         * @param groupsZ Work groups on z; must be positive.
         * @throws std::invalid_argument If a count is not positive or exceeds the device limit.
         */
        void dispatch(int groupsX, int groupsY = 1, int groupsZ = 1);

        /**
         * @brief Orders memory access after a dispatch.
         *
         * plans/plan_modern.md `MOD-1524`. @ref dispatch already issues the two barriers a compute pass
         * almost always needs -- `ShaderStorage` and `ShaderImageAccess` -- so results are visible
         * to the *next dispatch* and to a `getBytes` read-back without the caller doing anything.
         * What it cannot know is how the data will be consumed by the rest of the pipeline: a
         * buffer about to be drawn as vertices needs `VertexAttribArray`, a texture about to be
         * sampled needs `TextureFetch`, and those are this method.
         *
         * @param bits Which accesses to order.
         */
        void barrier(CNA::GraphicsMemoryBarrier bits);

        /** @brief Returns whether the program compiled and is usable. */
        [[nodiscard]] bool isValid() const;

        /** @brief Returns the compiler log from a failed compile; empty after a successful one. */
        [[nodiscard]] const std::string& getCompileError() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<CNA::Internal::Renderers::IComputeShaderRenderer> renderer_;
        std::string compileError_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
