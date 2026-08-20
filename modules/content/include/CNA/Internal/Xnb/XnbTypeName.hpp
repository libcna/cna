// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/Internal/Xnb/XnbReadLimits.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief A parsed, assembly-stripped .NET type name (plans/plan_xnb.md XNB-13), used as the
     *        canonical `.xnb` type-reader registry key decided in XNB-5.
     *
     * Represents `Namespace.TypeName` (optionally with a generic-arity backtick suffix like
     * `` `1 ``, kept as plain text of @ref baseName since it needs no special parsing) plus,
     * for a generic type, its type arguments -- each itself a fully parsed, recursively
     * assembly-stripped XnbTypeName, matching how .NET's own `AssemblyQualifiedName` nests
     * (`Dictionary\`2[[TKey,...],[TValue,...]], AssemblyOfDictionary, ...`).
     */
    struct XnbTypeName
    {
        /** @brief Namespace + type name + generic-arity suffix if any, with no assembly qualification. */
        std::string baseName;

        /** @brief Type arguments, in order; empty for a non-generic type. */
        std::vector<XnbTypeName> genericArguments;

        /**
         * @brief Reconstructs the canonical registry key: @ref baseName, plus
         *        `[[arg1],[arg2],...]` (each arg itself canonicalized the same way) if this is
         *        a generic type.
         */
        [[nodiscard]] std::string ToCanonicalString() const
        {
            if (genericArguments.empty())
            {
                return baseName;
            }

            std::string result = baseName;
            result += '[';
            for (std::size_t i = 0; i < genericArguments.size(); ++i)
            {
                if (i > 0) result += ',';
                result += '[';
                result += genericArguments[i].ToCanonicalString();
                result += ']';
            }
            result += ']';
            return result;
        }
    };

    namespace Detail
    {
        inline void SkipSpaces(std::string_view s, std::size_t& pos)
        {
            while (pos < s.size() && s[pos] == ' ') ++pos;
        }

        /**
         * @brief Parses one assembly-qualified .NET type name starting at @p pos, stopping at
         *        the end of @p s or at the first top-level `,`/`]` that isn't part of this
         *        type's own generic-argument list.
         *
         * REMED-CONTENT-006: recurses once per nested generic-argument level with no limit of its
         * own -- @p nestingDepth/@p maxDepth close that (a crafted type name costs ~4 bytes per
         * nesting level, so a sub-1MB file previously exhausted the C++ call stack; confirmed
         * empirically before this fix, see REMEDIATION_PROGRESS.md).
         *
         * @param nestingDepth Current nesting depth (0 at the top-level call). Deliberately not
         *                     named `depth` -- this function already has an unrelated local
         *                     `depth` used for bracket-matching inside the generic-argument loop
         *                     below, which would otherwise silently shadow it.
         * @param maxDepth     Bound from @c XnbReadLimits::maxObjectNestingDepth.
         */
        inline XnbTypeName ParseOne(std::string_view s, std::size_t& pos, int32_t nestingDepth, int32_t maxDepth)
        {
            if (nestingDepth > maxDepth)
            {
                throw std::invalid_argument(
                    "XnbTypeName: exceeds the maximum generic-argument nesting depth (" +
                    std::to_string(maxDepth) + ").");
            }

            XnbTypeName result;

            const std::size_t nameStart = pos;
            while (pos < s.size() && s[pos] != '[' && s[pos] != ',' && s[pos] != ']')
            {
                ++pos;
            }
            result.baseName = std::string(s.substr(nameStart, pos - nameStart));

            if (pos < s.size() && s[pos] == '[')
            {
                // Generic argument list: '[' '[' arg ']' (',' '[' arg ']')* ']'
                ++pos; // consume the outer '['
                while (true)
                {
                    SkipSpaces(s, pos);
                    if (pos >= s.size() || s[pos] != '[')
                    {
                        throw std::invalid_argument(
                            "XnbTypeName: expected '[' to start a generic argument.");
                    }
                    ++pos; // consume the argument's own '['

                    // Find this argument's matching ']' by bracket depth, since the argument's
                    // own text may contain further nested '[...]' from its own generics.
                    const std::size_t argStart = pos;
                    int depth = 1;
                    while (pos < s.size() && depth > 0)
                    {
                        if (s[pos] == '[') ++depth;
                        else if (s[pos] == ']') { --depth; if (depth == 0) break; }
                        ++pos;
                    }
                    if (depth != 0)
                    {
                        throw std::invalid_argument(
                            "XnbTypeName: unbalanced brackets in generic argument list.");
                    }
                    const std::string_view argText = s.substr(argStart, pos - argStart);
                    std::size_t argPos = 0;
                    result.genericArguments.push_back(ParseOne(argText, argPos, nestingDepth + 1, maxDepth));
                    ++pos; // consume the argument's closing ']'

                    SkipSpaces(s, pos);
                    if (pos < s.size() && s[pos] == ',')
                    {
                        ++pos;
                        continue;
                    }
                    if (pos < s.size() && s[pos] == ']')
                    {
                        ++pos; // consume the outer generic-argument-list ']'
                        break;
                    }
                    throw std::invalid_argument(
                        "XnbTypeName: expected ',' or ']' after a generic argument.");
                }
            }

            // Whatever remains (a top-level ',' followed by this type's own assembly/version/
            // culture/publicKeyToken qualifier, or nothing) is deliberately not consumed here --
            // it carries no information the canonical key needs, per the XNB-5 decision.
            return result;
        }
    }

    /**
     * @brief Parses a raw, possibly assembly-qualified `.xnb` type-reader name into its
     *        canonical, assembly-stripped form (plans/plan_xnb.md XNB-13).
     *
     * Handles nested generic-argument brackets correctly (`ListReader\`1[[Vector3, ...]]`,
     * `DictionaryReader\`2[[String, ...],[ListReader\`1[[Int32, ...]], ...]]`) -- a naive
     * `substr(0, name.find(','))` truncation breaks on every generic reader name, since commas
     * inside `[[...]]` are not the name/assembly boundary.
     *
     * @param rawTypeName The type-reader name exactly as read from a `.xnb` type-reader table
     *                    entry (or an already-bare name; the parser tolerates both).
     * @param limits      Bounds the generic-argument nesting depth (REMED-CONTENT-006,
     *                    plans/plan_xnb.md XNB-43); defaults to @ref DefaultXnbReadLimits().
     * @return The parsed name, ready for ToCanonicalString() or direct field inspection.
     * @throws std::invalid_argument if @p rawTypeName has unbalanced or malformed brackets, or
     *         nests generic arguments deeper than @p limits.maxObjectNestingDepth.
     */
    inline XnbTypeName ParseXnbTypeName(const std::string& rawTypeName,
                                         const XnbReadLimits& limits = DefaultXnbReadLimits())
    {
        std::size_t pos = 0;
        return Detail::ParseOne(rawTypeName, pos, 0, limits.maxObjectNestingDepth);
    }

    /**
     * @brief Convenience wrapper: parses @p rawTypeName and returns its canonical registry key
     *        string directly (see ParseXnbTypeName() and XnbTypeName::ToCanonicalString()).
     */
    inline std::string NormalizeXnbTypeReaderName(const std::string& rawTypeName,
                                                   const XnbReadLimits& limits = DefaultXnbReadLimits())
    {
        return ParseXnbTypeName(rawTypeName, limits).ToCanonicalString();
    }
}
