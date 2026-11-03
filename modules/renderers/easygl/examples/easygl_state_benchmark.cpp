// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace
{
    // Wrap the platform resolver only in this diagnostic executable. Actual driver calls are
    // counted even when reversing GL-002/003 removes their renderer-side diagnostic methods.
    // Counter runs pay the wrapper cost; primary timing runs return the native procedures.
    struct Calls
    {
        std::uint64_t texture = 0, pointer = 0, vao = 0, divisor = 0, disable = 0;
    } calls;
    bool instrumented = true;
    CNA::Platform::GlProcAddressLoader realLoader = nullptr;
    void (*bindTexture)(unsigned, unsigned) = nullptr;
    void (*attribPointer)(unsigned, int, unsigned, unsigned char, int, const void*) = nullptr;
    void (*bindVao)(unsigned) = nullptr;
    void (*attribDivisor)(unsigned, unsigned) = nullptr;
    void (*disableAttrib)(unsigned) = nullptr;
    void CountTexture(unsigned target, unsigned name) { ++calls.texture; bindTexture(target, name); }
    void CountPointer(unsigned index, int size, unsigned type, unsigned char normalized,
                      int stride, const void* pointer)
    { ++calls.pointer; attribPointer(index, size, type, normalized, stride, pointer); }
    void CountVao(unsigned name) { ++calls.vao; bindVao(name); }
    void CountDivisor(unsigned index, unsigned divisor)
    { ++calls.divisor; attribDivisor(index, divisor); }
    void CountDisable(unsigned index) { ++calls.disable; disableAttrib(index); }

    void* CountedLoader(const char* name)
    {
        const auto proc = realLoader(name);
        if (!proc) return nullptr;
        if (!instrumented) return proc;
#define COUNT_GL(symbol, original, wrapper) \
    if (std::strcmp(name, symbol) == 0) { \
        original = reinterpret_cast<decltype(original)>(proc); \
        return reinterpret_cast<void*>(wrapper); }
        COUNT_GL("glBindTexture", bindTexture, CountTexture)
        COUNT_GL("glVertexAttribPointer", attribPointer, CountPointer)
        COUNT_GL("glBindVertexArray", bindVao, CountVao)
        COUNT_GL("glVertexAttribDivisor", attribDivisor, CountDivisor)
        COUNT_GL("glDisableVertexAttribArray", disableAttrib, CountDisable)
#undef COUNT_GL
        return proc;
    }

    using namespace CNA::Platform;

    class CountingContext final : public IPlatformGlContext
    {
        IPlatformGlContext& inner_;
    public:
        explicit CountingContext(IPlatformGlContext& inner) : inner_(inner)
        { realLoader = inner.GetProcAddressLoader(); }
        GlContextHandle CreateContext(WindowId w, const GlContextDescription& d) override
        { return inner_.CreateContext(w, d); }
        void DestroyContext(GlContextHandle c) override { inner_.DestroyContext(c); }
        void MakeCurrent(WindowId w, GlContextHandle c) override { inner_.MakeCurrent(w, c); }
        GlContextBinding GetCurrentBinding() const override { return inner_.GetCurrentBinding(); }
        void SwapBuffers(WindowId w) override { inner_.SwapBuffers(w); }
        bool SetSwapInterval(int i) override { return inner_.SetSwapInterval(i); }
        void* GetProcAddress(const std::string& n) const override { return CountedLoader(n.c_str()); }
        GlProcAddressLoader GetProcAddressLoader() const override { return CountedLoader; }
        GlContextDescription GetContextAttributes(GlContextHandle c) const override
        { return inner_.GetContextAttributes(c); }
    };

    class CountingPlatform final : public IPlatform
    {
        std::unique_ptr<IPlatform> inner_ = PlatformFactory::Create();
        CountingContext context_{*inner_->GetGlContext()};
    public:
        CountingPlatform() { SetCurrentPlatform(this); }
        ~CountingPlatform() override { SetCurrentPlatform(nullptr); }
        const std::string& GetName() const override { return inner_->GetName(); }
        PlatformCapabilities GetCapabilities() const override { return inner_->GetCapabilities(); }
        void AcquireSubsystem(PlatformSubsystem s) override { inner_->AcquireSubsystem(s); }
        void ReleaseSubsystem(PlatformSubsystem s) override { inner_->ReleaseSubsystem(s); }
        bool IsSubsystemInitialized(PlatformSubsystem s) const override
        { return inner_->IsSubsystemInitialized(s); }
        std::unique_ptr<IPlatformWindow> CreateWindow(const WindowDescription& d) override
        { return inner_->CreateWindow(d); }
        std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId w) override { return inner_->AdoptWindow(w); }
        std::unique_ptr<IPlatformWindow> AdoptWindowHandle(std::uintptr_t h) override
        { return inner_->AdoptWindowHandle(h); }
        void PollEvents(std::vector<PlatformEvent>& events) override { inner_->PollEvents(events); }
        std::uint64_t GetPerformanceCounter() const override { return inner_->GetPerformanceCounter(); }
        std::uint64_t GetPerformanceFrequency() const override { return inner_->GetPerformanceFrequency(); }
        std::uint64_t GetTicksMilliseconds() const override { return inner_->GetTicksMilliseconds(); }
        void Delay(std::uint32_t ms) override { inner_->Delay(ms); }
        IPlatformGlContext* GetGlContext() override { return &context_; }
        std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(IPlatformWindow& w) override
        { return inner_->CreateSurfacePresenter(w); }
#define FORWARD_SERVICE(Type, Name) Type* Name() override { return inner_->Name(); }
        FORWARD_SERVICE(IPlatformKeyboard, GetKeyboard)
        FORWARD_SERVICE(IPlatformMouse, GetMouse)
        FORWARD_SERVICE(IPlatformGamepad, GetGamepad)
        FORWARD_SERVICE(IPlatformJoystick, GetJoystick)
        FORWARD_SERVICE(IPlatformTextInput, GetTextInput)
        FORWARD_SERVICE(IPlatformSensors, GetSensors)
        FORWARD_SERVICE(IPlatformHaptics, GetHaptics)
        FORWARD_SERVICE(IPlatformInputDevices, GetInputDevices)
        FORWARD_SERVICE(IPlatformClipboard, GetClipboard)
        FORWARD_SERVICE(IPlatformClipboard, GetPrimarySelection)
        FORWARD_SERVICE(IPlatformDisplays, GetDisplays)
        FORWARD_SERVICE(IPlatformDialogs, GetDialogs)
        FORWARD_SERVICE(IPlatformFileSystem, GetFileSystem)
        FORWARD_SERVICE(IPlatformSystemInfo, GetSystemInfo)
#undef FORWARD_SERVICE
        IPlatformTray* GetTrayService() override { return inner_->GetTray(); }
        IPlatformCameraProvider* GetCameraService() override { return inner_->GetCamera(); }
    };

    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Renderers;
    using Clock = std::chrono::steady_clock;

    struct Vertex { float x, y, z; std::uint32_t color; float u, v; };
    static_assert(sizeof(Vertex) == 24);
    constexpr int kWidth = 256, kHeight = 192, kCells = 64, kRepeats = 8;

    struct RendererCounters
    {
        std::uint64_t uniforms = 0;
        std::optional<std::uint64_t> textures;
        std::optional<std::uint64_t> layouts;
    };

    template<class Renderer>
    RendererCounters ReadDiagnostics(const Renderer& renderer)
    {
        RendererCounters counters;
        counters.uniforms = renderer.GetStockUniformUploadCountEXT();
        if constexpr (requires { renderer.GetTextureBindCountEXT(); })
            counters.textures = renderer.GetTextureBindCountEXT();
        if constexpr (requires { renderer.GetVertexLayoutSpecificationCountEXT(); })
            counters.layouts = renderer.GetVertexLayoutSpecificationCountEXT();
        return counters;
    }

    void DiagnosticRate(std::ostream& output, const char* name,
                        std::optional<std::uint64_t> before,
                        std::optional<std::uint64_t> after, int frames)
    {
        output << ",\"" << name << "\":";
        if (before && after) output << double(*after - *before) / frames;
        else output << "null";
    }

    template<class Buffer>
    std::size_t CacheRecords(const Buffer& buffer)
    {
        if constexpr (requires { buffer.GetVertexLayoutCacheSizeEXT(); })
            return buffer.GetVertexLayoutCacheSizeEXT();
        return 0;
    }

    int Positive(const char* text)
    {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (text[consumed] != '\0' || value < 1 || value > 100000)
            throw std::runtime_error("sample/warmup/frame counts must be 1..100000");
        return value;
    }

    double Milliseconds(Clock::time_point start, Clock::time_point end)
    { return std::chrono::duration<double, std::milli>(end - start).count(); }

    void Counter(std::ostream& output, const char* name, std::uint64_t count, int frames)
    {
        output << ",\"" << name << "\":";
        if (instrumented) output << double(count) / frames;
        else output << "null";
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--help")
        {
            std::cout << "easygl_state_benchmark EFFECT.fxb [samples=9] [warmup=60] [frames_per_sample=30]\n"
                         "CNA_EASYGL_BENCH_RESULT_FILE must name the dedicated JSONL output file.\n"
                         "CNA_EASYGL_BENCH_INSTRUMENT=0 disables wrappers for primary CPU timings.\n"
                         "Run only through tools/platform/run_gpu_tests_private.sh --exec.\n";
            return 0;
        }
        if (argc < 2 || argc > 5) throw std::runtime_error("expected EFFECT.fxb [samples] [warmup] [frames]");
        const int samples = argc > 2 ? Positive(argv[2]) : 9;
        const int warmup = argc > 3 ? Positive(argv[3]) : 60;
        const int frames = argc > 4 ? Positive(argv[4]) : 30;
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        if (bytes.empty()) throw std::runtime_error("cannot read compiled fixture");

        const char* resultPath = std::getenv("CNA_EASYGL_BENCH_RESULT_FILE");
        if (resultPath == nullptr || *resultPath == '\0')
            throw std::runtime_error("CNA_EASYGL_BENCH_RESULT_FILE must name a dedicated JSONL file");
        std::ofstream results;
        results.exceptions(std::ios::failbit | std::ios::badbit);
        results.open(resultPath, std::ios::out | std::ios::trunc);
        std::cout << "EasyGL benchmark: JSON results -> " << resultPath << '\n';

        const char* counterMode = std::getenv("CNA_EASYGL_BENCH_INSTRUMENT");
        instrumented = counterMode == nullptr || std::string(counterMode) != "0";
        CountingPlatform platform;
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(kWidth, kHeight));
        auto& renderer = dynamic_cast<EasyGL::EasyGLRenderer&>(device.GetRenderer());
        if (!renderer.SupportsCompiledEffects())
            throw std::runtime_error("requires CNA_EASYGL_COMPILED_EFFECTS=ON; compiled path must not silently skip");
        if (instrumented && (!bindTexture || !attribPointer || !bindVao || !attribDivisor || !disableAttrib))
            throw std::runtime_error("GL call interception did not observe renderer initialization");
        const auto finish = reinterpret_cast<void (*)()>(realLoader("glFinish"));
        const auto getString = reinterpret_cast<const unsigned char* (*)(unsigned)>(realLoader("glGetString"));
        const auto getError = reinterpret_cast<unsigned (*)()>(realLoader("glGetError"));
        if (!finish || !getString || !getError) throw std::runtime_error("required GL query missing");
        results << "{\"kind\":\"environment\",\"vendor\":"
                  << std::quoted(reinterpret_cast<const char*>(getString(0x1F00)))
                  << ",\"renderer\":" << std::quoted(std::string(device.GetGraphicsRendererName()))
                  << ",\"gpu\":" << std::quoted(reinterpret_cast<const char*>(getString(0x1F01)))
                  << ",\"driver\":" << std::quoted(reinterpret_cast<const char*>(getString(0x1F02)))
                  << ",\"samples\":" << samples << ",\"warmup\":" << warmup
                  << ",\"frames_per_sample\":" << frames
                  << ",\"mesh_draws_per_frame\":" << kCells * kRepeats
                  << ",\"sprites_per_frame\":128,\"allocations\":null,\"instrumented\":"
                  << (instrumented ? "true" : "false") << "}" << std::endl;

        RenderTarget2D target(device, kWidth, kHeight);
        SpriteBatch sprites(device);
        std::array<std::unique_ptr<Texture2D>, 3> textures;
        const std::array<Color, 3> colors{Color::Red, Color::Lime, Color::Blue};
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            textures[i] = std::make_unique<Texture2D>(device, 1, 1);
            textures[i]->SetData(&colors[i], 1);
        }
        std::array<std::unique_ptr<IVertexBufferRenderer>, 2> buffers;
        const std::array<Vertex, 3> vertices{{
            {-1, -1, 0, 0xffffffffu, 0, 1}, {3, -1, 0, 0xffffffffu, 1, 1},
            {-1, 3, 0, 0xffffffffu, 0, 0}}};
        for (std::size_t i = 0; i < buffers.size(); ++i)
        {
            // Same packed bytes, different declaration order, each resident on its own buffer.
            std::vector<VertexElement> elements{
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)};
            if (i == 1) std::swap(elements[0], elements[1]);
            buffers[i] = renderer.CreateVertexBuffer(3);
            buffers[i]->SetVertexDeclaration(VertexDeclaration(sizeof(Vertex), elements));
            buffers[i]->SetData(vertices.data(), 3, sizeof(Vertex));
        }
        auto compiled = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        if (!compiled) throw std::runtime_error("compiled fixture creation failed");
        compiled->SetTechnique(0);
        const auto& identity = Matrix::getIdentityProperty();
        const auto draw = [&]
        {
            device.SetRenderTarget(&target);
            device.Clear(Color::Magenta);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            for (int cell = 0; cell < kCells; ++cell)
            {
                // Disjoint cells keep every stock/compiled + texture/layout combination visible
                // in the final checksum. Repeated draws within a cell exercise the cache hits.
                renderer.SetViewport((cell % 8) * 32, (cell / 8) * 16, 32, 16, 0, 1);
                auto& texture = *textures[static_cast<std::size_t>(cell % 3)];
                GpuDrawParams params{};
                params.textureEnabled = true;
                params.texture0 = &texture.GetRenderer();
                if (cell % 4 == 3)
                {
                    compiled->SetParameterTexture(5, &texture);
                    // Repeated prefix writes exercise the GL-005 saved-value copy cost.
                    const float tintPrefix = 0.25f;
                    compiled->SetParameterValue(1, &tintPrefix, sizeof(tintPrefix));
                    CompiledEffectDeviceState states;
                    CompiledEffectPassStateChanges changes;
                    compiled->ApplyPass(0, states, changes);
                    params.compiledEffectRuntime = compiled.get();
                }
                for (int repeat = 0; repeat < kRepeats; ++repeat)
                    renderer.DrawPrimitivesEx(*buffers[static_cast<std::size_t>(cell % 2)],
                        identity, identity, identity, PrimitiveType::TriangleList, 1, params);
            }
            renderer.SetViewport(0, 0, kWidth, kHeight, 0, 1);
            sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            for (int i = 0; i < 128; ++i)
                sprites.Draw(*textures[static_cast<std::size_t>((i / 4) % 3)],
                    Rectangle((i % 32) * 8, 128 + (i / 32) * 16, 8, 16), Color::White);
            sprites.End();
            device.SetRenderTarget(nullptr);
        };
        for (int frame = 0; frame < warmup; ++frame) { draw(); finish(); }
        if (getError() != 0) throw std::runtime_error("GL error during warmup");
        if (instrumented && (!calls.vao || !calls.texture || !calls.pointer))
            throw std::runtime_error("GL counters did not observe the rendered scene");
        std::vector<Color> pixels(static_cast<std::size_t>(kWidth * kHeight));
        std::string reference;
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto before = calls;
            const auto diagnosticsBefore = ReadDiagnostics(renderer);
            double submission = 0, completed = 0;
            for (int frame = 0; frame < frames; ++frame)
            {
                const auto start = Clock::now();
                draw();
                const auto submitted = Clock::now();
                finish();
                const auto end = Clock::now();
                submission += Milliseconds(start, submitted);
                completed += Milliseconds(start, end);
            }
            const auto after = calls;
            const auto diagnosticsAfter = ReadDiagnostics(renderer);
            if (getError() != 0) throw std::runtime_error("GL error during measured sample");
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto& pixel : pixels)
            {
                const auto packed = pixel.getPackedValueProperty();
                for (int shift = 0; shift < 32; shift += 8)
                    hash = (hash ^ ((packed >> shift) & 255u)) * 1099511628211ull;
            }
            const auto checksum = std::to_string(hash);
            if (!reference.empty() && checksum != reference)
                throw std::runtime_error("nondeterministic image across samples");
            reference = checksum;
            // Independent solid-colour expectations reject an equally wrong A/B image too.
            for (int cell = 0; cell < kCells; ++cell)
            {
                const auto& pixel = pixels[static_cast<std::size_t>((cell / 8 * 16 + 8) * kWidth + cell % 8 * 32 + 16)];
                const bool compiledCell = cell % 4 == 3;
                const std::array<Color, 3> compiledColors{Color(16, 0, 0, 26),
                                                         Color(0, 13, 0, 26), Color(0, 0, 19, 26)};
                const Color expected = compiledCell ? compiledColors[static_cast<std::size_t>(cell % 3)]
                                                    : colors[static_cast<std::size_t>(cell % 3)];
                const int tolerance = compiledCell ? 2 : 0;
                if (std::abs(int(pixel.getRProperty()) - int(expected.getRProperty())) > tolerance ||
                    std::abs(int(pixel.getGProperty()) - int(expected.getGProperty())) > tolerance ||
                    std::abs(int(pixel.getBProperty()) - int(expected.getBProperty())) > tolerance ||
                    std::abs(int(pixel.getAProperty()) - int(expected.getAProperty())) > tolerance)
                    throw std::runtime_error("mesh cell " + std::to_string(cell) + " has unexpected colour");
            }
            for (int sprite = 0; sprite < 128; ++sprite)
                if (pixels[static_cast<std::size_t>((136 + sprite / 32 * 16) * kWidth + sprite % 32 * 8 + 4)] !=
                    colors[static_cast<std::size_t>((sprite / 4) % 3)])
                    throw std::runtime_error("sprite cell has unexpected colour");
            results << std::setprecision(10) << "{\"kind\":\"sample\",\"sample\":" << sample
                      << ",\"frames\":" << frames << ",\"cpu_submit_ms\":" << submission / frames
                      << ",\"completed_frame_ms\":" << completed / frames;
            Counter(results, "glBindTexture", after.texture - before.texture, frames);
            Counter(results, "glVertexAttribPointer", after.pointer - before.pointer, frames);
            Counter(results, "glBindVertexArray", after.vao - before.vao, frames);
            Counter(results, "glVertexAttribDivisor", after.divisor - before.divisor, frames);
            Counter(results, "glDisableVertexAttribArray", after.disable - before.disable, frames);
            results << ",\"vertex_buffer_object_bytes\":" << sizeof(EasyGL::EasyGLVertexBufferRenderer)
                      << ",\"layout_records_per_buffer\":"
                      << CacheRecords(static_cast<const EasyGL::EasyGLVertexBufferRenderer&>(*buffers[0]))
                      << ",\"allocations\":null,\"checksum_fnv1a64\":\"" << checksum << '"';
            DiagnosticRate(results, "stock_uniform_writes_per_frame", diagnosticsBefore.uniforms,
                           diagnosticsAfter.uniforms, frames);
            DiagnosticRate(results, "tracked_texture_binds_per_frame", diagnosticsBefore.textures,
                           diagnosticsAfter.textures, frames);
            DiagnosticRate(results, "layout_specifications_per_frame", diagnosticsBefore.layouts,
                           diagnosticsAfter.layouts, frames);
            results << "}" << std::endl;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] EasyGL state benchmark: " << error.what() << '\n';
        return 1;
    }
}
