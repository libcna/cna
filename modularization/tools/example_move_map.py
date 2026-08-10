#!/usr/bin/env python3
"""Generate the tracked-example move map for the CNA module-examples campaign.

Classifies every tracked example file (from the baseline inventory) into
SUBSYSTEM_MODULE / RENDERER_MODULE / EXTENSION_MODULE / SHARED_EXAMPLE_ASSET
and derives its destination path under the owning physical module. Emits
move-map.tsv (old_path, classification, owner, new_path); KEEP rows record
deliberately retained top-level paths. Every baseline file must be covered
exactly once; anything unmatched is a hard error.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "..", "module-examples", "baseline", "example-files.tsv")
OUT = os.path.join(HERE, "..", "module-examples", "move-map.tsv")

# renderer source prefix -> renderer module directory
RENDERER_PREFIX = {
    "ascii": "ascii", "bgfx": "bgfx", "canvas": "canvas", "diligent": "diligent",
    "direct2d": "direct2d", "directx1": "directx1", "directx2": "directx2",
    "directx3": "directx3", "directx5": "directx5", "directx6": "directx6",
    "directx7": "directx7", "directx8": "directx8", "directx9": "directx9",
    "directx10": "directx10", "directx11": "directx11", "directx12": "directx12",
    "easygl": "easygl", "freedirect": "freedirect", "gdi": "gdi", "glide": "glide",
    "headless": "headless", "htmldom": "html-dom", "llgl": "llgl",
    "magnum": "magnum", "metal": "metal", "opengl1": "opengl1",
    "opengl2": "opengl2", "opengl4": "opengl4", "opengles1": "opengles1",
    "sdlgpu": "sdl-gpu", "sdlrenderer": "sdl-renderer", "skia": "skia",
    "software": "software", "sokol": "sokol", "stub": "stub",
    "vulkan": "vulkan", "webgpu": "webgpu",
}

# demo/application directory -> owning framework module
DEMO_DIR = {
    "demo_2d": "graphics",
    "demo_sound": "audio",
    "demo_xact": "audio",
    "demo_input": "input",
    "input_smoke": "input",
    "demo_devices": "devices",
    "demo_avatar": "gamer-services",
    "demo_avatar_animation_gallery": "gamer-services",
    "demo_avatar_appearance_tint_studio": "gamer-services",
    "demo_avatar_bone_state_boundary": "gamer-services",
    "demo_avatar_dual_compare": "gamer-services",
    "demo_avatar_multi_attach_stress": "gamer-services",
    "demo_avatar_wardrobe_hotswap": "gamer-services",
    "demo_gamerservices_signin_presence": "gamer-services",
    "demo_achievement_showcase": "gamer-services",
    "demo_leaderboard_viewer": "gamer-services",
    "demo_guide_overlay_console": "gamer-services",
    "demo_gamerservices_dispatcher_watchdog": "gamer-services",
    "demo_gamer_profile_privileges": "gamer-services",
    "demo_friends_and_gamercard": "gamer-services",
    # Primary demonstrated API surface is Microsoft.Xna.Framework.Net
    # (NetworkSession / NetworkGamer / PacketWriter / QualityOfService):
    "demo_net_client_server_arena": "net",
    "demo_packet_roundtrip": "net",
    "demo_qos_probe": "net",
    "demo_simulated_network_conditions": "net",
    "demo_session_browser": "net",
    "demo_gamer_roster_hud": "net",
    "demo_session_lifecycle_events": "net",
    "demo_net_avatar_sync": "net",
}

# single root files with a non-default owner
SINGLE = {
    # CNAEXT extension examples (RenderPipelineSettings/PbrMaterial, DepthEffect, CRTEffect
    # all live in modules/graphics-ext)
    "cnaext_settings_example.cpp": ("EXTENSION_MODULE", "graphics-ext"),
    "crt_effect_demo_test.cpp": ("EXTENSION_MODULE", "graphics-ext"),
    "depth_effect_demo_test.cpp": ("EXTENSION_MODULE", "graphics-ext"),
    # AvatarRenderer integration fixtures (gamer-services API, instantiated per renderer)
    "avatar_attach_part_integration_test.cpp": ("SUBSYSTEM_MODULE", "gamer-services"),
    "avatar_real_render_integration_test.cpp": ("SUBSYSTEM_MODULE", "gamer-services"),
    "avatar_tint_routing_integration_test.cpp": ("SUBSYSTEM_MODULE", "gamer-services"),
}

# shared example-support headers under examples/common/ -> owning module
COMMON = {
    "common/PixelTestGame.hpp": ("SUBSYSTEM_MODULE", "graphics"),
    "common/ScreenshotEXT.hpp": ("SUBSYSTEM_MODULE", "graphics"),
    "common/SimpleFontEXT.hpp": ("SUBSYSTEM_MODULE", "graphics"),
    "common/ViewSpaceFogRef.hpp": ("SUBSYSTEM_MODULE", "graphics"),
    "common/LlglVulkanWsiSkipGuard.cpp": ("RENDERER_MODULE", "renderers/llgl"),
    "common/SkiaSuccessorOracle.hpp": ("RENDERER_MODULE", "renderers/skia"),
}


def classify(rel):
    """rel is the path below examples/. Returns (classification, owner, newpath|None)."""
    top = rel.split("/", 1)[0]
    if rel.startswith("golden/"):
        # Cross-renderer golden oracle corpus, loaded at run time by CWD-relative
        # "examples/golden/*.png" literals from easygl/opengl1/opengl2/opengl4/skia
        # suites alike: genuinely shared, stays at the documented top-level location.
        return ("SHARED_EXAMPLE_ASSET", "examples/golden", None)
    if rel in COMMON:
        cls, owner = COMMON[rel]
        base = f"modules/{owner}/examples" if owner.startswith("renderers/") \
            else f"modules/{owner}/examples"
        return (cls, owner, f"{base}/{rel}")
    if rel in SINGLE:
        cls, owner = SINGLE[rel]
        return (cls, owner, f"modules/{owner}/examples/{rel}")
    if top in DEMO_DIR:
        owner = DEMO_DIR[top]
        return ("SUBSYSTEM_MODULE", owner, f"modules/{owner}/examples/{rel}")
    if "/" not in rel:
        stem = rel.split("_", 1)[0]
        if stem in RENDERER_PREFIX and "_" in rel:
            fam = RENDERER_PREFIX[stem]
            return ("RENDERER_MODULE", f"renderers/{fam}",
                    f"modules/renderers/{fam}/examples/{rel}")
        # everything else at the examples root is a renderer-agnostic graphics
        # conformance fixture / demo (validated per renderer by the family suites)
        return ("SUBSYSTEM_MODULE", "graphics", f"modules/graphics/examples/{rel}")
    return (None, None, None)


def main():
    rows = []
    unmatched = []
    with open(BASE) as f:
        next(f)
        for line in f:
            path, _blob = line.rstrip("\n").split("\t")
            assert path.startswith("examples/")
            rel = path[len("examples/"):]
            cls, owner, new = classify(rel)
            if cls is None:
                unmatched.append(path)
                continue
            rows.append((path, cls, owner, new or "KEEP"))
    if unmatched:
        for p in unmatched:
            print("UNMATCHED:", p, file=sys.stderr)
        return 1
    with open(OUT, "w") as f:
        f.write("old_path\tclassification\towner\tnew_path\n")
        for row in sorted(rows):
            f.write("\t".join(row) + "\n")
    counts = {}
    for _p, cls, owner, new in rows:
        key = (cls, owner, "KEEP" if new == "KEEP" else "move")
        counts[key] = counts.get(key, 0) + 1
    for key in sorted(counts):
        print(f"{counts[key]:5d}  {key[0]:22s} {key[1]:22s} {key[2]}")
    print(f"total {len(rows)} -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
