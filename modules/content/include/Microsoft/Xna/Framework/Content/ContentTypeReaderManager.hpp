// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "System/Type.hpp"
#include <typeindex>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace detail { struct ContentTypeReaderRegistryAccess; }

    /**
     * @brief Matches FNA's real, public `Microsoft.Xna.Framework.Content.ContentTypeReaderManager`
     *        (`src/Content/ContentTypeReaderManager.cs`) -- this task (XNB-14/14A)
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
        /** @brief Factory signature: constructs one fresh reader instance (XNB-14A). */
        using ReaderFactory = std::function<std::unique_ptr<ContentTypeReaderBase>()>;

        /**
         * @brief FNA's `internal static void AddTypeCreator(string typeString, Func<
         *        ContentTypeReader> createFunction)`, made `CNAEXT`-callable from ordinary CNA
         *        code since CNA has no reflection fallback ahead of it.
         *
         * Registers a global, process-wide factory (XNB-14A) -- @p factory is called
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
         *        constructing an instance (XNB-67 -- the manifest's reader-usage
         *        summary needs this without any construction side effect).
         */
        CNAEXT [[nodiscard]] static bool IsRegistered(const std::string& canonicalName);

        /**
         * @brief Looks up a reader for the specified target type.
         *
         * XNA's `public ContentTypeReader GetTypeReader(Type targetType)` -- an *instance* method
         * on the manager a reader's Initialize() is handed, which is why this one is too, even
         * though CNA's registry is process-wide and the instance carries no state of its own.
         * CreateReaderForTargetTypeEXT() below is the same lookup without an instance.
         *
         * XNA's manager keeps a target-type-to-reader table alongside the name-to-reader one,
         * filled in as readers are created, because every reader knows its own `TargetType`. CNA's
         * readers know their target type as a canonical XNA *name* -- a `System::Type` is RTTI here
         * and carries no XNA name -- so the bridge between the two is recorded where both are still
         * known: `ContentTypeReader<T>`'s constructors call AssociateTargetType() with `typeid(T)`
         * and the target name they were given. A successfully returned registered reader also
         * associates that target name with its factory key; the two names need not match.
         * Lookup constructs a fresh reader from that factory. Removing or replacing a factory
         * withdraws its target associations. Explicit factory-key associations and factories
         * registered directly under a target name remain supported.
         *
         * @param targetType The type the reader will handle.
         * @return A freshly created reader for @p targetType, or nullptr when no associated
         *         factory is registered. Registration alone does not construct a reader.
         */
        [[nodiscard]] std::unique_ptr<ContentTypeReaderBase> GetTypeReader(
            const System::Type& targetType) const;

        /**
         * @brief CNAEXT: the same lookup as GetTypeReader(), without needing a manager instance.
         *
         * CNA's registry is process-wide, so the lookup needs no manager; this is what
         * ContentReader's ReadRawObject<T>() uses, and what GetTypeReader() forwards to.
         *
         * @param targetType The type the reader will handle.
         * @return A freshly created reader, or nullptr when the type has no associated reader.
         */
        CNAEXT [[nodiscard]] static std::unique_ptr<ContentTypeReaderBase>
        CreateReaderForTargetTypeEXT(const System::Type& targetType);

        /**
         * @brief Associates a C++ target type with its canonical target name or explicit factory key.
         *
         * CNAEXT: the association XNA gets for free from `ContentTypeReader.TargetType`. Called by
         * `ContentTypeReader<T>`'s constructors, which are the only place where the C++ type and the
         * canonical target name are both known. A repeat call for the same type replaces its name.
         * Registered reader creation separately associates target names with factory keys.
         *
         * @param targetType    The C++ type the reader handles.
         * @param canonicalName The canonical target name or explicit registered factory key;
         *                      an empty name is ignored.
         */
        CNAEXT static void AssociateTargetType(const System::Type& targetType,
                                               const std::string& canonicalName);

        /** @brief CNAEXT: forgets every target-type association -- for test isolation. */
        CNAEXT static void ClearTargetTypeAssociationsEXT();

    private:
        friend struct detail::ContentTypeReaderRegistryAccess;
        static std::unordered_map<std::string, std::shared_ptr<ReaderFactory>>& TypeCreators();
        static std::unordered_map<std::type_index, std::string>& TargetTypeNames();
    };

    // Declared built-in readers (docs/link-size-architecture.md). A Game declares every built-in
    // reader registered, as its constructor used to register them all, but a reader is installed
    // only when its type is first loaded -- or when the registry is asked about, or told to remove,
    // a reader by name (IsRegistered, CreateReader, RemoveTypeCreatorEXT, which install every
    // declared reader first, in their own translation unit). Once everything is installed a load
    // installs nothing, so a reader removed since stays removed until the next Game declares the
    // built-ins again, as its constructor re-registered them. The registry therefore answers as the
    // eager registration did, while a program links only the readers of the types it loads.
    // A standalone ContentManager also installs its loaded type's family, without declaring
    // unrelated families. Explicit removals survive later lazy family installation; explicit
    // re-registration or a new Game restores them.
    namespace detail
    {
        /**
         * @brief The load path's lookup: a reader from the registered factory, without installing
         *        declared readers (the readers of the type being loaded are installed before it).
         * @param canonicalName The canonical reader name.
         * @return A fresh reader, or nullptr when nothing is registered under the name.
         */
        std::unique_ptr<ContentTypeReaderBase> CreateRegisteredReader(const std::string& canonicalName);

        /**
         * @brief The canonical names a load could resolve now, without installing declared
         *        readers: a snapshot of the registry's keys, for tests and diagnostics.
         * @return The registered names, in no particular order.
         */
        [[nodiscard]] std::vector<std::string> RegisteredReaderNames();

        /** @brief Declares every built-in reader registered (Game's constructor). */
        void DeclareBuiltinXnbReaders();

        /** @brief Whether the built-in readers are declared. @return True after DeclareBuiltinXnbReaders() until ClearTypeCreators(). */
        [[nodiscard]] bool BuiltinXnbReadersDeclared();

        /** @brief Whether some declared reader may not be installed yet. @return True when declared and not all installed. */
        [[nodiscard]] bool DeclaredXnbReadersPending();

        /** @brief Records that every built-in reader is installed (RegisterAllBuiltInXnbReaders()). */
        void NoteEveryBuiltinXnbReaderRegistered();

        /**
         * @brief Runs one family's lazy reader registration unless all built-ins are installed.
         *        Works without a Game declaration and preserves explicitly withdrawn readers.
         *        Under a Game declaration, replaces provisional registrations that an eagerly
         *        installed built-in would have kept out; earlier explicit factories retain priority.
         * @param registerReaders The family's registration function.
         */
        void RegisterDeclaredXnbReaders(void (*registerReaders)());

        /**
         * @brief Installs every declared reader, once: runs @p registerEveryReader while the
         *        built-in readers are declared and not all installed.
         * @param registerEveryReader CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders, passed in so
         *        that only its callers link every reader.
         */
        void InstallDeclaredXnbReaders(void (*registerEveryReader)());

        /** @brief Marks the AddTypeCreator() calls made during its lifetime as CNA's built-in ones. */
        class BuiltinXnbRegistrationScope
        {
        public:
            /** @brief Opens the scope on this thread. */
            BuiltinXnbRegistrationScope();
            /** @brief Closes it. */
            ~BuiltinXnbRegistrationScope();
            BuiltinXnbRegistrationScope(const BuiltinXnbRegistrationScope&) = delete;
            BuiltinXnbRegistrationScope& operator=(const BuiltinXnbRegistrationScope&) = delete;
        };
    }
}
