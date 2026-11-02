// SPDX-License-Identifier: MS-PL
//
// CNBF-105: does chunk compression earn its place in CNB?
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
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>
#include <zstd.h>

namespace
{
    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("input is not a regular file");
        const auto size = std::filesystem::file_size(path);
        if (size == 0 || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
            size > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("input is empty or too large");
        std::ifstream in(path, std::ios::binary);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
            throw std::runtime_error("cannot read the complete input");
        if (in.get() != std::char_traits<char>::eof() || in.bad())
            throw std::runtime_error("input changed size or could not be read");
        return bytes;
    }

    std::size_t CheckZstd(std::size_t result, const char* stage)
    {
        if (ZSTD_isError(result)) throw std::runtime_error(std::string(stage) + ": " + ZSTD_getErrorName(result));
        return result;
    }

    void CheckZlib(int result, const char* stage)
    {
        if (result != Z_OK) throw std::runtime_error(std::string(stage) + ": " + zError(result));
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
                if (elapsed <= 0.0) throw std::runtime_error("timer resolution is insufficient");
                return elapsed / iterations;
            }
            iterations *= 4;
        }
    }

    Result MeasureZstd(const std::vector<std::uint8_t>& raw, int level)
    {
        Result r;
        std::vector<std::uint8_t> compressed(ZSTD_compressBound(raw.size()));
        const std::size_t produced = CheckZstd(ZSTD_compress(compressed.data(), compressed.size(),
                                                    raw.data(), raw.size(), level), "zstd compress");
        compressed.resize(produced);
        r.compressedBytes = produced;

        std::vector<std::uint8_t> restored(raw.size());
        const auto decompress = [&]
        {
            const auto size = CheckZstd(ZSTD_decompress(restored.data(), restored.size(),
                compressed.data(), compressed.size()), "zstd decompress");
            if (size != raw.size()) throw std::runtime_error("zstd decoded length mismatch");
        };
        decompress();
        if (restored != raw) throw std::runtime_error("zstd round trip mismatch");
        std::vector<std::uint8_t> lastCompressed;
        r.compressMs = TimeMs([&]
        {
            std::vector<std::uint8_t> scratch(ZSTD_compressBound(raw.size()));
            const auto size = CheckZstd(ZSTD_compress(scratch.data(), scratch.size(), raw.data(), raw.size(), level),
                "timed zstd compress");
            if (size != produced) throw std::runtime_error("timed zstd compressed length mismatch");
            scratch.resize(size);
            lastCompressed = std::move(scratch);
        });
        if (lastCompressed != compressed) throw std::runtime_error("timed zstd output mismatch");
        r.decompressMs = TimeMs(decompress);
        if (restored != raw) throw std::runtime_error("timed zstd round trip mismatch");
        return r;
    }

    Result MeasureZlib(const std::vector<std::uint8_t>& raw, int level)
    {
        Result r;
        if (raw.size() > std::numeric_limits<uLong>::max())
            throw std::runtime_error("input size does not fit the zlib API");
        uLongf bound = compressBound(static_cast<uLong>(raw.size()));
        std::vector<std::uint8_t> compressed(bound);
        CheckZlib(compress2(compressed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()),
                      level), "zlib compress");
        compressed.resize(bound);
        r.compressedBytes = bound;

        std::vector<std::uint8_t> restored(raw.size());
        const auto decompress = [&]
        {
            uLongf size = static_cast<uLongf>(restored.size());
            CheckZlib(uncompress(restored.data(), &size, compressed.data(),
                static_cast<uLong>(compressed.size())), "zlib decompress");
            if (size != raw.size()) throw std::runtime_error("zlib decoded length mismatch");
        };
        decompress();
        if (restored != raw) throw std::runtime_error("zlib round trip mismatch");
        std::vector<std::uint8_t> lastCompressed;
        r.compressMs = TimeMs([&]
        {
            uLongf n = compressBound(static_cast<uLong>(raw.size()));
            std::vector<std::uint8_t> scratch(n);
            CheckZlib(compress2(scratch.data(), &n, raw.data(), static_cast<uLong>(raw.size()), level),
                "timed zlib compress");
            if (n != compressed.size()) throw std::runtime_error("timed zlib compressed length mismatch");
            scratch.resize(n);
            lastCompressed = std::move(scratch);
        });
        if (lastCompressed != compressed) throw std::runtime_error("timed zlib output mismatch");
        r.decompressMs = TimeMs(decompress);
        if (restored != raw) throw std::runtime_error("timed zlib round trip mismatch");
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
                    "(%7.0f MiB/s)\n",
                    label, codec, raw.size(), r.compressedBytes, ratio, r.compressMs,
                    r.decompressMs, decMbPerS);
    }
}

int main(int argc, char** argv)
{
    std::string codec = "all";
    std::vector<std::string> inputs;
    bool pathsOnly = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (!pathsOnly && argument == "--") { pathsOnly = true; continue; }
        if (!pathsOnly && argument.starts_with("--codec="))
        {
            codec = argument.substr(8);
            if (codec == "all" || codec == "zstd" || codec == "zlib") continue;
        }
        else if (pathsOnly || !argument.starts_with("--")) { inputs.push_back(argument); continue; }
        std::fprintf(stderr, "usage: %s [--codec=all|zstd|zlib] payload...\n", argv[0]);
        return 2;
    }
    if (inputs.empty())
    {
        std::fprintf(stderr, "usage: %s [--codec=all|zstd|zlib] payload...\n", argv[0]);
        return 2;
    }
    std::printf("compiler=%s zstd=%s zlib=%s\n", __VERSION__, ZSTD_versionString(), zlibVersion());
    std::printf("%-26s %-10s %9s    %9s  %6s   %-17s %-17s\n", "payload", "codec", "raw",
                "compressed", "ratio", "compress", "decompress");
    std::printf("%s\n", std::string(120, '-').c_str());

    bool failed = false;
    for (const auto& input : inputs)
    {
        try
        {
            const std::vector<std::uint8_t> raw = ReadFile(input);
            std::string label = input;
            const std::size_t slash = label.find_last_of('/');
            if (slash != std::string::npos) { label = label.substr(slash + 1); }
            const std::size_t dot = label.rfind(".payload");
            if (dot != std::string::npos) { label = label.substr(0, dot); }
            if (label.rfind("cnbf105-", 0) == 0) { label = label.substr(8); }

            if (codec != "zlib")
            {
                Report(label.c_str(), raw, "zstd-1", MeasureZstd(raw, 1));
                Report(label.c_str(), raw, "zstd-3", MeasureZstd(raw, 3));
                Report(label.c_str(), raw, "zstd-9", MeasureZstd(raw, 9));
                Report(label.c_str(), raw, "zstd-19", MeasureZstd(raw, 19));
            }
            if (codec != "zstd") Report(label.c_str(), raw, "zlib-6", MeasureZlib(raw, 6));
            std::printf("\n");
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "%s: %s\n", input.c_str(), error.what());
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
