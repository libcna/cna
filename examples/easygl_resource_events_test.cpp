// SPDX-License-Identifier: MS-PL
// Task 217: GraphicsDevice.ResourceCreated / ResourceDestroyed events.
//
// Verified:
//   1. ResourceCreated fires once per resource construction (when device is set).
//   2. ResourceDestroyed fires once per Dispose() call.
//   3. Double Dispose() fires ResourceDestroyed only once.
//   4. Resources constructed without a device (BlendState) do NOT fire the events.
//   5. Name/tag passed to ResourceDestroyed match what was set on the resource.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

struct TagObj : public System::Object
{
    const std::string& GetTypeName() const override
    {
        static const std::string n = "TagObj";
        return n;
    }
};

class ResourceEventsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    static void disposeVia(System::IDisposable& r) { r.Dispose(); }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── 1. ResourceCreated fires when VertexBuffer is constructed ──────
        {
            int createCount = 0;
            int destroyCount = 0;
            dev.ResourceCreated  += [&](System::Object*, const ResourceCreatedEventArgs&) { ++createCount;  };
            dev.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyCount; };

            {
                VertexBuffer vb(dev, 4);
                check(createCount == 1, "VB ctor fires ResourceCreated once");
                check(destroyCount == 0, "No ResourceDestroyed before Dispose");

                disposeVia(vb);
                check(destroyCount == 1, "VB Dispose fires ResourceDestroyed once");

                disposeVia(vb);  // double-dispose
                check(destroyCount == 1, "Second Dispose does NOT re-fire ResourceDestroyed");
            }

            dev.ResourceCreated.Clear();
            dev.ResourceDestroyed.Clear();
        }

        // ── 2. Name and tag are forwarded to ResourceDestroyed ─────────────
        {
            std::string capturedName;
            System::Object* capturedTag = nullptr;

            dev.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs& e) {
                capturedName = e.getNameProperty();
                capturedTag  = e.getTagProperty();
            };

            TagObj tagObj;
            {
                IndexBuffer ib(dev, 6);
                ib.setNameProperty("MyIB");
                ib.setTagProperty(&tagObj);
                disposeVia(ib);
            }

            check(capturedName == "MyIB",   "ResourceDestroyed carries correct Name");
            check(capturedTag  == &tagObj,  "ResourceDestroyed carries correct Tag");

            dev.ResourceDestroyed.Clear();
        }

        // ── 3. Multiple resource types all fire the events ─────────────────
        {
            int createCount  = 0;
            int destroyCount = 0;
            dev.ResourceCreated   += [&](System::Object*, const ResourceCreatedEventArgs&)   { ++createCount;  };
            dev.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyCount; };

            {
                VertexBuffer vb(dev, 4);
                IndexBuffer  ib(dev, 4);
                Texture2D    tx(dev, 2, 2);

                check(createCount == 3, "3 resources → 3 ResourceCreated events");

                disposeVia(vb);
                disposeVia(ib);
                disposeVia(tx);

                check(destroyCount == 3, "3 Disposes → 3 ResourceDestroyed events");
            }

            dev.ResourceCreated.Clear();
            dev.ResourceDestroyed.Clear();
        }

        // ── 4. BlendState without device does NOT fire the events ──────────
        {
            int createCount = 0;
            dev.ResourceCreated += [&](System::Object*, const ResourceCreatedEventArgs&) { ++createCount; };

            {
                BlendState bs;  // no device — NOXNA extension, device=nullptr
                (void)bs;
            }

            check(createCount == 0, "BlendState(no device) does NOT fire ResourceCreated");

            dev.ResourceCreated.Clear();
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ResourceEventsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ResourceEventsTest game;
    game.Run();
    return game.getResult();
}
