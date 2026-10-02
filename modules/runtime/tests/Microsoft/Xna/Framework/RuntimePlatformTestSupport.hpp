// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

#include <exception>
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

    /** True when a game's device could not be created because the platform refused what the
        renderer needs: Game reports that as XNA does, with the platform's exception inside. */
    inline bool IsPlatformRefusal(
        const Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException& error)
    {
        if (!error.getInnerExceptionProperty())
        {
            return false;
        }
        try
        {
            std::rethrow_exception(error.getInnerExceptionProperty());
        }
        catch (const CNA::Platform::PlatformException&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
