// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <cstddef>
#include <map>
#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Compiles a named pass shader once per device and hands the same effect back after.
     *
     * plan_modern.md `MOD-210`. Two `BloomPass` objects on one device — a game with a split screen,
     * a chain rebuilt after a resize, an editor preview beside the scene — otherwise compile the
     * same three programs twice. Compilation is a load-time cost rather than a frame-time one, so
     * this is not about frame rate: it is about a second pass costing what a second pass should.
     *
     * **Keyed by name, not by source.** A key like `"BloomPass.blur"` is compared once; hashing two
     * kilobytes of GLSL to discover it is the same GLSL is work to avoid work. The consequence is
     * a rule rather than a check: a name must mean one shader. Handing the same name two different
     * sources returns the first, and is a bug in the caller.
     *
     * The factory owns every effect it compiled and outlives the passes that borrow them, so a pass
     * must not delete what it is handed. Where a pass needs to own its effect — one it configures
     * per instance, for example — it should build it directly and skip the cache.
     */
    class ShaderEffectFactory
    {
    public:
        /**
         * @brief Creates a factory for one device.
         *
         * @param device The device every cached effect is compiled on.
         */
        explicit ShaderEffectFactory(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the factory and every effect it compiled. */
        ~ShaderEffectFactory();

        ShaderEffectFactory(const ShaderEffectFactory&)            = delete;
        ShaderEffectFactory& operator=(const ShaderEffectFactory&) = delete;

        /**
         * @brief Returns the effect for @p name, compiling it on first request.
         *
         * @param name             A stable key, e.g. `"BloomPass.blur"`. Must not be empty.
         * @param vertexSource     GLSL vertex source, used only on the first request for @p name.
         * @param fragmentSource   GLSL fragment source, likewise.
         * @return The effect, owned by this factory. Never null — a shader that fails to compile
         *         is still returned, because `ShaderEffect` reports that through `IsEffectValid()`
         *         and a null here would make every caller handle a second failure mode.
         * @throws std::invalid_argument If @p name is empty.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* acquire(
            const std::string& name, const std::string& vertexSource,
            const std::string& fragmentSource);

        /**
         * @brief Whether @p name has already been compiled.
         *
         * @param name The key to look for.
         * @return True when a later `acquire` with this name will not compile anything.
         */
        [[nodiscard]] bool contains(const std::string& name) const;

        /** @brief How many distinct shaders this factory has compiled. @return The count. */
        [[nodiscard]] std::size_t getCompileCount() const;

        /**
         * @brief Releases every cached effect.
         *
         * Anything still holding a pointer from @ref acquire is left dangling, so this is for
         * device teardown, not for reclaiming memory mid-frame.
         */
        void clear();

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::map<std::string, std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect>>
            effects_;
        std::size_t compileCount_ = 0;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
