# VLC plugin headers are not shipped in libvlc-dev on Ubuntu.
# For the frame tap plugin, point CMake at a VLC source tree (3.0.x).

if(NOT VLC_SOURCE_DIR)
    set(VLC_FOUND FALSE)
    return()
endif()

set(VLC_PLUGIN_INCLUDE_DIR "${VLC_SOURCE_DIR}/include")

if(NOT EXISTS "${VLC_PLUGIN_INCLUDE_DIR}/vlc_common.h")
    message(FATAL_ERROR "VLC_SOURCE_DIR does not contain include/vlc_common.h")
endif()

set(VLC_FOUND TRUE)

if(NOT TARGET VLC::PluginHeaders)
    add_library(VLC::PluginHeaders INTERFACE IMPORTED)
    set_target_properties(VLC::PluginHeaders PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${VLC_PLUGIN_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(VLC_PLUGIN_INCLUDE_DIR)
