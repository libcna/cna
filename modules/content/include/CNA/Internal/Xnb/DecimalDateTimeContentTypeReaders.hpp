// SPDX-License-Identifier: MS-PL
#pragma once

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"

// plans/plan_xnb.md XNB-18B/18C: Decimal/DateTime/TimeSpan .xnb readers -- see
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
        DateTimeReader() : ContentTypeReader<System::DateTime>("System.DateTime") {}
    protected:
        /**
         * @brief FNA's DateTimeReader: reads a UInt64, masking out the top 2 bits as
         *        `DateTimeKind` and the rest as ticks.
         *
         * Deviation: `DateTimeKind` is parsed but discarded -- `System::DateTime` is documented
         * `Status: Partial` and does not store/track `DateTimeKind` at all (a pre-existing
         * sharp-runtime limitation, not a `.xnb`-specific one; fixing it would mean adding a new
         * field and touching every existing `DateTime` constructor, out of scope for this reader).
         */
        System::DateTime Read(ContentReader& input, std::optional<System::DateTime>) override
        {
            const uint64_t value = input.ReadUInt64();
            constexpr uint64_t kindMask = uint64_t{3} << 62;
            const int64_t ticks = static_cast<int64_t>(value & ~kindMask);
            return System::DateTime(ticks);
        }
    };

    /** @brief Registers Decimal/TimeSpan/DateTime readers above under their real FNA canonical names. Idempotent. */
    void RegisterDecimalDateTimeXnbReaders();
}
