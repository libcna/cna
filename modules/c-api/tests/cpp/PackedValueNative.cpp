// SPDX-License-Identifier: MS-PL
#include "CNA/C/packed_vectors.h"
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
#include <cstring>
#include <thread>
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
template <typename T> uint64_t answer(uint64_t bits, int32_t *hash, char *text)
{
    T value;
    value.setPackedValueProperty(static_cast<decltype(value.getPackedValueProperty())>(bits));
    *hash = value.GetHashCode();
    const auto str = value.ToString();
    std::memcpy(text, str.data(), str.size());
    return str.size();
}
extern "C" uint64_t cna_test_packed_native(uint32_t format, uint64_t bits, int32_t *hash, char *text)
{
    switch (format) {
    case 0:
        return answer<Alpha8>(bits, hash, text);
    case 1:
        return answer<Bgr565>(bits, hash, text);
    case 2:
        return answer<Bgra4444>(bits, hash, text);
    case 3:
        return answer<Bgra5551>(bits, hash, text);
    case 4:
        return answer<Byte4>(bits, hash, text);
    case 5:
        return answer<HalfSingle>(bits, hash, text);
    case 6:
        return answer<HalfVector2>(bits, hash, text);
    case 7:
        return answer<HalfVector4>(bits, hash, text);
    case 8:
        return answer<NormalizedByte2>(bits, hash, text);
    case 9:
        return answer<NormalizedByte4>(bits, hash, text);
    case 10:
        return answer<NormalizedShort2>(bits, hash, text);
    case 11:
        return answer<NormalizedShort4>(bits, hash, text);
    case 12:
        return answer<Rg32>(bits, hash, text);
    case 13:
        return answer<Rgba1010102>(bits, hash, text);
    case 14:
        return answer<Rgba64>(bits, hash, text);
    case 15:
        return answer<Short2>(bits, hash, text);
    case 16:
        return answer<Short4>(bits, hash, text);
    default:
        return 0;
    }
}
extern "C" int cna_test_packed_worker(int (*test)(void))
{
    int result = 0;
    std::thread worker([&] { result = test(); });
    worker.join();
    return result;
}
