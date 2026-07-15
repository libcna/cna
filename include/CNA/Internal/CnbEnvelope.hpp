// SPDX-License-Identifier: MS-PL
#pragma once

#include <cctype>
#include <cstddef>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Internal
{
    /**
     * @brief Parsed top-level fields of a `.cnb` content envelope (see `cnb.md`).
     *
     * A `.cnb` file is a JSON document whose top level always carries a `"cnbVersion"`
     * schema version and a `"type"` identifying which per-type CNA loader should handle
     * the rest of the document, plus an optional `"sourceFile"` naming another asset this
     * document is metadata for (see `cnb.md`'s `sourceFile` section). Everything else in a
     * `.cnb` document is per-type and parsed by that type's own reader, not by this struct.
     */
    struct CnbEnvelope
    {
        /** @brief Whether the document had a top-level `"cnbVersion"` field. */
        bool hasCnbVersion = false;

        /** @brief Parsed `"cnbVersion"` value, or 0 if the field was absent. */
        int cnbVersion = 0;

        /** @brief Whether the document had a top-level `"type"` field. */
        bool hasType = false;

        /** @brief Parsed `"type"` value, or empty if the field was absent. */
        std::string type;

        /** @brief Whether the document had a top-level `"sourceFile"` field. */
        bool hasSourceFile = false;

        /** @brief Parsed `"sourceFile"` value, or empty if the field was absent. */
        std::string sourceFile;
    };

    namespace Detail
    {
        inline bool JsonFindKeyColon(const std::string& json, const std::string& key, std::size_t& colonPos)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = json.find(needle);
            if (pos == std::string::npos) return false;
            pos = json.find(':', pos + needle.size());
            if (pos == std::string::npos) return false;
            colonPos = pos;
            return true;
        }

        inline bool JsonExtractString(const std::string& json, const std::string& key, std::string& out)
        {
            std::size_t colon;
            if (!JsonFindKeyColon(json, key, colon)) return false;
            auto pos = json.find('"', colon + 1);
            if (pos == std::string::npos) return false;
            auto end = json.find('"', pos + 1);
            if (end == std::string::npos) return false;
            out = json.substr(pos + 1, end - pos - 1);
            return true;
        }

        inline bool JsonExtractInt(const std::string& json, const std::string& key, int& out)
        {
            std::size_t colon;
            if (!JsonFindKeyColon(json, key, colon)) return false;
            std::size_t pos = colon + 1;
            while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
            if (pos >= json.size()) return false;
            try { out = std::stoi(json.substr(pos)); return true; } catch (...) { return false; }
        }
    }

    /**
     * @brief Parses the top-level `.cnb` envelope fields out of a raw JSON document.
     *
     * Pure extraction -- never throws, and is tolerant of a malformed or partial document
     * (missing fields simply leave the corresponding `hasXxx` flag false). Use
     * ValidateCnbEnvelope() separately to enforce well-formedness.
     *
     * @param json Raw `.cnb` file contents.
     * @return The parsed envelope, with a `hasXxx` flag set for each field actually found.
     */
    inline CnbEnvelope ParseCnbEnvelope(const std::string& json)
    {
        CnbEnvelope env;
        env.hasCnbVersion = Detail::JsonExtractInt(json, "cnbVersion", env.cnbVersion);
        env.hasType = Detail::JsonExtractString(json, "type", env.type);
        env.hasSourceFile = Detail::JsonExtractString(json, "sourceFile", env.sourceFile);
        return env;
    }

    /**
     * @brief Validates a parsed `.cnb` envelope against the type a reader expected.
     *
     * Throws Microsoft::Xna::Framework::Content::ContentLoadException if `"cnbVersion"` or
     * `"type"` is missing, or if `"type"` does not match @p expectedType exactly. This is
     * the integrity check described in `cnb.md`'s "Note on dispatch" -- the primary dispatch
     * key stays the caller's compile-time `T` at the `Load<T>()` call site; this only catches
     * a `.cnb` file that doesn't actually hold what the caller asked for.
     *
     * @param envelope     Envelope previously returned by ParseCnbEnvelope().
     * @param expectedType The type name the calling reader produces (e.g. `"SpriteFont"`).
     * @param path         File path, used only to build the exception message.
     */
    inline void ValidateCnbEnvelope(const CnbEnvelope& envelope,
                                     const std::string& expectedType,
                                     const std::string& path)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        if (!envelope.hasCnbVersion)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'cnbVersion' field.");
        }
        if (!envelope.hasType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'type' field.");
        }
        if (envelope.type != expectedType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' has type '" + envelope.type +
                "', but was requested as '" + expectedType + "'.");
        }
    }
}
