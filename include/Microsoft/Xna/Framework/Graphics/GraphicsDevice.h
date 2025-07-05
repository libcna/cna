#ifndef CNA_GRAPHICSDEVICE_H
#define CNA_GRAPHICSDEVICE_H

#include <SDL3/SDL.h>

#include "Viewport.h"
#include "Microsoft/Xna/Framework/Color.h"

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice {
    public:
        GraphicsDevice();

        ~GraphicsDevice();

        void Clear(const Color& color);
        void Clear(float r, float g, float b, float a);
        void Present();
        SDL_Renderer* GetRenderer();
        DEF_PROP(Microsoft::Xna::Framework::Graphics::Viewport, Viewport, getter1, setter0, member1, static0, constret0, ref1, constmet0)

    protected:

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;

    //friend class SpriteBatch;
    };

}

#endif // CNA_GRAPHICSDEVICE_H
