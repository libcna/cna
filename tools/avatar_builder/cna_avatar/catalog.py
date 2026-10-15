# SPDX-License-Identifier: MS-PL
"""The catalog's entries and their GLB files.

Catalog v3 revision 2 keeps every id of the first v3 release in its kind (format 1 and 2
descriptions name only those) and adds entries only format 3 descriptions name ("format": 3).
Textures shared by many assets are catalog files of their own (TEXTURES), named by each material.
"""
from . import rig, body, textures
from .glb import GlbBuilder


FEATURE_TINTS = {"eyeLeft": ("none", "eyes"), "eyeRight": ("none", "eyes"), "eyebrowLeft": ("hair",),
                 "eyebrowRight": ("hair",), "mouth": ("none",)}
# Highlight scale per material family: wet eyes and lips, hair and leather sheen, matte cloth.
SPECULAR = {"eyeLeft": (2.5, 3.0), "eyeRight": (2.5, 3.0), "eyebrowLeft": (0.4,), "eyebrowRight": (0.4,), "mouth": (1.6,)}
CLOTH_SPECULAR = {"knit": 0.35, "rib": 0.35, "cable": 0.3, "fleece": 0.25, "denim": 0.45, "canvas": 0.5, "pleat": 0.4,
                  "leather": 2.0, "plain": 1.0, "strands": 2.0}
HAIR_SPECULAR = 2.2
# Makeup decals: (tint, specular, layer).
MAKEUP_DECALS = {"shadowLeft": ("eyeShadow", 0.6, 0), "shadowRight": ("eyeShadow", 0.6, 0), "blushLeft": ("blush", 0.2, 0),
                 "blushRight": ("blush", 0.2, 0), "lips": ("lips", 1.8, 0), "linerLeft": ("none", 1.0, 1),
                 "linerRight": ("none", 1.0, 1)}


def texture_file(kind):
    """The catalog file of a shared texture."""
    return "tex_%s.png" % kind


def texture_bytes(kind):
    if kind == "hair_clump":
        return textures.hair_clump()
    if kind == "hair_cap":
        return textures.hair_cap()
    if kind == "hair_coil":
        return textures.hair_coil()
    if kind == "hair_twist":
        return textures.hair_twist()
    if kind.startswith("stubble"):
        from . import accessories
        heavy, body_type = kind.startswith("stubble_heavy"), kind.rsplit("_", 1)[1]
        return accessories.stubble_texture(body_type, 0.85 if heavy else 0.55)
    if kind == "skin_head":
        return textures.skin_head()
    return textures.cloth(kind)


def body_glb(body_type):
    """Body, head and the face-feature and makeup decal patches (drawn with atlas tiles)."""
    from . import face
    skin, head, details = body.build(body_type)
    builder = GlbBuilder({"cna": {"asset": "body", "bodyType": body_type, "rig": "cna-avatar-71",
                                  "authoredHeight": rig.PROPORTIONS[body_type]["height"]}})
    skin_index = builder.add_rig(body_type)
    skin_material = builder.add_material("skin", "skin")
    parts = [(skin, skin_material), (head, builder.add_material("head", "skin", texture_file=texture_file("skin_head")))]
    if details.positions:
        parts.append((details, skin_material))
    decals = face.build_decals(body_type)
    makeup = face.build_makeup(body_type)

    def decal(name, mesh, tint, specular, layer):
        parts.append((mesh, builder.add_material("%s%d" % (name, layer), tint, specular=specular,
                                                 extras={"cnaFeature": name, "cnaLayer": layer})))
    # Draw order within a layer is this order: makeup under the features it frames.
    for name in ("shadowLeft", "shadowRight", "blushLeft", "blushRight"):
        decal(name, makeup[name], *MAKEUP_DECALS[name])
    for feature in ("eyeLeft", "eyeRight", "eyebrowLeft", "eyebrowRight"):
        for layer, tint in enumerate(FEATURE_TINTS[feature]):
            decal(feature, decals[feature], tint, SPECULAR[feature][layer], layer)
    decal("lips", makeup["lips"], *MAKEUP_DECALS["lips"])
    decal("mouth", decals["mouth"], "none", SPECULAR["mouth"][0], 0)
    for name in ("linerLeft", "linerRight"):
        decal(name, makeup[name], *MAKEUP_DECALS[name])
    builder.add_skinned_mesh("body", parts, skin_index)
    return builder.build(), ["skin_head"]


# (id, slot, name, CreateRandom weight for female, male[, extras]). Ids of the first v3 release
# (1-123) keep their kind; everything since needs format 3.
ITEMS = [
    (1, "hair", "hair_short", 0.6, 1.4), (2, "hair", "hair_spiky", 0.4, 1.0), (3, "hair", "hair_bob", 1.4, 0.4),
    (4, "hair", "hair_ponytail", 1.4, 0.3), (5, "hair", "hair_buzz", 0.3, 1.2), (6, "hair", "hair_long", 1.4, 0.3),
    (7, "hair", "hair_curly", 1.0, 1.0), (8, "hair", "hair_sidepart", 0.6, 1.2), (9, "hair", "hair_bun", 1.2, 0.2),
    (10, "hair", "hair_pigtails", 1.0, 0.1),
    (20, "top", "top_tshirt", 1.0, 1.0), (21, "top", "top_longsleeve", 1.0, 1.0), (22, "top", "top_hoodie", 1.0, 1.0),
    (23, "top", "top_tank", 1.0, 0.6), (24, "top", "top_shirt", 0.8, 1.0), (25, "top", "top_sweater", 1.0, 1.0),
    (26, "top", "top_jacket", 1.0, 1.0), (27, "top", "top_polo", 0.7, 1.0), (28, "top", "top_turtleneck", 1.0, 0.7),
    (40, "bottom", "bottom_jeans", 1.0, 1.4), (41, "bottom", "bottom_shorts", 0.8, 1.0), (42, "bottom", "bottom_skirt", 1.2, 0.05),
    (43, "bottom", "bottom_cargo", 0.6, 1.2), (44, "bottom", "bottom_joggers", 1.0, 1.0),
    (45, "bottom", "bottom_longskirt", 0.8, 0.02),
    (60, "shoes", "shoes_sneakers", 1.2, 1.2), (61, "shoes", "shoes_boots", 1.0, 1.0), (62, "shoes", "shoes_hightops", 1.0, 1.0),
    (63, "shoes", "shoes_dress", 0.6, 1.0), (64, "shoes", "shoes_flats", 1.0, 0.05),
    (80, "glasses", "glasses_round", 1.0, 1.0, {"follow": "eyes"}), (81, "glasses", "glasses_square", 1.0, 1.0, {"follow": "eyes"}),
    (82, "glasses", "glasses_aviator", 1.0, 1.0, {"follow": "eyes"}),
    (83, "glasses", "glasses_browline", 1.0, 1.0, {"follow": "eyes"}),
    (100, "hat", "hat_cap", 1.0, 1.0), (101, "hat", "hat_beanie", 1.0, 1.0), (102, "hat", "hat_bucket", 1.0, 1.0),
    (103, "hat", "hat_fedora", 0.8, 1.0), (104, "hat", "hat_headband", 1.0, 0.3),
]
# Revision 2 hairstyles (format 3): (name, female weight, male weight).
REVISED_HAIR = [
    ("hair_crew", 0.2, 1.3), ("hair_fade", 0.2, 1.2), ("hair_shaved", 0.1, 0.5), ("hair_balding", 0.0, 0.35),
    ("hair_quiff", 0.2, 0.9), ("hair_slick", 0.3, 0.7), ("hair_crop", 0.5, 1.0), ("hair_curlyshort", 0.6, 1.0),
    ("hair_afro", 0.6, 0.7), ("hair_twists", 0.4, 0.6), ("hair_shag", 0.8, 0.5), ("hair_wavy", 1.0, 0.4),
    ("hair_bobbangs", 1.2, 0.1), ("hair_lob", 1.2, 0.2), ("hair_curtain", 1.0, 0.5), ("hair_mohawk", 0.1, 0.25),
    ("hair_topknot", 0.3, 0.5), ("hair_halfup", 1.0, 0.1), ("hair_longbangs", 1.2, 0.1), ("hair_longwavy", 1.2, 0.15),
    ("hair_layered", 1.1, 0.1), ("hair_longcurly", 1.0, 0.1), ("hair_braid", 0.9, 0.05), ("hair_lowponytail", 1.0, 0.2),
    ("hair_highponytail", 1.0, 0.05), ("hair_sideponytail", 0.7, 0.05), ("hair_spacebuns", 0.5, 0.05),
]
ITEMS += [(200 + k, "hair", name, f, m, {"format": 3}) for k, (name, f, m) in enumerate(REVISED_HAIR)]
# Revision 2 tops, bottoms and shoes (format 3).
REVISED_TOPS = [("top_vneck", 1.0, 1.0), ("top_raglan", 0.8, 1.0), ("top_buttonup", 0.8, 1.0), ("top_flannel", 0.6, 1.0),
                ("top_sweatshirt", 1.0, 1.0), ("top_ziphoodie", 0.9, 1.0), ("top_quarterzip", 0.6, 0.8),
                ("top_cardigan", 1.0, 0.5), ("top_vest", 0.4, 0.6), ("top_puffervest", 0.6, 0.7), ("top_puffer", 0.6, 0.7),
                ("top_blazer", 0.6, 0.8), ("top_denimjacket", 0.8, 0.8), ("top_jersey", 0.5, 0.9), ("top_stripedtee", 1.0, 0.9),
                ("top_blouse", 1.2, 0.05), ("top_henley", 0.6, 0.9), ("top_windbreaker", 0.7, 0.8), ("top_layered", 0.8, 1.0)]
REVISED_BOTTOMS = [("bottom_chinos", 0.8, 1.2), ("bottom_formal", 0.5, 0.8), ("bottom_sportshorts", 0.6, 0.8),
                   ("bottom_cargoshorts", 0.4, 0.9), ("bottom_capri", 0.8, 0.1), ("bottom_wideleg", 0.8, 0.3),
                   ("bottom_leggings", 1.0, 0.05), ("bottom_denimskirt", 0.9, 0.02), ("bottom_midiskirt", 0.9, 0.02),
                   ("bottom_trackpants", 0.8, 1.0), ("bottom_bermuda", 0.4, 0.8)]
REVISED_SHOES = [("shoes_running", 1.0, 1.0), ("shoes_loafers", 0.6, 0.8), ("shoes_slipons", 0.9, 0.8), ("shoes_hiking", 0.5, 0.8),
                 ("shoes_chelsea", 0.7, 0.7), ("shoes_sandals", 0.6, 0.4), ("shoes_rainboots", 0.4, 0.3),
                 ("shoes_skate", 0.7, 0.9), ("shoes_maryjanes", 0.8, 0.02)]
ITEMS += [(300 + k, "top", name, f, m, {"format": 3}) for k, (name, f, m) in enumerate(REVISED_TOPS)]
ITEMS += [(350 + k, "bottom", name, f, m, {"format": 3}) for k, (name, f, m) in enumerate(REVISED_BOTTOMS)]
ITEMS += [(400 + k, "shoes", name, f, m, {"format": 3}) for k, (name, f, m) in enumerate(REVISED_SHOES)]
REVISED_GLASSES = [("glasses_narrow", 1.0, 1.0), ("glasses_wire", 0.8, 0.8), ("glasses_thick", 0.8, 1.0),
                   ("glasses_cateye", 1.0, 0.2), ("glasses_oversized", 0.9, 0.5), ("glasses_bigsquare", 0.7, 0.8),
                   ("sunglasses_sport", 0.4, 0.6), ("sunglasses_square", 0.6, 0.8), ("sunglasses_round", 0.6, 0.6)]
REVISED_HATS = [("hat_backwardcap", 0.5, 1.0), ("hat_trucker", 0.4, 0.9), ("hat_flatcap", 0.4, 0.8), ("hat_sunhat", 0.9, 0.3),
                ("hat_tophat", 0.2, 0.3), ("hat_beret", 0.8, 0.3), ("hat_helmet", 0.2, 0.3), ("hat_slouchbeanie", 0.8, 0.8),
                ("hat_visor", 0.6, 0.5), ("hat_western", 0.3, 0.5)]
ITEMS += [(430 + k, "glasses", name, f, m, {"format": 3, "follow": "eyes"}) for k, (name, f, m) in enumerate(REVISED_GLASSES)]
ITEMS += [(450 + k, "hat", name, f, m, {"format": 3}) for k, (name, f, m) in enumerate(REVISED_HATS)]
# Accessories (format 3): earrings follow the ears; gloves hide rings.
ACCESSORY_ITEMS = [(510, "earrings", "earrings_stud", 1.0, 0.3), (511, "earrings", "earrings_bigstud", 0.8, 0.1),
                   (512, "earrings", "earrings_hoop", 1.0, 0.1), (513, "earrings", "earrings_bighoop", 0.7, 0.02),
                   (514, "earrings", "earrings_drop", 0.8, 0.02), (515, "earrings", "earrings_huggie", 0.8, 0.2),
                   (530, "wristwear", "wrist_watch", 1.0, 1.4), (531, "wristwear", "wrist_sportwatch", 0.6, 1.0),
                   (532, "wristwear", "wrist_bangle", 1.0, 0.1), (533, "wristwear", "wrist_beads", 0.8, 0.5),
                   (534, "wristwear", "wrist_band", 0.4, 0.5), (535, "wristwear", "wrist_cuff", 0.6, 0.2),
                   (550, "gloves", "gloves_knit", 1.0, 1.0), (551, "gloves", "gloves_fingerless", 0.8, 0.8),
                   (552, "gloves", "gloves_work", 0.4, 0.8), (553, "gloves", "gloves_formal", 0.5, 0.3),
                   (560, "rings", "ring_band", 1.0, 1.0), (561, "rings", "ring_signet", 0.4, 0.8), (562, "rings", "ring_gem", 1.0, 0.1)]
ITEMS += [(i, slot, name, f, m, {"format": 3, **({"follow": "ears"} if slot == "earrings" else {}),
                                 **({"hides": ["rings"]} if slot == "gloves" else {})})
          for i, slot, name, f, m in ACCESSORY_ITEMS]
# Personal features: facial hair (format 2, and format 3 for revision 2's) and ear shapes (format 3).
REVISED_FACIAL = [("facial_stubble", 1.4), ("facial_heavystubble", 1.0), ("facial_shortbeard", 1.0), ("facial_longbeard", 0.4),
                  ("facial_horseshoe", 0.4), ("facial_handlebar", 0.2), ("facial_soulpatch", 0.3), ("facial_chinbeard", 0.4),
                  ("facial_vandyke", 0.5), ("facial_sideburns", 0.5), ("facial_muttonchops", 0.2), ("facial_pencil", 0.2)]
FEATURE_ITEMS = [
    (120, "facialHair", "facial_mustache", 0.0, 1.0), (121, "facialHair", "facial_goatee", 0.0, 1.0),
    (122, "facialHair", "facial_beard", 0.0, 1.0), (123, "facialHair", "facial_chinstrap", 0.0, 1.0),
] + [(480 + k, "facialHair", name, 0.0, weight, {"format": 3}) for k, (name, weight) in enumerate(REVISED_FACIAL)] + [(500 + k, "ears", "ears_" + shape.lower(), 1.0, 1.0) for k, shape in enumerate(
    ("round", "small", "large", "longLobe", "attached", "protruding"))]
EAR_SHAPE_OF = {"ears_" + shape.lower(): shape for shape in ("round", "small", "large", "longLobe", "attached", "protruding")}


# Ready-made outfits (the editor's Outfits page): every one sets the top, bottoms, shoes, headwear
# and gloves (0 removes them) and may set glasses or wristwear; the colours go together. Original
# generic themes, nothing from any store.
OUTFITS = [
    ("Casual", {"top": 20, "bottom": 40, "shoes": 60, "hat": 0, "gloves": 0},
     {"top": (50, 150, 200), "bottom": (36, 52, 92), "shoes": (240, 240, 236)}),
    ("Smart casual", {"top": 302, "bottom": 350, "shoes": 401, "hat": 0, "gloves": 0},
     {"top": (232, 220, 196), "bottom": (36, 52, 92), "shoes": (120, 90, 60)}),
    ("Formal", {"top": 311, "bottom": 351, "shoes": 63, "hat": 0, "gloves": 0},
     {"top": (30, 32, 36), "bottom": (30, 32, 36), "shoes": (30, 32, 36), "accessory": (240, 240, 236)}),
    ("Sport", {"top": 313, "bottom": 352, "shoes": 400, "hat": 0, "gloves": 0, "wristwear": 534},
     {"top": (220, 60, 56), "bottom": (240, 240, 236), "shoes": (240, 240, 236), "accessory": (220, 60, 56)}),
    ("Outdoor", {"top": 317, "bottom": 43, "shoes": 403, "hat": 100, "gloves": 0},
     {"top": (108, 116, 64), "bottom": (196, 174, 130), "shoes": (120, 90, 60), "accessory": (240, 150, 40)}),
    ("Retro", {"top": 301, "bottom": 355, "shoes": 62, "hat": 0, "gloves": 0, "glasses": 438},
     {"top": (250, 210, 60), "bottom": (120, 90, 60), "shoes": (220, 60, 56), "accessory": (240, 150, 40)}),
    ("Gamer", {"top": 305, "bottom": 44, "shoes": 407, "hat": 450, "gloves": 0},
     {"top": (30, 32, 36), "bottom": (60, 62, 70), "shoes": (240, 240, 236), "accessory": (90, 180, 80)}),
    ("Summer", {"top": 314, "bottom": 41, "shoes": 405, "hat": 453, "gloves": 0, "glasses": 437},
     {"top": (50, 150, 200), "bottom": (232, 220, 196), "shoes": (120, 90, 60), "accessory": (232, 220, 196)}),
    ("Winter", {"top": 310, "bottom": 40, "shoes": 61, "hat": 457, "gloves": 550},
     {"top": (120, 36, 48), "bottom": (36, 52, 92), "shoes": (120, 90, 60), "accessory": (232, 220, 196),
      "gloves": (232, 220, 196)}),
    ("Cozy", {"top": 25, "bottom": 44, "shoes": 402, "hat": 101, "gloves": 0},
     {"top": (196, 174, 130), "bottom": (150, 152, 156), "shoes": (240, 240, 236), "accessory": (130, 80, 170)}),
]

# Named shapes for the editor, each a few controls in [-1, 1] of their range (at most 0.6: the editor
# applies them 1.35 times, which still leaves a slider step either side). Shapes, not people: no
# preset stands for an ethnicity or a gender.
FACE_PRESETS = [
    ("face", "Balanced", {}),
    ("face", "Round", {"headWidth": 0.35, "headHeight": -0.35, "jawWidth": 0.4, "jawTaper": -0.55, "chinSize": -0.45,
                       "chinHeight": -0.3, "cheekFullness": 0.6, "jawAngle": -0.35}),
    ("face", "Long", {"headWidth": -0.3, "headHeight": 0.6, "jawWidth": -0.25, "chinHeight": 0.6, "chinSize": 0.2,
                      "cheekFullness": -0.4, "foreheadHeight": 0.35, "noseSize": 0.15}),
    ("face", "Square", {"headWidth": 0.15, "jawWidth": 0.6, "jawAngle": 0.6, "jawTaper": -0.6, "chinSize": 0.45,
                        "cheekFullness": -0.3, "cheekboneWidth": 0.15}),
    ("face", "Heart", {"headWidth": 0.25, "foreheadHeight": 0.4, "cheekboneWidth": 0.55, "jawWidth": -0.6, "jawTaper": 0.6,
                       "chinSize": -0.35, "chinProjection": 0.2, "jawAngle": -0.45}),
    ("face", "Oval", {"headWidth": -0.15, "headHeight": 0.3, "cheekboneWidth": 0.3, "jawTaper": 0.3, "chinHeight": 0.2}),
    ("face", "Soft", {"cheekFullness": 0.6, "jawAngle": -0.55, "chinSize": -0.2, "jawTaper": -0.2, "chinProjection": -0.3,
                      "noseSize": -0.15}),
    ("face", "Angular", {"cheekboneWidth": 0.6, "cheekFullness": -0.6, "jawAngle": 0.6, "chinProjection": 0.55, "jawTaper": 0.3,
                         "chinSize": 0.3, "noseBridge": 0.45}),
    ("face", "Broad", {"headWidth": 0.6, "jawWidth": 0.5, "cheekboneWidth": 0.5, "noseWidth": 0.35, "mouthWidth": 0.3,
                       "headHeight": -0.2}),
    ("face", "Narrow", {"headWidth": -0.6, "jawWidth": -0.45, "cheekboneWidth": -0.45, "noseWidth": -0.3, "headHeight": 0.3,
                        "eyeSpacing": -0.25}),
    ("face", "Youthful", {"eyeSize": 0.5, "noseSize": -0.45, "noseTip": 0.35, "cheekFullness": 0.5, "jawWidth": -0.3,
                          "chinSize": -0.35, "foreheadHeight": 0.45, "browHeight": 0.2, "headHeight": -0.3, "mouthWidth": -0.2}),
    ("face", "Mature", {"eyeSize": -0.3, "noseSize": 0.45, "noseBridge": 0.35, "jawWidth": 0.35, "jawAngle": 0.35,
                        "chinProjection": 0.35, "cheekFullness": -0.45, "browHeight": -0.3, "upperLip": -0.35, "lowerLip": -0.25,
                        "headHeight": 0.25, "eyeShape": -0.3}),
    ("eyes", "Balanced", {}),
    ("eyes", "Bright", {"eyeSize": 0.45, "eyeShape": 0.3, "browHeight": 0.2}),
    ("eyes", "Narrow", {"eyeSize": -0.3, "eyeShape": -0.5}),
    ("eyes", "Wide-set", {"eyeSpacing": 0.5, "eyeSize": 0.1}),
    ("eyes", "Close-set", {"eyeSpacing": -0.5}),
    ("eyes", "Upturned", {"eyeTilt": 0.55, "browHeight": 0.15}),
    ("eyes", "Downturned", {"eyeTilt": -0.5, "browHeight": -0.2}),
    ("eyes", "Round", {"eyeShape": 0.6, "eyeSize": 0.2}),
    ("eyes", "Almond", {"eyeShape": -0.3, "eyeTilt": 0.25, "eyeSize": 0.1}),
    ("brows", "Natural", {}),
    ("brows", "High", {"browHeight": 0.6}),
    ("brows", "Low", {"browHeight": -0.5, "browAngle": -0.2}),
    ("brows", "Wide apart", {"browSpacing": 0.6}),
    ("brows", "Close", {"browSpacing": -0.6}),
    ("brows", "Lifted", {"browAngle": 0.6, "browHeight": 0.2}),
    ("brows", "Bold", {"browThickness": 0.6}),
    ("brows", "Fine", {"browThickness": -0.6}),
    ("ears", "Natural", {}),
    ("ears", "Small", {"earSize": -0.6}),
    ("ears", "Large", {"earSize": 0.6}),
    ("ears", "High", {"earHeight": 0.6}),
    ("ears", "Low", {"earHeight": -0.6}),
    ("ears", "Flat", {"earAngle": -0.6}),
    ("ears", "Prominent", {"earAngle": 0.6, "earSize": 0.2}),
    ("nose", "Balanced", {}),
    ("nose", "Button", {"noseSize": -0.35, "noseWidth": -0.1, "noseHeight": 0.15, "noseTip": 0.4}),
    ("nose", "Broad", {"noseSize": 0.25, "noseWidth": 0.55, "noseBridge": 0.4}),
    ("nose", "Long", {"noseSize": 0.35, "noseWidth": -0.2, "noseHeight": -0.3, "noseTip": -0.3}),
    ("nose", "Narrow", {"noseWidth": -0.5, "noseBridge": -0.4}),
    ("nose", "Upturned", {"noseTip": 0.6, "noseHeight": 0.2}),
    ("nose", "Strong", {"noseBridge": 0.6, "noseSize": 0.3, "noseTip": -0.2}),
    ("nose", "Small", {"noseSize": -0.6}),
    ("mouth", "Balanced", {}),
    ("mouth", "Wide smile", {"mouthWidth": 0.5, "mouthHeight": 0.1}),
    ("mouth", "Small", {"mouthWidth": -0.45}),
    ("mouth", "Full lips", {"upperLip": 0.5, "lowerLip": 0.6}),
    ("mouth", "Thin lips", {"upperLip": -0.5, "lowerLip": -0.4}),
    ("mouth", "Full upper", {"upperLip": 0.6}),
    ("mouth", "Full lower", {"lowerLip": 0.6}),
    ("body", "Average", {}),
    ("body", "Petite", {"height": -0.6, "build": -0.3, "proportion": -0.3}),
    ("body", "Youthful", {"height": -0.75, "build": -0.2, "proportion": -0.9}),
    ("body", "Tall", {"height": 0.6, "proportion": 0.4}),
    ("body", "Athletic", {"height": 0.2, "build": 0.25, "proportion": 0.3}),
    ("body", "Broad", {"build": 0.6, "proportion": -0.1}),
    ("body", "Slim", {"build": -0.6, "proportion": 0.2}),
    ("body", "Heavy", {"build": 0.95}),
]


# What the editor calls entries whose names do not read well on their own.
LABELS = {
    "hair_short": "Short", "hair_spiky": "Spiky", "hair_bob": "Bob", "hair_ponytail": "Ponytail", "hair_buzz": "Buzz cut",
    "hair_long": "Long", "hair_curly": "Curly", "hair_sidepart": "Side part", "hair_bun": "Bun", "hair_pigtails": "Pigtails",
    "hair_crew": "Crew cut", "hair_fade": "Fade", "hair_shaved": "Shaved", "hair_balding": "Balding", "hair_quiff": "Quiff",
    "hair_slick": "Slicked back", "hair_crop": "Textured crop", "hair_curlyshort": "Short curls", "hair_afro": "Afro",
    "hair_twists": "Twists", "hair_shag": "Shag", "hair_wavy": "Wavy", "hair_bobbangs": "Bob with bangs", "hair_lob": "Long bob",
    "hair_curtain": "Curtain bangs", "hair_mohawk": "Mohawk", "hair_topknot": "Top knot", "hair_halfup": "Half up",
    "hair_longbangs": "Long with bangs", "hair_longwavy": "Long waves", "hair_layered": "Long layers", "hair_longcurly": "Long curls",
    "hair_braid": "Braid", "hair_lowponytail": "Low ponytail", "hair_highponytail": "High ponytail",
    "hair_sideponytail": "Side ponytail", "hair_spacebuns": "Two buns",
    "top_tshirt": "T-shirt", "top_longsleeve": "Long sleeve", "top_hoodie": "Hoodie", "top_tank": "Tank top", "top_shirt": "Shirt",
    "top_sweater": "Cable sweater", "top_jacket": "Zip jacket", "top_polo": "Polo", "top_turtleneck": "Turtleneck",
    "top_vneck": "V-neck", "top_raglan": "Raglan tee", "top_buttonup": "Button-up", "top_flannel": "Flannel", "top_sweatshirt": "Sweatshirt",
    "top_ziphoodie": "Zip hoodie", "top_quarterzip": "Quarter zip", "top_cardigan": "Cardigan", "top_vest": "Waistcoat",
    "top_puffervest": "Puffer vest", "top_puffer": "Puffer jacket", "top_blazer": "Blazer", "top_denimjacket": "Denim jacket",
    "top_jersey": "Sport jersey", "top_stripedtee": "Striped tee", "top_blouse": "Blouse", "top_henley": "Henley",
    "top_windbreaker": "Windbreaker", "top_layered": "Layered tee",
    "bottom_jeans": "Jeans", "bottom_shorts": "Shorts", "bottom_skirt": "Pleated skirt", "bottom_cargo": "Cargo trousers",
    "bottom_joggers": "Joggers", "bottom_longskirt": "Long skirt", "bottom_chinos": "Chinos", "bottom_formal": "Dress trousers",
    "bottom_sportshorts": "Sport shorts", "bottom_cargoshorts": "Cargo shorts", "bottom_capri": "Capris", "bottom_wideleg": "Wide leg",
    "bottom_leggings": "Leggings", "bottom_denimskirt": "Denim skirt", "bottom_midiskirt": "Midi skirt",
    "bottom_trackpants": "Track pants", "bottom_bermuda": "Bermudas",
    "shoes_sneakers": "Sneakers", "shoes_boots": "Boots", "shoes_hightops": "High-tops", "shoes_dress": "Dress shoes",
    "shoes_flats": "Flats", "shoes_running": "Running shoes", "shoes_loafers": "Loafers", "shoes_slipons": "Slip-ons",
    "shoes_hiking": "Hiking boots", "shoes_chelsea": "Chelsea boots", "shoes_sandals": "Sandals", "shoes_rainboots": "Rain boots",
    "shoes_skate": "Skate shoes", "shoes_maryjanes": "Mary Janes",
    "glasses_round": "Round", "glasses_square": "Square", "glasses_aviator": "Aviators", "glasses_browline": "Browline",
    "glasses_narrow": "Narrow", "glasses_wire": "Wire", "glasses_thick": "Thick frame", "glasses_cateye": "Cat-eye",
    "glasses_oversized": "Oversized", "glasses_bigsquare": "Big square", "sunglasses_sport": "Sport shades",
    "sunglasses_square": "Square shades", "sunglasses_round": "Round shades",
    "hat_cap": "Cap", "hat_beanie": "Beanie", "hat_bucket": "Bucket hat", "hat_fedora": "Fedora", "hat_headband": "Headband",
    "hat_backwardcap": "Cap, backwards", "hat_trucker": "Trucker cap", "hat_flatcap": "Flat cap", "hat_sunhat": "Sun hat",
    "hat_tophat": "Top hat", "hat_beret": "Beret", "hat_helmet": "Bike helmet", "hat_slouchbeanie": "Slouch beanie",
    "hat_visor": "Visor", "hat_western": "Western hat",
    "facial_mustache": "Mustache", "facial_goatee": "Goatee", "facial_beard": "Full beard", "facial_chinstrap": "Chinstrap",
    "facial_stubble": "Stubble", "facial_heavystubble": "Heavy stubble", "facial_shortbeard": "Short beard",
    "facial_longbeard": "Long beard", "facial_horseshoe": "Horseshoe", "facial_handlebar": "Handlebar", "facial_soulpatch": "Soul patch",
    "facial_chinbeard": "Chin beard", "facial_vandyke": "Van Dyke", "facial_sideburns": "Sideburns", "facial_muttonchops": "Mutton chops",
    "facial_pencil": "Pencil mustache",
    "ears_round": "Round", "ears_small": "Small", "ears_large": "Large", "ears_longlobe": "Long lobes", "ears_attached": "Attached lobes",
    "ears_protruding": "Standing out",
    "earrings_stud": "Studs", "earrings_bigstud": "Big studs", "earrings_hoop": "Hoops", "earrings_bighoop": "Big hoops",
    "earrings_drop": "Drops", "earrings_huggie": "Huggies",
    "wrist_watch": "Watch", "wrist_sportwatch": "Sport watch", "wrist_bangle": "Bangles", "wrist_beads": "Beads",
    "wrist_band": "Sweatband", "wrist_cuff": "Cuff",
    "gloves_knit": "Knit", "gloves_fingerless": "Fingerless", "gloves_work": "Work", "gloves_formal": "Formal",
    "ring_band": "Band", "ring_signet": "Signet", "ring_gem": "Gem",
    "brows_natural": "Natural", "brows_straight": "Straight", "brows_softarch": "Soft arch", "brows_higharch": "High arch",
    "brows_thin": "Thin", "brows_thick": "Thick", "brows_short": "Short", "brows_long": "Long", "brows_angled": "Angled",
    "brows_rounded": "Rounded", "brows_bushy": "Bushy", "brows_lowflat": "Low and flat",
    "shadow_soft": "Soft", "shadow_smoky": "Smoky", "shadow_winged": "Winged", "shadow_highlight": "Highlight",
    "liner_thin": "Thin", "liner_winged": "Winged", "liner_bold": "Bold", "blush_round": "Round", "blush_sweep": "Sweep",
    "lips_tint": "Tint", "lips_color": "Colour", "lips_bold": "Bold",
}


def outfits():
    return [{"name": name, "items": items, "colors": {k: list(v) for k, v in colors.items()}} for name, items, colors in OUTFITS]


def face_presets():
    return [{"group": g, "name": n, "values": v} for g, n, v in FACE_PRESETS]


def face_styles(styles):
    """Face style entries from the atlas's style tiles: eyebrows, eye shadow, eyeliner, blush, lips."""
    from . import face
    out = []
    brow_ids = {name: 600 + k for k, name in enumerate(n for n in face.BROW_STYLES if n != "classic")}
    brow_weights = {"natural": (1.4, 1.2), "straight": (0.6, 1.2), "softArch": (1.2, 0.5), "highArch": (0.8, 0.2),
                    "thin": (0.8, 0.3), "thick": (0.6, 1.2), "short": (0.5, 0.6), "long": (0.6, 0.5), "angled": (0.5, 0.5),
                    "rounded": (0.8, 0.5), "bushy": (0.2, 0.9), "lowFlat": (0.4, 1.0)}
    for name, id_ in brow_ids.items():
        out.append({"id": id_, "slot": "eyebrows", "name": "brows_" + name.lower(), "tiles": styles[("eyebrows", name)],
                    "random": dict(zip(("female", "male"), brow_weights[name])), "format": 3})
    for k, name in enumerate(face.SHADOW_STYLES):
        out.append({"id": 640 + k, "slot": "eyeShadow", "name": "shadow_" + name, "tiles": styles[("eyeShadow", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3, "opacity": 0.85})
    for k, name in enumerate(face.LINER_STYLES):
        out.append({"id": 650 + k, "slot": "eyeliner", "name": "liner_" + name, "tiles": styles[("eyeliner", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3})
    for k, name in enumerate(face.BLUSH_STYLES):
        out.append({"id": 660 + k, "slot": "blush", "name": "blush_" + name, "tiles": styles[("blush", name)],
                    "random": {"female": 1.0, "male": 0.1}, "format": 3, "opacity": 0.75})
    for k, (name, opacity) in enumerate((("tint", 0.45), ("color", 0.75), ("bold", 0.95))):
        out.append({"id": 670 + k, "slot": "lips", "name": "lips_" + name, "tiles": styles[("lips", "mask")],
                    "random": {"female": 1.0, "male": 0.05}, "format": 3, "opacity": opacity})
    return out


def item_glb(body_type, item_id, slot, name):
    """One item fitted to one body. Returns (GLB bytes, shared texture kinds it names)."""
    from . import hair, garments, accessories, head
    builder = GlbBuilder({"cna": {"asset": "item", "id": item_id, "slot": slot, "name": name, "bodyType": body_type,
                                  "rig": "cna-avatar-71"}})
    skin_index = builder.add_rig(body_type)
    parts = []
    used = []

    def material(part_name, tint, kind, colour=(1.0, 1.0, 1.0), specular=None, extras=None):
        if kind not in used:
            used.append(kind)
        return builder.add_material(part_name, tint, color=colour, texture_file=texture_file(kind), specular=specular, extras=extras)
    if slot == "hair":
        locks, scalp, bands = hair.build_parts(body_type, name)
        if scalp.positions:
            # Under the hair the scalp shell is in its shade: darker, so a part or a hairline reads
            # as depth (the shared textures and the renderer have no occlusion of their own).
            shade = (0.88, 0.88, 0.88) if locks.positions else (1.0, 1.0, 1.0)
            parts.append((scalp, material(name + "_scalp", "hair", "hair_cap", colour=shade, specular=HAIR_SPECULAR)))
        if locks.positions:
            texture = hair.TEXTURE.get(name, "hair_clump")
            parts.append((locks, material(name, "hair", texture, specular=HAIR_SPECULAR * (0.5 if texture != "hair_clump" else 1.0))))
        if bands.positions:
            # Hair ties: a darker band of the hair's own colour where it is gathered.
            parts.append((bands, material(name + "_tie", "hair", "hair_cap", colour=(0.42, 0.40, 0.40), specular=1.2)))
    elif slot == "ears":
        shell, folds = head.ear_meshes(body_type, EAR_SHAPE_OF[name])
        # The shell is in the shade of the rim and folds round it.
        parts.append((shell, builder.add_material(name + "_shell", "skin", color=(0.93, 0.91, 0.91))))
        parts.append((folds, builder.add_material(name, "skin")))
    else:
        built = {"top": garments.top, "bottom": garments.bottom, "shoes": garments.shoes, "glasses": accessories.glasses,
                 "hat": accessories.hat, "facialHair": accessories.facial_hair, "earrings": accessories.earrings,
                 "wristwear": garments.wristwear, "gloves": garments.gloves, "rings": garments.rings}[slot](body_type, name)
        for index, part in enumerate(built):
            mesh, tint, kind, colour = part[:4]
            extras = part[4] if len(part) > 4 else None
            texture = "hair_clump" if kind == "strands" else kind
            specular = 2.5 if slot == "glasses" else 0.2 if kind.startswith("stubble") else \
                HAIR_SPECULAR * 0.45 if slot == "facialHair" else 3.0 if tint == "jewelry" else CLOTH_SPECULAR.get(kind, 1.0)
            parts.append((mesh, material("%s_%d" % (name, index), tint, texture, colour, specular, extras)))
    builder.add_skinned_mesh(name, parts, skin_index)
    return builder.build(), used


from . import animations  # noqa: E402


def _tangents(times, values, loop):
    """Catmull-Rom derivatives (per second); zero at the ends of a one-shot clip."""
    n = len(values)
    size = len(values[0])
    out = []
    for k in range(n):
        if 0 < k < n - 1:
            prev, nxt, dt = values[k - 1], values[k + 1], times[k + 1] - times[k - 1]
        elif loop and n > 2:
            # The first and last keys are the same pose; look across the seam.
            prev, nxt = values[n - 2], values[1]
            dt = (times[n - 1] - times[n - 2]) + (times[1] - times[0])
        else:
            out.append(tuple(0.0 for _ in range(size)))
            continue
        out.append(tuple((nxt[i] - prev[i]) / dt for i in range(size)))
    return out


def _hermite(times, triples, t):
    """The runtime's cubic spline (AvatarClips.cpp) at time t."""
    value = lambda k: triples[k][1]
    if len(times) == 1 or t <= times[0]:
        return value(0)
    if t >= times[-1]:
        return value(len(times) - 1)
    k = max(i for i in range(len(times) - 1) if times[i] <= t)
    td = times[k + 1] - times[k]
    s = (t - times[k]) / td
    s2, s3 = s * s, s * s * s
    v0, b0, a1, v1 = value(k), triples[k][2], triples[k + 1][0], value(k + 1)
    return tuple(v0[i] * (2 * s3 - 3 * s2 + 1) + b0[i] * td * (s3 - 2 * s2 + s) + v1[i] * (-2 * s3 + 3 * s2) +
                 a1[i] * td * (s3 - s2) for i in range(len(v0)))


def _channels(times, keys, loop):
    """Cubic-spline channels for every animated slot of a list of poses at `times`."""
    channels = []
    for slot in range(rig.BONE_COUNT):
        values = [pose.local[slot] for pose in keys]
        if all(max(abs(v[0]), abs(v[1]), abs(v[2])) < 1e-6 for v in values):
            continue
        assert slot not in rig.HELPERS, "helper slots are never animated"
        continuous = [values[0]]
        for v in values[1:]:
            previous = continuous[-1]
            continuous.append(tuple(-c for c in v) if sum(a * b for a, b in zip(v, previous)) < 0 else v)
        if all(max(abs(a - b) for a, b in zip(v, continuous[0])) < 1e-6 for v in continuous):
            channels.append((slot, "rotation", [((0.0,) * 4, continuous[0], (0.0,) * 4)], None))
            continue
        tangents = _tangents(times, continuous, loop)
        channels.append((slot, "rotation", [(t, v, t) for v, t in zip(continuous, tangents)], None))
    roots = [pose.root for pose in keys]
    if any(max(abs(c) for c in r) > 1e-7 for r in roots):
        tangents = _tangents(times, roots, loop)
        channels.append((0, "translation", [(t, v, t) for v, t in zip(roots, tangents)], None))
    return channels


LEGS = [rig.INDEX[n + side] for side in ("Left", "Right") for n in ("Hip", "Knee", "Ankle")]


def _feet_between(keys, times, t):
    """Foot targets at t: each key's lift/forward offsets eased between neighbouring keys."""
    k = max(i for i in range(len(times)) if times[i] <= t + 1e-9)
    if k == len(times) - 1:
        return keys[k].feet
    s = (t - times[k]) / (times[k + 1] - times[k])
    s = s * s * (3.0 - 2.0 * s)
    out = []
    for part in range(2):
        a, b = keys[k].feet[part] or {}, keys[k + 1].feet[part] or {}
        merged = {}
        for side in set(a) | set(b):
            va, vb = a.get(side, (0.0, 0.0, 0.0)), b.get(side, (0.0, 0.0, 0.0))
            merged[side] = tuple(x + (y - x) * s for x, y in zip(va, vb))
        out.append(merged or None)
    return tuple(out)


def _refine_feet(clip, times, keys, channels, step=1.0 / 12.0, tolerance=0.004):
    """Keys are IK-solved only where they were authored; in between, interpolated rotations can let
    a planted foot slide or sink. Where it would move more than `tolerance`, the legs are solved
    again at `step` and their channels replaced by the denser keys."""
    from .posing import Pose
    by_slot = {(slot, path): (tr, tt) for slot, path, tr, tt in channels}
    dense = sorted(set([round(i * step, 6) for i in range(int(clip.duration / step) + 1)] + list(times)))
    if dense[-1] < clip.duration - 1e-9:
        dense.append(clip.duration)
    worst = 0.0
    solved = []
    for t in dense:
        pose = Pose(clip.body)
        for (slot, path), (triples, _) in by_slot.items():
            value = _hermite(times if len(triples) > 1 else [0.0], triples, t)
            if path == "rotation":
                n = math.sqrt(sum(c * c for c in value))
                pose.local[slot] = tuple(c / n for c in value)
            else:
                pose.root = value
        lift, forward = _feet_between(keys, times, t)
        _, pos = pose.world()
        for side, sign in rig.SIDES:
            target = pose.bind[rig.INDEX["Ankle" + side]]
            if lift and side in lift:
                target = tuple(a + b for a, b in zip(target, lift[side]))
            worst = max(worst, math.sqrt(sum((a - b) ** 2 for a, b in zip(pos[rig.INDEX["Ankle" + side]], target))))
        pose.plant_feet(lift, forward)
        solved.append(pose)
    if worst <= tolerance:
        return channels
    kept = [c for c in channels if c[0] not in LEGS or c[1] != "rotation"]
    for slot in LEGS:
        values = [pose.local[slot] for pose in solved]
        continuous = [values[0]]
        for v in values[1:]:
            continuous.append(tuple(-c for c in v) if sum(a * b for a, b in zip(v, continuous[-1])) < 0 else v)
        tangents = _tangents(dense, continuous, clip.loop)
        kept.append((slot, "rotation", [(tg, v, tg) for v, tg in zip(continuous, tangents)], dense))
    return kept


import math  # noqa: E402


def animations_glb():
    clips = animations.build_clips()
    builder = GlbBuilder({"cna": {"asset": "animations", "rig": "cna-avatar-71", "presets": animations.PRESETS}})
    builder.add_rig("male")
    for index, clip in enumerate(clips):
        times = [t for t, _ in clip.keys]
        keys = [pose for _, pose in clip.keys]
        assert times[0] == 0.0 and abs(times[-1] - clip.duration) < 1e-9 and times == sorted(times)
        channels = _refine_feet(clip, times, keys, _channels(times, keys, clip.loop))
        extras = {"cnaPreset": index, "cnaLoop": clip.loop,
                  "cnaExpressions": [[round(e[0], 4)] + list(e[1:]) for e in sorted(clip.expressions)]}
        builder.add_animation(clip.name, times, channels, extras)
    return builder.build()
