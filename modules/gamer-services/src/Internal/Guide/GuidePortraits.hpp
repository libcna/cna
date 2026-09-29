// SPDX-License-Identifier: MS-PL
#pragma once
// Avatars in the system UI, drawn through the standard AvatarRenderer into render targets that the
// Guide then draws as sprites: a still head-and-shoulders portrait for lists and toasts, or a live
// full-body view for the gamer card.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
class Texture2D;
}

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief How an avatar is framed. */
enum class Framing : std::uint8_t { Head, Body };

/**
 * @brief The system UI's avatar views. request() names what a frame wants; render() (once per
 * frame, outside any SpriteBatch) draws what changed; texture() hands back the latest picture.
 * Pictures not requested for a few seconds are released.
 */
class Portraits {
public:
    /** @brief Creates an empty cache. */
    Portraits();
    /** @brief Releases every picture. */
    ~Portraits();
    Portraits(const Portraits&)=delete;
    Portraits& operator=(const Portraits&)=delete;

    /** @brief Asks for a picture this frame. @param description 1021 bytes (anything else: none).
     * @param framing Framing. @param width Pixels. @param height Pixels. @param live Animate (idle)
     * every frame rather than draw once. @return The picture drawn so far, or null while it loads. */
    Microsoft::Xna::Framework::Graphics::Texture2D* request(const std::vector<unsigned char>& description,Framing framing,int width,int height,bool live);
    /** @brief Draws the pictures requested this frame that need it; restores the back buffer as the
     * render target. @param device Device. @param seconds Time for live views. */
    void render(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,double seconds);
    /** @brief Releases every picture (device lost or changed). */
    void clear();

private:
    struct Entry;
    std::vector<std::unique_ptr<Entry>> entries_;
};
}
