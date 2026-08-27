// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-105: does chunk compression earn its place in CNB?
//
// The task says to measure before choosing, so this measures. For each real CNB payload it
// reports the compressed size, the compression cost and -- the number that actually decides it --
// the DECOMPRESSION throughput, because that is what every load pays forever while the size win
// is paid once at build time.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>
#include <zstd.h>

namespace
{
    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>());
    }

    struct Result
    {
        std::size_t compressedBytes = 0;
        double compressMs = 0.0;
        double decompressMs = 0.0;
    };

    /// Repeats until at least a few milliseconds have elapsed, so a fast codec on a small buffer
    /// is not measured as "0 ms". Reports the per-iteration mean.
    template <typename Fn>
    double TimeMs(Fn&& fn)
    {
        using clock = std::chrono::steady_clock;
        int iterations = 1;
        for (;;)
        {
            const auto start = clock::now();
            for (int i = 0; i < iterations; ++i) { fn(); }
            const double elapsed =
                std::chrono::duration<double, std::milli>(clock::now() - start).count();
            if (elapsed >= 50.0 || iterations >= (1 << 20))
            {
                return elapsed / iterations;
            }
            iterations *= 4;
        }
    }

    Result MeasureZstd(const std::vector<std::uint8_t>& raw, int level)
    {
        Result r;
        std::vector<std::uint8_t> compressed(ZSTD_compressBound(raw.size()));
        const std::size_t produced = ZSTD_compress(compressed.data(), compressed.size(),
                                                    raw.data(), raw.size(), level);
        if (ZSTD_isError(produced)) { std::fprintf(stderr, "zstd error\n"); return r; }
        compressed.resize(produced);
        r.compressedBytes = produced;

        r.compressMs = TimeMs([&]
        {
            std::vector<std::uint8_t> scratch(ZSTD_compressBound(raw.size()));
            (void)ZSTD_compress(scratch.data(), scratch.size(), raw.data(), raw.size(), level);
        });
        std::vector<std::uint8_t> restored(raw.size());
        r.decompressMs = TimeMs([&]
        {
            (void)ZSTD_decompress(restored.data(), restored.size(), compressed.data(),
                                  compressed.size());
        });
        if (std::memcmp(restored.data(), raw.data(), raw.size()) != 0)
        {
            std::fprintf(stderr, "zstd round trip mismatch!\n");
        }
        return r;
    }

    Result MeasureZlib(const std::vector<std::uint8_t>& raw, int level)
    {
        Result r;
        uLongf bound = compressBound(static_cast<uLong>(raw.size()));
        std::vector<std::uint8_t> compressed(bound);
        if (compress2(compressed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()),
                      level) != Z_OK)
        {
            std::fprintf(stderr, "zlib error\n");
            return r;
        }
        compressed.resize(bound);
        r.compressedBytes = bound;

        r.compressMs = TimeMs([&]
        {
            uLongf n = compressBound(static_cast<uLong>(raw.size()));
            std::vector<std::uint8_t> scratch(n);
            (void)compress2(scratch.data(), &n, raw.data(), static_cast<uLong>(raw.size()), level);
        });
        std::vector<std::uint8_t> restored(raw.size());
        r.decompressMs = TimeMs([&]
        {
            uLongf n = static_cast<uLongf>(restored.size());
            (void)uncompress(restored.data(), &n, compressed.data(),
                             static_cast<uLong>(compressed.size()));
        });
        if (std::memcmp(restored.data(), raw.data(), raw.size()) != 0)
        {
            std::fprintf(stderr, "zlib round trip mismatch!\n");
        }
        return r;
    }

    void Report(const char* label, const std::vector<std::uint8_t>& raw, const char* codec,
                const Result& r)
    {
        const double ratio =
            raw.empty() ? 0.0 : 100.0 * static_cast<double>(r.compressedBytes) / raw.size();
        const double decMbPerS =
            r.decompressMs > 0.0 ? (raw.size() / 1048576.0) / (r.decompressMs / 1000.0) : 0.0;
        std::printf("%-26s %-10s %9zu -> %9zu  %6.1f%%   comp %8.3f ms   decomp %8.3f ms  "
                    "(%7.0f MB/s)\n",
                    label, codec, raw.size(), r.compressedBytes, ratio, r.compressMs,
                    r.decompressMs, decMbPerS);
    }
}

int main(int argc, char** argv)
{
    std::printf("%-26s %-10s %9s    %9s  %6s   %-17s %-17s\n", "payload", "codec", "raw",
                "compressed", "ratio", "compress", "decompress");
    std::printf("%s\n", std::string(120, '-').c_str());

    for (int i = 1; i < argc; ++i)
    {
        const std::vector<std::uint8_t> raw = ReadFile(argv[i]);
        if (raw.empty())
        {
            std::fprintf(stderr, "skipping empty/missing %s\n", argv[i]);
            continue;
        }
        std::string label = argv[i];
        const std::size_t slash = label.find_last_of('/');
        if (slash != std::string::npos) { label = label.substr(slash + 1); }
        const std::size_t dot = label.rfind(".payload");
        if (dot != std::string::npos) { label = label.substr(0, dot); }
        if (label.rfind("cnbf105-", 0) == 0) { label = label.substr(8); }

        Report(label.c_str(), raw, "zstd-1", MeasureZstd(raw, 1));
        Report(label.c_str(), raw, "zstd-3", MeasureZstd(raw, 3));
        Report(label.c_str(), raw, "zstd-9", MeasureZstd(raw, 9));
        Report(label.c_str(), raw, "zstd-19", MeasureZstd(raw, 19));
        Report(label.c_str(), raw, "zlib-6", MeasureZlib(raw, 6));
        std::printf("\n");
    }
    return 0;
}
