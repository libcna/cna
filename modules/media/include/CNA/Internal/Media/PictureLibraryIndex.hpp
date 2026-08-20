// SPDX-License-Identifier: MS-PL
#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace CNA::Internal::Media
{
    /// One scanned picture file.
    struct IndexedPicture
    {
        std::string path;
        std::string name; // filename without extension
        int width = 0;
        int height = 0;
        std::string albumPath; // canonical path of the containing PictureAlbumNode
        std::chrono::system_clock::time_point date;
    };

    /// One node in the real Picture/PictureAlbum tree, mirroring the actual filesystem tree under
    /// the Pictures root, one node per real subdirectory.
    struct PictureAlbumNode
    {
        std::string path; // canonical path -- the key used in PictureLibraryIndex::GetAlbums()
        std::string name;
        std::string parentPath; // empty for the root node
        std::vector<std::string> childPaths;
        std::vector<std::size_t> pictureIndices; // indices into GetPictures()
    };

    /// One-shot recursive scan of a Pictures root, building a real Picture/PictureAlbum tree.
    /// Reuses the already-existing CNA::Internal::Graphics::ImageLoader for picture dimensions
    /// rather than reimplementing image decoding (plans/plan_media.md MEDIA-56/D4).
    class PictureLibraryIndex
    {
    public:
        explicit PictureLibraryIndex(const std::string& picturesRoot);

        [[nodiscard]] const std::vector<IndexedPicture>& GetPictures() const { return pictures_; }
        [[nodiscard]] const std::map<std::string, PictureAlbumNode>& GetAlbums() const { return albums_; }
        [[nodiscard]] const std::string& GetRootAlbumPath() const { return rootPath_; }

    private:
        void ScanDirectory(const std::filesystem::path& dir, const std::string& parentPath,
                            std::set<std::filesystem::path>& visited);

        std::vector<IndexedPicture> pictures_;
        std::map<std::string, PictureAlbumNode> albums_;
        std::string rootPath_;
    };
}
