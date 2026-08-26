// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <string>

#include "CNA/Content/Cnb/CnbDocument.hpp"

namespace Microsoft::Xna::Framework::Content { class ContentManager; }

namespace CNA::Content
{
    /**
     * @brief The process-wide table mapping a `.cnb` file's numeric asset type to the code that
     *        turns that file into a runtime object (plans/plan_cnb.md `CNBF-080`).
     *
     * This is CNB's whole extension mechanism, and it is deliberately far smaller than XNB's. A
     * `.cnb` file says which asset type it holds as one `u32`; the loader registered for that
     * number decodes it. There is no reflection, no assembly-qualified name, no reader
     * negotiation and no per-file reader table -- CNA owns both the writer and the reader, so the
     * only thing that has to travel in the file is the identifier.
     *
     * Registrations are process-wide and outlive any individual `ContentManager`, matching how
     * `ContentTypeReaderManager` already works on the `.xnb` side.
     */
    class CnbLoaderRegistry
    {
    public:
        /**
         * @brief Signature of a `.cnb` asset loader.
         *
         * Receives the already-validated container, the `ContentManager` that is performing the
         * load (so the loader can resolve the file's external references through the normal
         * cache), and the logical asset name for diagnostics. Returns the constructed asset boxed
         * in a `std::any` whose contained type must be exactly the `T` callers will ask for.
         */
        using LoaderFn = std::function<std::any(
            const Cnb::CnbDocument& document,
            Microsoft::Xna::Framework::Content::ContentManager& contentManager,
            const std::string& assetName)>;

        /**
         * @brief Registers a loader for one asset type identifier.
         *
         * Registering the same identifier twice with the same @p debugTypeName is accepted and has
         * no effect, so two static-initialisation paths registering the same built-in is not an
         * error. Registering the same identifier under a *different* name is refused: for a custom
         * identifier that is exactly the hash-collision case `CnbAssetTypeIdFromName()`'s 31-bit
         * space makes possible, and silently letting the second registration win would mean
         * loading one game type's file with another type's loader.
         *
         * @param assetTypeId   The identifier appearing in a `.cnb` header.
         * @param debugTypeName Human-readable name of the type, used in diagnostics and to detect
         *                      an identifier collision. Must not be empty.
         * @param loader        The loader. Must not be empty.
         * @throws std::invalid_argument if @p debugTypeName or @p loader is empty, or if
         *         @p assetTypeId is CnbAssetTypeId::Invalid.
         * @throws std::logic_error if @p assetTypeId is already registered under a different name.
         */
        static void Register(std::uint32_t assetTypeId, const std::string& debugTypeName,
                             LoaderFn loader);

        /**
         * @brief Withdraws the loader registered for @p assetTypeId.
         *
         * @param assetTypeId The identifier to withdraw.
         * @return True when a loader was registered and has now been removed; false when nothing
         *         was registered, which is not an error.
         */
        static bool Remove(std::uint32_t assetTypeId);

        /** @brief Withdraws every registration. Primarily for test isolation. */
        static void Clear();

        /**
         * @brief Whether a loader is registered for @p assetTypeId.
         *
         * @param assetTypeId The identifier to query.
         * @return True when a loader is registered.
         */
        [[nodiscard]] static bool IsRegistered(std::uint32_t assetTypeId);

        /**
         * @brief Looks up the loader registered for @p assetTypeId.
         *
         * @param assetTypeId The identifier to look up.
         * @return A pointer to the registered loader, or nullptr when none is registered. The
         *         pointer is invalidated by a later Register()/Remove()/Clear() call.
         */
        [[nodiscard]] static const LoaderFn* Find(std::uint32_t assetTypeId);

        /**
         * @brief The debug type name recorded when @p assetTypeId was registered.
         *
         * @param assetTypeId The identifier to look up.
         * @return The registered name, or an empty string when nothing is registered.
         */
        [[nodiscard]] static std::string RegisteredTypeName(std::uint32_t assetTypeId);

        /**
         * @brief Registers the loaders for every asset type CNA itself compiles to `.cnb`.
         *
         * Idempotent, and called automatically by every `ContentManager` constructor, so games
         * never need to call it. Exposed because a test that calls Clear() has to be able to put
         * the built-ins back.
         */
        static void RegisterBuiltIns();
    };
}
