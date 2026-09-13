// SPDX-License-Identifier: MS-PL

#include "Win32WindowClass.hpp"

#include "Win32Error.hpp"
#include "Win32Window.hpp"

#include <mutex>

namespace CNA::Platform::Win32 {

    namespace {

        constexpr const wchar_t* kClassName = L"CnaPlatformWindow";

        std::mutex& RegistrationMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        int& RegistrationCount()
        {
            static int count = 0;
            return count;
        }

        HINSTANCE ModuleInstance()
        {
            // The instance of the module this code is linked into, which is not necessarily the
            // process's own: CNA may be inside a DLL. GetModuleHandleExW with the address of a
            // local static is the documented way to ask "which module am I".
            static HINSTANCE instance = [] {
                HMODULE module = nullptr;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(&kClassName), &module) != FALSE)
                {
                    return reinterpret_cast<HINSTANCE>(module);
                }
                return GetModuleHandleW(nullptr);
            }();
            return instance;
        }

    } // namespace

    Win32WindowClass::Win32WindowClass()
    {
        const std::lock_guard<std::mutex> guard(RegistrationMutex());
        if (RegistrationCount() > 0)
        {
            ++RegistrationCount();
            return;
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        // CS_OWNDC because a GL context binds to a window's device context for its lifetime, and
        // CS_DBLCLKS because without it Windows never sends WM_*BUTTONDBLCLK and
        // MouseButtonEvent::clicks could never be 2.
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
        windowClass.lpfnWndProc = &Win32Window::StaticWindowProc;
        windowClass.hInstance = ModuleInstance();
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // No class background brush: every CNA window is painted by a renderer, and letting the
        // system erase it first produces a visible flash on every resize.
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kClassName;

        if (RegisterClassExW(&windowClass) == 0)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS)
                ThrowError("Win32WindowClass::Register", error);
        }
        ++RegistrationCount();
    }

    Win32WindowClass::~Win32WindowClass()
    {
        const std::lock_guard<std::mutex> guard(RegistrationMutex());
        if (--RegistrationCount() > 0)
            return;

        // A failure here is not actionable and must not escape a destructor. It happens when a
        // window of this class is still alive, in which case leaving the class registered is
        // exactly the right outcome.
        UnregisterClassW(kClassName, ModuleInstance());
    }

    const wchar_t* Win32WindowClass::GetClassName()
    {
        return kClassName;
    }

    HINSTANCE Win32WindowClass::GetInstance()
    {
        return ModuleInstance();
    }

} // namespace CNA::Platform::Win32
