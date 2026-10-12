// SPDX-License-Identifier: MS-PL
#pragma once
// What more than one canonical .xnb decoder translation unit shares.
#include <string>

#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief Refuses a type-reader reference that names a different reader than expected.
     * @param entry The referenced table entry. @param expected Expected reader name.
     * @param asset Asset name for the message.
     */
    void RequireReader(const XnbTypeReaderTableEntry& entry, const std::string& expected,
                       const std::string& asset);
}
