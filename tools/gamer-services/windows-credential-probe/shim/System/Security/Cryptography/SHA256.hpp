// Probe-only stand-in: the MinGW probe tests the Windows store, not sharp-runtime's hash.
#pragma once
#include <vector>
namespace System::Security::Cryptography {
struct SHA256 {
    std::vector<unsigned char> ComputeHash(const std::vector<unsigned char>& bytes) {
        std::vector<unsigned char> out(32,0);for(std::size_t i=0;i<bytes.size();++i)out[i%32]=static_cast<unsigned char>(out[i%32]*31+bytes[i]);return out;
    }
};
}
