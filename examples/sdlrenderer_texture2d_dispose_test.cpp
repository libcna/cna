// SPDX-License-Identifier: MS-PL
// Task 689: Verify Texture2D::Dispose releases the underlying SDL_Texture exactly once on
// SDL_Renderer -- no double-free.
//
// Texture2D::Dispose(bool) calls backend_.reset() unconditionally, then Texture::Dispose(bool)
// (which itself guards on the shared, backend-agnostic isDisposed_ flag). Since backend_ is a
// std::shared_ptr<ITextureBackend>, calling .reset() on an already-null shared_ptr is a
// well-defined, safe no-op (not a double-free) -- so calling Dispose() twice on the same
// Texture2D cannot double-free the real SdlTextureBackend/SDL_Texture*, regardless of the
// isDisposed_ guard's own placement relative to backend_.reset().
//
// The genuinely CNA-specific (not XNA-standard) wrinkle worth verifying on SDL_Renderer
// specifically: Texture2D is a copyable VALUE type here (backed by a shared_ptr), unlike XNA's
// reference-type Texture2D (a single C# object, where "copies" are just aliases to the one
// object and Disposing it disposes that one shared instance for every alias). Copying a
// Texture2D in CNA creates a second, independent GraphicsResource (its own isDisposed_), but
// BOTH copies share the same backend_ shared_ptr -- so Disposing one copy only decrements the
// backend's refcount; the real SdlTextureBackend (and its real SDL_Texture*) is only actually
// destroyed, calling SDL_DestroyTexture exactly once, when the LAST copy releases it.
//
// This test exercises that exact scenario against the real SDL_Renderer backend:
//   1. Create tex1 (2x1 Red|Blue), copy it to tex2 (shares backend_).
//   2. Draw tex1, read back real pixels -- confirm correct.
//   3. Dispose tex1. Confirm tex1.IsDisposed == true.
//   4. Draw tex2 (the surviving copy) again, read back real pixels -- confirm STILL correct: the
//      real SDL_Texture was not destroyed while tex2 still held a reference to it (no
//      use-after-free), and was not corrupted by tex1's Dispose (no double-free artifact).
//   5. Dispose tex2 too -- now the real SDL_Texture is genuinely released (last owner gone).
//   6. Dispose tex1 AGAIN (double-Dispose on an already-disposed instance) -- must not crash.
//   7. Dispose tex2 AGAIN (double-Dispose via the OTHER copy, after the shared backend is
//      already fully released) -- must not crash either.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// A crash (segfault / ASAN double-free abort) is the only realistic failure mode this test can
// hit; a clean run to completion combined with the pixel/IsDisposed checks below is the pass
// signal. Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kRed (255,   0,   0, 255);
static const Color kBlue(  0,   0, 255, 255);

class SdlTexture2DDisposeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    Color DrawAndSample(Texture2D& tex)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(tex, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 2, 1), Color::White);
        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(4, 4, 1, 1); // left half -> Red
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::vector<Color> px = { kRed, kBlue };
        Texture2D tex1(dev, 2, 1);
        tex1.SetData(px.data(), 2);
        Texture2D tex2 = tex1; // shares backend_ (shared_ptr) with tex1

        check(colourMatch(DrawAndSample(tex1), kRed), "tex1 draws correctly before Dispose");

        tex1.Dispose();
        check(tex1.getIsDisposedProperty(), "tex1.IsDisposed == true after Dispose()");
        check(!tex2.getIsDisposedProperty(), "tex2.IsDisposed == false (independent GraphicsResource state)");

        check(colourMatch(DrawAndSample(tex2), kRed),
              "tex2 (surviving copy) still draws correctly after tex1.Dispose() -- no premature free");

        // No crash on double-Dispose of the already-disposed tex1.
        tex1.Dispose();
        check(true, "tex1.Dispose() called a second time did not crash");

        tex2.Dispose();
        check(tex2.getIsDisposedProperty(), "tex2.IsDisposed == true after its own Dispose()");

        // No crash on double-Dispose of tex2, now that the shared backend is fully released.
        tex2.Dispose();
        check(true, "tex2.Dispose() called a second time did not crash");

        Exit();
    }

public:
    SdlTexture2DDisposeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(8);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DDisposeTest game;
    game.Run();
    return game.getResult();
}
