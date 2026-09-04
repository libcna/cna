// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbFileOptions.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief How the exact spelling of one reader/target type name was established
     *        (plans/plan_xnapipeline.md §2.5, §3).
     *
     * Recorded per entry so the emitted type-reader table never presents a guess as a fact, and
     * so a future correction is a one-line data change.
     */
    enum class XnbNameEvidence
    {
        /** @brief Read out of the committed genuine Microsoft XNA 4.0 Content Pipeline fixture. */
        Xna40Fixture,
        /** @brief Read out of a committed externally produced MonoGame fixture. */
        MonoGameFixture,
        /**
         * @brief Derived from the rule the fixtures establish (assembly-qualify a reader that does
         *        not live in `Microsoft.Xna.Framework`; always assembly-qualify generic arguments)
         *        applied to a type no committed fixture exercises.
         */
        DerivedRule,
    };

    /** @brief The XNA 4.0 assembly a reader or target type belongs to. */
    enum class XnbAssembly
    {
        /** @brief No assembly qualification is emitted for this name. */
        None,
        /** @brief `mscorlib` -- the .NET base class library. */
        Mscorlib,
        /** @brief `Microsoft.Xna.Framework` -- the XNA core assembly. */
        Framework,
        /** @brief `Microsoft.Xna.Framework.Graphics`. */
        FrameworkGraphics,
    };

    /**
     * @brief One reader identity: what CNA writes into the type-reader table, and why.
     *
     * `readerBaseName` and `targetBaseName` are the assembly-free .NET names. Assembly
     * qualification is applied by @ref FormatXnbReaderName according to the selected
     * @ref XnbReaderNameStyle, so the same identity serves both spellings.
     */
    struct XnbReaderIdentity
    {
        /** @brief Assembly-free reader type name, e.g. `Microsoft.Xna.Framework.Content.Texture2DReader`. */
        std::string readerBaseName;

        /** @brief Assembly hosting the reader, or @ref XnbAssembly::None when never qualified. */
        XnbAssembly readerAssembly = XnbAssembly::None;

        /** @brief Assembly-free target type name, e.g. `Microsoft.Xna.Framework.Graphics.Texture2D`. */
        std::string targetBaseName;

        /** @brief Assembly hosting the target type, used when this identity is a generic argument. */
        XnbAssembly targetAssembly = XnbAssembly::None;

        /** @brief Reader version emitted after the name; every built-in XNA 4.0 reader uses zero. */
        std::int32_t readerVersion = 0;

        /** @brief Generic arguments in order, each itself a complete identity. */
        std::vector<XnbReaderIdentity> genericArguments;

        /**
         * @brief Whether @ref genericArguments are also the *target* type's own arguments.
         *
         * True for `List<T>`, `Dictionary<K,V>` and `Nullable<T>`, where the reader and the type
         * it produces are generic over the same arguments and share one spelling. False where
         * only the reader is generic: `EnumReader\`1[[SurfaceFormat]]` produces the plain,
         * non-generic `SurfaceFormat`, and `ArrayReader\`1[[Int32]]` produces `Int32[]`, whose
         * element type is already spelled inside @ref targetBaseName. Setting it false stops the
         * argument list being appended a second time when this identity appears as a nested
         * generic argument or is asked for its target type name.
         */
        bool targetSharesGenericArguments = true;

        /** @brief How this entry's spelling was established. */
        XnbNameEvidence evidence = XnbNameEvidence::DerivedRule;

        /** @brief Compares the complete identity including generic arguments and evidence. */
        bool operator==(const XnbReaderIdentity& other) const = default;
    };

    /**
     * @brief Returns the assembly-qualification suffix for one assembly.
     *
     * @param assembly The assembly to qualify with.
     * @return A suffix beginning `", "`, or an empty string for @ref XnbAssembly::None.
     */
    [[nodiscard]] std::string XnbAssemblyQualifier(XnbAssembly assembly);

    /**
     * @brief Formats the reader type name exactly as it must appear in the type-reader table.
     *
     * Under @ref XnbReaderNameStyle::Xna40 the reader type carries its assembly qualifier only
     * when @ref XnbReaderIdentity::readerAssembly is not @ref XnbAssembly::None, and every generic
     * argument is fully assembly-qualified using its own target assembly. Under
     * @ref XnbReaderNameStyle::Portable no assembly qualification is emitted anywhere.
     *
     * @param identity The reader identity to format.
     * @param style The configured name spelling.
     * @return The complete type-reader table entry name.
     */
    [[nodiscard]] std::string FormatXnbReaderName(const XnbReaderIdentity& identity,
                                                  XnbReaderNameStyle style);

    /**
     * @brief Formats the assembly-free canonical name CNA's own reader registry is keyed by.
     *
     * This is the exact string `NormalizeXnbTypeReaderName()` produces from
     * @ref FormatXnbReaderName's output, so a writer and the runtime registry can be checked
     * against each other without parsing.
     *
     * @param identity The reader identity to format.
     * @return The canonical assembly-free registry key.
     */
    [[nodiscard]] std::string XnbCanonicalReaderName(const XnbReaderIdentity& identity);

    /**
     * @brief Formats the assembly-free .NET name of the target type this reader produces.
     *
     * @param identity The reader identity to format.
     * @return The target type name, with generic arguments in `[[...]]` form when present.
     */
    [[nodiscard]] std::string XnbTargetTypeName(const XnbReaderIdentity& identity);
}
