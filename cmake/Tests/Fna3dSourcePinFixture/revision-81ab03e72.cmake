set(_cna_fna3d_default_repository "https://github.com/libcna/FNA3D.git")
set(_cna_fna3d_default_tag "f4bef618d2c2e59866b02896b52efa25815c829b")
# A build tree keeps the defaults of the CNA revision that first configured it. A cached value that
# is one of CNA's own former defaults follows the current one; any other value is a developer's
# override and is kept. Until the forks the defaults were upstream FNA3D at 3240147, which this file
# then patched -- fetched as it is now, that revision would build without CNA's fixes.
foreach(_cna_fna3d_setting IN ITEMS REPOSITORY TAG)
    string(TOLOWER "${_cna_fna3d_setting}" _cna_fna3d_setting_lower)
    set(_cna_fna3d_former_defaults "${_CNA_FNA3D_DEFAULT_GIT_${_cna_fna3d_setting}}")
    if(_cna_fna3d_setting STREQUAL "REPOSITORY")
        list(APPEND _cna_fna3d_former_defaults "https://github.com/FNA-XNA/FNA3D.git")
    else()
        list(APPEND _cna_fna3d_former_defaults "3240147")
    endif()
    get_property(_cna_fna3d_setting_type CACHE CNA_FNA3D_GIT_${_cna_fna3d_setting} PROPERTY TYPE)
    if(_cna_fna3d_setting_type STREQUAL "STRING"
       AND CNA_FNA3D_GIT_${_cna_fna3d_setting} IN_LIST _cna_fna3d_former_defaults
       AND NOT CNA_FNA3D_GIT_${_cna_fna3d_setting} STREQUAL _cna_fna3d_default_${_cna_fna3d_setting_lower})
        message(STATUS "CNA: CNA_FNA3D_GIT_${_cna_fna3d_setting} follows the new default "
            "(${CNA_FNA3D_GIT_${_cna_fna3d_setting}} -> ${_cna_fna3d_default_${_cna_fna3d_setting_lower}})")
        set_property(CACHE CNA_FNA3D_GIT_${_cna_fna3d_setting} PROPERTY VALUE
            "${_cna_fna3d_default_${_cna_fna3d_setting_lower}}")
    endif()
    set(_CNA_FNA3D_DEFAULT_GIT_${_cna_fna3d_setting} "${_cna_fna3d_default_${_cna_fna3d_setting_lower}}"
        CACHE INTERNAL "CNA's own default for CNA_FNA3D_GIT_${_cna_fna3d_setting}")
endforeach()
set(CNA_FNA3D_GIT_REPOSITORY "${_cna_fna3d_default_repository}"
    CACHE STRING "FNA3D repository fetched for the FNA3D graphics renderer (CNA's fork)")
set(CNA_FNA3D_GIT_TAG "${_cna_fna3d_default_tag}"
    CACHE STRING "Pinned FNA3D commit used by the FNA3D graphics renderer")
