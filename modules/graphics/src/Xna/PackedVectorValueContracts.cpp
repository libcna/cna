// SPDX-License-Identifier: MS-PL
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    namespace
    {
        /// Reproduces .NET's `value.ToString("X<digits>", CultureInfo.InvariantCulture)`: uppercase
        /// hexadecimal, zero-padded to the channel storage width. Every XNA packed-vector ToString()
        /// but the three Half* ones is exactly this, with `digits` set to two per storage byte.
        std::string PackedHex(std::uint64_t value, int digits)
        {
            std::ostringstream text;
            text << std::uppercase << std::hex << std::setfill('0') << std::setw(digits) << value;
            return text.str();
        }
    }

    std::string Alpha8::ToString() const { return PackedHex(packedValue_, 2); }

    std::string Bgr565::ToString() const { return PackedHex(packedValue_, 4); }

    std::string Bgra4444::ToString() const { return PackedHex(packedValue_, 4); }

    std::string Bgra5551::ToString() const { return PackedHex(packedValue_, 4); }

    std::string Byte4::ToString() const { return PackedHex(packedValue_, 8); }

    std::string NormalizedByte2::ToString() const { return PackedHex(packedValue_, 4); }

    std::string NormalizedByte4::ToString() const { return PackedHex(packedValue_, 8); }

    std::string NormalizedShort2::ToString() const { return PackedHex(packedValue_, 8); }

    std::string NormalizedShort4::ToString() const { return PackedHex(packedValue_, 16); }

    std::string Rg32::ToString() const { return PackedHex(packedValue_, 8); }

    std::string Rgba1010102::ToString() const { return PackedHex(packedValue_, 8); }

    std::string Rgba64::ToString() const { return PackedHex(packedValue_, 16); }

    std::string Short2::ToString() const { return PackedHex(packedValue_, 8); }

    std::string Short4::ToString() const { return PackedHex(packedValue_, 16); }

    // The three Half* types format the expanded value rather than the packed bits, so each one
    // inherits whatever float formatting CNA already applies to that value's own type.
    std::string HalfSingle::ToString() const
    {
        std::ostringstream text;
        text << ToSingle();
        return text.str();
    }

    std::string HalfVector2::ToString() const { return ToVector2().ToString(); }

    std::string HalfVector4::ToString() const { return ToVector4().ToString(); }
}
