// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformCapability.hpp"

#include <stdexcept>
#include <string>

namespace CNA::Platform {

    /**
     * @brief Thrown when a platform operation fails.
     *
     * Derives from `std::runtime_error`, matching the convention the rest of CNA already uses,
     * so a caller that only knows about `std::runtime_error` still catches it.
     *
     * ### What throws and what does not
     *
     * The contract distinguishes three kinds of outcome, and the distinction is deliberate:
     *
     * - **Throw** when an operation that was expected to succeed failed — window creation,
     *   context creation, subsystem acquisition. These are exceptional and a caller usually
     *   cannot continue.
     * - **Return a status** for the ordinary "not present" answers a caller is expected to
     *   branch on — no clipboard content, no gamepad at that index, a `TryGet*` accessor for a
     *   different windowing system. Throwing here would turn normal control flow into exceptions.
     * - **Refuse via NotSupported** when the implementation genuinely cannot do the thing at all.
     *   This is what a capability reported as `false` promises: a deterministic refusal, never a
     *   silent no-op.
     *
     * ### Underlying error text
     *
     * The implementation's own message (SDL's `SDL_GetError()`, an `errno` string, a terminal
     * capability report) is carried in the `detail` string. That keeps the diagnostic value of
     * the underlying library without putting its types in this header — a caller reads a
     * `std::string`, never an SDL symbol.
     */
    class PlatformException : public std::runtime_error
    {
    public:
        /**
         * @brief Creates an exception describing a failed platform operation.
         *
         * @param operation The operation that failed, in CNA's own vocabulary (e.g. `"CreateWindow"`).
         * @param detail The underlying implementation's error text, or empty when there is none.
         */
        PlatformException(const std::string& operation, const std::string& detail);

        /**
         * @brief Creates an exception describing a failed platform operation with no detail text.
         *
         * @param operation The operation that failed, in CNA's own vocabulary.
         */
        explicit PlatformException(const std::string& operation);

        /**
         * @brief Gets the operation that failed.
         *
         * @return The operation name this exception was constructed with.
         */
        [[nodiscard]] const std::string& GetOperation() const;

        /**
         * @brief Gets the underlying implementation's error text.
         *
         * @return The detail text, or an empty string when the failure carried none.
         */
        [[nodiscard]] const std::string& GetDetail() const;

    protected:
        /**
         * @brief Creates an exception from an already-composed message.
         *
         * For subclasses whose message is a complete sentence rather than an operation name.
         * Without this, a refusal would read `"CNA::Platform: CNA::Platform: X does not support
         * Y failed"` -- the base composing a second time around a sentence that already reads as
         * one.
         *
         * @param composedMessage The full message, used verbatim.
         * @param operation The operation name, for GetOperation().
         */
        PlatformException(const std::string& composedMessage, const std::string& operation,
                          int /*composedTag*/);

    private:
        std::string operation_;
        std::string detail_;
    };

    /**
     * @brief Thrown when a platform implementation cannot perform an operation at all.
     *
     * This is the deterministic refusal a `false` capability promises. It names the missing
     * capability, so a caller learns *which* capability was absent rather than only that
     * something failed — and so a test can assert that a capability-gated call refuses for the
     * right reason.
     */
    class PlatformNotSupportedException : public PlatformException
    {
    public:
        /**
         * @brief Creates a refusal for a capability the implementation does not provide.
         *
         * @param capability The capability that is missing.
         * @param platformName The implementation that refused, for the message (e.g. `"Terminal"`).
         */
        PlatformNotSupportedException(PlatformCapability capability, const std::string& platformName);

        /**
         * @brief Gets the capability that was missing.
         *
         * @return The capability this refusal names.
         */
        [[nodiscard]] PlatformCapability GetCapability() const;

    private:
        PlatformCapability capability_;
    };

} // namespace CNA::Platform
