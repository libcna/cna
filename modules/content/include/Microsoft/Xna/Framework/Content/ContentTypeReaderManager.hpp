// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief Matches FNA's real, public `Microsoft.Xna.Framework.Content.ContentTypeReaderManager`
     *        (`src/Content/ContentTypeReaderManager.cs`) -- this task (plans/plan_xnb.md XNB-14/14A)
     *        implements its type-creator registration surface; `LoadAssetReaders`/`GetTypeReader`
     *        (which need a real `ContentReader` to exist) land with that class (XNB-15/16).
     *
     * FNA resolves each type-reader table entry via .NET reflection (`Type.GetType(name)` +
     * `Activator`-style construction), with a static `Dictionary<string, Func<ContentTypeReader>>`
     * fallback (`typeCreators`) for AOT platforms where reflection-based discovery doesn't work.
     * CNA has no reflection at all, so that fallback dictionary becomes the *only* path: every
     * reader CNA supports must be registered via AddTypeCreator(), keyed by the XNB-5 canonical
     * name (see NormalizeXnbTypeReaderName()). This mirrors FNA's own real method name/shape
     * exactly, just promoted from an AOT-only fallback to CNA's sole registration mechanism.
     */
    class ContentTypeReaderManager
    {
    public:
        /** @brief Factory signature: constructs one fresh reader instance (plans/plan_xnb.md XNB-14A). */
        using ReaderFactory = std::function<std::unique_ptr<ContentTypeReaderBase>()>;

        /**
         * @brief FNA's `internal static void AddTypeCreator(string typeString, Func<
         *        ContentTypeReader> createFunction)`, made `CNAEXT`-callable from ordinary CNA
         *        code since CNA has no reflection fallback ahead of it.
         *
         * Registers a global, process-wide factory (plans/plan_xnb.md XNB-14A) -- @p factory is called
         * once per `.xnb` file that references @p canonicalName, producing one fresh, unshared
         * reader instance per file (see CreateReader()). Matches FNA's own behavior of silently
         * ignoring a repeat registration of the same key (`if (!typeCreators.ContainsKey(...))`),
         * rather than throwing -- a game/CNA subsystem registering the same built-in reader twice
         * (e.g. via two static-init paths) should not be a hard error.
         *
         * @param canonicalName The XNB-5 canonical reader name this factory handles.
         * @param factory       Constructs one fresh reader instance; must not be empty.
         */
        CNAEXT static void AddTypeCreator(const std::string& canonicalName, ReaderFactory factory);

        /** @brief FNA's `internal static void ClearTypeCreators()` -- primarily for test isolation. */
        CNAEXT static void ClearTypeCreators();

        /**
         * @brief CNAEXT single-key removal, which FNA has no counterpart for.
         *
         * FNA's registry is populated once by static initialization and only ever cleared
         * wholesale, so it never needed one. A registration that belongs to something with a
         * *lifetime* does: a caller-supplied reader registered from outside this library -- a
         * language binding's custom content type, for one -- has to be withdrawn when its owner
         * goes away, and ClearTypeCreators() would take every built-in reader with it.
         *
         * @param canonicalName The XNB-5 canonical reader name to withdraw.
         * @return True when a factory was registered under that name and has now been removed;
         *         false when nothing was registered, which is not an error.
         */
        CNAEXT static bool RemoveTypeCreatorEXT(const std::string& canonicalName);

        /**
         * @brief CNAEXT convenience wrapping the registered factory lookup: constructs a fresh
         *        reader instance for @p canonicalName, or returns null if nothing is registered
         *        under that name.
         *
         * @param canonicalName The XNB-5 canonical reader name to create an instance for.
         * @return A freshly constructed reader instance, or nullptr if unregistered.
         */
        CNAEXT static std::unique_ptr<ContentTypeReaderBase> CreateReader(const std::string& canonicalName);

        /**
         * @brief CNAEXT query-only check: is a factory registered for @p canonicalName, without
         *        constructing an instance (plans/plan_xnb.md XNB-67 -- the manifest's reader-usage
         *        summary needs this without any construction side effect).
         */
        CNAEXT [[nodiscard]] static bool IsRegistered(const std::string& canonicalName);

    private:
        static std::unordered_map<std::string, ReaderFactory>& TypeCreators();
    };
}
