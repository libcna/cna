// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <gtest/gtest.h>
#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"
#include "Microsoft/Xna/Framework/Media/MediaLibrary.hpp"
#include "Microsoft/Xna/Framework/Media/Picture.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbum.hpp"
#include "Microsoft/Xna/Framework/Media/PictureAlbumCollection.hpp"
#include "Microsoft/Xna/Framework/Media/PictureCollection.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework::Media::Test
{
    // This suite qualifies CNA's filesystem picture feature. Refresh Windows returns empty
    // catalogs / null streams and refuses SavePicture; see the TQ-086 D6 decision proposal.
    class Tq086PictureTest : public ::testing::Test
    {
    protected:
        std::filesystem::path root = CNA::TestSupport::FixtureRoot() / "tq086-pictures";
        std::filesystem::path pictures = root / "Pictures";
        std::unique_ptr<MediaLibrary> library;

        void SetUp() override
        {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(pictures);
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride((root / "missing-music").string());
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride(pictures.string());
        }

        void TearDown() override
        {
            library.reset();
            CNA::Internal::Media::MediaLibraryPaths::SetMusicRootOverride("");
            CNA::Internal::Media::MediaLibraryPaths::SetPictureRootOverride("");
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }

        void Load() { library = std::make_unique<MediaLibrary>(); }

        // Literal BMP format oracle: 14-byte file header + 40-byte BITMAPINFOHEADER;
        // little-endian 24-bit BGR rows padded to a multiple of four bytes, bottom up.
        // Every pixel is RGB(10,20,30); this survives any averaging filter exactly.
        static std::vector<uint8_t> Bmp(int width = 3, int height = 2)
        {
            const int stride = (3 * width + 3) & ~3;
            std::vector<uint8_t> bytes(static_cast<std::size_t>(54 + stride * height), 0);
            auto put = [&](int offset, uint32_t value, int count)
            {
                for (int i = 0; i < count; ++i)
                    bytes[static_cast<std::size_t>(offset + i)] = static_cast<uint8_t>(value >> (8 * i));
            };
            bytes[0] = 'B'; bytes[1] = 'M';
            put(2, static_cast<uint32_t>(bytes.size()), 4);
            put(10, 54, 4); put(14, 40, 4);
            put(18, static_cast<uint32_t>(width), 4); put(22, static_cast<uint32_t>(height), 4);
            put(26, 1, 2); put(28, 24, 2); put(34, static_cast<uint32_t>(stride * height), 4);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const auto offset = static_cast<std::size_t>(54 + y * stride + 3 * x);
                    bytes[offset] = 30; bytes[offset + 1] = 20; bytes[offset + 2] = 10;
                }
            return bytes;
        }

        static void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary);
            ASSERT_TRUE(out.is_open());
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            out.close();
            ASSERT_TRUE(out.good());
        }

        static std::vector<uint8_t> FileBytes(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            EXPECT_TRUE(in.is_open());
            return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        }

        static std::vector<uint8_t> Read(System::IO::Stream* stream)
        {
            EXPECT_NE(stream, nullptr);
            if (stream == nullptr) return {};
            std::vector<uint8_t> bytes;
            uint8_t chunk[257];
            for (;;)
            {
                const auto count = stream->Read(chunk, 0, 257);
                EXPECT_GE(count, 0);
                EXPECT_LE(count, 257);
                if (count <= 0 || count > 257) break;
                bytes.insert(bytes.end(), chunk, chunk + count);
            }
            return bytes;
        }

        static std::vector<uint8_t> ReadOwned(System::IO::Stream* stream)
        {
            const std::unique_ptr<System::IO::Stream> owner(stream);
            return Read(owner.get());
        }

        template<class Collection>
        static std::vector<std::string> Names(const Collection& collection)
        {
            std::vector<std::string> names;
            for (auto* value : collection) names.push_back(value->getNameProperty());
            return names;
        }
    };
}
