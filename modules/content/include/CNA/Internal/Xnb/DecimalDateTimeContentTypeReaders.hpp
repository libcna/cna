// SPDX-License-Identifier: MS-PL
#pragma once

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

// XNB-18B/18C: Decimal/DateTime/TimeSpan .xnb readers -- see
// PrimitiveContentTypeReaders.hpp's own note on why these live in CNA::Internal::Xnb.
//
// System::Decimal requires a native 128-bit integer. Keep its reader on sharp-runtime's public
// compiler-capability boundary rather than making TimeSpanReader and DateTimeReader depend on it.
#if SHARP_RUNTIME_HAS_NATIVE_INT128
#include "System/Decimal.hpp"
#endif

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;

#if SHARP_RUNTIME_HAS_NATIVE_INT128
    class DecimalReader : public ContentTypeReader<System::Decimal>
    {
    public:
        DecimalReader() : ContentTypeReader<System::Decimal>("System.Decimal") {}
    protected:
        System::Decimal Read(ContentReader& input, std::optional<System::Decimal>) override
        {
            return input.ReadDecimal();
        }
    };
#endif

    class TimeSpanReader : public ContentTypeReader<System::TimeSpan>
    {
    public:
        TimeSpanReader() : ContentTypeReader<System::TimeSpan>("System.TimeSpan") {}
    protected:
        /** @brief FNA's TimeSpanReader: a plain Int64 tick count. */
        System::TimeSpan Read(ContentReader& input, std::optional<System::TimeSpan>) override
        {
            return System::TimeSpan(input.ReadInt64());
        }
    };

    class DateTimeReader : public ContentTypeReader<System::DateTime>
    {
    public:
        /** @brief Creates a reader for System.DateTime values. */
        DateTimeReader();
    protected:
        /**
         * @brief Reads ticks and Kind, restoring local values from UTC payload ticks.
         * @param input Reader positioned at the encoded value.
         * @param existingInstance Ignored existing value.
         * @return The decoded DateTime in its encoded kind.
         */
        System::DateTime Read(ContentReader& input, std::optional<System::DateTime> existingInstance) override;
    };

    /** @brief Registers Decimal/TimeSpan/DateTime readers above under their real FNA canonical names. Idempotent. */
    void RegisterDecimalDateTimeXnbReaders();
}
