// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/PictureLibraryIndex.hpp"

#include <algorithm>
#include <exception>

#include "CNA/Internal/Graphics/ImageLoader.hpp"

namespace CNA::Internal::Media
{
    namespace
    {
        bool HasSupportedImageExtension(const std::filesystem::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
        }

        std::chrono::system_clock::time_point FileLastWriteTime(const std::filesystem::path& p)
        {
            std::error_code ec;
            auto ftime = std::filesystem::last_write_time(p, ec);
            if (ec)
            {
                return {};
            }
            // Portable file_time_type -> system_clock conversion (C++20 clock_cast is not
            // universally available across this project's supported toolchains).
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            return sctp;
        }
    }

    PictureLibraryIndex::PictureLibraryIndex(const std::string& picturesRoot)
    {
        std::error_code ec;
        if (picturesRoot.empty() || !std::filesystem::exists(picturesRoot, ec) || ec)
        {
            return;
        }
        std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(picturesRoot, ec);
        if (ec)
        {
            return;
        }
        rootPath_ = canonicalRoot.string();

        std::set<std::filesystem::path> visited;
        ScanDirectory(picturesRoot, /*parentPath=*/"", visited);
    }

    void PictureLibraryIndex::ScanDirectory(const std::filesystem::path& dir,
                                             const std::string& parentPath,
                                             std::set<std::filesystem::path>& visited)
    {
        std::error_code ec;
        std::filesystem::path canonicalDir = std::filesystem::weakly_canonical(dir, ec);
        if (ec)
        {
            return;
        }
        if (!visited.insert(canonicalDir).second)
        {
            return; // cycle guard, matching MediaLibraryIndex's approach
        }

        std::string thisPath = canonicalDir.string();
        PictureAlbumNode node;
        node.path = thisPath;
        node.name = canonicalDir.filename().string();
        node.parentPath = parentPath;
        albums_.emplace(thisPath, node);
        if (!parentPath.empty())
        {
            albums_[parentPath].childPaths.push_back(thisPath);
        }

        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return;
        }
        // directory_iterator's enumeration order is filesystem-dependent, not alphabetical --
        // sort entries first for deterministic, reproducible scan results (matching
        // MediaLibraryIndex's own fix for the same underlying non-determinism).
        std::vector<std::filesystem::directory_entry> entries;
        std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec) break;
            entries.push_back(*it);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.path() < b.path(); });

        for (const auto& entry : entries)
        {
            std::error_code entryEc;

            if (entry.is_directory(entryEc) && !entryEc)
            {
                ScanDirectory(entry.path(), thisPath, visited);
            }
            else if (entry.is_regular_file(entryEc) && !entryEc &&
                     HasSupportedImageExtension(entry.path()))
            {
                // A real Pictures folder can contain a corrupted or partially-written image file
                // -- ImageLoader::Load throws std::runtime_error in that case; skip it rather than
                // aborting the whole scan.
                try
                {
                    Graphics::ImageData img = Graphics::ImageLoader::Load(entry.path().string());

                    IndexedPicture pic;
                    pic.path = entry.path().string();
                    pic.name = entry.path().stem().string();
                    pic.width = img.width;
                    pic.height = img.height;
                    pic.albumPath = thisPath;
                    pic.date = FileLastWriteTime(entry.path());

                    pictures_.push_back(std::move(pic));
                    albums_[thisPath].pictureIndices.push_back(pictures_.size() - 1);
                }
                catch (const std::exception&)
                {
                }
            }
        }
    }
}
