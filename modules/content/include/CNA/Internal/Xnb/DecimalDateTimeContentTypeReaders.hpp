// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "System/ArgumentException.hpp"
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
            // XNA DecimalReader.Read IL_0000-0024 passes four Int32 to new Decimal(int[]), whose
            // SetBits (mscorlib 4.0 IL_0018-002d, IL_0052-0061) refuses any flag bit outside the
            // sign and the scale, or a scale above 28, with ArgumentException.
            const std::int32_t lo = input.ReadInt32();
            const std::int32_t mid = input.ReadInt32();
            const std::int32_t hi = input.ReadInt32();
            const auto flags = static_cast<std::uint32_t>(input.ReadInt32());
            if ((flags & 0x7F00FFFFu) != 0u || (flags & 0x00FF0000u) > 0x001C0000u)
            {
                throw System::ArgumentException(
                    "Decimal byte array constructor requires an array of length four containing "
                    "valid decimal bytes.");
            }
            return System::Decimal(lo, mid, hi, (flags & 0x80000000u) != 0u,
                                   static_cast<SharpRuntime::bytecs>((flags >> 16) & 0xFFu));
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
