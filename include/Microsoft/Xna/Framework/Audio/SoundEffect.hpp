#pragma once

#include <memory>
#include <string>

#include "SoundEffectI.hpp"
#include "SoundEffectInstance.hpp"

namespace Microsoft::Xna::Framework::Audio {
    class SoundEffect : public SoundEffectI {
        friend class SoundEffectInstance;

    private:
        class Impl;
        std::shared_ptr<Impl> impl_;

        static float MasterVolume_;

    private:
        [[nodiscard]] void* getNativeAudioHandle() const;

    public:
        explicit SoundEffect(const std::string& assetName);
        ~SoundEffect() override;

        SoundEffect(const SoundEffect&) = default;
        SoundEffect& operator=(const SoundEffect&) = default;
        SoundEffect(SoundEffect&&) noexcept = default;
        SoundEffect& operator=(SoundEffect&&) noexcept = default;

        [[nodiscard]] static float getMasterVolumeProperty();
        static void setMasterVolumeProperty(const float& v);
        static void setMasterVolumeProperty(float&& v);

        [[nodiscard]] SoundEffectInstance CreateInstance() const override;
    };
}