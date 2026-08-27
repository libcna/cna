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
         * @brief Registers a **game extension's** loader for one custom asset type identifier.
         *
         * This is the only registration route a caller outside CNA has, and it accepts a custom
         * identifier only -- `CnbAssetTypeId::CustomRangeFirst` or above, which is what
         * `CnbAssetTypeIdFromName()` mints. CNA's built-in identifiers (`0x00000001`-`0x3FFFFFFF`)
         * and its reserved range (`0x40000000`-`0x7FFFFFFF`) belong to CNA, and there is
         * deliberately **no parameter** by which a caller can claim one: the built-in route is the
         * private RegisterBuiltIn(), reachable only by `ContentManager`
         * (plans/plan_cnb.md `CNBF-122`).
         *
         * That the boundary is a *compile-time* one matters. While it was an ownership argument on
         * this function, a game could pass the built-in value, register its own factory under a
         * built-in identifier's canonical name before any `ContentManager` existed, and -- because
         * the first equivalent registration is retained -- keep CNA's genuine loader from ever
         * being installed.
         *
         * Registering the same identifier twice with the same @p canonicalTypeName is accepted and
         * has no effect, so two initialisation paths registering the same game type is not an
         * error. Registering the same identifier under a *different* name is refused: that is
         * exactly the hash-collision case `CnbAssetTypeIdFromName()`'s 31-bit space makes possible,
         * and silently letting the second registration win would mean loading one game type's file
         * with another's loader.
         *
         * @param assetTypeId       The identifier appearing in a `.cnb` header. Must be a custom
         *                          identifier.
         * @param canonicalTypeName The type's canonical name. This is not merely a diagnostic
         *                          label: it is compared against the name the file itself carries
         *                          before dispatch (see ResolveForDocument()), so it must be
         *                          exactly the string passed to `CnbAssetTypeIdFromName()`. Must
         *                          not be empty.
         * @param loader            The loader. Must not be empty.
         * @throws std::invalid_argument if @p canonicalTypeName or @p loader is empty, if
         *         @p assetTypeId is CnbAssetTypeId::Invalid, if @p assetTypeId is not a custom
         *         identifier, or if @p canonicalTypeName does not hash to @p assetTypeId.
         * @throws std::logic_error if @p assetTypeId is already registered under a different name,
         *         or is already held by one of CNA's own built-in loaders.
         */
        static void Register(std::uint32_t assetTypeId, const std::string& canonicalTypeName,
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

    private:
        /**
         * @brief Registers one of **CNA's own** built-in loaders. Not reachable by a game
         *        (plans/plan_cnb.md `CNBF-122`).
         *
         * The counterpart of Register(), and the reason that one needs no ownership argument. It
         * accepts a built-in or reserved identifier and refuses a custom one, so the two routes
         * partition the identifier space between them and neither can be told to behave as the
         * other. Access is the boundary: this is private, and `ContentManager` -- which installs
         * the eight device-bound built-ins -- is the only type outside this class that can reach
         * it. A game therefore cannot register a built-in identifier at all, rather than being
         * refused at run time for passing the wrong enumerator.
         *
         * A repeat registration under the same name is tolerated, because every `ContentManager`
         * constructor repeats it; a game-owned registration for the same identifier is not, in
         * either order.
         *
         * @param assetTypeId       One of CNA's own identifiers; must not be a custom one.
         * @param canonicalTypeName The type's canonical .NET name. Must not be empty.
         * @param loader            The loader. Must not be empty.
         * @throws std::invalid_argument if @p assetTypeId is CnbAssetTypeId::Invalid, is a custom
         *         identifier, or if @p canonicalTypeName or @p loader is empty.
         * @throws std::logic_error if @p assetTypeId is already registered under a different name
         *         or by a game extension.
         */
        static void RegisterBuiltIn(std::uint32_t assetTypeId,
                                    const std::string& canonicalTypeName, LoaderFn loader);

        // The compile-time half of the CNA/game boundary (plans/plan_cnb.md `CNBF-122`).
        // ContentManager registers the eight built-in loaders that need a GraphicsDevice or the
        // manager itself, so it -- and nothing else -- reaches RegisterBuiltIn().
        friend class Microsoft::Xna::Framework::Content::ContentManager;
    };
}
