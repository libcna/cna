// SPDX-License-Identifier: MS-PL
// Link boundary: the service client and the asset cache hash what they download; keep this out of
// the avatar catalog's translation unit, which would otherwise come along with the hash.
#include "CNA/Internal/GamerServices/ContentHash.hpp"
#include "System/Security/Cryptography/SHA256.hpp"
#include <vector>

namespace CNA::Internal::GamerServices {
std::string sha256Hex(std::span<const std::uint8_t> bytes)
{
    System::Security::Cryptography::SHA256 hash;
    const auto digest=hash.ComputeHash(std::vector<SharpRuntime::bytecs>(bytes.begin(),bytes.end()));
    static constexpr char Hex[]="0123456789abcdef";
    std::string text;
    for(auto byte:digest){text+=Hex[byte>>4];text+=Hex[byte&15];}
    return text;
}
}
