set(CNA_FNA3D_GIT_REPOSITORY "https://github.com/FNA-XNA/FNA3D.git"
    CACHE STRING "Upstream FNA3D repository fetched for the FNA3D graphics renderer")
# Release tag 26.08 (FNA3D's own calendar versioning), commit 3240147. Bump deliberately: FNA3D's
# driver list and FNA3D_* ABI move between releases, and modules/renderers/fna3d/effects/ holds
# stock-effect binaries whose MojoShader parse path must keep matching the pinned library.
set(CNA_FNA3D_GIT_TAG "3240147"
    CACHE STRING "Pinned FNA3D revision used by the FNA3D graphics renderer")
