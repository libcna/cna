// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Xnb/XnbReadLimits.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/BinaryReader.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingSphere;
    struct Color;
    struct Matrix;
    struct Quaternion;
    struct Vector2;
    struct Vector3;
    struct Vector4;
}

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager;

    namespace Detail
    {
        template <typename T>
        struct IsDisposableSharedPtr : std::false_type {};

        template <typename U>
        struct IsDisposableSharedPtr<std::shared_ptr<U>>
            : std::bool_constant<std::is_base_of_v<System::IDisposable, U>> {};
    }

    /**
     * @brief Matches FNA's real, public-sealed `Microsoft.Xna.Framework.Content.ContentReader`
     *        (`src/Content/ContentReader.cs`) -- the object-graph reader passed to every
     *        `ContentTypeReader<T>::Read()`.
     *
     * Implements plans/plan_xnb.md XNB-15 (shared-resource fixups), XNB-16/16B (1-based
     * type-reader-index root/nested-object dispatch, reader-version enforcement), and XNB-35
     * (`ReadExternalReference<T>()`, which needs a working `ContentManager::Load<T>()` round-trip
     * -- see that method's own docs).
     *
     * @p manager may be null (unlike real XNA, where it is always a real instance): this class is
     * usable standalone, ahead of `ContentManager`'s own `.xnb` integration (Phase B2, XNB-17B).
     * A null @p manager only matters if `ReadExternalReference<T>()` or the `ContentManager`-
     * fallback path of `RecordDisposable()` is actually reached.
     */
    class ContentReader : public System::IO::BinaryReader
    {
    public:
        /** @brief Callback invoked to record a disposable result, mirroring FNA's `Action<IDisposable>`. */
        using RecordDisposableFn = std::function<void(std::shared_ptr<System::IDisposable>)>;

        /**
         * @brief FNA's internal constructor.
         *
         * @param manager                Owning ContentManager, or nullptr (see class docs).
         * @param stream                 Backing stream, positioned immediately after the `.xnb`
         *                               container header and its type-reader table have both
         *                               been consumed by the caller -- @ref InitializeTypeReaders
         *                               re-derives the table itself from @p stream's current
         *                               position, matching FNA's own `InitializeTypeReaders`.
         * @param assetName              Logical asset name, used for diagnostics.
         * @param version                Container version (4 or 5) from the `.xnb` header.
         * @param platform               Platform identifier byte from the `.xnb` header.
         * @param recordDisposableObject Optional override for disposal tracking; falls back to
         *                               @p manager if null and @p manager is non-null, else is a
         *                               no-op (see RecordDisposable()).
         * @param limits                 Bounds applied to the type-reader/shared-resource counts
         *                               (plans/plan_xnb.md XNB-10A).
         */
        ContentReader(
            ContentManager* manager,
            System::IO::Stream* stream,
            std::string assetName,
            int version,
            char platform,
            RecordDisposableFn recordDisposableObject = nullptr,
            const CNA::Internal::Xnb::XnbReadLimits& limits = CNA::Internal::Xnb::DefaultXnbReadLimits());

        /** @brief FNA's `ContentReader.ContentManager` property. May be null (see class docs). */
        [[nodiscard]] ContentManager* getContentManagerProperty() const { return contentManager_; }

        /** @brief FNA's `ContentReader.AssetName` property. */
        [[nodiscard]] const std::string& getAssetNameProperty() const { return assetName_; }

        /** @brief CNAEXT diagnostic accessor for FNA's `internal int version` field. */
        CNAEXT [[nodiscard]] int getVersionProperty() const { return version_; }

        /** @brief CNAEXT diagnostic accessor for FNA's `internal char platform` field. */
        CNAEXT [[nodiscard]] char getPlatformProperty() const { return platform_; }

        // -- Public Read Methods (FNA's own "Public Read Methods" region) --

        /** @brief FNA's `ContentReader.ReadMatrix()`. */
        [[nodiscard]] Matrix ReadMatrix();

        /** @brief FNA's `ContentReader.ReadQuaternion()`. */
        [[nodiscard]] Quaternion ReadQuaternion();

        /** @brief FNA's `ContentReader.ReadVector2()`. */
        [[nodiscard]] Vector2 ReadVector2();

        /** @brief FNA's `ContentReader.ReadVector3()`. */
        [[nodiscard]] Vector3 ReadVector3();

        /** @brief FNA's `ContentReader.ReadVector4()`. */
        [[nodiscard]] Vector4 ReadVector4();

        /** @brief FNA's `ContentReader.ReadColor()`. */
        [[nodiscard]] Color ReadColor();

        /**
         * @brief FNA's `T ReadExternalReference<T>()`: reads a relative-path string naming a
         *        sibling asset and loads it through the owning `ContentManager` (plans/plan_xnb.md
         *        XNB-35).
         *
         * The path is resolved relative to this `ContentReader`'s own asset name (matching FNA's
         * `MonoGame.Utilities.FileHelpers.ResolveRelativePath`), then loaded via
         * `ContentManager::Load<T>()` -- so it goes through the exact same `.xnb`/`.cnj`/native
         * resolution order a direct `Load<T>()` call would. As a hardening addition with no FNA
         * equivalent (FNA simply lets the OS fail to find an escaping path), a reference that is
         * itself absolute, or that resolves above the content root's own logical space, is
         * rejected outright rather than attempting to load whatever happens to be there
         * (REMED-CONTENT-002).
         *
         * Returns `std::optional<T>` rather than FNA's literal bare `T`: real FNA callers check
         * the result against C#'s reference-type `null` (`if (texture != null)`) to tell "no
         * reference was present" apart from a real loaded value, but CNA's asset types (e.g.
         * `Texture2D`) are value types with no uniform "null" of their own -- a default-constructed
         * `Texture2D` is a real, distinguishable-only-by-convention value, not a sentinel. This
         * follows the same `std::optional<T>` idiom already used throughout this reader subsystem
         * for `existingInstance`, rather than inventing a second one.
         *
         * Declared here but defined (and explicitly instantiated for the concrete asset types
         * that use it) in `ContentReader.cpp`, not inline: this method needs `ContentManager`'s
         * full definition to call `Load<T>()`, but `ContentManager.hpp` already includes this
         * header, so an inline body here would create a circular include.
         *
         * @tparam T Requested asset type; must match (via `std::any_cast` inside `Load<T>()`)
         *           whatever the referenced file's root type-reader actually produces.
         * @return The loaded asset, or `std::nullopt` if the reference string is empty (matching
         *         FNA's `default(T)`/`null` for an empty/absent reference).
         * @throws ContentLoadException if no `ContentManager` owns this reader, or the resolved
         *         path escapes the content root.
         */
        template <typename T>
        std::optional<T> ReadExternalReference();

        /** @brief FNA's `T ReadObject<T>()`: reads the next object via the 1-based dispatch protocol. */
        template <typename T>
        T ReadObject()
        {
            return InnerReadObject<T>(std::nullopt);
        }

        /**
         * @brief FNA's `object ReadObject<object>()`: reads the next object via the 1-based
         *        dispatch protocol without knowing its static type ahead of time (used for
         *        `Tag` fields, e.g. `ModelReader`'s per-model/per-mesh `object Tag`).
         *
         * A non-template overload rather than a `ReadObject<T>()` call with `T = std::any`:
         * `std::any_cast<std::any>` isn't a meaningful operation (the generic path expects the
         * boxed type to match `T` exactly), so this reuses the same type-erased path
         * `ReadSharedResources()` already relies on instead.
         *
         * @return The deserialized object, type-erased as `std::any`; empty if the underlying
         *         reference was null (matching FNA's `null` for an unset `Tag`).
         */
        std::any ReadObject()
        {
            return InnerReadObjectAny();
        }

        /** @brief FNA's `T ReadObject<T>(T existingInstance)`: as ReadObject<T>(), deserializing into @p existingInstance. */
        template <typename T>
        T ReadObject(T existingInstance)
        {
            return InnerReadObject<T>(std::optional<T>(std::move(existingInstance)));
        }

        /** @brief FNA's `T ReadObject<T>(ContentTypeReader typeReader)`: reads using a specific reader, bypassing dispatch. */
        template <typename T>
        T ReadObject(ContentTypeReaderBase& typeReader)
        {
            T result = std::any_cast<T>(typeReader.ReadUntyped(*this, std::any{}));
            RecordDisposable(result);
            return result;
        }

        /** @brief FNA's `T ReadObject<T>(ContentTypeReader typeReader, T existingInstance)`. */
        template <typename T>
        T ReadObject(ContentTypeReaderBase& typeReader, T existingInstance)
        {
            T result = std::any_cast<T>(typeReader.ReadUntyped(*this, std::any(std::move(existingInstance))));
            RecordDisposable(result);
            return result;
        }

        /** @brief FNA's `T ReadRawObject<T>(ContentTypeReader typeReader)`: invokes @p typeReader directly, no dispatch/index consumed. */
        template <typename T>
        T ReadRawObject(ContentTypeReaderBase& typeReader)
        {
            return std::any_cast<T>(typeReader.ReadUntyped(*this, std::any{}));
        }

        /** @brief FNA's `T ReadRawObject<T>(ContentTypeReader typeReader, T existingInstance)`. */
        template <typename T>
        T ReadRawObject(ContentTypeReaderBase& typeReader, T existingInstance)
        {
            return std::any_cast<T>(typeReader.ReadUntyped(*this, std::any(std::move(existingInstance))));
        }

        /**
         * @brief FNA's `void ReadSharedResource<T>(Action<T> fixup)` (plans/plan_xnb.md XNB-15).
         *
         * Reads the shared-resource's 1-based index; index 0 means "no reference", and @p fixup
         * is never called. Otherwise, @p fixup is queued and invoked once the referenced shared
         * resource is actually read (see ReadSharedResources()) -- always strictly after
         * InitializeTypeReaders()/the root object have been read, matching FNA's own two-pass
         * "read every shared resource, then run all fixups" order.
         *
         * @throws std::bad_any_cast if the shared resource ends up holding something other than @p T.
         */
        template <typename T>
        void ReadSharedResource(std::function<void(T)> fixup)
        {
            const int32_t index = Read7BitEncodedInt();
            if (index > 0)
            {
                if (index - 1 >= static_cast<int32_t>(sharedResourceFixups_.size()))
                {
                    throw ContentLoadException(
                        "'" + assetName_ + "' references an out-of-range shared resource index.");
                }
                sharedResourceFixups_[static_cast<std::size_t>(index - 1)].push_back(
                    [fixup](const std::any& value) { fixup(std::any_cast<T>(value)); });
            }
        }

        /**
         * @brief FNA's internal `object ReadAsset<T>()`: the single top-level entry point --
         *        initializes the type-reader table, reads the root object, then reads and fixes
         *        up every shared resource.
         */
        template <typename T>
        T ReadAsset()
        {
            InitializeTypeReaders();
            T result = ReadObject<T>();
            ReadSharedResources();
            return result;
        }

        /**
         * @brief FNA's internal `void InitializeTypeReaders()`: parses the type-reader table
         *        (plans/plan_xnb.md XNB-12) at the stream's current position, creates one fresh reader
         *        instance per entry via `ContentTypeReaderManager` (XNB-14/14A), calls
         *        `Initialize()` on each, then reads the shared-resource count and allocates the
         *        per-resource fixup lists.
         *
         * @throws ContentLoadException if any table entry names an unregistered reader.
         */
        void InitializeTypeReaders();

        /**
         * @brief FNA's internal `void ReadSharedResources()`: reads every shared resource (in
         *        file order), then runs every queued fixup for each one, in that order --
         *        matching FNA's own two-pass comment ("we have to read _all_ the objects first,
         *        BEFORE doing fixups").
         */
        void ReadSharedResources();

        /** @brief FNA's internal `BoundingSphere ReadBoundingSphere()`. */
        [[nodiscard]] BoundingSphere ReadBoundingSphere();

        /**
         * @brief CNAEXT bounds check for a collection reader's (`ArrayReader<T>`/`ListReader<T>`/
         *        `DictionaryReader<TKey,TValue>`) declared element count against
         *        `XnbReadLimits::maxCollectionElementCount` (plans/plan_xnb.md XNB-43), called before
         *        any collection allocation/reservation is attempted -- a corrupt or adversarial
         *        count (e.g. `0xFFFFFFFF` from a `uint32`, or a negative `int32` reinterpreted as
         *        an enormous `size_t` by `std::vector::reserve`) could otherwise trigger a huge
         *        allocation attempt, or an effectively unbounded read loop, before the
         *        underlying stream's own truncation would naturally be detected.
         *
         * @param count      The declared element count, as read from the file (widened to
         *                   `int64_t` so both `uint32_t` and `int32_t` callers can pass it
         *                   through without their own overflow/sign concerns).
         * @param readerName Canonical reader name, used only for the exception message.
         * @throws ContentLoadException if @p count is negative or exceeds the configured limit.
         */
        CNAEXT void CheckCollectionElementCount(int64_t count, const std::string& readerName) const;

        /**
         * @brief CNAEXT bounds check for a decoded pixel/voxel buffer's byte size (e.g.
         *        `width * height * 4` for an uncompressed `Texture2D`) against
         *        `XnbReadLimits::maxDecompressedSize` (plans/plan_xnb.md XNB-43), called before any
         *        texture-sized allocation is attempted.
         *
         * Deliberately a *separate* check from `CheckCollectionElementCount()`, not a reuse of
         * it: `maxCollectionElementCount` (10 million) is tuned for array/list/dictionary element
         * counts and would incorrectly reject a common, entirely legitimate real texture size
         * (e.g. 4096x4096 = ~16.8 million pixels). `maxDecompressedSize` (256MB) already exists
         * for exactly this "one large in-memory buffer" purpose (Phase D's LZX payload) and is a
         * far more appropriate ceiling for a decoded RGBA buffer's byte size specifically.
         *
         * @param byteSize   The decoded buffer's required size in bytes (e.g.
         *                   `int64_t{width} * height * 4`, computed by the caller in `int64_t` so
         *                   the multiplication itself can't silently wrap back into range).
         * @param readerName Canonical reader name, used only for the exception message.
         * @throws ContentLoadException if @p byteSize is negative or exceeds the configured limit.
         */
        CNAEXT void CheckDecodedByteSize(int64_t byteSize, const std::string& readerName) const;

        /**
         * @brief CNAEXT hardened counterpart of `BinaryReader::ReadBytes(int)` for `.xnb` readers
         *        that read a declared-length raw byte blob (texture pixel data, audio data,
         *        vertex/index buffer data) and require getting back *exactly* that many bytes
         *        (plans/plan_xnb.md XNB-43).
         *
         * `BinaryReader::ReadBytes(int)` itself silently trims its result on a truncated stream
         * (matching real .NET's own non-throwing `ReadBytes(int)` contract) -- correct for that
         * primitive, but dangerous for a caller that assumes the returned vector is always
         * exactly the requested length and then passes the ORIGINAL requested length (not the
         * vector's actual size) to a raw pointer+count API afterward: a truncated/corrupt file
         * would silently produce an out-of-bounds read at that point instead of a clean error.
         *
         * @param count      The number of bytes required.
         * @param readerName Canonical reader name, used only for the exception message.
         * @return Exactly @p count bytes.
         * @throws ContentLoadException if @p count is negative.
         * @throws System::IO::EndOfStreamException if fewer than @p count bytes were available.
         */
        CNAEXT [[nodiscard]] std::vector<uint8_t> ReadBytesExactOrThrow(int32_t count, const std::string& readerName);

    private:
        /**
         * @brief Type-erased counterpart of InnerReadObject<T>(), used only by
         *        ReadSharedResources() to read each shared resource without knowing its static
         *        type ahead of time (matching FNA's own `InnerReadObject<object>(null)` call
         *        there). Deliberately does not call RecordDisposable() -- doing so would need a
         *        dynamic "is this actually IDisposable" check with no static T to dispatch on;
         *        see RecordDisposable()'s own docs. `ReadObject<T>()`/`ReadRawObject<T>()`, which
         *        always know T statically, remain the disposal-tracked paths.
         */
        std::any InnerReadObjectAny();

        // existingInstance is std::optional<T>, not a bare T -- see ContentTypeReader<T>::Read's
        // own docs for why: C++ has no way to construct a placeholder T{} generically when T has
        // no default constructor (e.g. SpriteFont), unlike C#'s free default(T) for reference
        // types. nullopt uniformly represents "no existing instance" regardless of whether T
        // happens to be default-constructible.
        // REMED-CONTENT-006: InnerReadObject() recurses whenever a type reader's own ReadUntyped()
        // calls back into ReadObject<U>() for a nested member (e.g. a Model's meshes, each
        // containing effects, each containing parameters...) with no depth limit -- unbounded
        // recursion driven entirely by attacker-controlled file structure, the same class of bug
        // as XnbTypeName::ParseOne()'s (see that file's own fix). This RAII guard bounds it by
        // limits_.maxObjectNestingDepth; decrements on every exit path, including an exception
        // thrown deeper in the call stack.
        struct ObjectDepthGuard
        {
            ContentReader& self;
            explicit ObjectDepthGuard(ContentReader& s) : self(s) { ++self.objectNestingDepth_; }
            ~ObjectDepthGuard() { --self.objectNestingDepth_; }
        };

        template <typename T>
        T InnerReadObject(std::optional<T> existingInstance)
        {
            if (objectNestingDepth_ > limits_.maxObjectNestingDepth)
            {
                throw ContentLoadException(
                    "'" + assetName_ + "' exceeds the maximum object nesting depth (" +
                    std::to_string(limits_.maxObjectNestingDepth) + ").");
            }
            ObjectDepthGuard depthGuard(*this);

            const int32_t typeReaderIndex = Read7BitEncodedInt();
            if (typeReaderIndex == 0)
            {
                // Real FNA never fails here: default(T) always succeeds, for both C# reference
                // types (null) and value types (zero-init). Mirror that for a default-constructible
                // T; only a T with no default constructor AND no existing instance has no C++
                // representation for "null" to fall back to.
                if (existingInstance.has_value())
                {
                    return std::move(*existingInstance);
                }
                if constexpr (std::is_default_constructible_v<T>)
                {
                    return T{};
                }
                else
                {
                    throw ContentLoadException(
                        "'" + assetName_ + "' has a null object of a type with no default "
                        "constructor and no existing instance to fall back to.");
                }
            }
            if (typeReaderIndex < 0 || static_cast<std::size_t>(typeReaderIndex) > typeReaders_.size())
            {
                throw ContentLoadException(
                    "'" + assetName_ + "' has an incorrect type reader index.");
            }
            ContentTypeReaderBase& typeReader = *typeReaders_[static_cast<std::size_t>(typeReaderIndex - 1)];

            // See ContentTypeReader<T>::ReadUntyped()'s own docs: a move-only T (no CopyConstructible,
            // e.g. SoundEffect) is boxed as std::shared_ptr<T> instead of a bare T, since std::any
            // requires CopyConstructible unconditionally.
            if constexpr (std::is_copy_constructible_v<T>)
            {
                std::any existingAny = existingInstance.has_value()
                    ? std::any(std::move(*existingInstance))
                    : std::any{};
                std::any resultAny = typeReader.ReadUntyped(*this, std::move(existingAny));
                T result = std::any_cast<T>(std::move(resultAny));
                RecordDisposable(result);
                return result;
            }
            else
            {
                std::any existingAny = existingInstance.has_value()
                    ? std::any(std::make_shared<T>(std::move(*existingInstance)))
                    : std::any{};
                std::any resultAny = typeReader.ReadUntyped(*this, std::move(existingAny));
                T result = std::move(*std::any_cast<std::shared_ptr<T>>(std::move(resultAny)));
                RecordDisposable(result);
                return result;
            }
        }

        template <typename T>
        void RecordDisposable(const T& result)
        {
            if constexpr (Detail::IsDisposableSharedPtr<T>::value)
            {
                if (!result) return;
                auto disposable = std::static_pointer_cast<System::IDisposable>(result);
                if (recordDisposableObject_)
                {
                    recordDisposableObject_(std::move(disposable));
                }
                // Falling back to contentManager_->RecordDisposable(...) is deferred to Phase B2
                // (XNB-17B), which is when ContentManager gains that tracking mechanism at all.
            }
            // Value types (Vector3, Matrix, Curve, ...) are never IDisposable and are skipped.
        }

        ContentManager* contentManager_;
        std::string assetName_;
        int version_;
        char platform_;
        RecordDisposableFn recordDisposableObject_;
        CNA::Internal::Xnb::XnbReadLimits limits_;

        std::vector<std::unique_ptr<ContentTypeReaderBase>> typeReaders_;
        int32_t sharedResourceCount_ = 0;
        std::vector<std::any> sharedResources_;
        std::vector<std::vector<std::function<void(const std::any&)>>> sharedResourceFixups_;
        int32_t objectNestingDepth_ = 0;
    };
}
