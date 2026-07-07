// SPDX-License-Identifier: MS-PL
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SkinnedModelEXT;

namespace
{
    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // A tests-only scratch content root, unique per test process run so parallel/repeated runs
    // never collide. Cleaned up on destruction.
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_content_manager_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };
}

class ContentManagerSkinnedModelTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// Task 11.7: BinReaderEXT::Read<T>() never checked Pos + sizeof(T) <= Data.size() before
// std::memcpy-ing out of Data - a truncated/corrupt .skeleton.bin caused a real out-of-bounds
// heap read (undefined behavior) instead of a clean, catchable error. Here, boneCount = 1 is
// written (4 bytes), but the file is truncated right after that header - the very next read
// (ParentBoneIndices[0], another int32) must be rejected instead of reading past the buffer.
TEST_F(ContentManagerSkinnedModelTest, TruncatedSkeletonBinThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "avatar.skinnedmodel.json", R"({"skeleton": "skeleton.bin"})");

    const std::int32_t boneCount = 1;
    std::vector<std::uint8_t> truncated(sizeof(boneCount));
    std::memcpy(truncated.data(), &boneCount, sizeof(boneCount));
    WriteBytes(root.path() / "skeleton.bin", truncated);

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(
        cm.Load<std::shared_ptr<SkinnedModelEXT>>("avatar"),
        ContentLoadException);
}

// A skeleton.bin that is merely empty (0 bytes) must also be rejected cleanly - the very first
// read (boneCount itself) is already past the end of an empty buffer.
TEST_F(ContentManagerSkinnedModelTest, EmptySkeletonBinThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "avatar.skinnedmodel.json", R"({"skeleton": "skeleton.bin"})");
    WriteBytes(root.path() / "skeleton.bin", {});

    ContentManager cm(nullptr, root.path().string());
    cm.setGraphicsDevice(gd);

    EXPECT_THROW(
        cm.Load<std::shared_ptr<SkinnedModelEXT>>("avatar"),
        ContentLoadException);
}
