// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "CNA/Content/Xnb/XnbWriteLimits.hpp"

namespace CNA::Content::Xnb
{
    class XnbWriter;

    /**
     * @brief Assembly identity of `Microsoft.Xna.Framework.dll` in XNA 4.0.
     *
     * A `.xnb` file's type-reader table stores .NET type names the loading runtime resolves with
     * `Type.GetType()`, which searches only mscorlib and the calling assembly. XNA resolves them
     * from inside `Microsoft.Xna.Framework`, so a reader living there is written unqualified and
     * a reader living anywhere else must carry its assembly, or a real XNA runtime cannot find it.
     * Verified against real, externally produced fixtures rather than assumed -- see
     * plans/plan_xnapipeline.md `XNAP-G7`.
     */
    inline constexpr const char* XnaFrameworkAssembly =
        "Microsoft.Xna.Framework, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=842cf8be1de50553";

    /** @brief Assembly identity of `Microsoft.Xna.Framework.Graphics.dll` in XNA 4.0. */
    inline constexpr const char* XnaGraphicsAssembly =
        "Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=842cf8be1de50553";

    /** @brief Assembly identity of `mscorlib.dll` in the .NET Framework 4.0. */
    inline constexpr const char* MscorlibAssembly =
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089";

    /**
     * @brief Returns the assembly-qualified spelling a `.xnb` type-reader table uses for a type.
     *
     * `System.*` types are qualified with mscorlib and everything under
     * `Microsoft.Xna.Framework.Graphics` with the graphics assembly; every other
     * `Microsoft.Xna.Framework` type is left unqualified, because that is the assembly a real XNA
     * runtime resolves from. A name that already carries a qualifier is returned unchanged.
     *
     * @param typeName Serialized .NET type name.
     * @return The spelling to write into the file.
     */
    [[nodiscard]] std::string XnbQualifiedTypeName(const std::string& typeName);

    /**
     * @brief Returns the spelling a reader name takes in the type-reader table.
     *
     * The same rule as XnbQualifiedTypeName(), applied to the reader rather than the target type:
     * a reader for a `Microsoft.Xna.Framework.Graphics` type lives in the graphics assembly and is
     * qualified; every other built-in reader lives in `Microsoft.Xna.Framework` and is not.
     *
     * @param readerName Bare reader name, e.g. `"Microsoft.Xna.Framework.Content.Texture2DReader"`.
     * @param assembly Assembly the reader lives in, or empty for `Microsoft.Xna.Framework`.
     * @return The spelling to write into the file.
     */
    [[nodiscard]] std::string XnbQualifiedReaderName(const std::string& readerName,
                                                     const std::string& assembly = {});

    /**
     * @brief Serializes one .NET type into an `.xnb` object graph
     *        (plans/plan_xnapipeline.md `XNAP-004`).
     *
     * The write-side counterpart of `Microsoft::Xna::Framework::Content::ContentTypeReader<T>`,
     * and the CNA equivalent of XNA's `ContentTypeWriter<T>`. An implementation answers three
     * questions the container format needs and then emits a payload:
     *
     * - which serialized .NET type it handles (`TargetTypeName()`), which is the registry key;
     * - which runtime reader will read it back (`RuntimeReaderName()`), which is what lands in
     *   the file's type-reader table -- XNA's `GetRuntimeReader()`;
     * - whether the type is a value type, which decides between the raw and the polymorphic wire
     *   form wherever the format specifies `Object? T`.
     *
     * One registered instance may serve concurrent writes after its registry is frozen, so an
     * implementation must be reentrant or synchronize its own mutable state. The stock writers
     * hold no mutable state at all.
     */
    class XnbTypeWriter
    {
    public:
        /** @brief Enables correct destruction through the writer interface. */
        virtual ~XnbTypeWriter() = default;

        /**
         * @brief Returns the serialized .NET type this writer handles.
         *
         * This is the stable registry key, e.g. `"Microsoft.Xna.Framework.Graphics.Texture2D"` or
         * `"System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Rectangle]]"`. It never
         * depends on a C++ ABI spelling or on RTTI.
         *
         * @return The serialized type name.
         */
        [[nodiscard]] virtual std::string TargetTypeName() const = 0;

        /**
         * @brief Returns the runtime `ContentTypeReader` name recorded in the type-reader table.
         *
         * @return The reader name, e.g. `"Microsoft.Xna.Framework.Content.Texture2DReader"`.
         */
        [[nodiscard]] virtual std::string RuntimeReaderName() const = 0;

        /**
         * @brief Returns the reader version recorded beside the reader name.
         *
         * Zero for every built-in XNA 4.0 type. A custom writer changes this only when its
         * payload layout changes and its reader can distinguish the versions.
         *
         * @return The type version.
         */
        [[nodiscard]] virtual std::int32_t TypeVersion() const { return 0; }

        /**
         * @brief Returns whether the target type is a .NET value type.
         *
         * Value types are written raw wherever the format specifies `Object? T`; reference types
         * are written in the polymorphic form with a leading type identifier.
         *
         * @return True for a value type.
         */
        [[nodiscard]] virtual bool IsValueType() const = 0;

        /**
         * @brief Emits this type's payload, with no leading type identifier.
         *
         * @param output Writer positioned where the payload begins.
         * @param value The value to serialize; its contained type is the writer's own.
         * @throws XnbWriteException when @p value does not hold the expected type or is invalid.
         */
        virtual void Write(XnbWriter& output, const std::any& value) const = 0;
    };

    /**
     * @brief Typed convenience base that unwraps `std::any` once, in one place.
     *
     * @tparam T The C++ representation of the serialized type.
     */
    template <typename T>
    class XnbTypeWriterT : public XnbTypeWriter
    {
    public:
        /** @brief The C++ representation this writer serializes. */
        using ValueType = T;

        /**
         * @brief Unwraps @p value and forwards to the typed overload.
         *
         * @param output Writer positioned where the payload begins.
         * @param value The boxed value; must contain a `T`.
         * @throws XnbWriteException when @p value does not contain a `T`.
         */
        void Write(XnbWriter& output, const std::any& value) const final
        {
            const T* typed = std::any_cast<T>(&value);
            if (typed == nullptr)
            {
                throw XnbWriteException(
                    "XnbTypeWriter for '" + TargetTypeName() +
                    "' received a value of a different type.");
            }
            Write(output, *typed);
        }

        /**
         * @brief Emits this type's payload from an already-unwrapped value.
         *
         * @param output Writer positioned where the payload begins.
         * @param value The value to serialize.
         */
        virtual void Write(XnbWriter& output, const T& value) const = 0;
    };

    /**
     * @brief Maps a C++ type to the serialized .NET type name that identifies its writer.
     *
     * The trait is what keeps writer lookup typed without RTTI: the compiler resolves `T` to a
     * key, and the registry resolves the key to a writer. Specialize it for a custom type, next
     * to that type's `XnbTypeWriterT<T>` subclass. Generic specializations compose, so
     * `XnbTypeKey<std::vector<Rectangle>>::Name()` is built from `XnbTypeKey<Rectangle>::Name()`.
     *
     * @tparam T The C++ representation.
     */
    template <typename T>
    struct XnbTypeKey;

    /**
     * @brief Explicit, deterministic registry of `.xnb` type writers.
     *
     * Follows `CNA::Content::Pipeline::ContentPipelineRegistry`'s proven shape: configure, freeze,
     * then use concurrently and read-only. Registration order never resolves an ambiguity;
     * lookup is by serialized type name only.
     */
    class XnbTypeWriterRegistry
    {
    public:
        /**
         * @brief Registers one writer owned by this registry.
         *
         * @param writer Non-null writer with a nonempty target type and reader name.
         * @throws XnbWriteException for a malformed declaration, a duplicate target type, or a
         *         registry that has already been frozen.
         */
        void Register(std::shared_ptr<const XnbTypeWriter> writer);

        /**
         * @brief Permanently seals this registry for concurrent read-only use.
         *
         * Idempotent. `WriteXnbFile()` calls it before any byte is produced, so callers only need
         * it when they want an explicit configure-then-freeze boundary of their own.
         */
        void Freeze() const;

        /**
         * @brief Returns whether this registry has been sealed.
         *
         * @return True after Freeze().
         */
        [[nodiscard]] bool IsFrozen() const noexcept;

        /**
         * @brief Finds the writer registered for a serialized type name.
         *
         * @param targetTypeName The serialized .NET type name.
         * @return The writer, or null when nothing is registered for @p targetTypeName.
         */
        [[nodiscard]] std::shared_ptr<const XnbTypeWriter> Find(
            const std::string& targetTypeName) const;

        /**
         * @brief Finds the writer for a serialized type name, or fails with a precise diagnostic.
         *
         * @param targetTypeName The serialized .NET type name.
         * @return The writer, never null.
         * @throws XnbWriteException when nothing is registered for @p targetTypeName.
         */
        [[nodiscard]] std::shared_ptr<const XnbTypeWriter> Resolve(
            const std::string& targetTypeName) const;

        /**
         * @brief Returns every registered serialized type name in deterministic order.
         *
         * @return Sorted target type names.
         */
        [[nodiscard]] std::vector<std::string> RegisteredTypeNames() const;

    private:
        mutable std::shared_mutex mutex_;
        mutable std::atomic_bool frozen_{false};
        std::map<std::string, std::shared_ptr<const XnbTypeWriter>> writers_;
    };
}
