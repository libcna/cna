// SPDX-License-Identifier: MS-PL
#include "CNA/AssemblyInfo.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Version.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/LaunchParameters.hpp"
#include "System/IO/MemoryStream.hpp"
#include "SharpRuntime/Version.hpp"
#include <cstdio>
#include <string_view>
#ifdef CNA_PACKAGE_TEST_C_API
extern "C" int installed_c_check_state(void);
extern "C" int installed_c_set_level(void);
extern "C" int installed_c_check_level(void);
#endif

namespace
{
const CNA::AssemblyTitleAttributeEXT title{"installed C++ runtime"};
}

int main()
{
    using namespace Microsoft::Xna::Framework;
    if (Vector3::Distance(Vector3::Zero, Vector3(3, 4, 0)) != 5) return 1;
    if (CNA::getCurrentGraphicsRendererName() != std::string_view{CNA_PACKAGE_EXPECTED_RENDERER}) return 10;
    if (Input::KeyboardState().IsKeyDown(Input::Keys::Space)) return 2;
    if (Graphics::SamplerState::PointClamp.getIsDisposedProperty()) return 3;
    System::IO::MemoryStream stream;
    stream.WriteByte(0x4f);
    stream.setPositionProperty(0);
    if (stream.ReadByte() != 0x4f) return 4;
    Content::ContentManager content;
    content.setRootDirectoryProperty("package-assets");
    if (content.getRootDirectoryProperty() != "package-assets") return 7;
    content.Dispose();
    Audio::AudioEmitter emitter;
    emitter.setDopplerScaleProperty(2.0f);
    if (emitter.getDopplerScaleProperty() != 2.0f) return 8;
    const LaunchParameters parameters;
    if (!parameters.empty()) return 9;
    CNA::Logger::SetMinimumLevel(CNA::LogLevel::WARN);
#ifdef CNA_PACKAGE_TEST_C_API
    if (installed_c_check_state() != 0) return 5;
    if (installed_c_set_level() != 0) return 6;
    if (CNA::Logger::GetMinimumLevel() != CNA::LogLevel::FATAL) return 10;
    CNA::Logger::SetMinimumLevel(CNA::LogLevel::DEBUG);
    if (installed_c_check_level() != 0) return 11;
#endif
    std::puts("installed CNA/Sharp Runtime consumer passed");
    return 0;
}
