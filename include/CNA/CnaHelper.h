//
// Created by robertvokac on 5/30/25.
//
#ifndef CNAHELPER_H
#define CNAHELPER_H
#define INTCS_MAX std::numeric_limits<CNA::intcs>::max()
#define INTCS_MIN std::numeric_limits<CNA::intcs>::min()
#define LONGCS_MAX std::numeric_limits<CNA::longcs>::max()
#define LONGCS_MIN std::numeric_limits<CNA::longcs>::min()


#include <cstdint>

namespace CNA {
    /**
     * intcs is 32 bits long, as C# int.
     */
    typedef int32_t intcs;
    /**
     * longcs is 64 bits long, as C# long.
     */
    typedef int64_t longcs;
    typedef unsigned char byte;
// class Helper {
//
// };

} // CNA

#endif // CNAHELPER_H
