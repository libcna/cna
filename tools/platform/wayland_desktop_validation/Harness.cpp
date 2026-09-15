// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114: reporting, options and the platform session.

#include "Harness.hpp"

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace CnaWaylandValidation {

    Tally& Results()
    {
        static Tally tally;
        return tally;
    }

    void Pass(const std::string& check, const std::string& detail)
    {
        ++Results().passed;
        std::cout << "PASS " << check << (detail.empty() ? "" : " -- " + detail) << std::endl;
    }

    void Fail(const std::string& check, const std::string& detail)
    {
        ++Results().failed;
        std::cout << "FAIL " << check << (detail.empty() ? "" : " -- " + detail) << std::endl;
    }

    void Skip(const std::string& check, const std::string& reason)
    {
        ++Results().skipped;
        std::cout << "SKIP " << check << " -- " << reason << std::endl;
    }

    void Info(const std::string& text)
    {
        std::cout << "INFO " << text << std::endl;
    }

    bool Check(const bool condition, const std::string& check, const std::string& detail)
    {
        if (condition)
        {
            Pass(check, detail);
        }
        else
        {
            Fail(check, detail);
        }
        return condition;
    }

    ProcessSample SampleProcess()
    {
        ProcessSample sample;
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
        {
            if (line.rfind("VmRSS:", 0) == 0)
            {
                std::istringstream value(line.substr(6));
                value >> sample.residentKb;
            }
        }
        if (DIR* directory = ::opendir("/proc/self/fd"))
        {
            while (const dirent* entry = ::readdir(directory))
            {
                if (entry->d_name[0] != '.')
                {
                    ++sample.openDescriptors;
                }
            }
            ::closedir(directory);
            --sample.openDescriptors;  // the directory stream itself
        }
        return sample;
    }

    double NowMs()
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string OptionString(const std::vector<std::string>& arguments, const std::string& name,
                             const std::string& fallback)
    {
        const std::string flag = "--" + name;
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (arguments[index] == flag && index + 1 < arguments.size())
            {
                return arguments[index + 1];
            }
            if (arguments[index].rfind(flag + "=", 0) == 0)
            {
                return arguments[index].substr(flag.size() + 1);
            }
        }
        return fallback;
    }

    long OptionInt(const std::vector<std::string>& arguments, const std::string& name, const long fallback)
    {
        const std::string value = OptionString(arguments, name, "");
        return value.empty() ? fallback : std::stol(value);
    }

    bool OptionFlag(const std::vector<std::string>& arguments, const std::string& name)
    {
        for (const std::string& argument : arguments)
        {
            if (argument == "--" + name)
            {
                return true;
            }
        }
        return false;
    }

    Session::Session()
    {
        platform_ = std::make_unique<Wayland::WaylandPlatform>();
        if (platform_->GetConnectionForTesting() == nullptr)
        {
            error_ = platform_->GetConnectionError();
            return;
        }
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
            acquired_ = true;
        }
        catch (const PlatformException& exception)
        {
            error_ = exception.what();
        }
    }

    Session::~Session()
    {
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    std::unique_ptr<IPlatformWindow> Session::Make(const std::string& title, const int width, const int height,
                                                   const bool visible, const WindowRenderIntent intent,
                                                   const bool highDpi)
    {
        WindowDescription description;
        description.title = title;
        description.width = width;
        description.height = height;
        description.visible = visible;
        description.renderIntent = intent;
        description.highDpi = highDpi;
        return platform_->CreateWindow(description);
    }

    const std::vector<PlatformEvent>& Session::Poll()
    {
        batch_.clear();
        platform_->PollEvents(batch_);
        seen_.insert(seen_.end(), batch_.begin(), batch_.end());
        return batch_;
    }

    bool Session::PumpUntil(const std::function<bool()>& condition, const std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!condition())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return condition();
            }
            if (Wayland::WaylandConnection* connection = platform_->GetConnectionForTesting())
            {
                (void) connection->DispatchFor(std::chrono::milliseconds(5));
            }
            Poll();
        }
        return true;
    }

    void Session::PumpFor(const std::chrono::milliseconds duration)
    {
        (void) PumpUntil([] { return false; }, duration);
    }

    int Session::Count(const WindowId window, const WindowEventKind kind) const
    {
        int count = 0;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
            {
                count += windowEvent->window == window && windowEvent->kind == kind ? 1 : 0;
            }
        }
        return count;
    }

    bool Session::WaitFor(const WindowId window, const WindowEventKind kind, const std::chrono::milliseconds budget)
    {
        return PumpUntil([&] { return Count(window, kind) > 0; }, budget);
    }

    bool Session::Alive()
    {
        Wayland::WaylandConnection* connection = platform_->GetConnectionForTesting();
        return connection != nullptr && connection->IsAlive();
    }

    std::string Session::ConnectionError()
    {
        Wayland::WaylandConnection* connection = platform_->GetConnectionForTesting();
        return connection != nullptr ? connection->GetError() : platform_->GetConnectionError();
    }

    void PresentSolid(IPlatformSurfacePresenter& presenter, IPlatformWindow& window, const std::uint32_t rgb)
    {
        const WindowSize size = window.GetPixelSize();
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4);
        for (std::size_t index = 0; index < pixels.size(); index += 4)
        {
            pixels[index + 0] = static_cast<std::uint8_t>(rgb >> 16);
            pixels[index + 1] = static_cast<std::uint8_t>(rgb >> 8);
            pixels[index + 2] = static_cast<std::uint8_t>(rgb);
            pixels[index + 3] = 0xFF;
        }
        SurfaceFrame frame;
        frame.pixels = pixels.data();
        frame.width = size.width;
        frame.height = size.height;
        frame.strideBytes = size.width * 4;
        presenter.Present(frame);
    }

} // namespace CnaWaylandValidation
