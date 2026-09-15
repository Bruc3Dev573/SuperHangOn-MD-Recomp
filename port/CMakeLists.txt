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
# the block structure in port/data/SET/code_map.c, and applies the overlays'
# bytes of port/data/SET/rom_patch.c. These files are made from the ROM by
# tools/update_code_map.sh; the build needs no ROM. With the default overlays
# both the 60 and the 120 ticks per second sets are built in (the settings
# choose); without overlays the original game.
# Otherwise the game code is recompiled to C at build time (needs the ROM),
# at SHANGON_TICK_RATE logic ticks per second.
option(SHANGON_RUNTIME_TRANSLATION "Translate the game code from the ROM at start-up" ON)
set(SHANGON_TICK_RATE 60 CACHE STRING "Recompiled build: logic ticks per second with the overlays (60 or 120)")
set(DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)

if(SHANGON_RUNTIME_TRANSLATION)
  if(SHANGON_PATCHES STREQUAL "patches/pc60;patches/relax")
    set(GAME_SOURCES src/rt_translate.c src/m68k_decode.c ${DATA}/60hz/code_map.c ${DATA}/60hz/rom_patch.c
        ${DATA}/120hz/code_map.c ${DATA}/120hz/rom_patch.c)
    set(GAME_DEFINES RT_TRANSLATE RT_CODE_SETS_RATES)
  elseif(SHANGON_PATCHES STREQUAL "")
    set(GAME_SOURCES src/rt_translate.c src/m68k_decode.c ${DATA}/original/code_map.c ${DATA}/original/rom_patch.c)
    set(GAME_DEFINES RT_TRANSLATE RT_CODE_SET_ORIGINAL)
  else()
    message(FATAL_ERROR "the runtime translation build has code sets for the default overlays or none")
  endif()
else()
if(SHANGON_TICK_RATE EQUAL 120)
  set(TICK_SHIFT 2)
else()
  set(TICK_SHIFT 1)
endif()
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
  COMMAND ${MAKE_PROGRAM} -C ${ROOT} patched "PATCHES=${PATCHES_ARG}" NAME=port_game "DEFINES=TickShift=${TICK_SHIFT}"
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/recomp/recomp.py
          ${ROOT}/build/port_game.md ${ROOT}/build/port_game.lst ${GEN_CODE}
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/recomp/romdiff.py
          ${ROOT}/rom/baserom.md ${ROOT}/build/port_game.md ${GEN_PATCH}
  DEPENDS ${ROOT}/rom/baserom.md ${ROOT}/symbols.txt ${ROOT}/analysis/trace_entries.txt ${RECOMP_PY} ${PATCH_FILES}
  COMMENT "Recompiling 68000 game code to C (overlays: ${SHANGON_PATCHES})"
  VERBATIM)
set(GAME_SOURCES ${GEN_CODE} ${GEN_PATCH})
if(TICK_SHIFT EQUAL 2 AND NOT SHANGON_PATCHES STREQUAL "")
  set(GAME_DEFINES SHANGON_FIXED_RATE=2)
else()
  set(GAME_DEFINES SHANGON_FIXED_RATE=1)
endif()
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

find_package(SDL2 REQUIRED)
if(WIN32)
  # static libgcc so the executable needs no MinGW runtime DLLs
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

add_executable(shangon src/main.c src/input.c src/persist.c src/sha1.c src/overlay.c src/png.c src/video_gl.c src/ui.c src/menu.c src/settings.c ${RUNTIME_SOURCES} ${GAME_SOURCES})
target_compile_definitions(shangon PRIVATE ${GAME_DEFINES})
target_include_directories(shangon PRIVATE src third_party/chips)
set_target_properties(shangon PROPERTIES WIN32_EXECUTABLE TRUE)   # no console window on Windows
target_compile_options(shangon PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall>)
target_link_libraries(shangon PRIVATE nuked_opn2 m)
if(TARGET SDL2::SDL2)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(shangon PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(shangon PRIVATE SDL2::SDL2)
else()
  target_include_directories(shangon PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(shangon PRIVATE ${SDL2_LIBRARIES})
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
