// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "CNA/Content/Cnb/CnbDocument.hpp"

namespace Microsoft::Xna::Framework::Content { class ContentManager; }

namespace CNA::Content
{
    /**
     * @brief Who a `.cnb` loader registration belongs to (plans/plan_cnb.md `CNBF-119`).
     *
     * The registry is one process-wide table shared by CNA's own built-in loaders and by any a
     * game adds, and the two are not interchangeable: CNA assigns and freezes the built-in
     * identifiers, and installs their loaders from every `ContentManager` constructor. Without
     * this distinction a game registering a built-in identifier under its canonical name was
     * accepted, and which factory ended up in the table depended on which call ran first --
     * silently, either way.
     */
    enum class CnbLoaderOwnership
    {
        /**
         * @brief A game-defined type, which must use a custom identifier
         *        (`CnbAssetTypeId::CustomRangeFirst` or above). The default.
         */
        GameExtension,

        /**
         * @brief One of CNA's own built-in asset types. Not available to a game: CNA's built-in
         *        identifiers are outside the custom range, and this value is refused for one
         *        inside it.
         */
        CnaBuiltIn,
    };

    /**
     * @brief The process-wide table mapping a `.cnb` file's numeric asset type to the code that
     *        turns that file into a runtime object (plans/plan_cnb.md `CNBF-080`).
     *
     * This is CNB's whole extension mechanism, and it is deliberately far smaller than XNB's. A
     * `.cnb` file says which asset type it holds as one `u32`; the loader registered for that
     * number decodes it. There is no reflection, no assembly-qualified name, no reader
     * negotiation and no per-file reader table.
     *
     * Two properties of this class are load-bearing and were both added by the hardening pass
     * (`CNBF-H002`/`CNBF-H003`):
     *
     * - **Every accessor is safe to call from several threads at once.** The table is guarded by a
     *   `std::shared_mutex`, and lookups return the loader **by value** rather than a pointer into
     *   the table -- a pointer would be invalidated by any later registration that rehashes, and
     *   the caller would have no way to know.
     * - **Custom asset types are identified by name as well as by number.** A custom identifier is
     *   a 31-bit hash, so two different game types can collide; the numeric match alone is
     *   therefore not enough to prove a file belongs to a loader. See ResolveForDocument().
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
         * Registering the same identifier twice with the same @p canonicalTypeName **and** the
         * same @p ownership is accepted and has no effect, so two static-initialisation paths
         * registering the same built-in is not an error. Registering the same identifier under a
         * *different* name is refused: for a custom identifier that is exactly the hash-collision
         * case `CnbAssetTypeIdFromName()`'s 31-bit space makes possible, and silently letting the
         * second registration win would mean loading one game type's file with another's loader.
         *
         * The @p ownership half of that rule is what stops a repeat registration being tolerated
         * across the boundary between CNA and a game (plans/plan_cnb.md `CNBF-119`): the first
         * caller's factory is retained, so an extension registering a built-in identifier under
         * its canonical name would have won or lost depending purely on which ran first.
         *
         * @param assetTypeId       The identifier appearing in a `.cnb` header.
         * @param canonicalTypeName The type's canonical name. For a **custom** identifier this is
         *                          not merely a diagnostic label: it is compared against the name
         *                          the file itself carries before dispatch (see
         *                          ResolveForDocument()), so it must be exactly the string passed
         *                          to `CnbAssetTypeIdFromName()`. Must not be empty.
         * @param loader            The loader. Must not be empty.
         * @param ownership         Who the registration belongs to. The default,
         *                          CnbLoaderOwnership::GameExtension, accepts a **custom**
         *                          identifier only -- CNA's built-in identifiers and its reserved
         *                          range belong to CNA, whose own registrations pass
         *                          CnbLoaderOwnership::CnaBuiltIn and are in turn refused a custom
         *                          identifier. A repeat registration is tolerated only when the
         *                          name **and** the ownership both match, so an extension cannot
         *                          quietly inherit a built-in's slot or be inherited by one
         *                          (plans/plan_cnb.md `CNBF-119`).
         * @throws std::invalid_argument if @p canonicalTypeName or @p loader is empty, if
         *         @p assetTypeId is CnbAssetTypeId::Invalid, if @p assetTypeId is outside the
         *         range @p ownership may claim, or if @p assetTypeId is a custom identifier that
         *         @p canonicalTypeName does not actually hash to.
         * @throws std::logic_error if @p assetTypeId is already registered under a different name
         *         or a different ownership.
         */
        static void Register(std::uint32_t assetTypeId, const std::string& canonicalTypeName,
                             LoaderFn loader,
                             CnbLoaderOwnership ownership = CnbLoaderOwnership::GameExtension);

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
         * Returns a **copy**. A pointer into the table would be invalidated by any subsequent
         * registration, and by the time a caller invoked it the object could be gone.
         *
         * This performs no type-name check, so it is the wrong entry point for loading a file --
         * use ResolveForDocument() for that. It exists for tooling and tests that want to ask
         * whether something is registered without holding a document.
         *
         * @param assetTypeId The identifier to look up.
         * @return The registered loader, or `std::nullopt` when none is registered.
         */
        [[nodiscard]] static std::optional<LoaderFn> Find(std::uint32_t assetTypeId);

        /**
         * @brief The canonical type name recorded when @p assetTypeId was registered.
         *
         * @param assetTypeId The identifier to look up.
         * @return The registered name, or an empty string when nothing is registered.
         */
        [[nodiscard]] static std::string RegisteredTypeName(std::uint32_t assetTypeId);

        /**
         * @brief Resolves the loader that may decode @p document, proving identity as well as
         *        matching the number (plans/plan_cnb.md `CNBF-H002`).
         *
         * For a **built-in** asset type the numeric identifier is authoritative: CNA assigns those
         * itself and they are frozen, so a match is a proof of identity and the file's optional
         * `CMET` type name is not consulted.
         *
         * For a **custom** asset type it is not. A custom identifier is
         * `FNV-1a-32(name) | 0x80000000`, i.e. 31 usable bits, so two unrelated game types can
         * legitimately hash to the same number. This therefore additionally requires that the file
         * carry a canonical type name in its `CMET` chunk and that the name equal the one the
         * loader was registered under. A file whose number matches but whose name does not is
         * refused: it is a different type that happens to collide, and decoding it with this
         * loader would be a silent misinterpretation of someone's content.
         *
         * @param document The container to resolve a loader for.
         * @return A copy of the registered loader, safe to invoke after this call returns.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException when no loader is
         *         registered for the file's type, when a custom-typed file carries no canonical
         *         type name, or when that name disagrees with the registered one.
         */
        [[nodiscard]] static LoaderFn ResolveForDocument(const Cnb::CnbDocument& document);

        /**
         * @brief Registers the built-in loaders that need nothing but their own codec: `Curve` and
         *        `AnimationClip`.
         *
         * **Not every built-in type** (plans/plan_cnb.md `CNBF-119`) -- the name is inherited from
         * when those were the only two. The other eight built-in loaders (`Model`, `Texture2D`,
         * `TextureCube`, `Texture3D`, `SpriteFont`, `SoundEffect`, `Song`, `Video`) are registered
         * by `ContentManager::RegisterBuiltinLoaders()` instead, because each of them constructs a
         * runtime object that needs a `GraphicsDevice` or the `ContentManager` itself, neither of
         * which this module can reach. A test that calls Clear() and wants the whole table back
         * must construct a `ContentManager`, not merely call this.
         *
         * Idempotent, thread-safe, and called automatically by every `ContentManager` constructor,
         * so games never need to call it.
         */
        static void RegisterBuiltIns();
    };
}
