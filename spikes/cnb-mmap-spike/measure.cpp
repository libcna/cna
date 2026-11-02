// SPDX-License-Identifier: MS-PL
//
// CNBF-108: would memory-mapping a .cnb actually make loading faster?
//
// The task says to benchmark before implementing, and names the reason to be sceptical: a
// VertexBuffer still has to be uploaded CPU->GPU, so mmap is not automatically zero-copy to the
// GPU. This measures the step mmap could actually remove -- getting the file's bytes addressable
// -- against the work CNB does regardless, which is verifying every chunk's CRC-32C.
//
// The comparison that decides it is not "mmap vs read". It is "what mmap saves" against "what the
// load must do anyway".

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__x86_64__)
#include <nmmintrin.h>
#include <cpuid.h>
#endif

namespace
{
    using clock_type = std::chrono::steady_clock;

    /// Keeps a computed value from being optimised away. Without this the CRC timings come out
    /// as 0.000 ms for 32 MiB -- the compiler removes a call whose result is discarded, and the
    /// benchmark cheerfully reports that verification is free. It is not.
    template <typename T>
    void DoNotOptimise(const T& value)
    {
        asm volatile("" : : "r,m"(value) : "memory");
    }

    template <typename Fn>
    double TimeMs(Fn&& fn, int minIterations = 3)
    {
        double best = 1e30;
        for (int i = 0; i < minIterations; ++i)
        {
            const auto start = clock_type::now();
            fn();
            const double ms =
                std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
            if (ms < best) { best = ms; }
        }
        if (best <= 0.0) throw std::runtime_error("timer resolution is insufficient");
        return best;
    }

    // --- CRC-32C, exactly as CnbCrc32c.cpp computes it today: reflected table, byte at a time ---
    constexpr std::uint32_t kPoly = 0x82F63B78u;

    std::array<std::uint32_t, 256> BuildTable()
    {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256u; ++i)
        {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc & 1u) ? ((crc >> 1) ^ kPoly) : (crc >> 1);
            }
            table[i] = crc;
        }
        return table;
    }
    const std::array<std::uint32_t, 256> kTable = BuildTable();

    std::uint32_t Crc32cTable(const std::uint8_t* data, std::size_t size)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < size; ++i)
        {
            crc = kTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

#if defined(__x86_64__)
    // The same polynomial, used only after runtime feature detection.
    __attribute__((target("sse4.2"))) std::uint32_t Crc32cHardware(const std::uint8_t* data,
                                                                    std::size_t size)
    {
        std::uint64_t crc = 0xFFFFFFFFu;
        std::size_t i = 0;
        for (; i + 8u <= size; i += 8u)
        {
            std::uint64_t chunk;
            std::memcpy(&chunk, data + i, sizeof(chunk));
            crc = _mm_crc32_u64(crc, chunk);
        }
        for (; i < size; ++i)
        {
            crc = _mm_crc32_u8(static_cast<std::uint32_t>(crc), data[i]);
        }
        return static_cast<std::uint32_t>(crc) ^ 0xFFFFFFFFu;
    }
#endif

    bool HardwareAvailable()
    {
#if defined(__x86_64__)
        unsigned int a, b, c, d;
        return __get_cpuid(1u, &a, &b, &c, &d) != 0 && (c & bit_SSE4_2) != 0;
#else
        return false;
#endif
    }

    [[noreturn]] void IoError(const char* operation)
    {
        throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
    }

    struct File
    {
        int fd;
        explicit File(const char* path) : fd(::open(path, O_RDONLY | O_CLOEXEC))
        {
            if (fd < 0) IoError("open");
        }
        ~File() { ::close(fd); }
        File(const File&) = delete;
        File& operator=(const File&) = delete;

        std::size_t Size() const
        {
            struct stat info{};
            if (::fstat(fd, &info) != 0) IoError("fstat");
            if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
                static_cast<std::uintmax_t>(info.st_size) > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("input must be a nonempty, representable regular file");
            return static_cast<std::size_t>(info.st_size);
        }

        void RequireSize(std::size_t expected) const
        {
            if (Size() != expected) throw std::runtime_error("input changed size during measurement");
        }
    };

    void ReadFile(const char* path, std::vector<std::uint8_t>& bytes)
    {
        const File file(path);
        file.RequireSize(bytes.size());
        std::size_t done = 0;
        while (done != bytes.size())
        {
            const auto count = std::min(bytes.size() - done,
                static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto result = ::read(file.fd, bytes.data() + done, count);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) IoError("read");
            if (result == 0) throw std::runtime_error("short read before the complete input");
            done += static_cast<std::size_t>(result);
        }
        file.RequireSize(bytes.size());
    }

    struct Mapping
    {
        void* address;
        std::size_t size;
        Mapping(const File& file, std::size_t length)
            : address(::mmap(nullptr, length, PROT_READ, MAP_PRIVATE, file.fd, 0)), size(length)
        {
            if (address == MAP_FAILED) IoError("mmap");
        }
        ~Mapping() { if (address != MAP_FAILED) ::munmap(address, size); }
        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;
        void Close()
        {
            if (::munmap(address, size) != 0) IoError("munmap");
            address = MAP_FAILED;
        }
    };

    void Measure(const char* path, bool tableOnly)
    {
        const File input(path);
        const auto size = input.Size();
        const double mib = size / 1048576.0;
        std::vector<std::uint8_t> owned(size);
        ReadFile(path, owned);
        const auto expected = Crc32cTable(owned.data(), size);
        using Crc = std::uint32_t (*)(const std::uint8_t*, std::size_t);
        Crc selected = Crc32cTable;
        const bool hardware = !tableOnly && HardwareAvailable();
#if defined(__x86_64__)
        if (hardware) selected = Crc32cHardware;
#endif
        const auto check = [&](std::uint32_t crc)
        {
            if (crc != expected) throw std::runtime_error("CRC mismatch in complete input scan");
            DoNotOptimise(crc);
        };
        check(selected(owned.data(), size));
        const auto readMs = TimeMs([&] { ReadFile(path, owned); });
        const auto readCrcMs = TimeMs([&] { ReadFile(path, owned); check(selected(owned.data(), size)); });
        const auto mappedCrcMs = TimeMs([&]
        {
            const File file(path);
            file.RequireSize(size);
            Mapping mapping(file, size);
            check(selected(static_cast<const std::uint8_t*>(mapping.address), size));
            file.RequireSize(size);
            mapping.Close();
        });
        const auto mappedOnlyMs = TimeMs([&]
        {
            const File file(path);
            file.RequireSize(size);
            Mapping mapping(file, size);
            mapping.Close();
        });
        const auto tableMs = TimeMs([&] { check(Crc32cTable(owned.data(), size)); });
        std::printf("\n=== %s -- %.2f MiB; CRC32C=%08X ===\n", path, mib, expected);
        std::printf("  read whole file        %8.3f ms  (%7.0f MiB/s)\n", readMs, mib / (readMs / 1000.0));
        std::printf("  read + full CRC        %8.3f ms  (%7.0f MiB/s)\n", readCrcMs, mib / (readCrcMs / 1000.0));
        std::printf("  mmap + full CRC        %8.3f ms  (%7.0f MiB/s)\n", mappedCrcMs, mib / (mappedCrcMs / 1000.0));
        std::printf("  mmap, never touched    %8.3f ms  <- not a load\n", mappedOnlyMs);
        std::printf("  CRC-32C, table         %8.3f ms  (%7.0f MiB/s)\n", tableMs, mib / (tableMs / 1000.0));
#if defined(__x86_64__)
        if (hardware)
        {
            const auto hardwareMs = TimeMs([&] { check(Crc32cHardware(owned.data(), size)); });
            std::printf("  CRC-32C, SSE4.2        %8.3f ms  (%7.0f MiB/s)\n", hardwareMs, mib / (hardwareMs / 1000.0));
        }
#endif
        if (!hardware)
            std::printf(tableOnly ? "  hardware CRC disabled (--crc=table)\n" : "  hardware CRC unavailable; using table\n");
        std::printf("  read+CRC minus mmap+CRC %8.3f ms (same full-input CRC work)\n", readCrcMs - mappedCrcMs);
    }
}

int main(int argc, char** argv)
{
    bool tableOnly = false;
    bool failed = false;
    std::vector<const char*> inputs;
    for (int a = 1; a < argc; ++a)
    {
        if (std::strcmp(argv[a], "--crc=table") == 0) tableOnly = true;
        else if (std::strncmp(argv[a], "--", 2) == 0)
        {
            std::fprintf(stderr, "usage: %s [--crc=table] input...\n", argv[0]);
            return 2;
        }
        else inputs.push_back(argv[a]);
    }
    if (inputs.empty())
    {
        std::fprintf(stderr, "usage: %s [--crc=table] input...\n", argv[0]);
        return 2;
    }
    std::printf("compiler=%s; minimum of three warm/cache-uncontrolled component runs\n", __VERSION__);
    for (const auto* path : inputs)
    {
        try { Measure(path, tableOnly); }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "%s: %s\n", path, error.what());
            failed = true;
        }
    }
    if (std::fflush(stdout) != 0 || std::ferror(stdout))
    {
        std::fprintf(stderr, "cannot write measurement output\n");
        failed = true;
    }
    return failed ? 1 : 0;
}
