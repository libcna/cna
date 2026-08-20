// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Internal
{
    /**
     * @brief Parsed top-level fields of a `.cnj` content envelope (see `cnj.md`).
     *
     * A `.cnj` file is a JSON document whose top level always carries a `"cnjVersion"`
     * schema version and a `"type"` identifying which per-type CNA loader should handle
     * the rest of the document, plus an optional `"sourceFile"` naming another asset this
     * document is metadata for (see `cnj.md`'s `sourceFile` section). Everything else in a
     * `.cnj` document is per-type and parsed by that type's own reader, not by this struct.
     */
    struct CnjEnvelope
    {
        /** @brief Whether the document had a top-level `"cnjVersion"` field. */
        bool hasCnjVersion = false;

        /** @brief Parsed `"cnjVersion"` value truncated to `int`, or 0 if the field was absent. */
        int cnjVersion = 0;

        /**
         * @brief Exact parsed `"cnjVersion"` value as a double, or 0.0 if absent.
         *
         * Kept separately from @ref cnjVersion (which truncates) so ValidateCnjEnvelope() can
         * strictly reject a non-integer version like `1.5` instead of silently accepting it as
         * `1`.
         */
        double cnjVersionRaw = 0.0;

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
     * @brief Parses the top-level `.cnj` envelope fields out of a raw JSON document.
     *
     * Pure extraction -- never throws. A malformed document or a non-object root leaves every
     * `hasXxx` flag false and records @ref CnjEnvelope::parseErrorDetail "parseErrorDetail";
     * a well-formed object missing individual fields leaves just those flags false. Use
     * ValidateCnjEnvelope() separately to enforce well-formedness and throw on either case.
     *
     * @param json Raw `.cnj` file contents.
     * @return The parsed envelope, with a `hasXxx` flag set for each field actually found.
     */
    inline CnjEnvelope ParseCnjEnvelope(const std::string& json)
    {
        CnjEnvelope env;

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

        if (const JsonValue* v = root.FindMember("cnjVersion"))
        {
            if (v->IsNumber())
            {
                env.hasCnjVersion = true;
                env.cnjVersionRaw = v->numberValue;
                env.cnjVersion = static_cast<int>(v->numberValue);
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
     *        `"cnjVersion"`, and a present `"type"` -- without checking @p envelope's `"type"`
     *        value against any specific expectation.
     *
     * Shared by ValidateCnjEnvelope() (which additionally checks `"type"` equality against a
     * fixed expected value) and `ContentManager::RegisterCnjLoader<T>()`'s dispatch path (which
     * uses `"type"` as a runtime lookup key instead of an equality check, so it cannot use
     * ValidateCnjEnvelope() directly).
     *
     * @param envelope   Envelope previously returned by ParseCnjEnvelope().
     * @param path       File path, used only to build exception messages.
     * @param maxVersion Highest `"cnjVersion"` this document type understands. Defaults to 1, the
     *        original format and still the only version every type other than `Model` writes.
     *        `Model` passes 2, the version that adds the `"bones"` hierarchy and the per-mesh
     *        `"parentBone"` index (plans/plan_gltf.md GLTF-129, Phase 5). Scoping the ceiling per type
     *        rather than raising it globally keeps "a future version is rejected" true for every
     *        type that has not actually defined one.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document was not
     *         valid JSON, its root was not an object, `"cnjVersion"` is missing, is not an integer
     *         in `[1, maxVersion]`, or `"type"` is missing.
     */
    inline void ValidateCnjEnvelopeBaseline(const CnjEnvelope& envelope, const std::string& path,
                                             int maxVersion = 1)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        if (!envelope.parseErrorDetail.empty())
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' could not be parsed as a .cnj document (" +
                envelope.parseErrorDetail + ").");
        }

        if (!envelope.hasCnjVersion)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'cnjVersion' field.");
        }

        // cnjVersionRaw rather than cnjVersion so a non-integer version like 1.5 is still
        // rejected outright instead of truncating into the supported range.
        const double highest = static_cast<double>(maxVersion);
        if (!(envelope.cnjVersionRaw >= 1.0 && envelope.cnjVersionRaw <= highest) ||
            envelope.cnjVersionRaw != static_cast<double>(static_cast<int>(envelope.cnjVersionRaw)))
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' has unsupported 'cnjVersion' (" +
                std::to_string(envelope.cnjVersionRaw) + "); only version" +
                (maxVersion > 1 ? "s 1 to " + std::to_string(maxVersion) + " are"
                                : std::string(" 1 is")) + " supported.");
        }

        if (!envelope.hasType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' is missing the required 'type' field.");
        }
    }

    /**
     * @brief Validates a parsed `.cnj` envelope against the type a reader expected.
     *
     * Calls ValidateCnjEnvelopeBaseline() first, then additionally checks that `"type"` matches
     * @p expectedType exactly. This is the integrity check described in `cnj.md`'s "Note on
     * dispatch" -- the primary dispatch key stays the caller's compile-time `T` at the
     * `Load<T>()` call site; this only catches a `.cnj` file that doesn't actually hold what the
     * caller asked for.
     *
     * @param envelope     Envelope previously returned by ParseCnjEnvelope().
     * @param expectedType The type name the calling reader produces (e.g. `"SpriteFont"`).
     * @param path         File path, used only to build the exception message.
     * @param maxVersion   Highest `"cnjVersion"` this reader understands (see
     *        ValidateCnjEnvelopeBaseline()). Defaults to 1.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException for any
     *         ValidateCnjEnvelopeBaseline() failure, or if `"type"` does not equal
     *         @p expectedType.
     */
    inline void ValidateCnjEnvelope(const CnjEnvelope& envelope,
                                     const std::string& expectedType,
                                     const std::string& path,
                                     int maxVersion = 1)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        ValidateCnjEnvelopeBaseline(envelope, path, maxVersion);

        if (envelope.type != expectedType)
        {
            throw ContentLoadException(
                "ContentManager: '" + path + "' has type '" + envelope.type +
                "', but was requested as '" + expectedType + "'.");
        }
    }
}
