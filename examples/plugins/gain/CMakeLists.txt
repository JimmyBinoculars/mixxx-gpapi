cmake_minimum_required(VERSION 3.16)
project(mixxx_example_gain C)

# Example native (C ABI) DSP plugin. Built as a MODULE so it can be loaded at
# runtime with QLibrary. See ../../../src/plugins/api/mixxx_plugin_abi.h.
add_library(gain MODULE gain_plugin.c)

target_include_directories(gain PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/plugins/api")

set_target_properties(gain PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "gain"
    # Mixxx builds with -fvisibility=hidden; make sure the entry symbol is
    # exported even for plugins that forget MIXXX_DECLARE_PLUGIN_ENTRY().
    C_VISIBILITY_PRESET default)

# When built as part of the main tree, install the library and its manifest
# alongside the bundled plugins so the host discovers it.
if(DEFINED MIXXX_INSTALL_DATADIR)
    install(TARGETS gain
        LIBRARY DESTINATION "${MIXXX_INSTALL_DATADIR}/plugins/gain")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        DESTINATION "${MIXXX_INSTALL_DATADIR}/plugins/gain")
endif()
