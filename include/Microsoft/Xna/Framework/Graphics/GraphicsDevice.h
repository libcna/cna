#ifndef CNA_GRAPHICSDEVICE_H
#define CNA_GRAPHICSDEVICE_H

#include <SDL3/SDL.h>

#include "Viewport.h"
#include "Microsoft/Xna/Framework/Color.h"
#include "NeoSdk/Property.h"
#include "Microsoft/Xna/Framework/Graphics/Viewport.h"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice {
    public:
        GraphicsDevice();

        ~GraphicsDevice();

        void Clear(const Color& color);
        void Clear(float r, float g, float b, float a);
        void Present();
        SDL_Renderer* GetRenderer();
        NeoSdk::Property<Microsoft::Xna::Framework::Graphics::Viewport> Viewport;

    protected:

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;
        Microsoft::Xna::Framework::Graphics::Viewport viewport;

    //friend class SpriteBatch;
    };

}

#endif // CNA_GRAPHICSDEVICE_H
