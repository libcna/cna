#ifndef CNA_GAME_H
#define CNA_GAME_H

#include "GraphicsDevice.h"
#include "SpriteBatch.h"

namespace CNA {

    class Game {
    public:
        Game();
        virtual ~Game();

        void Run();
        void Exit() { isRunning = false; }

    protected:
        virtual void Initialize();
        virtual void LoadContent();
        virtual void Update(float deltaTime);
        virtual void Draw();

        GraphicsDevice* graphicsDevice;
        SpriteBatch* spriteBatch;

    private:
        bool isRunning;
    };
}

#endif // CNA_GAME_H
