// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbCrc32c.hpp"

#include <array>
#include <cstring>

// plans/plan_cnb.md CNBF-108. CRC-32C is not a detail of loading a `.cnb` -- it IS most of the
// cost of it. Measured on a 32 MiB file (docs/cnb-mmap-measurements.md): reading the whole file
// takes 7 ms and verifying it took 62 ms, so verification was nine times the I/O it protects.
//
// That measurement was taken while investigating memory-mapped chunk access, and it is the reason
// that investigation ended here instead: mmap could save about 4.7 ms of the read, while doing the
// same CRC in hardware saves about 59 ms. The bottleneck was never getting the bytes.
//
// So the polynomial is the same, the results are bit-identical, and the only thing that changed is
// how many bytes are folded per instruction. The table below remains the definition of correct and
// the fallback for every target without a CRC instruction.
#if defined(__x86_64__) || defined(__i386__)
#define CNA_CNB_CRC32C_X86 1
#include <nmmintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#elif defined(__aarch64__)
#define CNA_CNB_CRC32C_ARM64 1
#if defined(__linux__)
#include <sys/auxv.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
#include <arm_acle.h>
#endif
#endif

namespace CNA::Content::Cnb
{
    namespace
    {
        // Reflected CRC-32C polynomial. Built once at compile time so the table is a constant in
        // .rodata rather than something a static initializer has to race to fill in.
        constexpr std::uint32_t kReflectedPolynomial = 0x82F63B78u;

        constexpr std::array<std::uint32_t, 256> BuildTable()
        {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0; i < 256u; ++i)
            {
                std::uint32_t crc = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc = (crc & 1u) != 0u ? ((crc >> 1) ^ kReflectedPolynomial) : (crc >> 1);
                }
                table[i] = crc;
            }
            return table;
        }

        constexpr std::array<std::uint32_t, 256> kTable = BuildTable();

        /// The portable definition. Every other path in this file must agree with it bit for bit,
        /// and a test checks that on inputs of every length modulo 8.
        std::uint32_t FoldTable(std::uint32_t crc, const std::uint8_t* data,
                                std::size_t size) noexcept
        {
            for (std::size_t i = 0; i < size; ++i)
            {
                crc = kTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
            }
            return crc;
        }

#if defined(CNA_CNB_CRC32C_X86)
        // SSE4.2, i.e. every x86-64 CPU since 2008. Detected at RUNTIME rather than required at
        // compile time, so one binary still runs on a machine without it -- a build flag would
        // trade a crash for a speed-up nobody asked for.
        __attribute__((target("sse4.2"))) std::uint32_t FoldHardware(std::uint32_t crc,
                                                                     const std::uint8_t* data,
                                                                     std::size_t size) noexcept
        {
            std::size_t i = 0;
#if defined(__x86_64__)
            std::uint64_t wide = crc;
            for (; i + 8u <= size; i += 8u)
            {
                std::uint64_t block;
                std::memcpy(&block, data + i, sizeof(block));
                wide = _mm_crc32_u64(wide, block);
            }
            crc = static_cast<std::uint32_t>(wide);
#endif
            for (; i + 4u <= size; i += 4u)
            {
                std::uint32_t block;
                std::memcpy(&block, data + i, sizeof(block));
                crc = _mm_crc32_u32(crc, block);
            }
            for (; i < size; ++i) { crc = _mm_crc32_u8(crc, data[i]); }
            return crc;
        }

        bool DetectHardware() noexcept
        {
#if defined(__GNUC__) || defined(__clang__)
            unsigned int eax = 0;
            unsigned int ebx = 0;
            unsigned int ecx = 0;
            unsigned int edx = 0;
            if (__get_cpuid(1u, &eax, &ebx, &ecx, &edx) == 0) { return false; }
            return (ecx & bit_SSE4_2) != 0u;
#else
            return false;
#endif
        }
#elif defined(CNA_CNB_CRC32C_ARM64)
        // ARMv8's optional CRC32 extension, which every Apple silicon and essentially every
        // 64-bit Android device has. Same polynomial, same results.
        __attribute__((target("+crc"))) std::uint32_t FoldHardware(std::uint32_t crc,
                                                                    const std::uint8_t* data,
                                                                    std::size_t size) noexcept
        {
            std::size_t i = 0;
            for (; i + 8u <= size; i += 8u)
            {
                std::uint64_t block;
                std::memcpy(&block, data + i, sizeof(block));
                crc = __crc32cd(crc, block);
            }
            for (; i < size; ++i) { crc = __crc32cb(crc, data[i]); }
            return crc;
        }

        bool DetectHardware() noexcept
        {
#if defined(__linux__) && defined(HWCAP_CRC32)
            return (::getauxval(AT_HWCAP) & HWCAP_CRC32) != 0;
#elif defined(__APPLE__)
            return true; // every arm64 Apple target has it
#else
            return false;
#endif
        }
#endif

#if defined(CNA_CNB_CRC32C_X86) || defined(CNA_CNB_CRC32C_ARM64)
        // Detected once. A function-local static is thread-safe initialisation in C++11 and later,
        // which matters because CnbLoaderRegistry explicitly supports loading from several threads.
        bool HardwareAvailable() noexcept
        {
            static const bool available = DetectHardware();
            return available;
        }
#endif

        std::uint32_t Fold(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept
        {
#if defined(CNA_CNB_CRC32C_X86) || defined(CNA_CNB_CRC32C_ARM64)
            if (HardwareAvailable()) { return FoldHardware(crc, data.data(), data.size()); }
#endif
            return FoldTable(crc, data.data(), data.size());
        }
    }

    std::uint32_t Crc32cContinue(std::uint32_t previous, std::span<const std::uint8_t> data) noexcept
    {
        return ~Fold(~previous, data);
    }

    std::uint32_t Crc32c(std::span<const std::uint8_t> data) noexcept
    {
        return Crc32cContinue(Crc32cSeed(), data);
    }

    bool Crc32cUsesHardwareEXT() noexcept
    {
#if defined(CNA_CNB_CRC32C_X86) || defined(CNA_CNB_CRC32C_ARM64)
        return HardwareAvailable();
#else
        return false;
#endif
    }

    std::uint32_t Crc32cPortableEXT(std::span<const std::uint8_t> data) noexcept
    {
        return ~FoldTable(~Crc32cSeed(), data.data(), data.size());
    }
}
