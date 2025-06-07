//
// Created by robertvokac on 5/30/25.
//
#ifndef CNAHELPER_H
#define CNAHELPER_H


#define CSINT_MAX std::numeric_limits<csint>::max()
#define CSINT_MIN std::numeric_limits<csint>::min()
#define CSLONG_MAX std::numeric_limits<cslong>::max()
#define CSLONG_MIN std::numeric_limits<cslong>::min()
#include <cstdint>

namespace CNA {
    /**
     * csint is 32 bits long, as C# int.
     */
    typedef int32_t csint;
    /**
     * cslong is 64 bits long, as C# long.
     */
    typedef int64_t cslong;
    typedef unsigned char byte;
// class Helper {
//
// };

} // CNA


#endif // CNAHELPER_H
