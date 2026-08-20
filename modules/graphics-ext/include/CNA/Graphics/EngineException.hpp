// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "System/Exception.hpp"

#include <string>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The exception the engine layer throws when a renderer cannot do what was asked.
     *
     * plans/plan_modern.md `MOD-9`. Nearly every failure in this layer is the same shape — a subsystem
     * wanted something the active renderer does not provide — and the useful message names all
     * three parts: which subsystem asked, what it wanted, and which renderer said no. Assembling
     * that by hand at each throw site produced three slightly different sentences for the same
     * situation, so @ref notSupported builds it once.
     *
     * The layer's preferred behaviour is still to *ask* rather than throw: every subsystem exposes
     * an `isSupported()` and degrades quietly where it sensibly can. This exception is for the
     * paths where there is no sensible degradation.
     */
    class EngineException : public System::Exception
    {
    public:
        /**
         * @brief Creates an exception with a ready-made message.
         *
         * @param message A description of the failure.
         */
        explicit EngineException(const std::string& message);

        /**
         * @brief Creates the standard "this renderer cannot do that" exception.
         *
         * The message reads `"<subsystem>: <what> is not supported by the <renderer> renderer"`.
         *
         * @param subsystem    The engine-layer subsystem that asked, e.g. `"BloomPass"`.
         * @param what         What it needed, as a noun phrase, e.g. `"float render targets"`.
         * @param rendererName The active renderer's name, from
         *                     `GraphicsDevice::GetGraphicsRendererName()`.
         * @return The exception, ready to throw.
         */
        [[nodiscard]] static EngineException notSupported(const std::string& subsystem,
                                                          const std::string& what,
                                                          const std::string& rendererName);

        /** @brief The subsystem that raised this, or an empty string when it was not recorded. */
        [[nodiscard]] const std::string& getSubsystemProperty() const;

        /** @brief What was needed, or an empty string when it was not recorded. */
        [[nodiscard]] const std::string& getRequirementProperty() const;

        /** @brief The renderer that refused, or an empty string when it was not recorded. */
        [[nodiscard]] const std::string& getRendererNameProperty() const;

    private:
        EngineException(const std::string& message, std::string subsystem, std::string requirement,
                        std::string rendererName);

        std::string subsystem_;
        std::string requirement_;
        std::string rendererName_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
