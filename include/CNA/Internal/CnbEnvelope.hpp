// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/Internal/Json.hpp"
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

        /** @brief Parsed `"cnbVersion"` value truncated to `int`, or 0 if the field was absent. */
        int cnbVersion = 0;

        /**
         * @brief Exact parsed `"cnbVersion"` value as a double, or 0.0 if absent.
         *
         * Kept separately from @ref cnbVersion (which truncates) so ValidateCnbEnvelope() can
         * strictly reject a non-integer version like `1.5` instead of silently accepting it as
         * `1`.
         */
        double cnbVersionRaw = 0.0;

        /** @brief Whether the document had a top-level `"type"` field. */
        bool hasType = false;

        /** @brief Parsed `"type"` value, or empty if the field was absent. */
        std::string type;

        /** @brief Whether the document had a top-level `"sourceFile"` field. */
        bool hasSourceFile = false;

        /** @brief Parsed `"sourceFile"` value, or empty if the field was absent. */
        std::string sourceFile;

        /**
         * @brief Diagnostic set when the document could not be parsed as JSON at all, or its
         *        root value was not a JSON object. Empty on successful parsing, even if
         *        individual expected fields are still missing.
         */
        std::string parseErrorDetail;
    };

    /**
     * @brief Parses the top-level `.cnb` envelope fields out of a raw JSON document.
     *
     * Pure extraction -- never throws. A malformed document or a non-object root leaves every
     * `hasXxx` flag false and records @ref CnbEnvelope::parseErrorDetail "parseErrorDetail";
     * a well-formed object missing individual fields leaves just those flags false. Use
     * ValidateCnbEnvelope() separately to enforce well-formedness and throw on either case.
     *
     * @param json Raw `.cnb` file contents.
     * @return The parsed envelope, with a `hasXxx` flag set for each field actually found.
     */
    inline CnbEnvelope ParseCnbEnvelope(const std::string& json)
    {
        CnbEnvelope env;

        JsonValue root;
        try
        {
            root = ParseJson(json);
        }
        catch (const JsonParseException& e)
        {
            env.parseErrorDetail = std::string("not valid JSON (") + e.what() + ")";
            return env;
        }

        if (!root.IsObject())
        {
            env.parseErrorDetail = "root value is not a JSON object";
            return env;
        }

        if (const JsonValue* v = root.FindMember("cnbVersion"))
        {
            if (v->IsNumber())
            {
                env.hasCnbVersion = true;
                env.cnbVersionRaw = v->numberValue;
                env.cnbVersion = static_cast<int>(v->numberValue);
            }
        }
        if (const JsonValue* v = root.FindMember("type"))
        {
            if (v->IsString())
            {
                env.hasType = true;
                env.type = v->stringValue;
            }
        }
        if (const JsonValue* v = root.FindMember("sourceFile"))
        {
            if (v->IsString())
            {
                env.hasSourceFile = true;
                env.sourceFile = v->stringValue;
            }
        }

        return env;
    }

    /**
     * @brief Validates an envelope's baseline requirements: well-formed JSON, a supported
     *        `"cnbVersion"`, and a present `"type"` -- without checking @p envelope's `"type"`
     *        value against any specific expectation.
     *
     * Shared by ValidateCnbEnvelope() (which additionally checks `"type"` equality against a
     * fixed expected value) and `ContentManager::RegisterCnbLoader<T>()`'s dispatch path (which
     * uses `"type"` as a runtime lookup key instead of an equality check, so it cannot use
     * ValidateCnbEnvelope() directly).
     *
     * @param envelope Envelope previously returned by ParseCnbEnvelope().
     * @param path     File path, used only to build exception messages.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document was not
     *         valid JSON, its root was not an object, `"cnbVersion"` is missing or is not
     *         exactly `1` (the only currently-supported envelope version), or `"type"` is
     *         missing.
     */
    inline void ValidateCnbEnvelopeBaseline(const CnbEnvelope& envelope, const std::string& path)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        if (!envelope.parseErrorDetail.empty())
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' could not be parsed as a .cnb document (" +
                envelope.parseErrorDetail + ").");
        }

        if (!envelope.hasCnbVersion)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'cnbVersion' field.");
        }

        constexpr double kSupportedCnbVersion = 1.0;
        if (envelope.cnbVersionRaw != kSupportedCnbVersion)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' has unsupported 'cnbVersion' (" +
                std::to_string(envelope.cnbVersionRaw) + "); only version 1 is supported.");
        }

        if (!envelope.hasType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'type' field.");
        }
    }

    /**
     * @brief Validates a parsed `.cnb` envelope against the type a reader expected.
     *
     * Calls ValidateCnbEnvelopeBaseline() first, then additionally checks that `"type"` matches
     * @p expectedType exactly. This is the integrity check described in `cnb.md`'s "Note on
     * dispatch" -- the primary dispatch key stays the caller's compile-time `T` at the
     * `Load<T>()` call site; this only catches a `.cnb` file that doesn't actually hold what the
     * caller asked for.
     *
     * @param envelope     Envelope previously returned by ParseCnbEnvelope().
     * @param expectedType The type name the calling reader produces (e.g. `"SpriteFont"`).
     * @param path         File path, used only to build the exception message.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException for any
     *         ValidateCnbEnvelopeBaseline() failure, or if `"type"` does not equal
     *         @p expectedType.
     */
    inline void ValidateCnbEnvelope(const CnbEnvelope& envelope,
                                     const std::string& expectedType,
                                     const std::string& path)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        ValidateCnbEnvelopeBaseline(envelope, path);

        if (envelope.type != expectedType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' has type '" + envelope.type +
                "', but was requested as '" + expectedType + "'.");
        }
    }
}
