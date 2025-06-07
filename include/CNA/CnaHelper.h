//
// Created by robertvokac on 5/30/25.
//
#ifndef CNAHELPER_H
#define CNAHELPER_H
#define INTCS_MAX std::numeric_limits<CNA::intcs>::max()
#define INTCS_MIN std::numeric_limits<CNA::intcs>::min()
#define UINTCS_MAX std::numeric_limits<CNA::uintcs>::max()
#define UINTCS_MIN std::numeric_limits<CNA::uintcs>::min()
#define LONGCS_MAX std::numeric_limits<CNA::longcs>::max()
#define LONGCS_MIN std::numeric_limits<CNA::longcs>::min()
#define BYTE_MAX std::numeric_limits<CNA::byte>::max()
#define BYTE_MIN std::numeric_limits<CNA::byte>::min()


#include <cstdint>

namespace CNA {
    /**
     * intcs is 32 bits long, as C# int.
     */
    typedef int32_t intcs;
    /**
     * uintcs is 32 bits long and unsigned, as C# int.
     */
    typedef uint32_t uintcs;

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
