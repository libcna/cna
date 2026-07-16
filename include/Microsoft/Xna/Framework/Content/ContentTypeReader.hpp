// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content
{
    class ContentReader; // forward declaration -- see ContentReader.hpp (plan_xnb.md XNB-15/16)
    class ContentTypeReaderManager;

    /**
     * @brief NOXNA non-generic base for ContentTypeReader<T>.
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
     * object, since CNA has no runtime reflection -- see plan_xnb.md XNB-16A/16C for the
     * `RuntimeTypeId` layered on top of this same string for fast dispatch/type-checking.
     */
    class NOXNA ContentTypeReaderBase
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
         * @param existingInstance An existing instance to deserialize into, or null for a new one.
         * @return The deserialized object, type-erased as `std::shared_ptr<void>`.
         */
        virtual std::shared_ptr<void> ReadUntyped(
            ContentReader& input, std::shared_ptr<void> existingInstance) = 0;

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
         */
        virtual T Read(ContentReader& input, T existingInstance) = 0;

    public:
        /**
         * @brief FNA's `protected internal override object Read(ContentReader, object)`: unboxes
         *        @p existingInstance (if non-null) to `T`, delegates to the typed Read(), then
         *        re-boxes the result -- the C++ counterpart of .NET's object-boxing glue.
         */
        std::shared_ptr<void> ReadUntyped(
            ContentReader& input, std::shared_ptr<void> existingInstance) override
        {
            T existing = existingInstance ? *std::static_pointer_cast<T>(existingInstance) : T{};
            T result = Read(input, std::move(existing));
            return std::make_shared<T>(std::move(result));
        }
    };
}
