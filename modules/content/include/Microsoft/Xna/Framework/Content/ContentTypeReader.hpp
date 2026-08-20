// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content
{
    class ContentReader; // forward declaration -- see ContentReader.hpp (plans/plan_xnb.md XNB-15/16)
    class ContentTypeReaderManager;

    /**
     * @brief CNAEXT non-generic base for ContentTypeReader<T>.
     *
     * Corresponds to FNA's non-generic `Microsoft.Xna.Framework.Content.ContentTypeReader`
     * (`src/Content/ContentTypeReader.cs`). C++ cannot declare both a plain class and a template
     * under the identical bare name `ContentTypeReader` in one namespace (unlike C#, where arity
     * distinguishes the non-generic type from `ContentTypeReader<T>`) -- this base is the one
     * forced rename; the template below keeps the real, literal XNA name, since that is the one
     * a ported reader class actually inherits from.
     *
     * `TargetType` is represented as a canonical XNA type name string (e.g.
     * `"Microsoft.Xna.Framework.Graphics.Texture2D"`) rather than a real `System::Type`/`Type`
     * object, since CNA has no runtime reflection. FNA's boxed `object` (used for both
     * `TargetType`-erased dispatch and `existingInstance`) becomes `std::any` here rather than
     * `std::shared_ptr<void>`: `std::any` is self-describing and `std::any_cast<T>()` throws
     * `std::bad_any_cast` on a type mismatch, the same safety `object`'s checked unboxing cast
     * gives C# -- a bare `shared_ptr<void>` would need a hand-rolled parallel type tag to get
     * that same guarantee instead of silently reinterpreting the wrong type.
     */
    class CNAEXT ContentTypeReaderBase
    {
    public:
        virtual ~ContentTypeReaderBase() = default;

        /** @brief FNA's `ContentTypeReader.CanDeserializeIntoExistingObject` (default false). */
        [[nodiscard]] virtual bool getCanDeserializeIntoExistingObjectProperty() const { return false; }

        /** @brief FNA's `ContentTypeReader.TargetType`, represented as a canonical XNA type name. */
        [[nodiscard]] const std::string& getTargetTypeNameProperty() const { return targetTypeName_; }

        /** @brief FNA's `ContentTypeReader.TypeVersion` (default 0). */
        [[nodiscard]] virtual int getTypeVersionProperty() const { return 0; }

        /**
         * @brief CNAEXT reader-version enforcement (plans/plan_xnb.md XNB-16B) -- FNA has no equivalent
         * (it never checks the serialized version against `TypeVersion`, only ever parses and
         * discards it). Default "Strict" mode requires an exact match against
         * @ref getTypeVersionProperty(); a reader supporting multiple on-disk versions
         * ("Compatibility" mode) overrides this instead of just widening the check inline.
         *
         * @param serializedVersion The version int actually read from this file's type-reader table entry.
         * @return True if this reader instance can deserialize data serialized at that version.
         */
        [[nodiscard]] virtual bool SupportsVersion(int serializedVersion) const
        {
            return serializedVersion == getTypeVersionProperty();
        }

        /**
         * @brief FNA's `protected internal virtual void Initialize(ContentTypeReaderManager)`.
         *
         * Called once per freshly-created reader instance, after the whole type-reader table for
         * a file has been instantiated (matching FNA's own two-pass `LoadAssetReaders`: create
         * every reader first, then call `Initialize` on each one that needed it).
         */
        virtual void Initialize(ContentTypeReaderManager& manager) { (void)manager; }

        /**
         * @brief Type-erased counterpart of FNA's `protected internal abstract object Read(
         *        ContentReader, object)`.
         *
         * @param input            The reader positioned at this object's serialized data.
         * @param existingInstance An existing instance to deserialize into, or an empty
         *                         `std::any` for a new one (matching a null `object`).
         * @return The deserialized object, type-erased as `std::any`.
         */
        virtual std::any ReadUntyped(ContentReader& input, std::any existingInstance) = 0;

    protected:
        /** @brief FNA's `protected ContentTypeReader(Type targetType)`. */
        explicit ContentTypeReaderBase(std::string targetTypeName)
            : targetTypeName_(std::move(targetTypeName)) {}

    private:
        std::string targetTypeName_;
    };

    /**
     * @brief Abstract base for a `.xnb` type-specific reader, matching FNA's real
     *        `Microsoft.Xna.Framework.Content.ContentTypeReader<T>` (`src/Content/
     *        ContentTypeReader.cs`) -- ported readers subclass this exactly as a real
     *        XNA/FNA-derived reader class would (`class Texture2DReader : ContentTypeReader<
     *        Texture2D>`), matching CLAUDE.md's class-name/signature preservation rule.
     *
     * @tparam T The asset type this reader produces.
     */
    template <typename T>
    class ContentTypeReader : public ContentTypeReaderBase
    {
    protected:
        /** @brief FNA's `protected ContentTypeReader() : base(typeof(T))`. */
        explicit ContentTypeReader(std::string targetTypeName)
            : ContentTypeReaderBase(std::move(targetTypeName)) {}

        /**
         * @brief FNA's `protected internal abstract T Read(ContentReader input, T
         *        existingInstance)` -- the method a concrete reader overrides.
         *
         * @p existingInstance uses `std::optional<T>` rather than a bare `T` (FNA's literal
         * signature): C#'s `default(T)` is free for a reference type (`null`, no construction at
         * all) but real construction for a value type -- a distinction C++ has no uniform way to
         * express when `T` is a value-semantics type with no default constructor (e.g.
         * `SpriteFont`, which always requires real construction arguments). `std::nullopt`
         * represents FNA's null/`default(T)` uniformly regardless of whether `T` happens to be
         * default-constructible; a concrete reader constructs a fresh `T` itself when it's empty,
         * exactly as a real FNA reader does for `existingInstance == null`.
         */
        virtual T Read(ContentReader& input, std::optional<T> existingInstance) = 0;

    public:
        /**
         * @brief FNA's `protected internal override object Read(ContentReader, object)`: unboxes
         *        @p existingInstance (if non-empty) to `T`, delegates to the typed Read(), then
         *        re-boxes the result -- the C++ counterpart of .NET's object-boxing glue.
         *
         * `std::any` requires its held type to be `CopyConstructible`, which a handful of real
         * XNA reference types don't satisfy in CNA (e.g. `SoundEffect`'s move-only, per-owner
         * Dispose-cascade design, T-3G) -- for those, the boxed/unboxed value is a
         * `std::shared_ptr<T>` instead of a bare `T`, chosen at compile time via `if constexpr`
         * rather than always paying the extra indirection. `InnerReadObject<T>()` unwraps it
         * back out symmetrically.
         *
         * @throws std::bad_any_cast if @p existingInstance is non-empty but does not hold the
         *         expected boxed representation of `T`.
         */
        std::any ReadUntyped(ContentReader& input, std::any existingInstance) override
        {
            if constexpr (std::is_copy_constructible_v<T>)
            {
                std::optional<T> existing = existingInstance.has_value()
                    ? std::optional<T>(std::any_cast<T>(std::move(existingInstance)))
                    : std::nullopt;
                T result = Read(input, std::move(existing));
                return std::any(std::move(result));
            }
            else
            {
                std::optional<T> existing = existingInstance.has_value()
                    ? std::optional<T>(std::move(*std::any_cast<std::shared_ptr<T>>(std::move(existingInstance))))
                    : std::nullopt;
                T result = Read(input, std::move(existing));
                return std::any(std::make_shared<T>(std::move(result)));
            }
        }
    };
}
