// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "CNA/Internal/PathUtf8.hpp"

#include <cgltf.h>

namespace CNA::Internal::GltfImport
{
    /**
     * @file CgltfFileCallbacks.hpp
     * @brief CNA-owned file callbacks so cgltf never opens a path itself.
     *
     * cgltf's default reader is a bare `fopen(path, "rb")` — `cgltf_default_file_read` in the
     * vendored `third_party/cgltf/cgltf.h`, with no wide branch anywhere in the file. On Windows
     * that is the process ANSI code page, so a glTF document under a directory the code page
     * cannot spell simply fails to open, and `cgltf_load_buffers()` fails the same way for every
     * external `.bin` and image it resolves relative to that document.
     *
     * `cgltf_options::file` exists precisely so a host can answer that itself. These callbacks
     * take the path as CNA's UTF-8, widen it with PathFromUtf8(), and read through a native
     * `std::filesystem::path`.
     */
    namespace Detail
    {
        /**
         * @brief Reads a whole file for cgltf, opening it through a native path.
         *
         * @param memoryOptions cgltf's allocator hooks; its `alloc_func` is used when present.
         * @param fileOptions Unused; part of the cgltf callback signature.
         * @param path The file to read, encoded as UTF-8.
         * @param size Receives the number of bytes read.
         * @param data Receives the allocated buffer, owned by cgltf afterwards.
         * @return `cgltf_result_success`, or a cgltf error code.
         */
        inline cgltf_result CnaCgltfRead(const cgltf_memory_options* memoryOptions,
                                         const cgltf_file_options* fileOptions,
                                         const char* path, cgltf_size* size, void** data)
        {
            (void)fileOptions;
            if (path == nullptr || size == nullptr || data == nullptr)
            {
                return cgltf_result_invalid_options;
            }

            const std::optional<std::filesystem::path> native = TryPathFromUtf8(path);
            if (!native)
            {
                return cgltf_result_file_not_found;
            }

            std::ifstream stream(*native, std::ios::binary | std::ios::ate);
            if (!stream.good())
            {
                return cgltf_result_file_not_found;
            }

            const std::streamsize length = stream.tellg();
            if (length < 0)
            {
                return cgltf_result_io_error;
            }
            stream.seekg(0, std::ios::beg);

            const auto byteCount = static_cast<cgltf_size>(length);
            void* buffer = (memoryOptions != nullptr && memoryOptions->alloc_func != nullptr)
                               ? memoryOptions->alloc_func(memoryOptions->user_data, byteCount)
                               : std::malloc(byteCount != 0 ? byteCount : 1);
            if (buffer == nullptr)
            {
                return cgltf_result_out_of_memory;
            }

            if (byteCount != 0 && !stream.read(static_cast<char*>(buffer), length))
            {
                if (memoryOptions != nullptr && memoryOptions->free_func != nullptr)
                {
                    memoryOptions->free_func(memoryOptions->user_data, buffer);
                }
                else
                {
                    std::free(buffer);
                }
                return cgltf_result_io_error;
            }

            *size = byteCount;
            *data = buffer;
            return cgltf_result_success;
        }

        /**
         * @brief Releases a buffer produced by CnaCgltfRead().
         *
         * @param memoryOptions cgltf's allocator hooks; its `free_func` is used when present.
         * @param fileOptions Unused; part of the cgltf callback signature.
         * @param data The buffer to release.
         */
        inline void CnaCgltfRelease(const cgltf_memory_options* memoryOptions,
                                    const cgltf_file_options* fileOptions, void* data)
        {
            (void)fileOptions;
            if (data == nullptr) { return; }
            if (memoryOptions != nullptr && memoryOptions->free_func != nullptr)
            {
                memoryOptions->free_func(memoryOptions->user_data, data);
                return;
            }
            std::free(data);
        }
    }

    /**
     * @brief Installs CNA's file callbacks on a cgltf options block.
     *
     * Call before `cgltf_parse_file()` and pass the same options to `cgltf_load_buffers()`, which
     * resolves external buffer and image URIs through the same hook.
     *
     * @param options The cgltf options block to configure.
     */
    inline void UseCnaFileCallbacks(cgltf_options& options)
    {
        options.file.read = &Detail::CnaCgltfRead;
        options.file.release = &Detail::CnaCgltfRelease;
    }
}
