#ifndef CNA_GAME_H
#define CNA_GAME_H
#include <any>

#include "ExitingEventArgs.h"
#include "GameTime.h"
#include "Content/ContentManager.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/SpriteBatch.h"

#include "System/EventHandler.h"
#include "System/TimeSpan.h"
#include "System/Runtime/CompilerServices/EventArgs.h"

namespace Microsoft::Xna::Framework {

    class Game {
    public:
        dgetter(Content::ContentManager, Content)
        private: Graphics::GraphicsDevice GraphicsDevice_; public: [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();
        ddata(bool, IsMouseVisible)
        ddata(System::TimeSpan, TargetElapsedTime)
        ddata(System::TimeSpan, InactiveSleepTime)

        System::EventHandler<ExitingEventArgs> Exiting;

        Game();
        virtual ~Game();

        void Run();
        void Exit() { isRunning = false; }

    protected:
        virtual void Initialize();
        virtual void LoadContent();
        virtual void UnloadContent();
        virtual void OnDeactivated(std::any sender, System::Runtime::CompilerServices::EventArgs args);
        virtual void OnActivated(std::any sender, System::Runtime::CompilerServices::EventArgs args);


        virtual void Update(const Microsoft::Xna::Framework::GameTime &gameTime);
        virtual void Draw(const Microsoft::Xna::Framework::GameTime &gameTime);

        Graphics::SpriteBatch* spriteBatch;

    private:
        bool isRunning;
    };
}

#endif // CNA_GAME_H
