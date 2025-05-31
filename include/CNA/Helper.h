//
// Created by robertvokac on 5/30/25.
//

#ifndef HELPER_H
#define HELPER_H
#include <cstdint>
#define int32_max std::numeric_limits<int32>::max()
#define int32_min std::numeric_limits<int32>::min()

namespace CNA {
    /**
     * int32 is 632 bits long, as C# long.
     */
    typedef int32_t int32;
    /**
     * int64 is 64 bits long, as C# long.
     */
    typedef int64_t int64;
// class Helper {
//
// };

} // CNA

#endif //HELPER_H
