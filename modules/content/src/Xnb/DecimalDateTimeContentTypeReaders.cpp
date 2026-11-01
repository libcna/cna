// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"

#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/TimeZone.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    DateTimeReader::DateTimeReader() : ContentTypeReader<System::DateTime>("System.DateTime") {}

    System::DateTime DateTimeReader::Read(ContentReader& input, std::optional<System::DateTime>)
    {
        const std::uint64_t value = input.ReadUInt64();
        const auto ticks = static_cast<SharpRuntime::longcs>(value & 0x3fffffffffffffffULL);
        const auto kind = static_cast<System::DateTimeKind>(value >> 62);
        if (kind == System::DateTimeKind::Local)
        {
            // Genuine XNA converts local payloads from UTC; FNA constructs Local directly.
            return System::DateTime(ticks, System::DateTimeKind::Utc)
                .ToLocalTime(System::TimeZone::CurrentTimeZone());
        }
        return System::DateTime(ticks, kind);
    }

    void RegisterDecimalDateTimeXnbReaders()
    {
#if SHARP_RUNTIME_HAS_NATIVE_INT128
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DecimalReader", [] { return std::make_unique<DecimalReader>(); });
#endif
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.TimeSpanReader", [] { return std::make_unique<TimeSpanReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DateTimeReader", [] { return std::make_unique<DateTimeReader>(); });
    }
}
