// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices::Avatars {
/** @brief Categories of the CNA avatar editor, in the order its rail shows them: the person's
 * features first, then what they wear. */
enum class EditorCategory : std::uint8_t {
    Body, Skin, Face, Eyes, Eyebrows, Ears, Nose, Mouth, Hair, FacialHair, Makeup,
    Outfits, Tops, Bottoms, Shoes, Headwear, Glasses, Earrings, Wristwear, Gloves, Rings
};
/** @brief Number of editor categories. */
inline constexpr int EditorCategoryCount = 21;
/** @brief Categories of the first group (Features); the rest are Style. */
inline constexpr int EditorFeatureCategoryCount = 11;
/** @brief A category's name as the editor shows it. @param category Category. @return Name. */
const char* editorCategoryName(EditorCategory category);
/** @brief The group a category is shown under ("Features" or "Style"). @param category Category. @return Group. */
const char* editorCategoryGroup(EditorCategory category);

/** @brief How a row is shown and changed. */
enum class EditorRowKind : std::uint8_t {
    /** @brief Items shown as rendered cards (styles, body type, outfits). */
    Cards,
    /** @brief Colours. */
    Swatches,
    /** @brief A value between two ends (height, build, one face feature). */
    Slider,
    /** @brief Named shapes that set several features at once (rendered cards). */
    Presets,
    /** @brief A few named choices shown as text (which side an accessory is worn on). */
    Chips
};

/** @brief What one editor row shows. */
struct EditorField {
    /** @brief Row name ("Height", "Jaw"). */
    std::string label;
    /** @brief Current value as text ("1.72 m", "Hoodie", "Wider"). */
    std::string value;
    /** @brief How it is shown. */
    EditorRowKind kind=EditorRowKind::Cards;
    /** @brief The colour of a swatch row. */
    std::optional<AvatarColor> swatch;
    /** @brief Position of a slider row in [-1, 1]. */
    std::optional<float> slider;
    /** @brief A remark under the row ("Gloves hide rings"), empty for none. */
    std::string note;
};

/** @brief One choice of a cards, swatches, presets or chips row. */
struct EditorOption {
    /** @brief Its name ("Ponytail", "Round"). */
    std::string label;
    /** @brief The avatar with it chosen (a card's picture). */
    AvatarDescriptor result;
    /** @brief It is what the avatar has now. */
    bool current=false;
    /** @brief A swatch's colour. */
    std::optional<AvatarColor> color;
};

/** @brief Where the editor's camera looks for a category. */
enum class EditorView : std::uint8_t { Body, Head, Face, Profile, Upper, Lower, Feet, Hands };

/**
 * @brief The avatar editor's state: a description being edited against one catalog, as
 * categories of rows (item cards, colour swatches, sliders, named shapes, outfits) that offer
 * exactly what the catalog has. It edits only CNA description data; saving is the caller's (the
 * service's avatars.set or a local profile).
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
    /** @brief Category shown. @return Category. */
    [[nodiscard]] EditorCategory category() const {return category_;}
    /** @brief Shows a category; the first row is selected. @param category Category. */
    void setCategory(EditorCategory category);
    /** @brief Turns to the next or previous category, wrapping. @param delta Categories forward (negative back). */
    void turnPage(int delta);
    /** @brief Where the camera looks for the category shown. @return View. */
    [[nodiscard]] EditorView view() const;
    /** @brief Rows of the category shown. @return Count. */
    [[nodiscard]] std::size_t fieldCount() const {return rows().size();}
    /** @brief One row of the category shown. @param index Row. @return Field. */
    [[nodiscard]] EditorField field(std::size_t index) const;
    /** @brief The choices of a cards, swatches, presets or chips row. @param index Row. @return Options (empty for a slider). */
    [[nodiscard]] std::vector<EditorOption> options(std::size_t index) const;
    /** @brief Selected row. @return Index. */
    [[nodiscard]] std::size_t selection() const {return selection_;}
    /** @brief Moves the selection, stopping at the ends. @param delta Rows down (negative up). */
    void select(int delta);
    /** @brief Selects a row. @param index Row. */
    void selectRow(std::size_t index);
    /** @brief Steps the selected row: the next choice, colour or slider step. @param delta Steps.
     * @return Whether the avatar changed. */
    bool adjust(int delta);
    /** @brief Takes one choice of a row. @param row Row. @param option Choice. @return Whether the avatar changed. */
    bool choose(std::size_t row,std::size_t option);
    /** @brief A new random avatar of the same body type, believable rather than any mix (see
     * randomDescriptor). CreateRandom is not affected. */
    void randomize();
    /** @brief Randomizes only what the category shown covers; everything else stays. */
    void randomizeCategory();
    /** @brief Puts what the category shown covers back as editing started. @return Whether the avatar changed. */
    bool resetCategory();
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
    enum class Kind : std::uint8_t { BodyType, Height, Build, Proportion, Color, Entry, Sides, Face, Preset, Outfit };
    struct Row {
        Kind kind;
        int index=0;
        std::string label;
    };
    [[nodiscard]] std::vector<Row> rows() const;
    [[nodiscard]] std::vector<std::uint16_t> choices(AvatarItemSlot slot) const;
    [[nodiscard]] std::string entryName(std::uint16_t id,AvatarItemSlot slot) const;
    [[nodiscard]] std::uint16_t entryOf(const AvatarDescriptor& descriptor,AvatarItemSlot slot) const;
    void setEntry(AvatarDescriptor& descriptor,AvatarItemSlot slot,std::uint16_t id) const;
    [[nodiscard]] AvatarDescriptor withPreset(const AvatarDescriptor& source,const FacePreset& preset) const;
    [[nodiscard]] AvatarDescriptor coherentRandom(std::uint8_t bodyType);
    AvatarDescriptor fitted(const AvatarDescriptor& source) const;
    void takeCategory(const AvatarDescriptor& source);

    std::shared_ptr<const CatalogManifest> catalog_;
    std::vector<std::uint8_t> stored_;
    AvatarDescriptor start_;
    AvatarDescriptor working_;
    EditorCategory category_=EditorCategory::Body;
    std::size_t selection_=0;
    std::mt19937 random_;
};

}
