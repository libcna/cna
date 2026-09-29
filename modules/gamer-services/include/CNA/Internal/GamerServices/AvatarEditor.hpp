// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices::Avatars {
/** @brief Pages of the CNA avatar editor. */
enum class EditorCategory : std::uint8_t { Body, Features, Style };
/** @brief Number of editor pages. */
inline constexpr int EditorCategoryCount = 3;

/** @brief What one editor row shows. */
struct EditorField {
    /** @brief Row name ("Height", "Jaw width"). */
    std::string label;
    /** @brief Current value as text ("1.72 m", "Hoodie", "+2"). */
    std::string value;
    /** @brief The color of a color row. */
    std::optional<AvatarColor> swatch;
    /** @brief Position of a slider row in [-1, 1] (build and face shape). */
    std::optional<float> slider;
};

/**
 * @brief The avatar editor's state: a description being edited against one catalog, as pages of
 * rows that step through the values the catalog offers. It edits only CNA description data; saving
 * is the caller's (the service's avatars.set or a local profile).
 */
class AvatarEditorModel {
public:
    /**
     * @brief Starts editing.
     * @param catalog Catalog the result will name: the newest one the avatar's owner accepts.
     * @param current The stored description, or empty (or not a CNA avatar) to start from a random one.
     * @param seed Randomize entropy.
     */
    AvatarEditorModel(std::shared_ptr<const CatalogManifest> catalog,std::vector<std::uint8_t> current,std::uint32_t seed);

    /** @brief Catalog being edited against. @return Manifest. */
    [[nodiscard]] const CatalogManifest& catalog() const {return *catalog_;}
    /** @brief Page shown. @return Category. */
    [[nodiscard]] EditorCategory category() const {return category_;}
    /** @brief Turns the page, wrapping. @param delta Pages forward (negative back). */
    void turnPage(int delta);
    /** @brief Rows of the page shown. @return Count. */
    [[nodiscard]] std::size_t fieldCount() const {return rows().size();}
    /** @brief One row of the page shown. @param index Row. @return Field. */
    [[nodiscard]] EditorField field(std::size_t index) const;
    /** @brief Selected row. @return Index. */
    [[nodiscard]] std::size_t selection() const {return selection_;}
    /** @brief Moves the selection, wrapping. @param delta Rows down (negative up). */
    void select(int delta);
    /** @brief Steps the selected row's value. @param delta Steps. @return Whether the avatar changed. */
    bool adjust(int delta);
    /** @brief A new random avatar of the same body type. */
    void randomize();
    /** @brief Back to the avatar editing started from. */
    void revert();
    /** @brief The avatar as edited. @return Descriptor. */
    [[nodiscard]] const AvatarDescriptor& descriptor() const {return working_;}
    /** @brief The description to save. @return 1021 bytes. */
    [[nodiscard]] std::vector<std::uint8_t> encoded() const {return encode(working_);}
    /** @brief Whether saving would change the stored description. @return True when it differs. */
    [[nodiscard]] bool differsFromStored() const {return encoded()!=stored_;}
    /** @brief Records a successful save: the saved avatar is now the stored one. */
    void markSaved();

private:
    enum class Kind : std::uint8_t { BodyType, Height, Build, Color, Item, FacialHair, Face };
    struct Row {
        Kind kind;
        int index=0;
        std::string label;
    };
    [[nodiscard]] std::vector<Row> rows() const;
    [[nodiscard]] std::vector<std::uint16_t> choices(AvatarItemSlot slot) const;
    [[nodiscard]] std::string itemName(std::uint16_t id,bool feature) const;
    AvatarDescriptor fitted(const AvatarDescriptor& source) const;

    std::shared_ptr<const CatalogManifest> catalog_;
    std::vector<std::uint8_t> stored_;
    AvatarDescriptor start_;
    AvatarDescriptor working_;
    EditorCategory category_=EditorCategory::Body;
    std::size_t selection_=0;
    std::mt19937 random_;
};

}
