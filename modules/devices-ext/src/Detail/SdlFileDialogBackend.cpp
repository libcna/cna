// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/SdlFileDialogBackend.hpp"

#ifdef CNA_DEVICES

#include <memory>

#include <SDL3/SDL_dialog.h>

namespace
{
    using CNA::Devices::FileDialogFilter;
    using CNA::Devices::Detail::FileDialogResultCallback;

    // Owns everything a dialog call's asynchronous callback needs to stay valid
    // until it actually fires -- SDL3's own doc comment for `filters` is explicit
    // that the array "must remain valid at least until the callback is invoked",
    // which is *after* the Show*Dialog() call itself returns (these calls are
    // asynchronous). FilterNames/FilterPatterns are reserved to their final size
    // before any pointer into them is taken, so std::string's small-buffer/heap
    // storage never moves out from under SdlFilters afterward.
    struct DialogContext
    {
        FileDialogResultCallback Callback;
        std::string DefaultLocation;
        std::vector<std::string> FilterNames;
        std::vector<std::string> FilterPatterns;
        std::vector<SDL_DialogFileFilter> SdlFilters;
    };

    std::unique_ptr<DialogContext> MakeContext(
        FileDialogResultCallback callback,
        const std::string& defaultLocation,
        const std::vector<FileDialogFilter>& filters)
    {
        auto context = std::make_unique<DialogContext>();
        context->Callback = std::move(callback);
        context->DefaultLocation = defaultLocation;

        context->FilterNames.reserve(filters.size());
        context->FilterPatterns.reserve(filters.size());
        for (const FileDialogFilter& filter : filters)
        {
            context->FilterNames.push_back(filter.Name);
            context->FilterPatterns.push_back(filter.Pattern);
        }

        context->SdlFilters.reserve(filters.size());
        for (std::size_t i = 0; i < filters.size(); ++i)
        {
            context->SdlFilters.push_back(
                SDL_DialogFileFilter{context->FilterNames[i].c_str(), context->FilterPatterns[i].c_str()});
        }

        return context;
    }

    // Reconstructs and frees the heap-allocated DialogContext passed as userdata.
    // SDL3 guarantees each dialog callback fires exactly once (one-shot), so this
    // single reconstruct-then-destroy is correct, not a leak or a double-free risk.
    void ResultTrampoline(void* userdata, const char* const* filelist, int /*filter*/)
    {
        std::unique_ptr<DialogContext> context(static_cast<DialogContext*>(userdata));

        std::vector<std::string> files;
        if (filelist != nullptr)
        {
            for (const char* const* entry = filelist; *entry != nullptr; ++entry)
            {
                files.emplace_back(*entry);
            }
        }

        if (context->Callback)
        {
            context->Callback(files);
        }
    }
} // namespace

namespace CNA::Devices::Detail
{
    void SdlFileDialogBackend::ShowOpenFile(
        FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        std::unique_ptr<DialogContext> context = MakeContext(std::move(onResult), defaultLocation, filters);
        const bool hasFilters = !context->SdlFilters.empty();
        const bool hasDefaultLocation = !context->DefaultLocation.empty();
        const SDL_DialogFileFilter* sdlFiltersPtr = hasFilters ? context->SdlFilters.data() : nullptr;
        const int sdlFilterCount = static_cast<int>(context->SdlFilters.size());
        const char* defaultLocationPtr = hasDefaultLocation ? context->DefaultLocation.c_str() : nullptr;

        SDL_ShowOpenFileDialog(
            &ResultTrampoline,
            context.release(),
            nullptr,
            sdlFiltersPtr,
            sdlFilterCount,
            defaultLocationPtr,
            allowMultiple);
    }

    void SdlFileDialogBackend::ShowSaveFile(
        FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation)
    {
        std::unique_ptr<DialogContext> context = MakeContext(std::move(onResult), defaultLocation, filters);
        const bool hasFilters = !context->SdlFilters.empty();
        const bool hasDefaultLocation = !context->DefaultLocation.empty();
        const SDL_DialogFileFilter* sdlFiltersPtr = hasFilters ? context->SdlFilters.data() : nullptr;
        const int sdlFilterCount = static_cast<int>(context->SdlFilters.size());
        const char* defaultLocationPtr = hasDefaultLocation ? context->DefaultLocation.c_str() : nullptr;

        SDL_ShowSaveFileDialog(
            &ResultTrampoline,
            context.release(),
            nullptr,
            sdlFiltersPtr,
            sdlFilterCount,
            defaultLocationPtr);
    }

    void SdlFileDialogBackend::ShowOpenFolder(
        FileDialogResultCallback onResult,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        std::unique_ptr<DialogContext> context = MakeContext(std::move(onResult), defaultLocation, {});
        const bool hasDefaultLocation = !context->DefaultLocation.empty();
        const char* defaultLocationPtr = hasDefaultLocation ? context->DefaultLocation.c_str() : nullptr;

        SDL_ShowOpenFolderDialog(
            &ResultTrampoline,
            context.release(),
            nullptr,
            defaultLocationPtr,
            allowMultiple);
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
