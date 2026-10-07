# Included by the generated project (see GBRECOMP_GAME_TARGET).
# Explicit regression target; excluded from normal game builds.
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ram_native.c")
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/expanded_view.c")
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/object_slots.c")
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/moveset.c")
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/dance.c")
target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/forms.c")

# The generated project builds for size (-Os, the ROM code at -O1). The
# runtime and this game's modules run every frame: the PPU, APU and timers,
# and with the expanded view a compositor over up to millions of pixels, so
# they are built for speed. The generated sources keep their own level (a
# source's options come after these).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(gbrt PRIVATE -O2)
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/ram_native.c"
        "${CMAKE_CURRENT_LIST_DIR}/expanded_view.c"
        "${CMAKE_CURRENT_LIST_DIR}/object_slots.c"
        "${CMAKE_CURRENT_LIST_DIR}/moveset.c"
        "${CMAKE_CURRENT_LIST_DIR}/dance.c"
        "${CMAKE_CURRENT_LIST_DIR}/forms.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../extras.c"
        PROPERTIES COMPILE_OPTIONS -O2)
endif()
get_target_property(_shantae_check_sources ${GBRECOMP_GAME_TARGET} SOURCES)
list(FILTER _shantae_check_sources EXCLUDE REGEX "_main\\.c$")
add_library(shantae_check_code OBJECT EXCLUDE_FROM_ALL ${_shantae_check_sources})
target_include_directories(shantae_check_code PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shantae_check_code PRIVATE gbrt)
add_executable(native_dispatch_check EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/tools/native_dispatch_check.c" $<TARGET_OBJECTS:shantae_check_code>)
target_include_directories(native_dispatch_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(native_dispatch_check PRIVATE gbrt)

add_executable(whole_rom_check EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/tools/whole_rom_check.c" $<TARGET_OBJECTS:shantae_check_code>)
target_include_directories(whole_rom_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(whole_rom_check PRIVATE gbrt)

add_executable(ram_native_check EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/tools/ram_native_check.c" $<TARGET_OBJECTS:shantae_check_code>)
target_include_directories(ram_native_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ram_native_check PRIVATE gbrt)

add_executable(object_slots_check EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/tools/object_slots_check.c" $<TARGET_OBJECTS:shantae_check_code>)
target_include_directories(object_slots_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(object_slots_check PRIVATE gbrt)

add_executable(expanded_view_check EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/tools/expanded_view_check.c"
    "${CMAKE_CURRENT_LIST_DIR}/gbrecompiled/runtime/src/gb_custom_view.c")
target_include_directories(expanded_view_check PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gbrecompiled/runtime/include")

target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/extras_ui.cpp")

# librashader and the slang-shaders presets from tools/build_librashader.sh
# (x86 and arm64 builds into windows-<arch>/; tools/build_linux.sh builds the
# Linux libraries into linux-<arch>/), staged beside the exe. The runtime opens
# the library at run time, so without them the game still builds and the
# Shader Presets menu says why.
set(_shantae_lrs "${CMAKE_CURRENT_LIST_DIR}/third_party/librashader")
if(WIN32 AND SHANTAE_WINDOWS_ARCH)
    # tools/windows/toolchain.cmake (x86, arm64)
    set(_shantae_lrs_lib "${_shantae_lrs}/windows-${SHANTAE_WINDOWS_ARCH}/librashader.dll")
elseif(WIN32)
    set(_shantae_lrs_lib "${_shantae_lrs}/librashader.dll")
else()
    set(_shantae_lrs_lib "${_shantae_lrs}/linux-${CMAKE_SYSTEM_PROCESSOR}/librashader.so")
endif()
set(_shantae_lrs_outputs)
if(EXISTS "${_shantae_lrs_lib}")
    set(_shantae_dll_stamp "${CMAKE_CURRENT_BINARY_DIR}/librashader_dll.stamp")
    add_custom_command(OUTPUT "${_shantae_dll_stamp}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_shantae_lrs_lib}"
                "$<TARGET_FILE_DIR:${GBRECOMP_GAME_TARGET}>"
        COMMAND ${CMAKE_COMMAND} -E touch "${_shantae_dll_stamp}"
        DEPENDS "${_shantae_lrs_lib}"
        VERBATIM)
    list(APPEND _shantae_lrs_outputs "${_shantae_dll_stamp}")
endif()
if(UNIX AND NOT APPLE)
    # dlopen("librashader.so") then looks beside the executable, and only
    # there: not in the build machine's library folders, which CMake would add
    # to a build RPATH.
    set_target_properties(${GBRECOMP_GAME_TARGET} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
endif()
if(EXISTS "${_shantae_lrs}/shaders.stamp")
    # Copied once per build_librashader.sh run, not on every build (2500+ files).
    set(_shantae_shaders_stamp "${CMAKE_CURRENT_BINARY_DIR}/librashader_shaders.stamp")
    add_custom_command(OUTPUT "${_shantae_shaders_stamp}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shantae_lrs}/shaders"
                "$<TARGET_FILE_DIR:${GBRECOMP_GAME_TARGET}>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy "${_shantae_lrs}/shaders.stamp" "${_shantae_shaders_stamp}"
        DEPENDS "${_shantae_lrs}/shaders.stamp"
        VERBATIM)
    list(APPEND _shantae_lrs_outputs "${_shantae_shaders_stamp}")
endif()
if(_shantae_lrs_outputs)
    add_custom_target(shantae_librashader ALL DEPENDS ${_shantae_lrs_outputs})
endif()

# Shantae's options on the recomp-ui launcher's Mods page (needs
# -DRECOMP_UI_ENABLE_MODS=ON, which tools/build.sh passes).
if(RECOMP_UI_ROOT AND EXISTS "${RECOMP_UI_ROOT}/src/recomp_launcher.h")
    target_sources(${GBRECOMP_GAME_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/launcher_options.c")
    target_include_directories(${GBRECOMP_GAME_TARGET} PRIVATE "${RECOMP_UI_ROOT}/src")
endif()
