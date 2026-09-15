# Super Hang-On PC port
#
#   cmake -S port -B build/port && cmake --build build/port
#
# The game code is generated at build time from the disassembly: `make patched`
# in the repository root assembles src/shangon.asm with the overlays of
# SHANGON_PATCHES (build/port_game.md + listing), tools/recomp/recomp.py
# translates it to C and tools/recomp/romdiff.py extracts the bytes the
# overlays changed (applied to the ROM at load). Nothing derived from the ROM
# is committed; the ROM itself is read at run time.
cmake_minimum_required(VERSION 3.16)
project(shangon C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(EMSCRIPTEN)
  if(NOT SHANGON_ROM OR NOT EXISTS "${SHANGON_ROM}")
    message(FATAL_ERROR "SHANGON_ROM must point to the ROM to preload (it is never copied into the repository)")
  endif()
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gen)
set(GEN_CODE ${GEN_DIR}/shangon_code.c)
set(GEN_PATCH ${GEN_DIR}/rom_patch.c)

# source overlays applied to the game (patches/pc60: 60 Hz game logic,
# patches/relax: RELAX MODE on the title menu);
# an empty value builds the original 30 Hz game
set(SHANGON_PATCHES "patches/pc60;patches/relax" CACHE STRING "Overlay directories for the game code")

# SHANGON_RUNTIME_TRANSLATION: the executable holds no game code; it decodes
# the instructions from the ROM at start-up (src/rt_translate.c) following
# the block structure in port/data/<overlays>/code_map.c, and applies the
# overlays' bytes of port/data/<overlays>/rom_patch.c. These two files are
# made from the ROM by tools/update_code_map.sh; the build needs no ROM.
# Otherwise the game code is recompiled to C at build time (needs the ROM).
option(SHANGON_RUNTIME_TRANSLATION "Translate the game code from the ROM at start-up" ON)
string(REPLACE "patches/" "" DATA_TAG "${SHANGON_PATCHES}")
string(REPLACE ";" "+" DATA_TAG "${DATA_TAG}")
if(DATA_TAG STREQUAL "")
  set(DATA_TAG original)
endif()
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/${DATA_TAG})

if(SHANGON_RUNTIME_TRANSLATION)
  if(NOT EXISTS ${DATA_DIR}/code_map.c)
    message(FATAL_ERROR "no code map for the overlays ${SHANGON_PATCHES} (${DATA_DIR}); run tools/update_code_map.sh")
  endif()
  set(GAME_SOURCES src/rt_translate.c src/m68k_decode.c ${DATA_DIR}/code_map.c ${DATA_DIR}/rom_patch.c)
else()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MAKE_PROGRAM make REQUIRED)
file(GLOB RECOMP_PY CONFIGURE_DEPENDS ${ROOT}/tools/recomp/*.py ${ROOT}/tools/disasm/*.py)
set(PATCH_FILES)
foreach(dir ${SHANGON_PATCHES})
  file(GLOB files CONFIGURE_DEPENDS ${ROOT}/${dir}/*.asm)
  list(APPEND PATCH_FILES ${files})
endforeach()
string(REPLACE ";" " " PATCHES_ARG "${SHANGON_PATCHES}")

add_custom_command(
  OUTPUT ${GEN_CODE} ${GEN_PATCH}
  COMMAND ${MAKE_PROGRAM} -C ${ROOT} patched "PATCHES=${PATCHES_ARG}" NAME=port_game
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/recomp/recomp.py
          ${ROOT}/build/port_game.md ${ROOT}/build/port_game.lst ${GEN_CODE}
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/recomp/romdiff.py
          ${ROOT}/rom/baserom.md ${ROOT}/build/port_game.md ${GEN_PATCH}
  DEPENDS ${ROOT}/rom/baserom.md ${ROOT}/symbols.txt ${ROOT}/analysis/trace_entries.txt ${RECOMP_PY} ${PATCH_FILES}
  COMMENT "Recompiling 68000 game code to C (overlays: ${SHANGON_PATCHES})"
  VERBATIM)
set(GAME_SOURCES ${GEN_CODE} ${GEN_PATCH})
# generated code: huge, warnings off, no per-function optimisation blow-up
set_source_files_properties(${GEN_CODE} PROPERTIES COMPILE_OPTIONS "-w")
endif()

set(RUNTIME_SOURCES
  src/rt.c
  src/bus.c
  src/vdp.c
  src/render.c
  src/scene.c
  src/audio.c
  src/resample.c
  src/state.c)

# Nuked-OPN2 (LGPL-2.1) stays a separate library so it can be replaced;
# SHANGON_OPN2_SHARED builds it as a shared library for distribution
option(SHANGON_OPN2_SHARED "Build Nuked-OPN2 as a shared library" OFF)
if(SHANGON_OPN2_SHARED)
  add_library(nuked_opn2 SHARED third_party/nuked-opn2/ym3438.c)
else()
  add_library(nuked_opn2 STATIC third_party/nuked-opn2/ym3438.c)
endif()
target_include_directories(nuked_opn2 PUBLIC third_party/nuked-opn2)

if(NOT EMSCRIPTEN)
  find_package(SDL2 REQUIRED)
endif()
if(WIN32)
  # static libgcc so the executable needs no MinGW runtime DLLs
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

add_executable(shangon src/main.c src/input.c src/persist.c src/sha1.c src/overlay.c src/png.c src/video_gl.c src/ui.c src/menu.c src/settings.c ${RUNTIME_SOURCES} ${GAME_SOURCES})
if(SHANGON_RUNTIME_TRANSLATION)
  target_compile_definitions(shangon PRIVATE RT_TRANSLATE)
endif()
target_include_directories(shangon PRIVATE src third_party/chips)
set_target_properties(shangon PROPERTIES WIN32_EXECUTABLE TRUE)   # no console window on Windows
target_compile_options(shangon PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall>)
target_link_libraries(shangon PRIVATE nuked_opn2 m)
if(EMSCRIPTEN)
  set_target_properties(shangon PROPERTIES SUFFIX ".html")
  target_compile_options(shangon PRIVATE "-sUSE_SDL=2")
  target_link_options(shangon PRIVATE
    "-sUSE_SDL=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sNO_EXIT_RUNTIME=1"
    "--preload-file=${SHANGON_ROM}@/baserom.md")
else()
  if(TARGET SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(shangon PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(shangon PRIVATE SDL2::SDL2)
  else()
    target_include_directories(shangon PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(shangon PRIVATE ${SDL2_LIBRARIES})
  endif()
endif()

# ---- installation (tools/build_port.sh --install, tools/package_linux.sh) ----
include(GNUInstallDirs)
option(SHANGON_PORTABLE "Linux: install the executable at the top of the prefix (packages)" OFF)
if(WIN32)
  # everything next to shangon.exe
  install(TARGETS shangon RUNTIME DESTINATION . COMPONENT game)
  if(SHANGON_OPN2_SHARED)
    install(TARGETS nuked_opn2 RUNTIME DESTINATION . COMPONENT game)
  endif()
elseif(SHANGON_PORTABLE)
  # Linux package: the executable at the top, the ROM goes next to it
  install(TARGETS shangon RUNTIME DESTINATION . COMPONENT game)
  if(SHANGON_OPN2_SHARED)
    install(TARGETS nuked_opn2 LIBRARY DESTINATION lib COMPONENT game)
    set_target_properties(shangon PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
  endif()
else()
  install(TARGETS shangon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT game)
  if(SHANGON_OPN2_SHARED)
    install(TARGETS nuked_opn2 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT game)
    set_target_properties(shangon PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
  install(FILES packaging/shangon.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT game)
endif()
