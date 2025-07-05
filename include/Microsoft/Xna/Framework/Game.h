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
        DEF_PROP(Content::ContentManager, Content, getter1, setter0, member1, static0, constret1, ref1, constmet1)

        private: Graphics::GraphicsDevice GraphicsDevice_; public: [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();

        DEF_PROP(bool, IsMouseVisible, getter1, setter1, member1, static0, constret1, ref1, constmet1)

        //Default FPS is 60
    private:
        System::TimeSpan* TargetElapsedTime_;

    public:
        [[nodiscard]] System::TimeSpan* getTargetElapsedTimeProperty() const;

    public:
        void setTargetElapsedTimeProperty(System::TimeSpan* v);
        DEF_PROP(System::TimeSpan, InactiveSleepTime, getter1, setter1, member1, static0, constret1, ref1, constmet1)


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

        virtual void Update(Microsoft::Xna::Framework::GameTime &gameTime);
        virtual void Draw(const Microsoft::Xna::Framework::GameTime &gameTime);

    private:
        bool isRunning;
#ifdef XNA5
    public:
        [[nodiscard]] double getTargetFPSProperty() const;
        [[nodiscard]] double getTargetMsFrameTimeProperty() const;
        static double fpsToMillisecondsPerFrame(CNA::intcs framesPerSecond);
#endif
    };
}

#endif // CNA_GAME_H
