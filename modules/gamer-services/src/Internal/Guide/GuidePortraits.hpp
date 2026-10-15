// SPDX-License-Identifier: MS-PL
#pragma once
// Avatars in the system UI, drawn through the standard AvatarRenderer into render targets that the
// Guide then draws as sprites: a still head-and-shoulders portrait for lists and toasts, or a live
// full-body view for the gamer card.
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
class Texture2D;
}
namespace Microsoft::Xna::Framework::GamerServices {
class AvatarRenderer;
}

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief How an avatar is framed: head and shoulders, the whole avatar, or the part a kind of
 * clothing covers (top, bottoms, shoes). */
enum class Framing : std::uint8_t { Head, Body, Upper, Lower, Feet, Face, Profile, Hands };

/** @brief A camera for an avatar view: it looks at target from target + offset. */
struct Shot {
    /** @brief Point looked at. */
    Microsoft::Xna::Framework::Vector3 target;
    /** @brief Eye position relative to the target. */
    Microsoft::Xna::Framework::Vector3 offset;
    /** @brief Vertical field of view, radians. */
    float fov=0.5f;
};
/** @brief Joint positions of an avatar's bind pose (the renderer must be ready).
 * @param renderer Renderer. @return Position per bone. */
std::vector<Microsoft::Xna::Framework::Vector3> bindJoints(const Microsoft::Xna::Framework::GamerServices::AvatarRenderer& renderer);
/** @brief The camera for a framing, from the avatar's own proportions.
 * @param framing Framing. @param joints Bind-pose joints. @param height Avatar height, metres.
 * @param aspect View width over height. @return Camera. */
Shot shotFor(Framing framing,const std::vector<Microsoft::Xna::Framework::Vector3>& joints,float height,float aspect);

/**
 * @brief Draws avatar pictures for the system UI. request() names what a frame wants; render()
 * (once per frame, outside any SpriteBatch) draws what changed; the latest picture comes back from
 * request(). Implemented by the avatar views (makeAvatarPortraits).
 */
class PortraitSource {
public:
    /** @brief Releases every picture. */
    virtual ~PortraitSource()=default;
    /** @brief Asks for a picture this frame. @param description 1021 bytes (anything else: none).
     * @param framing Framing. @param width Pixels. @param height Pixels. @param live Animate (idle)
     * every frame rather than draw once. @return The picture drawn so far, or null while it loads. */
    virtual Microsoft::Xna::Framework::Graphics::Texture2D* request(const std::vector<unsigned char>& description,Framing framing,int width,int height,bool live)=0;
    /** @brief Draws the pictures requested this frame that need it; restores the back buffer as the
     * render target. @param device Device. @param seconds Time for live views. */
    virtual void render(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,double seconds)=0;
    /** @brief Releases every picture (device lost or changed). */
    virtual void clear()=0;
    /** @brief Pictures requested last frame that are not drawn yet. @return Count. */
    [[nodiscard]] virtual int waiting() const=0;
};

/** @brief The avatar views, drawn through the standard AvatarRenderer (GuidePortraits.cpp).
 * @return A new, empty cache. */
std::unique_ptr<PortraitSource> makeAvatarPortraits();
/** @brief Installs what the Guide's frame draws avatars with (installSystemGuide); until then a
 * portrait tile shows its placeholder. @param factory Factory, or null. */
void setPortraitSourceFactory(std::unique_ptr<PortraitSource>(*factory)());

/**
 * @brief The system UI's avatar views as the Guide's frame holds them.
 *
 * Link boundary: the frame (GuideScreens.cpp) reaches AvatarRenderer only through the installed
 * factory, so a game that shows a message box does not link the avatar system and its catalog.
 */
class Portraits {
public:
    /** @brief Creates an empty cache. */
    Portraits();
    /** @brief Releases every picture. */
    ~Portraits();
    Portraits(const Portraits&)=delete;
    Portraits& operator=(const Portraits&)=delete;
    /** @brief Asks for a picture this frame (PortraitSource::request). @param description 1021 bytes.
     * @param framing Framing. @param width Pixels. @param height Pixels. @param live Animate.
     * @return The picture drawn so far, or null while it loads or without avatar views. */
    Microsoft::Xna::Framework::Graphics::Texture2D* request(const std::vector<unsigned char>& description,Framing framing,int width,int height,bool live);
    /** @brief Draws what this frame asked for. @param device Device. @param seconds Time for live views. */
    void render(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,double seconds);
    /** @brief Releases every picture (device lost or changed). */
    void clear();
    /** @brief Pictures requested last frame that are not drawn yet. @return Count. */
    [[nodiscard]] int waiting() const;
private:
    PortraitSource* source();
    std::unique_ptr<PortraitSource> source_;
};
}
