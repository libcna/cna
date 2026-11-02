// SPDX-License-Identifier: MS-PL
#include "compiled_effect_fuzzer_support.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>

using namespace CNA::Tools::FxFuzz;

TEST(CompiledEffectFuzzerSupport, WordMutationCannotOverwriteShortInputStorage)
{
    for (std::size_t length = 0; length <= 8; ++length)
    {
        for (std::uint64_t seed = 0; seed < 256; ++seed)
        {
            std::vector<std::uint8_t> bytes(64, 0xA5);
            bytes.resize(length);
            Rng random{seed};
            Mutate(random, bytes);
            std::array<std::uint8_t, 64> storage{};
            // The entire allocation was initialized before shrinking the logical input.
            std::memcpy(storage.data(), bytes.data(), storage.size());
            for (std::size_t index = length; index < storage.size(); ++index)
            {
                ASSERT_EQ(storage[index], 0xA5) << "length=" << length << " seed=" << seed;
            }
            EXPECT_LE(bytes.size(), length);
        }
    }
}

TEST(CompiledEffectFuzzerSupport, AllocationFailuresEscapeWhileApiRejectionsAreAccepted)
{
    EXPECT_TRUE(Attempt([] {}));
    EXPECT_FALSE(Attempt([] { throw std::runtime_error("invalid candidate"); }));
    EXPECT_THROW(Attempt([] { throw std::bad_alloc{}; }), std::bad_alloc);
    EXPECT_THROW(Attempt([] { throw std::bad_array_new_length{}; }), std::bad_alloc);
}

TEST(CompiledEffectFuzzerSupport, NumericArgumentsRequireCompleteRepresentableValues)
{
    EXPECT_EQ(ParseUnsigned("18446744073709551615"), UINT64_MAX);
    EXPECT_EQ(ParseUnsigned("0x123", true), 0x123u);
    EXPECT_EQ(ParseUnsigned("0"), 0u);
    for (const auto* value : {"", "-1", "+1", "1x", "0x", "18446744073709551616"})
    {
        EXPECT_THROW(ParseUnsigned(value, true), std::invalid_argument) << value;
    }
}

TEST(CompiledEffectFuzzerSupport, FilesAndCorporaAreBoundedAndSortedBeforeUse)
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("cna-fx-support-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(fs::create_directory(root));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    fs::create_directory(root / "empty");
    for (const auto* name : {"z.bin", "a.bin"})
    {
        std::ofstream file(root / name, std::ios::binary);
        file << "four";
    }
    const auto files = EnumerateInputs({root});
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].filename(), "a.bin");
    EXPECT_EQ(files[1].filename(), "z.bin");
    EXPECT_EQ(ReadFile(files[0]).size(), 4u);
    EXPECT_THROW(ReadFile(files[0], 3), std::runtime_error);
    EXPECT_THROW(ReadFile(root / "missing"), std::runtime_error);
    EXPECT_THROW(EnumerateInputs({root / "empty"}), std::runtime_error);
    EXPECT_THROW(EnumerateInputs(std::vector<fs::path>(MaximumCorpusFiles + 1, files[0])), std::runtime_error);
    {
        std::ofstream oversized(root / "large.bin", std::ios::binary);
        oversized.seekp(MaximumInputBytes);
        oversized.put('x');
    }
    EXPECT_THROW(ReadFile(root / "large.bin"), std::runtime_error);
    {
        std::ofstream empty(root / "zero.bin", std::ios::binary);
    }
    EXPECT_THROW(ReadFile(root / "zero.bin"), std::runtime_error);
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-fixture")
    {
        CNA::TestSupport::SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeStringParameter = true;
        const auto bytes = CNA::TestSupport::BuildSyntheticEffect(options);
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        return file ? 0 : 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
