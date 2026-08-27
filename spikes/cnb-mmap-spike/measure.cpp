// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-108: would memory-mapping a .cnb actually make loading faster?
//
// The task says to benchmark before implementing, and names the reason to be sceptical: a
// VertexBuffer still has to be uploaded CPU->GPU, so mmap is not automatically zero-copy to the
// GPU. This measures the step mmap could actually remove -- getting the file's bytes addressable
// -- against the work CNB does regardless, which is verifying every chunk's CRC-32C.
//
// The comparison that decides it is not "mmap vs read". It is "what mmap saves" against "what the
// load must do anyway".

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__x86_64__)
#include <nmmintrin.h>
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
    // The same polynomial, in hardware. Present since SSE4.2 (2008), so this is not an exotic
    // requirement -- it is what every CPU CNA targets on x86-64 already has.
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

    void DropCaches()
    {
        // Best effort only; without privileges this is a no-op and the numbers are warm-cache,
        // which is stated in the report rather than pretended away.
        ::sync();
        const int fd = ::open("/proc/sys/vm/drop_caches", O_WRONLY);
        if (fd >= 0)
        {
            (void)::write(fd, "3\n", 2);
            ::close(fd);
        }
    }
}

int main(int argc, char** argv)
{
    for (int a = 1; a < argc; ++a)
    {
        const char* path = argv[a];
        struct stat st {};
        if (::stat(path, &st) != 0) { std::fprintf(stderr, "cannot stat %s\n", path); continue; }
        const std::size_t size = static_cast<std::size_t>(st.st_size);
        const double mib = size / 1048576.0;
        std::printf("\n=== %s -- %.2f MiB ===\n", path, mib);

        // A: what CnbDocument::ParseFile does today -- read the whole file into a vector.
        std::vector<std::uint8_t> owned(size);
        const double readMs = TimeMs([&]
        {
            const int fd = ::open(path, O_RDONLY);
            std::size_t done = 0;
            while (done < size)
            {
                const ssize_t n = ::read(fd, owned.data() + done, size - done);
                if (n <= 0) { break; }
                done += static_cast<std::size_t>(n);
            }
            ::close(fd);
        });

        // B: mmap, then touch every page -- because the bytes are not really "there" until
        // something faults them in, and CNB's own CRC pass will.
        const double mmapMs = TimeMs([&]
        {
            const int fd = ::open(path, O_RDONLY);
            void* p = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            volatile std::uint8_t sink = 0;
            const auto* bytes = static_cast<const std::uint8_t*>(p);
            for (std::size_t i = 0; i < size; i += 4096u) { sink = bytes[i]; }
            (void)sink;
            ::munmap(p, size);
            ::close(fd);
        });

        // C: mmap alone, without touching -- the number that looks like a huge win and is not,
        // because nothing has actually been read yet.
        const double mmapOnlyMs = TimeMs([&]
        {
            const int fd = ::open(path, O_RDONLY);
            void* p = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            ::munmap(p, size);
            ::close(fd);
        });

        // D: the work every load must do regardless of how the bytes arrived.
        const double crcTableMs =
            TimeMs([&] { DoNotOptimise(Crc32cTable(owned.data(), size)); });
#if defined(__x86_64__)
        const double crcHwMs =
            TimeMs([&] { DoNotOptimise(Crc32cHardware(owned.data(), size)); });
        if (Crc32cTable(owned.data(), size) != Crc32cHardware(owned.data(), size))
        {
            std::fprintf(stderr, "CRC implementations disagree -- results meaningless\n");
            return 1;
        }
#else
        const double crcHwMs = 0.0;
#endif

        std::printf("  read whole file        %8.3f ms  (%7.0f MB/s)\n", readMs, mib / (readMs / 1000.0));
        std::printf("  mmap + touch pages     %8.3f ms  (%7.0f MB/s)\n", mmapMs, mib / (mmapMs / 1000.0));
        std::printf("  mmap, never touched    %8.3f ms  <- not a load; nothing was read\n", mmapOnlyMs);
        std::printf("  CRC-32C, table (CNA)   %8.3f ms  (%7.0f MB/s)\n", crcTableMs, mib / (crcTableMs / 1000.0));
#if defined(__x86_64__)
        std::printf("  CRC-32C, SSE4.2        %8.3f ms  (%7.0f MB/s)\n", crcHwMs, mib / (crcHwMs / 1000.0));
#endif
        std::printf("  --\n");
        std::printf("  mmap could save        %8.3f ms of the read\n", readMs - mmapMs);
        std::printf("  CRC costs              %8.3f ms and cannot be skipped\n", crcTableMs);
#if defined(__x86_64__)
        std::printf("  hardware CRC would save%8.3f ms\n", crcTableMs - crcHwMs);
#endif
    }
    (void)DropCaches;
    return 0;
}
