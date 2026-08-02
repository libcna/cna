// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-164 -- an active EasyGL multisampled RenderTarget2D must expose its current
// colour attachment to GetData without requiring the caller to unbind it first.
//
// The ticket's original fixture reads the target while it is still bound.  Before the fix,
// EasyGLRenderTargetBackend::GetData read the single-sample resolve texture, but the only resolve
// lived in UnbindAsRenderTarget.  The active target therefore returned the untouched resolve
// storage -- observed as (0,0,0,0) -- even though its multisample renderbuffer held the requested
// opaque black Clear and the rendered triangles.  Explicitly unbinding, or switching to a target
// consumer, ran the resolve and made the same pixels exact.
//
// This fixture keeps those stages discriminating.  Its colours distinguish transparent black,
// opaque black, zero-RGB/nonzero-alpha, several nonzero RGB/alpha combinations, an untouched
// destination sentinel, and a loss of RGB as well as alpha.  It also compares EasyGL's raw RGBA
// backend bytes with public Color delivery so a native-transfer failure cannot be mistaken for a
// BGRA conversion failure.

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::IRenderTargetBackend;

namespace
{
    constexpr int kW = 8;
    constexpr int kH = 8;
    constexpr int kCount = kW * kH;
    constexpr int kStart = 5;
    constexpr int kSuffix = 7;

    const Color kSentinel(61, 7, 149, 203);
    const Color kTransparentBlack(0, 0, 0, 0);
    const Color kOpaqueBlack(0, 0, 0, 255);
    const Color kZeroRgbAlpha(0, 0, 0, 37);
    const Color kAlphaOne(13, 47, 211, 1);
    const Color kAlphaLow(205, 31, 77, 93);
    const Color kAlphaHigh(17, 181, 61, 173);
    const Color kOpaqueColour(240, 120, 33, 255);

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    std::string Text(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    class Gfx164Test final : public Game
    {
    public:
        Gfx164Test()
        {
            gdm_ = std::make_unique<GraphicsDeviceManager>(this);
            gdm_->setPreferredBackBufferWidthProperty(32);
            gdm_->setPreferredBackBufferHeightProperty(32);
        }

        [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }

    protected:
        void Draw(const GameTime&) override
        {
            if (ran_) return;
            ran_ = true;

            auto& dev = getGraphicsDeviceProperty();
            std::printf("[INFO] REMED-GFX-164 active-target alpha/resolve matrix on EASYGL\n");

            RunConstructionAndFormat(dev);
            RunClearAndDrawing(dev, 0, "non-MSAA");
            RunClearAndDrawing(dev, 4, "supported MSAA");
            RunRawTransferAndDelivery(dev);
            RunActiveMrtReadback(dev);
            RunTargetSampling(dev);
            RunOrdinaryTextureControl(dev);
            RunBackbufferControl(dev);
            RunDisposalAndTeardown(dev);
            RunGlDiagnostics();

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::printf("[INFO] EASYGL GFX-164: %d/%d checks passed\n", passed_, passed_ + failed_);
            Exit();
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> gdm_;
        std::unique_ptr<RenderTarget2D> heldToTeardown_;
        bool ran_ = false;
        int passed_ = 0;
        int failed_ = 0;

        void Check(bool ok, const std::string& label, const std::string& detail = {})
        {
            std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", label.c_str(),
                        detail.empty() ? "" : " -- ", detail.c_str());
            if (ok) ++passed_; else ++failed_;
        }

        static std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, int samples)
        {
            return std::make_unique<RenderTarget2D>(
                dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, samples,
                RenderTargetUsage::PreserveContents);
        }

        static void NeutralState(GraphicsDevice& dev)
        {
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
        }

        static void DrawUniform(GraphicsDevice& dev, const Color& color)
        {
            BasicEffect effect(dev);
            effect.setTextureEnabledProperty(false);
            effect.setLightingEnabledProperty(false);
            effect.VertexColorEnabled = true;
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.Apply();

            const VertexPositionColor vertices[6] = {
                { Vector3(-1.f,  1.f, 0.f), color },
                { Vector3( 1.f, -1.f, 0.f), color },
                { Vector3(-1.f, -1.f, 0.f), color },
                { Vector3(-1.f,  1.f, 0.f), color },
                { Vector3( 1.f,  1.f, 0.f), color },
                { Vector3( 1.f, -1.f, 0.f), color }
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
        }

        void CheckUniformWindow(const std::vector<Color>& data, int start, int count,
                                const Color& expected, const std::string& label)
        {
            bool prefix = true;
            bool suffix = true;
            int exact = 0;
            int transparent = 0;
            int lostAlphaOnly = 0;
            int wrongRgbAndAlpha = 0;
            std::string firstMismatch;

            for (int i = 0; i < start; ++i)
                prefix = prefix && Same(data[static_cast<std::size_t>(i)], kSentinel);
            for (std::size_t i = static_cast<std::size_t>(start + count); i < data.size(); ++i)
                suffix = suffix && Same(data[i], kSentinel);

            for (int i = 0; i < count; ++i)
            {
                const Color& got = data[static_cast<std::size_t>(start + i)];
                if (Same(got, expected))
                {
                    ++exact;
                    continue;
                }
                if (Same(got, kTransparentBlack)) ++transparent;
                const bool rgbExact = got.getRProperty() == expected.getRProperty()
                    && got.getGProperty() == expected.getGProperty()
                    && got.getBProperty() == expected.getBProperty();
                const bool alphaExact = got.getAProperty() == expected.getAProperty();
                if (rgbExact && !alphaExact) ++lostAlphaOnly;
                if (!rgbExact && !alphaExact) ++wrongRgbAndAlpha;
                if (firstMismatch.empty())
                    firstMismatch = "first=" + Text(got) + " expected=" + Text(expected);
            }

            Check(prefix, label + ": protected prefix survived");
            Check(suffix, label + ": protected suffix survived");
            Check(exact == count, label + ": exact RGBA payload",
                  std::to_string(exact) + "/" + std::to_string(count) + " exact, " +
                  std::to_string(transparent) + " transparent-black, " +
                  std::to_string(lostAlphaOnly) + " alpha-only mismatches, " +
                  std::to_string(wrongRgbAndAlpha) + " RGB+alpha mismatches" +
                  (firstMismatch.empty() ? "" : ", " + firstMismatch));
        }

        void ReadFull(RenderTarget2D& target, const Color& expected, const std::string& label)
        {
            std::vector<Color> data(
                static_cast<std::size_t>(kStart + kCount + kSuffix), kSentinel);
            target.GetData(data.data(), kStart, kCount);
            CheckUniformWindow(data, kStart, kCount, expected, label);
        }

        void ReadRectangle(RenderTarget2D& target, const Color& expected,
                           const std::string& label)
        {
            const Rectangle rect(2, 1, 3, 5);
            const int count = rect.Width * rect.Height;
            std::vector<Color> data(
                static_cast<std::size_t>(kStart + count + kSuffix), kSentinel);
            target.GetData(0, &rect, data.data(), kStart, count);
            CheckUniformWindow(data, kStart, count, expected, label);
        }

        void RunConstructionAndFormat(GraphicsDevice& dev)
        {
            auto plain = MakeTarget(dev, 0);
            auto msaa = MakeTarget(dev, 4);
            Check(plain->getFormatProperty() == SurfaceFormat::Color,
                  "creation: non-MSAA target reports SurfaceFormat::Color");
            Check(msaa->getFormatProperty() == SurfaceFormat::Color,
                  "creation: MSAA target reports SurfaceFormat::Color");
            Check(msaa->getMultiSampleCountProperty() > 0,
                  "creation: requested 4x selects supported multisample storage",
                  "applied=" + std::to_string(msaa->getMultiSampleCountProperty()));

            auto& native = dynamic_cast<IRenderTargetBackend&>(msaa->GetBackend());
            using GenFramebuffers = void (APIENTRY*)(GLsizei, GLuint*);
            using DeleteFramebuffers = void (APIENTRY*)(GLsizei, const GLuint*);
            using BindFramebuffer = void (APIENTRY*)(GLenum, GLuint);
            using AttachTexture = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
            using GetAttachment = void (APIENTRY*)(GLenum, GLenum, GLenum, GLint*);
            const auto gen = reinterpret_cast<GenFramebuffers>(
                SDL_GL_GetProcAddress("glGenFramebuffers"));
            const auto del = reinterpret_cast<DeleteFramebuffers>(
                SDL_GL_GetProcAddress("glDeleteFramebuffers"));
            const auto bind = reinterpret_cast<BindFramebuffer>(
                SDL_GL_GetProcAddress("glBindFramebuffer"));
            const auto attach = reinterpret_cast<AttachTexture>(
                SDL_GL_GetProcAddress("glFramebufferTexture2D"));
            const auto get = reinterpret_cast<GetAttachment>(
                SDL_GL_GetProcAddress("glGetFramebufferAttachmentParameteriv"));
            Check(gen && del && bind && attach && get,
                  "creation: OpenGL attachment diagnostics are available");
            if (!gen || !del || !bind || !attach || !get) return;

            GLuint probe = 0;
            gen(1, &probe);
            bind(GL_READ_FRAMEBUFFER, probe);
            attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                   native.GetColorGLHandle(), 0);
            GLint r = 0, g = 0, b = 0, a = 0;
            get(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &r);
            get(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &g);
            get(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &b);
            get(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &a);
            Check(r == 8 && g == 8 && b == 8 && a == 8,
                  "creation: native resolve texture has RGBA8 channel storage",
                  "bits=" + std::to_string(r) + "/" + std::to_string(g) + "/" +
                  std::to_string(b) + "/" + std::to_string(a));
            bind(GL_READ_FRAMEBUFFER, 0);
            del(1, &probe);
        }

        void RunClearAndDrawing(GraphicsDevice& dev, int requestedSamples,
                                const std::string& sampleLabel)
        {
            auto target = MakeTarget(dev, requestedSamples);
            const int applied = target->getMultiSampleCountProperty();
            const std::string prefix = sampleLabel + " (applied=" + std::to_string(applied) + ")";
            if (requestedSamples > 0)
                Check(applied > 0, prefix + ": multisampling is genuinely active");

            const std::array<Color, 7> clears = {
                kTransparentBlack, kOpaqueBlack, kZeroRgbAlpha, kAlphaOne,
                kAlphaLow, kAlphaHigh, kOpaqueColour
            };
            for (std::size_t i = 0; i < clears.size(); ++i)
            {
                dev.SetRenderTarget(target.get());
                NeutralState(dev);
                dev.Clear(clears[i]);
                const std::string label = prefix + " clear " + std::to_string(i) + " " + Text(clears[i]);
                ReadFull(*target, clears[i], label + " first active read");
                ReadRectangle(*target, clears[i], label + " valid rectangle repeated read");
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ReadFull(*target, clears[i], label + " post-unbind control");
            }

            const std::array<Color, 4> draws = {
                kZeroRgbAlpha, kAlphaLow, kAlphaHigh, kOpaqueColour
            };
            for (std::size_t i = 0; i < draws.size(); ++i)
            {
                dev.SetRenderTarget(target.get());
                NeutralState(dev);
                dev.Clear(kTransparentBlack);
                DrawUniform(dev, draws[i]);
                const std::string label = prefix + " draw " + std::to_string(i) + " " + Text(draws[i]);
                ReadFull(*target, draws[i], label + " active read");

                // GetData must restore the active draw FBO.  No rebind occurs before this Clear.
                const Color afterRead = (i & 1U) ? kOpaqueBlack : kAlphaOne;
                dev.Clear(afterRead);
                ReadRectangle(*target, afterRead, label + " continued producer after read");
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
        }

        void RunRawTransferAndDelivery(GraphicsDevice& dev)
        {
            auto target = MakeTarget(dev, 4);
            const Color expected(37, 113, 229, 73);
            dev.SetRenderTarget(target.get());
            NeutralState(dev);
            dev.Clear(expected);

            auto& native = dynamic_cast<IRenderTargetBackend&>(target->GetBackend());
            std::vector<std::uint8_t> raw(static_cast<std::size_t>(kCount) * 4, 0xCD);
            const bool delivered = native.GetData(0, 0, 0, kW, kH, raw.data(),
                                                   static_cast<int>(raw.size()));
            int rawExact = 0;
            for (int i = 0; i < kCount; ++i)
            {
                const std::size_t p = static_cast<std::size_t>(i) * 4;
                if (raw[p + 0] == expected.getRProperty()
                    && raw[p + 1] == expected.getGProperty()
                    && raw[p + 2] == expected.getBProperty()
                    && raw[p + 3] == expected.getAProperty())
                    ++rawExact;
            }
            Check(delivered && rawExact == kCount,
                  "native transfer: backend glReadPixels delivers exact RGBA bytes",
                  std::to_string(rawExact) + "/" + std::to_string(kCount));
            ReadFull(*target, expected,
                     "GetData delivery: public Color bytes match the native RGBA transfer");

            dev.Clear(kOpaqueBlack);
            ReadFull(*target, kOpaqueBlack,
                     "GetData delivery: native read did not disturb the active producer binding");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        void RunTargetSampling(GraphicsDevice& dev)
        {
            auto source = MakeTarget(dev, 4);
            auto consumer = MakeTarget(dev, 4);
            const Color expected(91, 23, 187, 149);

            dev.SetRenderTarget(source.get());
            NeutralState(dev);
            dev.Clear(expected);

            // The switch is the established producer->consumer boundary: it resolves source.
            dev.SetRenderTarget(consumer.get());
            NeutralState(dev);
            dev.Clear(kTransparentBlack);
            SpriteBatch sprites(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sprites.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point,
                          &noDepth, &noCull);
            sprites.Draw(*source, Rectangle(0, 0, kW, kH), Color::White);
            sprites.End();

            ReadFull(*consumer, expected,
                     "target sampling: sampled MSAA producer reaches active MSAA consumer");
            ReadFull(*source, expected,
                     "target sampling before direct readback preserves the producer texture");

            // Reading an idle source must not redirect the still-active consumer's draw target.
            dev.Clear(kZeroRgbAlpha);
            ReadFull(*consumer, kZeroRgbAlpha,
                     "target sampling: unrelated source read preserves consumer binding");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        void RunActiveMrtReadback(GraphicsDevice& dev)
        {
            auto first = MakeTarget(dev, 4);
            auto second = MakeTarget(dev, 4);
            dev.SetRenderTargets({ RenderTargetBinding(first.get()),
                                   RenderTargetBinding(second.get()) });
            NeutralState(dev);
            dev.Clear(kAlphaHigh);
            ReadFull(*first, kAlphaHigh,
                     "active MRT: slot 0 direct read resolves current colour");

            // The slot-0 read must restore the MRT draw FBO, not that target's single-target FBO.
            dev.Clear(kAlphaLow);
            ReadFull(*second, kAlphaLow,
                     "active MRT: continued producer reaches slot 1 after slot 0 read");
            ReadFull(*first, kAlphaLow,
                     "active MRT: continued producer also reaches slot 0");
            dev.SetRenderTargets({});
            ReadFull(*second, kAlphaLow,
                     "active MRT: post-unbind resolve agrees with the direct read");
        }

        void RunOrdinaryTextureControl(GraphicsDevice& dev)
        {
            Texture2D texture(dev, kW, kH, false, SurfaceFormat::Color);
            std::vector<Color> source(static_cast<std::size_t>(kCount), kSentinel);
            const std::array<Color, 7> palette = {
                kTransparentBlack, kOpaqueBlack, kZeroRgbAlpha, kAlphaOne,
                kAlphaLow, kAlphaHigh, kOpaqueColour
            };
            for (int i = 0; i < kCount; ++i)
                source[static_cast<std::size_t>(i)] = palette[static_cast<std::size_t>(i) % palette.size()];
            texture.SetData(source.data(), kCount);

            std::vector<Color> full(static_cast<std::size_t>(kStart + kCount + kSuffix), kSentinel);
            texture.GetData(full.data(), kStart, kCount);
            bool exact = true;
            for (int i = 0; i < kCount; ++i)
                exact = exact && Same(full[static_cast<std::size_t>(kStart + i)],
                                      source[static_cast<std::size_t>(i)]);
            bool guards = true;
            for (int i = 0; i < kStart; ++i) guards = guards && Same(full[static_cast<std::size_t>(i)], kSentinel);
            for (std::size_t i = kStart + kCount; i < full.size(); ++i) guards = guards && Same(full[i], kSentinel);
            Check(exact, "ordinary Texture2D: full GetData preserves every RGBA pattern");
            Check(guards, "ordinary Texture2D: startIndex and suffix remain protected");

            const Rectangle rect(1, 2, 4, 3);
            const int count = rect.Width * rect.Height;
            std::vector<Color> part(static_cast<std::size_t>(kStart + count + kSuffix), kSentinel);
            texture.GetData(0, &rect, part.data(), kStart, count);
            exact = true;
            for (int y = 0; y < rect.Height; ++y)
                for (int x = 0; x < rect.Width; ++x)
                    exact = exact && Same(
                        part[static_cast<std::size_t>(kStart + y * rect.Width + x)],
                        source[static_cast<std::size_t>(rect.Y + y) * kW + rect.X + x]);
            Check(exact, "ordinary Texture2D: valid rectangle preserves RGBA ordering");
        }

        void RunBackbufferControl(GraphicsDevice& dev)
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color expected(29, 101, 223, 255);
            dev.Clear(expected);
            const Rectangle rect(3, 4, 5, 3);
            const int count = rect.Width * rect.Height;
            std::vector<Color> data(static_cast<std::size_t>(kStart + count + kSuffix), kSentinel);
            dev.GetBackBufferData(&rect, data.data(), kStart, count);
            CheckUniformWindow(data, kStart, count, expected,
                               "backbuffer control: valid rectangle and guarded delivery");
        }

        void RunDisposalAndTeardown(GraphicsDevice& dev)
        {
            {
                auto target = MakeTarget(dev, 4);
                dev.SetRenderTarget(target.get());
                NeutralState(dev);
                dev.Clear(kAlphaHigh);
                ReadFull(*target, kAlphaHigh, "disposal: final active read is exact");
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                target->Dispose();
                Check(target->getIsDisposedProperty(), "disposal: target disposes after direct readback");
            }

            heldToTeardown_ = MakeTarget(dev, 4);
            dev.SetRenderTarget(heldToTeardown_.get());
            NeutralState(dev);
            dev.Clear(kAlphaLow);
            ReadFull(*heldToTeardown_, kAlphaLow,
                     "device teardown: live target's final active read is exact");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(true, "device teardown: live target retained through game shutdown");
        }

        void RunGlDiagnostics()
        {
            using GetError = GLenum (APIENTRY*)();
            const auto getError = reinterpret_cast<GetError>(SDL_GL_GetProcAddress("glGetError"));
            std::vector<GLenum> errors;
            if (getError)
                for (int i = 0; i < 64; ++i)
                {
                    const GLenum error = getError();
                    if (error == GL_NO_ERROR) break;
                    errors.push_back(error);
                }
            std::string detail;
            for (const GLenum error : errors)
            {
                char text[16]{};
                std::snprintf(text, sizeof(text), "0x%04X", error);
                if (!detail.empty()) detail += ",";
                detail += text;
            }
            Check(getError && errors.empty(),
                  "OpenGL diagnostics: complete error drain is clean", detail);
        }
    };
}

int main()
{
    Gfx164Test game;
    game.Run();
    return game.Result();
}
