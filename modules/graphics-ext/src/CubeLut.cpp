// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/CubeLut.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/EngineException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Texture3D;

    namespace {

        std::string Trim(const std::string& text)
        {
            std::size_t first = 0;
            while (first < text.size()
                   && std::isspace(static_cast<unsigned char>(text[first])) != 0)
                ++first;
            std::size_t last = text.size();
            while (last > first
                   && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0)
                --last;
            return text.substr(first, last - first);
        }

        std::string Unquote(const std::string& text)
        {
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                return text.substr(1, text.size() - 2);
            return text;
        }

        int ChannelByte(const float value)
        {
            return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

    } // namespace

    CubeLut CubeLut::parse(const std::string& text)
    {
        CubeLut lut;
        std::istringstream stream(text);
        std::string line;
        std::vector<float> entries;
        int declaredSize = 0;

        while (std::getline(stream, line))
        {
            // Files written on Windows and read here keep their carriage return, which turns the
            // last number of every line into something std::stof stops at rather than rejects --
            // silently correct for the value and wrong for anything compared against the token.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            std::istringstream tokens(trimmed);
            std::string keyword;
            tokens >> keyword;

            if (keyword == "TITLE")
            {
                std::string rest;
                std::getline(tokens, rest);
                lut.title_ = Unquote(Trim(rest));
                continue;
            }
            if (keyword == "LUT_3D_SIZE")
            {
                if (!(tokens >> declaredSize))
                    throw EngineException("CNA::Graphics::CubeLut: LUT_3D_SIZE has no value");
                continue;
            }
            if (keyword == "LUT_1D_SIZE")
            {
                // Refused by name rather than ignored. A 1D table is a per-channel curve and cannot
                // express the cross-channel work a grade is for; loading one as though it were a
                // cube would produce a table with the right entry count and the wrong meaning.
                throw EngineException(
                    "CNA::Graphics::CubeLut: this is a 1D table (LUT_1D_SIZE), which is a "
                    "per-channel curve rather than a colour cube and cannot be graded with");
            }
            if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX")
            {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (!(tokens >> x >> y >> z))
                    throw EngineException("CNA::Graphics::CubeLut: " + keyword
                                          + " needs three values");
                if (keyword == "DOMAIN_MIN") lut.domainMin_ = Vector3(x, y, z);
                else                         lut.domainMax_ = Vector3(x, y, z);
                continue;
            }

            // Anything else is either an entry or a keyword this parser does not know. A line that
            // starts with a number is an entry; a line that does not is skipped, because the format
            // grows keywords and refusing to load a table over one is worse than ignoring it.
            const char first = keyword.empty() ? '\0' : keyword[0];
            if (!(std::isdigit(static_cast<unsigned char>(first)) != 0 || first == '-'
                  || first == '+' || first == '.'))
                continue;

            try
            {
                std::istringstream values(trimmed);
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (!(values >> r >> g >> b))
                    throw EngineException("CNA::Graphics::CubeLut: an entry line holds fewer than "
                                          "three numbers: '" + trimmed + "'");
                entries.push_back(r);
                entries.push_back(g);
                entries.push_back(b);
            }
            catch (const std::invalid_argument&)
            {
                throw EngineException("CNA::Graphics::CubeLut: an entry line could not be read: '"
                                      + trimmed + "'");
            }
        }

        if (declaredSize == 0)
            throw EngineException(
                "CNA::Graphics::CubeLut: the document declares no LUT_3D_SIZE, so there is no way "
                "to know what shape its entries are");
        if (declaredSize < kMinSize || declaredSize > kMaxSize)
            throw EngineException(
                "CNA::Graphics::CubeLut: LUT_3D_SIZE is " + std::to_string(declaredSize)
                + ", outside the 2..64 this layer accepts -- below two nothing can be interpolated "
                  "and above 64 the strip layout needs a texture wider than 4096 texels");

        const std::size_t expected =
            static_cast<std::size_t>(declaredSize) * declaredSize * declaredSize * 3;
        if (entries.size() != expected)
            throw EngineException(
                "CNA::Graphics::CubeLut: LUT_3D_SIZE " + std::to_string(declaredSize)
                + " needs " + std::to_string(expected / 3) + " entries and the document holds "
                + std::to_string(entries.size() / 3));

        lut.size_ = declaredSize;
        lut.entries_ = std::move(entries);
        return lut;
    }

    CubeLut CubeLut::loadFromFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw EngineException("CNA::Graphics::CubeLut: cannot open '" + path + "'");
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return parse(buffer.str());
    }

    int CubeLut::getSize() const { return size_; }

    const std::string& CubeLut::getTitle() const { return title_; }

    Vector3 CubeLut::getDomainMin() const { return domainMin_; }

    Vector3 CubeLut::getDomainMax() const { return domainMax_; }

    bool CubeLut::isUnitDomain() const
    {
        constexpr float kEpsilon = 1e-5f;
        return std::abs(domainMin_.X) < kEpsilon && std::abs(domainMin_.Y) < kEpsilon
            && std::abs(domainMin_.Z) < kEpsilon
            && std::abs(domainMax_.X - 1.0f) < kEpsilon && std::abs(domainMax_.Y - 1.0f) < kEpsilon
            && std::abs(domainMax_.Z - 1.0f) < kEpsilon;
    }

    Vector3 CubeLut::getEntry(const int red, const int green, const int blue) const
    {
        if (red < 0 || red >= size_ || green < 0 || green >= size_ || blue < 0 || blue >= size_)
            throw std::out_of_range("CNA::Graphics::CubeLut::getEntry: index outside the table");
        // Red fastest, then green, then blue: the format's order, and the one a loop written from
        // the axis names gets backwards.
        const std::size_t index =
            (static_cast<std::size_t>(blue) * size_ * size_
             + static_cast<std::size_t>(green) * size_
             + static_cast<std::size_t>(red)) * 3;
        return Vector3(entries_[index], entries_[index + 1], entries_[index + 2]);
    }

    std::unique_ptr<Texture2D> CubeLut::createStripTexture(GraphicsDevice& device) const
    {
        const int width = size_ * size_;
        auto texture = std::make_unique<Texture2D>(device, width, size_);

        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * size_);
        // The strip's own layout: x holds the blue slice and the red index within it, y holds green.
        for (int green = 0; green < size_; ++green)
            for (int x = 0; x < width; ++x)
            {
                const Vector3 value = getEntry(x % size_, green, x / size_);
                texels.emplace_back(ChannelByte(value.X), ChannelByte(value.Y),
                                    ChannelByte(value.Z), 255);
            }
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

    std::unique_ptr<Texture3D> CubeLut::createVolumeTexture(GraphicsDevice& device) const
    {
        auto texture = std::make_unique<Texture3D>(device, size_, size_, size_, false,
                                                   SurfaceFormat::Color);
        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(size_) * size_ * size_);
        for (int blue = 0; blue < size_; ++blue)
            for (int green = 0; green < size_; ++green)
                for (int red = 0; red < size_; ++red)
                {
                    const Vector3 value = getEntry(red, green, blue);
                    texels.emplace_back(ChannelByte(value.X), ChannelByte(value.Y),
                                        ChannelByte(value.Z), 255);
                }
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
