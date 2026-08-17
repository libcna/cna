// SPDX-License-Identifier: MS-PL

#include "Platform/Null/NullAudioDevice.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace CNA::Audio::Platform::Null {

    NullAudioDevice::~NullAudioDevice()
    {
        Close();
    }

    AudioFormat NullAudioDevice::Open(
        const AudioFormat& requested,
        std::shared_ptr<IAudioBufferCallback> callback)
    {
        std::lock_guard lock(lifecycleMutex_);
        if (open_)
        {
            throw std::logic_error("null audio device is already open");
        }
        if (!IsValid(requested) || !callback)
        {
            throw std::invalid_argument("invalid null audio open request");
        }

        const std::size_t sampleCount = BufferFrameCount * requested.channels;
        std::vector<std::byte> scratch(
            sampleCount * BytesPerSample(requested.sampleFormat));

        callback_ = std::move(callback);
        scratch_ = std::move(scratch);
        format_ = requested;
        open_ = true;
        return format_;
    }

    void NullAudioDevice::Start()
    {
        std::lock_guard lock(lifecycleMutex_);
        if (!open_)
        {
            throw std::logic_error("null audio device is closed");
        }
        if (running_.load(std::memory_order_acquire))
        {
            return;
        }

        running_.store(true, std::memory_order_release);
        try
        {
            worker_ = std::thread(&NullAudioDevice::Run, this);
        }
        catch (...)
        {
            running_.store(false, std::memory_order_release);
            throw;
        }
    }

    void NullAudioDevice::StopLocked() noexcept
    {
        running_.store(false, std::memory_order_release);
        waitCondition_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void NullAudioDevice::Stop() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        StopLocked();
    }

    void NullAudioDevice::Close() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        StopLocked();
        callback_.reset();
        scratch_.clear();
        format_ = {};
        open_ = false;
    }

    bool NullAudioDevice::IsOpen() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return open_;
    }

    bool NullAudioDevice::IsRunning() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return open_ && running_.load(std::memory_order_acquire);
    }

    AudioFormat NullAudioDevice::GetFormat() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return open_ ? format_ : AudioFormat{};
    }

    void NullAudioDevice::Run() noexcept
    {
        const std::size_t sampleCount = BufferFrameCount * format_.channels;
        const auto bufferDuration = std::chrono::duration<double>(
            static_cast<double>(BufferFrameCount) / format_.sampleRate);

        while (running_.load(std::memory_order_acquire))
        {
            callback_->FillBuffer(scratch_, sampleCount);

            std::unique_lock waitLock(waitMutex_);
            waitCondition_.wait_for(waitLock, bufferDuration, [this]
            {
                return !running_.load(std::memory_order_acquire);
            });
        }
    }

} // namespace CNA::Audio::Platform::Null
