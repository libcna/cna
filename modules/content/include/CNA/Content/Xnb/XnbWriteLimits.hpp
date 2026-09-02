// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace CNA::Content::Xnb
{
    /**
     * @brief Failure raised by every `.xnb` write-side operation
     *        (plans/plan_xnapipeline.md `XNAP-002`).
     *
     * The write side keeps its own exception type rather than reusing the runtime reader's
     * `ContentLoadException`, because a build-time producer failure and a runtime load failure are
     * different events for a caller: one aborts a content build with a diagnosable input problem,
     * the other aborts a game's asset load. Both remain catchable as `std::runtime_error`.
     */
    class XnbWriteException : public std::runtime_error
    {
    public:
        /**
         * @brief Creates a write failure carrying a complete diagnostic message.
         *
         * @param message Human-readable reason, already including any component context.
         */
        explicit XnbWriteException(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };

    /**
     * @brief Bounds every count-driven `.xnb` write consults before emitting or growing a buffer.
     *
     * The deliberate mirror of `CNA::Internal::Xnb::XnbReadLimits`: a producer that will happily
     * emit a two-gigabyte collection count writes a file its own reader then refuses, which is a
     * silent, late, confusing failure. Checking the same ceilings on the way out turns that into
     * an immediate, precise build error naming the offending field.
     *
     * The defaults are deliberately never *looser* than the reader's defaults, so anything this
     * writer accepts, CNA can read back.
     */
    struct XnbWriteLimits
    {
        /** @brief Largest complete `.xnb` file this writer will produce, in bytes. */
        std::int32_t maxFileSize = 64 * 1024 * 1024;

        /** @brief Largest single string this writer will emit, in bytes. */
        std::int32_t maxStringBytes = 1 * 1024 * 1024;

        /** @brief Largest type-writer table this writer will emit. */
        std::int32_t maxTypeWriterCount = 4096;

        /** @brief Largest shared-resource table this writer will emit. */
        std::int32_t maxSharedResourceCount = 1'000'000;

        /** @brief Largest array/list/dictionary element count this writer will emit. */
        std::int32_t maxCollectionElementCount = 10'000'000;

        /** @brief Deepest nested-object graph this writer will follow before refusing. */
        std::int32_t maxObjectNestingDepth = 256;

        /** @brief Largest single raw byte payload (texture level, sample block) this writer emits. */
        std::int32_t maxPayloadBytes = 256 * 1024 * 1024;
    };

    /**
     * @brief Returns the process-wide default write limits.
     *
     * @return A reference valid for the lifetime of the process.
     */
    [[nodiscard]] inline const XnbWriteLimits& DefaultXnbWriteLimits()
    {
        static const XnbWriteLimits limits;
        return limits;
    }

    /**
     * @brief Multiplies non-negative factors, refusing instead of overflowing.
     *
     * The write-side counterpart of `CNA::Internal::Xnb::CheckedMultiplyOrThrow()`. Expected
     * payload sizes are products of caller-supplied dimensions, and a build tool is routinely
     * pointed at machine-generated or third-party content, so the product is validated rather
     * than assumed.
     *
     * @param factors Non-negative factors to multiply, in any order.
     * @param context Component name used verbatim in the failure message.
     * @return The exact product, guaranteed representable in `std::int64_t`.
     * @throws XnbWriteException if a factor is negative or the product would overflow.
     */
    [[nodiscard]] std::int64_t XnbCheckedMultiply(std::initializer_list<std::int64_t> factors,
                                                  const std::string& context);

    /**
     * @brief Adds non-negative values, refusing instead of overflowing.
     *
     * @param left First addend.
     * @param right Second addend.
     * @param context Component name used verbatim in the failure message.
     * @return The exact sum, guaranteed representable in `std::int64_t`.
     * @throws XnbWriteException if either value is negative or the sum would overflow.
     */
    [[nodiscard]] std::int64_t XnbCheckedAdd(std::int64_t left, std::int64_t right,
                                             const std::string& context);
}
