// SPDX-License-Identifier: MS-PL
#include "CNA/C/cna.h"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include <thread>
extern "C" CNA_Bool cna_test_volume_profile(uint32_t profile, uint32_t format)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    return Texture::IsVolumeFormatAllowedByProfileEXT(static_cast<GraphicsProfile>(profile),
        static_cast<SurfaceFormat>(format)) ? CNA_TRUE : CNA_FALSE;
}
extern "C" int32_t cna_test_fragment_sampler_count(void)
{
    return Microsoft::Xna::Framework::Graphics::SamplerStateCollection().ActiveSamplerCount();
}
extern "C" int cna_test_resource_worker(int (*function)(void))
{
    int result = 1;
    std::thread worker([&] { result = function(); });
    worker.join();
    return result;
}
