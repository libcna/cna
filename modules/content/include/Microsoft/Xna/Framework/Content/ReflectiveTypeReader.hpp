// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief Reads a type the XNA content pipeline serialized *reflectively*, from a field list
     *        the game declares once.
     *
     * XNA compiles a type that has no explicit `ContentTypeWriter` through an implicit
     * `ReflectiveReader<T>`, which walks the type's public fields with .NET reflection at load
     * time. CNA has no runtime reflection and `docs/xnb-content-pipeline-support.md` (XNB-42A)
     * says so. This is the middle ground: **the game supplies the one thing reflection provided —
     * its own field list — and CNA does the rest**, so the list exists in one place instead of
     * being duplicated in a hand-written reader that can drift from the type.
     *
     * The payload format this reads is XNA's, verified byte for byte against pipeline output:
     * value-type fields are written **inline, in declaration order**, and a reference-type field
     * is preceded by the 1-based index of its own type reader.
     *
     * ```cpp
     * ReflectiveTypeReaderBuilder<ParticleSystemSettings>("ParticlesSettings.ParticleSystemSettings")
     *     .Field(&ParticleSystemSettings::MinNumParticles)
     *     .Field(&ParticleSystemSettings::TextureFilename)
     *     .EnumField(&ParticleSystemSettings::AccelerationMode, "ParticlesSettings.AccelerationMode")
     *     .Register();
     * ```
     *
     * `Register()` also registers an `EnumReader` for every `EnumField`, which a game otherwise
     * has to do by hand: an `.xnb`'s type-reader table must resolve in FULL before any object is
     * read, even for readers this one never dispatches to.
     *
     * @tparam T The type being read. Must be default-constructible and assignable.
     */
    template <typename T>
    class CNAEXT ReflectiveTypeReader : public ContentTypeReader<T>
    {
    public:
        /** @brief One field's reader: pulls the next value out and stores it in the object. */
        using FieldReader = std::function<void(T&, ContentReader&)>;

        /**
         * @brief Constructs the reader.
         * @param targetTypeName The .NET name of the type, as the `.xnb` spells it.
         * @param fields         One entry per field, in the type's declaration order.
         */
        ReflectiveTypeReader(std::string targetTypeName, std::vector<FieldReader> fields)
            : ContentTypeReader<T>(std::move(targetTypeName)), fields_(std::move(fields))
        {
        }

        /**
         * @brief The canonical reader name an implicitly reflected type is written under.
         *
         * CNA normalizes the `.xnb`'s assembly-qualified name down to this, so this is the key
         * `ContentTypeReaderManager` is looking for.
         *
         * @param targetTypeName The .NET name of the serialized type.
         * @return The canonical reflective reader name.
         */
        [[nodiscard]] static std::string CanonicalReaderName(const std::string& targetTypeName)
        {
            return "Microsoft.Xna.Framework.Content.ReflectiveReader`1[[" + targetTypeName + "]]";
        }

    protected:
        /**
         * @brief Reads one object by running every field reader in order.
         * @param input            The reader, positioned at the object's first field.
         * @param existingInstance An instance to deserialize into, when the caller supplied one.
         * @return The populated object.
         */
        T Read(ContentReader& input, std::optional<T> existingInstance) override
        {
            T value = existingInstance.has_value() ? std::move(*existingInstance) : T{};
            for (const FieldReader& field : fields_)
                field(value, input);
            return value;
        }

    private:
        std::vector<FieldReader> fields_;
    };

    /**
     * @brief Reads an enum written by XNA's `EnumReader<T>`, which stores it as an `Int32`.
     *
     * Exists so that a reflectively-read type's enum fields can satisfy the `.xnb`'s type-reader
     * table; the reflective payload itself writes enums inline and never dispatches here.
     *
     * @tparam TEnum The enum type.
     */
    template <typename TEnum>
    class CNAEXT EnumTypeReader : public ContentTypeReader<TEnum>
    {
    public:
        /**
         * @brief Constructs the reader.
         * @param targetTypeName The .NET name of the enum, as the `.xnb` spells it.
         */
        explicit EnumTypeReader(std::string targetTypeName)
            : ContentTypeReader<TEnum>(std::move(targetTypeName))
        {
        }

        /**
         * @brief The canonical reader name an enum is written under.
         * @param targetTypeName The .NET name of the enum.
         * @return The canonical enum reader name.
         */
        [[nodiscard]] static std::string CanonicalReaderName(const std::string& targetTypeName)
        {
            return "Microsoft.Xna.Framework.Content.EnumReader`1[[" + targetTypeName + "]]";
        }

    protected:
        /**
         * @brief Reads the enum's underlying `Int32`.
         * @param input The reader.
         * @return The enum value.
         */
        TEnum Read(ContentReader& input, std::optional<TEnum>) override
        {
            return static_cast<TEnum>(input.ReadInt32());
        }
    };

    /**
     * @brief Declares a reflectively-serialized type's fields, in declaration order, and registers
     *        the readers the `.xnb` needs.
     *
     * @tparam T The type being described.
     */
    template <typename T>
    class CNAEXT ReflectiveTypeReaderBuilder
    {
    public:
        /**
         * @brief Begins describing a type.
         * @param targetTypeName The .NET name of the type, as the `.xnb` spells it.
         */
        explicit ReflectiveTypeReaderBuilder(std::string targetTypeName)
            : targetTypeName_(std::move(targetTypeName))
        {
        }

        /**
         * @brief Declares the next field.
         *
         * The member's C++ type decides how it is read: arithmetic types and the XNA math structs
         * are read inline, as XNA writes a value type; anything else goes through
         * `ContentReader::ReadObject`, which consumes the reference type's own reader index first.
         * An enum needs @ref EnumField instead, because its .NET name cannot be recovered here.
         *
         * @tparam TMember The member's type.
         * @param member Pointer to the member.
         * @return This builder, for chaining.
         */
        template <typename TMember>
        ReflectiveTypeReaderBuilder& Field(TMember T::*member)
        {
            static_assert(!std::is_enum_v<TMember>,
                          "Use EnumField(member, \"Namespace.Enum\"): an enum's .NET name is "
                          "needed to register the EnumReader the .xnb's table names.");
            fields_.push_back([member](T& target, ContentReader& input) {
                target.*member = ReadMember<TMember>(input);
            });
            return *this;
        }

        /**
         * @brief Declares the next field, which is an enum.
         * @tparam TEnum The enum type.
         * @param member       Pointer to the member.
         * @param enumTypeName The .NET name of the enum, as the `.xnb` spells it.
         * @return This builder, for chaining.
         */
        template <typename TEnum>
        ReflectiveTypeReaderBuilder& EnumField(TEnum T::*member, std::string enumTypeName)
        {
            static_assert(std::is_enum_v<TEnum>, "EnumField requires an enum member.");
            fields_.push_back([member](T& target, ContentReader& input) {
                target.*member = static_cast<TEnum>(input.ReadInt32());
            });
            // The canonical name is computed BEFORE the lambda is built. Passing both as
            // arguments to emplace_back would leave their evaluation order unspecified, and the
            // lambda moves from enumTypeName -- which on one ordering registers the reader under
            // an empty name and leaves the .xnb's table unresolved at load time.
            std::string canonicalName =
                EnumTypeReader<TEnum>::CanonicalReaderName(enumTypeName);
            ContentTypeReaderManager::ReaderFactory factory =
                [name = std::move(enumTypeName)] {
                    return std::unique_ptr<ContentTypeReaderBase>(
                        std::make_unique<EnumTypeReader<TEnum>>(name));
                };
            enumRegistrations_.emplace_back(std::move(canonicalName), std::move(factory));
            return *this;
        }

        /**
         * @brief Registers the reflective reader and every enum reader the type needs.
         *
         * Safe to call more than once: `AddTypeCreator` replaces an entry of the same name.
         */
        void Register()
        {
            for (auto& registration : enumRegistrations_)
                ContentTypeReaderManager::AddTypeCreator(registration.first, registration.second);

            ContentTypeReaderManager::AddTypeCreator(
                ReflectiveTypeReader<T>::CanonicalReaderName(targetTypeName_),
                [name = targetTypeName_, fields = fields_] {
                    return std::unique_ptr<ContentTypeReaderBase>(
                        std::make_unique<ReflectiveTypeReader<T>>(name, fields));
                });
        }

    private:
        template <typename TMember>
        static TMember ReadMember(ContentReader& input)
        {
            if constexpr (std::is_same_v<TMember, bool>)
                return input.ReadBoolean();
            else if constexpr (std::is_same_v<TMember, float>)
                return input.ReadSingle();
            else if constexpr (std::is_same_v<TMember, double>)
                return input.ReadDouble();
            else if constexpr (std::is_same_v<TMember, std::int32_t>)
                return input.ReadInt32();
            else if constexpr (std::is_same_v<TMember, std::uint32_t>)
                return input.ReadUInt32();
            else if constexpr (std::is_same_v<TMember, std::int64_t>)
                return input.ReadInt64();
            else if constexpr (std::is_same_v<TMember, std::uint8_t>)
                return input.ReadByte();
            else if constexpr (std::is_same_v<TMember, Vector2>)
                return input.ReadVector2();
            else if constexpr (std::is_same_v<TMember, Vector3>)
                return input.ReadVector3();
            else if constexpr (std::is_same_v<TMember, Vector4>)
                return input.ReadVector4();
            else if constexpr (std::is_same_v<TMember, Matrix>)
                return input.ReadMatrix();
            else if constexpr (std::is_same_v<TMember, Quaternion>)
                return input.ReadQuaternion();
            else if constexpr (std::is_same_v<TMember, Color>)
                return input.ReadColor();
            else
                // Everything else is a .NET reference type -- a string, a nested object, a list --
                // and XNA writes those with their own type-reader index in front.
                return input.template ReadObject<TMember>();
        }

        std::string targetTypeName_;
        std::vector<typename ReflectiveTypeReader<T>::FieldReader> fields_;
        std::vector<std::pair<std::string, ContentTypeReaderManager::ReaderFactory>>
            enumRegistrations_;
    };
}
