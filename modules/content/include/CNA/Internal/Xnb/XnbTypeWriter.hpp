// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
#include <memory>
#include <string>

#include "CNA/Internal/Xnb/XnbByteWriter.hpp"
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"

namespace CNA::Internal::Xnb
{
    class XnbWriter;

    /**
     * @brief A process-unique, RTTI-free identity for one serialized C++ type
     *        (plans/plan_xnapipeline.md `XNAP-15`).
     *
     * The address of a function-local static is unique per instantiation of the enclosing
     * template and stable for the process lifetime, which is exactly what a typed registry key
     * needs. Deliberately not `std::type_index`: the pipeline's own `ContentValue` already
     * documents that RTTI never participates in lookup, only in a defensive check, and this
     * registry must keep working in a build with RTTI disabled.
     */
    using XnbTypeId = const void*;

    /** @brief Yields the process-unique @ref XnbTypeId for one serialized C++ type. */
    template<typename T>
    struct XnbTypeKey
    {
        /**
         * @brief Returns the stable identity of `T`.
         *
         * @return A pointer unique to `T` and stable for the process lifetime.
         */
        [[nodiscard]] static XnbTypeId Id() noexcept
        {
            static const char marker = 0;
            return &marker;
        }
    };

    /**
     * @brief Type-erased base of every `.xnb` type writer.
     *
     * One registered instance may serve concurrent writes after the registry is frozen, so an
     * implementation must be reentrant and hold no mutable per-write state; every per-write
     * concern belongs to @ref XnbWriter.
     */
    class XnbTypeWriterBase
    {
    public:
        /** @brief Enables correct destruction through the type-writer interface. */
        virtual ~XnbTypeWriterBase() = default;

        /** @brief Returns the RTTI-free identity of the C++ type this writer serializes. */
        [[nodiscard]] virtual XnbTypeId TargetTypeId() const noexcept = 0;

        /** @brief Returns the reader identity written into the type-reader table. */
        [[nodiscard]] virtual XnbReaderIdentity ReaderIdentity() const = 0;

        /**
         * @brief Returns whether a nested element of this type carries its own dispatch index.
         *
         * True for .NET reference types (`String`, `List<T>`, `T[]`, any class), false for value
         * types. A collection writer consults this to decide between @ref XnbWriter::WriteObject
         * and a direct payload write, matching how `ListReader<T>` and `ArrayReader<T>` decide on
         * the reading side.
         */
        [[nodiscard]] virtual bool IsSerializedByReference() const noexcept = 0;

        /**
         * @brief Writes one value's payload, without any dispatch index.
         *
         * @param output Per-write object-graph writer.
         * @param value Pointer to a value of exactly the type this writer serializes.
         */
        virtual void WriteUntyped(XnbWriter& output, const void* value) const = 0;
    };

    /**
     * @brief Typed base class every built-in and custom `.xnb` type writer derives from.
     *
     * @tparam T The exact C++ type serialized by this writer.
     */
    template<typename T>
    class XnbTypeWriter : public XnbTypeWriterBase
    {
    public:
        /** @brief Returns `XnbTypeKey<T>::Id()`. */
        [[nodiscard]] XnbTypeId TargetTypeId() const noexcept final
        {
            return XnbTypeKey<T>::Id();
        }

        /**
         * @brief Casts the erased value back to `T` and delegates to @ref Write.
         *
         * @param output Per-write object-graph writer.
         * @param value Pointer to a `T`.
         */
        void WriteUntyped(XnbWriter& output, const void* value) const final
        {
            Write(output, *static_cast<const T*>(value));
        }

    protected:
        /**
         * @brief Writes one `T` payload, without any dispatch index.
         *
         * @param output Per-write object-graph writer.
         * @param value The value to serialize.
         */
        virtual void Write(XnbWriter& output, const T& value) const = 0;
    };

    /**
     * @brief Deterministic, freezable registry of `.xnb` type writers keyed by @ref XnbTypeId.
     *
     * Registration order never resolves an ambiguity: exactly one writer may claim a given C++
     * type, and a second registration for the same type is an error rather than a silent
     * override. A frozen registry is safe to share between concurrent writes.
     */
    class XnbTypeWriterRegistry
    {
    public:
        /**
         * @brief Registers one writer owned by this registry.
         *
         * @param writer Non-null writer whose target type is not already registered.
         * @throws XnbWriteException if @p writer is null, this registry is frozen, or the type is
         *         already registered.
         */
        void Register(std::shared_ptr<const XnbTypeWriterBase> writer);

        /**
         * @brief Registers one writer constructed in place.
         *
         * @tparam Writer Concrete writer type with a default constructor.
         * @throws XnbWriteException under the same conditions as @ref Register.
         */
        template<typename Writer>
        void Add()
        {
            Register(std::make_shared<const Writer>());
        }

        /**
         * @brief Permanently seals this registry for concurrent read-only use. Idempotent.
         */
        void Freeze() const;

        /** @brief Returns whether this registry has been sealed. */
        [[nodiscard]] bool IsFrozen() const noexcept;

        /**
         * @brief Finds the writer for one C++ type without throwing.
         *
         * @param type The type identity to look up.
         * @return Pointer valid for this registry's lifetime, or null when unregistered.
         */
        [[nodiscard]] const XnbTypeWriterBase* Find(XnbTypeId type) const;

        /**
         * @brief Resolves the writer for one C++ type.
         *
         * @param type The type identity to look up.
         * @param diagnosticTypeName Name used in the failure message when the type is unknown.
         * @return The registered writer.
         * @throws XnbWriteException when no writer claims @p type.
         */
        [[nodiscard]] const XnbTypeWriterBase& Require(
            XnbTypeId type, const std::string& diagnosticTypeName) const;

        /**
         * @brief Resolves the writer for one C++ type, naming it from its own reader identity.
         *
         * @tparam T The type to look up.
         * @return The registered writer.
         * @throws XnbWriteException when no writer claims `T`.
         */
        template<typename T>
        [[nodiscard]] const XnbTypeWriterBase& Require() const
        {
            return Require(XnbTypeKey<T>::Id(), "an unregistered C++ type");
        }

        /**
         * @brief Returns every registered writer's canonical reader name, sorted.
         *
         * Used by diagnostics and by the registration tests that prove the writer and runtime
         * reader registries agree.
         *
         * @return Sorted canonical reader names.
         */
        [[nodiscard]] std::vector<std::string> RegisteredReaderNames() const;

    private:
        void RequireMutable() const;

        std::map<XnbTypeId, std::shared_ptr<const XnbTypeWriterBase>> writers_;
        mutable bool frozen_ = false;
    };

    /**
     * @brief Registers every built-in `.xnb` type writer CNA ships.
     *
     * Registration is explicit and performs no dynamic loading or process-global mutation, so a
     * caller can add its own writers before or after this call and then freeze.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInXnbWriters(XnbTypeWriterRegistry& registry);

    /**
     * @brief Returns a frozen, process-wide registry containing only the built-in writers.
     *
     * @return A shared registry safe for concurrent use.
     */
    [[nodiscard]] const XnbTypeWriterRegistry& BuiltInXnbWriterRegistry();
}
