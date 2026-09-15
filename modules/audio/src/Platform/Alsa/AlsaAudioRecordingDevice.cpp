// SPDX-License-Identifier: MS-PL

#include "Platform/Alsa/AlsaAudioRecordingDevice.hpp"
#include "Platform/Alsa/AlsaLibrary.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace CNA::Audio::Platform::Alsa {

    namespace {

        snd_pcm_t* Pcm(void* handle)
        {
            return static_cast<snd_pcm_t*>(handle);
        }

        /// Ten-millisecond periods, read as they fill; half a second of device buffer between them.
        constexpr snd_pcm_uframes_t kPeriodMilliseconds = 10;
        constexpr snd_pcm_uframes_t kBufferMilliseconds = 500;
        /// The most a session keeps for a reader that has not come for it.
        constexpr std::size_t kQueueSeconds = 8;

        constexpr AudioRecordingDeviceId kDefaultId = 1;

        std::optional<std::string> ConfiguredDevice()
        {
            const char* configured = std::getenv("CNA_AUDIO_RECORDING_DEVICE");
            if (configured == nullptr || configured[0] == '\0')
            {
                return std::nullopt;
            }
            return std::string(configured);
        }

        /// A device that is gone for good, rather than one that hiccuped.
        bool IsLost(const long code)
        {
            return code == -ENODEV || code == -EBADFD || code == -ENXIO;
        }

        /// libasound hands hint strings out with malloc.
        struct FreeString
        {
            void operator()(char* text) const noexcept { std::free(text); }
        };
        using HintString = std::unique_ptr<char, FreeString>;

    } // namespace

    AlsaAudioRecordingDevice::AlsaAudioRecordingDevice(AudioRecordingDeviceInfo info, std::string pcmName)
        : info_(std::move(info)), pcmName_(std::move(pcmName))
    {
    }

    AlsaAudioRecordingDevice::~AlsaAudioRecordingDevice()
    {
        Close();
    }

    bool AlsaAudioRecordingDevice::IsConnected() const noexcept
    {
        std::lock_guard lock(queueMutex_);
        return !lost_;
    }

    AudioFormat AlsaAudioRecordingDevice::Open(const AudioFormat& requested)
    {
        std::lock_guard lock(lifecycleMutex_);
        if (pcm_ != nullptr)
        {
            throw std::logic_error("ALSA recording device is already open");
        }
        if (!IsValid(requested))
        {
            throw std::invalid_argument("invalid ALSA recording open request");
        }
        const AlsaApi& alsa = Alsa();
        if (!alsa.loaded || !alsa.captureLoaded)
        {
            throw std::runtime_error(!alsa.error.empty() ? alsa.error
                                                         : "libasound.so.2 lacks the capture functions");
        }

        snd_pcm_t* pcm = nullptr;
        int code = alsa.PcmOpen(&pcm, pcmName_.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
        if (code < 0)
        {
            throw std::runtime_error(DescribeAlsaError(
                ("ALSA recording device '" + pcmName_ + "' could not be opened").c_str(), code));
        }

        AudioFormat negotiated{};
        snd_pcm_uframes_t period = 0;
        try
        {
            snd_pcm_hw_params_t* hardware = nullptr;
            if ((code = alsa.HwParamsMalloc(&hardware)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("snd_pcm_hw_params_malloc", code));
            }
            const std::unique_ptr<snd_pcm_hw_params_t, decltype(alsa.HwParamsFree)> hardwareOwner(
                hardware, alsa.HwParamsFree);
            if ((code = alsa.HwParamsAny(pcm, hardware)) < 0 ||
                (code = alsa.HwParamsSetAccess(pcm, hardware, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("interleaved access", code));
            }
            const bool wantsFloat = requested.sampleFormat == AudioSampleFormat::Float32;
            const snd_pcm_format_t first = wantsFloat ? SND_PCM_FORMAT_FLOAT : SND_PCM_FORMAT_S16;
            const snd_pcm_format_t second = wantsFloat ? SND_PCM_FORMAT_S16 : SND_PCM_FORMAT_FLOAT;
            snd_pcm_format_t chosen = first;
            if (alsa.HwParamsSetFormat(pcm, hardware, first) < 0)
            {
                chosen = second;
                if ((code = alsa.HwParamsSetFormat(pcm, hardware, second)) < 0)
                {
                    throw std::runtime_error(DescribeAlsaError("16-bit or float samples", code));
                }
            }
            unsigned int channels = requested.channels;
            if ((code = alsa.HwParamsSetChannelsNear(pcm, hardware, &channels)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("channel count", code));
            }
            (void) alsa.HwParamsSetRateResample(pcm, hardware, 1);
            unsigned int rate = requested.sampleRate;
            int direction = 0;
            if ((code = alsa.HwParamsSetRateNear(pcm, hardware, &rate, &direction)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("sample rate", code));
            }
            period = std::max<snd_pcm_uframes_t>(64, rate * kPeriodMilliseconds / 1000);
            direction = 0;
            (void) alsa.HwParamsSetPeriodSizeNear(pcm, hardware, &period, &direction);
            snd_pcm_uframes_t buffer = std::max<snd_pcm_uframes_t>(period * 4, rate * kBufferMilliseconds / 1000);
            (void) alsa.HwParamsSetBufferSizeNear(pcm, hardware, &buffer);
            if ((code = alsa.HwParams(pcm, hardware)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("hardware parameters", code));
            }
            direction = 0;
            (void) alsa.HwParamsGetPeriodSize(hardware, &period, &direction);
            (void) alsa.HwParamsGetChannels(hardware, &channels);
            (void) alsa.HwParamsGetRate(hardware, &rate, &direction);
            if (period == 0 || channels == 0 || channels > 255 || rate == 0)
            {
                throw std::runtime_error("ALSA negotiated an unusable recording format");
            }

            snd_pcm_sw_params_t* software = nullptr;
            if ((code = alsa.SwParamsMalloc(&software)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("snd_pcm_sw_params_malloc", code));
            }
            const std::unique_ptr<snd_pcm_sw_params_t, decltype(alsa.SwParamsFree)> softwareOwner(
                software, alsa.SwParamsFree);
            // Wake when a period can be read; capture is started explicitly below.
            if ((code = alsa.SwParamsCurrent(pcm, software)) < 0 ||
                (code = alsa.SwParamsSetAvailMin(pcm, software, period)) < 0 ||
                (code = alsa.SwParams(pcm, software)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("software parameters", code));
            }
            if ((code = alsa.PcmPrepare(pcm)) < 0 || (code = alsa.PcmStart(pcm)) < 0)
            {
                throw std::runtime_error(DescribeAlsaError("starting capture", code));
            }
            negotiated.sampleRate = rate;
            negotiated.channels = static_cast<std::uint8_t>(channels);
            negotiated.sampleFormat = chosen == SND_PCM_FORMAT_FLOAT ? AudioSampleFormat::Float32
                                                                     : AudioSampleFormat::Signed16;
        }
        catch (...)
        {
            alsa.PcmClose(pcm);
            throw;
        }

        const snd_pcm_type_t type = alsa.PcmType(pcm);
        unclocked_ = type == SND_PCM_TYPE_NULL || type == SND_PCM_TYPE_FILE;
        pcm_ = pcm;
        format_ = negotiated;
        periodFrames_ = period;
        {
            std::lock_guard queueLock(queueMutex_);
            frameBytes_ = negotiated.channels * BytesPerSample(negotiated.sampleFormat);
            queue_.clear();
            queueLimit_ = static_cast<std::size_t>(negotiated.sampleRate) * frameBytes_ * kQueueSeconds;
            lost_ = false;
            failed_ = false;
        }
        running_.store(true, std::memory_order_release);
        try
        {
            worker_ = std::thread(&AlsaAudioRecordingDevice::Run, this);
        }
        catch (...)
        {
            running_.store(false, std::memory_order_release);
            CloseLocked();
            throw;
        }
        return format_;
    }

    void AlsaAudioRecordingDevice::Run() noexcept
    {
        const AlsaApi& alsa = Alsa();
        snd_pcm_t* pcm = Pcm(pcm_);
        std::vector<std::byte> period(periodFrames_ * frameBytes_);
        auto deadline = std::chrono::steady_clock::now();
        while (running_.load(std::memory_order_acquire))
        {
            const snd_pcm_sframes_t read =
                alsa.PcmReadi(pcm, period.data(), static_cast<snd_pcm_uframes_t>(periodFrames_));
            if (read > 0)
            {
                const std::size_t bytes = static_cast<std::size_t>(read) * frameBytes_;
                {
                    std::lock_guard lock(queueMutex_);
                    queue_.insert(queue_.end(), period.begin(),
                                  period.begin() + static_cast<std::ptrdiff_t>(bytes));
                    if (queue_.size() > queueLimit_)
                    {
                        // Oldest first, whole frames only.
                        std::size_t excess = queue_.size() - queueLimit_;
                        excess += (frameBytes_ - excess % frameBytes_) % frameBytes_;
                        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(excess));
                    }
                }
                if (unclocked_)
                {
                    // No clock to wait on: keep real time ourselves.
                    deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(static_cast<double>(read) / format_.sampleRate));
                    std::this_thread::sleep_until(deadline);
                }
                continue;
            }
            if (read == 0 || read == -EAGAIN)
            {
                // Nothing yet. Wait for a period, but never so long that Close() waits on us.
                (void) alsa.PcmWait(pcm, 100);
                continue;
            }
            if (IsLost(read))
            {
                std::lock_guard lock(queueMutex_);
                lost_ = true;
                return;
            }
            // An overrun (EPIPE), a suspend (ESTRPIPE) or an interrupted call: the library knows
            // how to come back from each -- and a capture stream, once prepared again, must be
            // started again.
            if (alsa.PcmRecover(pcm, static_cast<int>(read), 1) < 0 || alsa.PcmStart(pcm) < 0)
            {
                std::lock_guard lock(queueMutex_);
                failed_ = true;
                return;
            }
        }
    }

    void AlsaAudioRecordingDevice::CloseLocked() noexcept
    {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable())
        {
            worker_.join();
        }
        if (pcm_ != nullptr)
        {
            (void) Alsa().PcmDrop(Pcm(pcm_));
            (void) Alsa().PcmClose(Pcm(pcm_));
            pcm_ = nullptr;
        }
        format_ = {};
        periodFrames_ = 0;
        std::lock_guard lock(queueMutex_);
        queue_.clear();
    }

    void AlsaAudioRecordingDevice::Close() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        CloseLocked();
    }

    bool AlsaAudioRecordingDevice::IsOpen() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return pcm_ != nullptr && running_.load(std::memory_order_acquire);
    }

    AudioFormat AlsaAudioRecordingDevice::GetFormat() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return pcm_ != nullptr ? format_ : AudioFormat{};
    }

    AudioRecordingIoResult AlsaAudioRecordingDevice::GetAvailableBytes() const noexcept
    {
        std::lock_guard lock(queueMutex_);
        if (!queue_.empty())
        {
            return {AudioRecordingIoStatus::Success, queue_.size()};
        }
        if (lost_)
        {
            return {AudioRecordingIoStatus::DeviceLost, 0};
        }
        if (failed_)
        {
            return {AudioRecordingIoStatus::Error, 0};
        }
        return {AudioRecordingIoStatus::WouldBlock, 0};
    }

    AudioRecordingIoResult AlsaAudioRecordingDevice::Read(const std::span<std::byte> destination) noexcept
    {
        if (destination.empty())
        {
            return {AudioRecordingIoStatus::WouldBlock, 0};
        }
        std::lock_guard lock(queueMutex_);
        std::size_t count = std::min(queue_.size(), destination.size());
        if (frameBytes_ != 0)
        {
            count -= count % frameBytes_;
        }
        if (count == 0)
        {
            if (queue_.empty() && lost_)
            {
                return {AudioRecordingIoStatus::DeviceLost, 0};
            }
            if (queue_.empty() && failed_)
            {
                return {AudioRecordingIoStatus::Error, 0};
            }
            return {AudioRecordingIoStatus::WouldBlock, 0};
        }
        std::copy(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(count), destination.begin());
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(count));
        return {AudioRecordingIoStatus::Success, count};
    }

    // --- the provider ------------------------------------------------------------------------------

    std::vector<AlsaAudioRecordingDeviceProvider::Entry> AlsaAudioRecordingDeviceProvider::Enumerate() const
    {
        if (const std::optional<std::string> configured = ConfiguredDevice())
        {
            return {Entry{AudioRecordingDeviceInfo{kDefaultId, *configured, true}, *configured}};
        }
        const AlsaApi& alsa = Alsa();
        if (!alsa.loaded || !alsa.captureLoaded)
        {
            return {};
        }

        std::vector<Entry> entries;
        // ALSA's `default` -- PipeWire's or PulseAudio's plugin on a desktop, the card through
        // dsnoop on a bare system -- when the host's configuration lists it for input. Never
        // invented: a host whose configuration lists none gets no default entry.
        void** hints = nullptr;
        if (alsa.DeviceNameHint(-1, "pcm", &hints) == 0 && hints != nullptr)
        {
            for (void** hint = hints; *hint != nullptr; ++hint)
            {
                const HintString name(alsa.DeviceNameGetHint(*hint, "NAME"));
                const HintString direction(alsa.DeviceNameGetHint(*hint, "IOID"));
                if (name == nullptr || std::string(name.get()) != "default" ||
                    (direction != nullptr && std::string(direction.get()) != "Input"))
                {
                    continue;
                }
                const HintString description(alsa.DeviceNameGetHint(*hint, "DESC"));
                std::string label = description != nullptr ? description.get() : "default";
                label = label.substr(0, label.find('\n'));
                entries.push_back(Entry{AudioRecordingDeviceInfo{kDefaultId, label, true}, "default"});
                break;
            }
            (void) alsa.DeviceNameFreeHint(hints);
        }

        // Every card's capture devices. Opening a card's control interface reads what it has;
        // nothing is captured.
        int card = -1;
        while (alsa.CardNext(&card) == 0 && card >= 0)
        {
            snd_ctl_t* control = nullptr;
            if (alsa.CtlOpen(&control, ("hw:" + std::to_string(card)).c_str(), 0) < 0)
            {
                continue;
            }
            snd_ctl_card_info_t* cardInfo = nullptr;
            snd_pcm_info_t* pcmInfo = nullptr;
            if (alsa.CtlCardInfoMalloc(&cardInfo) == 0 && alsa.PcmInfoMalloc(&pcmInfo) == 0 &&
                alsa.CtlCardInfo(control, cardInfo) == 0)
            {
                const std::string cardName = alsa.CtlCardInfoGetName(cardInfo);
                int device = -1;
                while (alsa.CtlPcmNextDevice(control, &device) == 0 && device >= 0)
                {
                    alsa.PcmInfoSetDevice(pcmInfo, static_cast<unsigned int>(device));
                    alsa.PcmInfoSetSubdevice(pcmInfo, 0);
                    alsa.PcmInfoSetStream(pcmInfo, SND_PCM_STREAM_CAPTURE);
                    if (alsa.CtlPcmInfo(control, pcmInfo) < 0)
                    {
                        continue;  // This device plays only.
                    }
                    const AudioRecordingDeviceId id =
                        (static_cast<AudioRecordingDeviceId>(card) + 1) << 16 |
                        static_cast<AudioRecordingDeviceId>(device);
                    // The card's name and the device's; a device that has none (a digital
                    // microphone's often does not) by its number.
                    const std::string deviceName = alsa.PcmInfoGetName(pcmInfo);
                    entries.push_back(Entry{
                        AudioRecordingDeviceInfo{
                            id,
                            cardName + ", " +
                                (deviceName.empty() ? "device " + std::to_string(device) : deviceName),
                            false},
                        "plughw:" + std::to_string(card) + "," + std::to_string(device)});
                }
            }
            if (pcmInfo != nullptr) { alsa.PcmInfoFree(pcmInfo); }
            if (cardInfo != nullptr) { alsa.CtlCardInfoFree(cardInfo); }
            (void) alsa.CtlClose(control);
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
            if (left.info.isDefault != right.info.isDefault)
            {
                return left.info.isDefault;
            }
            return left.info.id < right.info.id;
        });
        return entries;
    }

    std::vector<AudioRecordingDeviceInfo> AlsaAudioRecordingDeviceProvider::GetDevices() const
    {
        std::vector<AudioRecordingDeviceInfo> devices;
        for (const Entry& entry : Enumerate())
        {
            devices.push_back(entry.info);
        }
        return devices;
    }

    std::unique_ptr<IAudioRecordingDevice> AlsaAudioRecordingDeviceProvider::CreateDevice(
        const AudioRecordingDeviceId id)
    {
        for (Entry& entry : Enumerate())
        {
            if (entry.info.id == id)
            {
                return std::make_unique<AlsaAudioRecordingDevice>(std::move(entry.info),
                                                                  std::move(entry.pcmName));
            }
        }
        return nullptr;
    }

} // namespace CNA::Audio::Platform::Alsa
