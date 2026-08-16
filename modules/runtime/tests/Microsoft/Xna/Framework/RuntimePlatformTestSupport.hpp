// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#include <memory>

namespace CNA::Runtime::Testing
{
    /** Probes the selected platform's ordinary hidden-window path without naming its backend. */
    inline bool DefaultPlatformCanCreateWindow() noexcept
    {
        std::unique_ptr<CNA::Platform::IPlatform> platform;
        bool videoAcquired = false;
        try
        {
            platform = CNA::Platform::PlatformFactory::Create();
            platform->AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
            videoAcquired = true;

            CNA::Platform::WindowDescription description;
            description.title = "cna-runtime-tests-probe";
            description.width = 64;
            description.height = 64;
            description.visible = false;
            std::unique_ptr<CNA::Platform::IPlatformWindow> window =
                platform->CreateWindow(description);
            const bool available = window != nullptr;
            window.reset();

            platform->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
            videoAcquired = false;
            return available;
        }
        catch (...)
        {
            if (videoAcquired && platform != nullptr)
            {
                try
                {
                    platform->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
                }
                catch (...)
                {
                    // A capability probe is boolean by contract; cleanup failure cannot escape it.
                }
            }
            return false;
        }
    }
}
