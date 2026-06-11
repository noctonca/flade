cmake_minimum_required(VERSION 3.16)
project(flade VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FLADE_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(FLADE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# SDL3 powers the player itself; the headless decoder tools need none, so it is
# optional - without it CI can still build and harden the decoders.
find_package(SDL3 CONFIG QUIET)

# The SDL-free decoder core (parsers + decoders): reused by the headless tools.
set(DECODER_SRC
  src/movie.c
  src/fla.c
  src/acf.c
  src/smk.c
  src/hqr.c
  src/iso9660.c
  src/voc.c
  src/libsmacker/smacker.c
  src/libsmacker/smacker_compat.c
  src/libsmacker/smk_bitstream.c
  src/libsmacker/smk_hufftree.c)

# Vendored libsmacker (LGPL) - build it without our strict warnings.
set_source_files_properties(
  src/libsmacker/smacker.c
  src/libsmacker/smacker_compat.c
  src/libsmacker/smk_bitstream.c
  src/libsmacker/smk_hufftree.c
  PROPERTIES COMPILE_OPTIONS "-w")

set(FLADE_TARGETS fladump movscan)

if(SDL3_FOUND)
  add_executable(flade
    src/main.c
    src/source.c
    src/gui.c
    src/player.c
    src/audio.c
    src/midi.c
    src/xmidi.c
    ${DECODER_SRC})
  # gui.c compiles the vendored Nuklear single-header toolkit.
  set_source_files_properties(src/gui.c PROPERTIES COMPILE_OPTIONS
    $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
  # Link the static SDL3 when it's the one that was built (release binaries),
  # otherwise the shared one (a normal dev build against system SDL3).
  if(TARGET SDL3::SDL3-static)
    set(SDL3_LIB SDL3::SDL3-static)
  else()
    set(SDL3_LIB SDL3::SDL3)
  endif()
  target_link_libraries(flade PRIVATE ${SDL3_LIB})
  target_compile_definitions(flade PRIVATE FLADE_VERSION="${PROJECT_VERSION}")
  list(APPEND FLADE_TARGETS flade)

  # GUI spike: proves the vendored Nuklear core + the official SDL3 renderer
  # backend (the groundwork for a future --gui). Built with -w since it compiles
  # the vendored single-header GUI library.
  add_executable(gui_spike tools/gui_spike.c)
  target_include_directories(gui_spike PRIVATE src)
  target_link_libraries(gui_spike PRIVATE ${SDL3_LIB})
  if(NOT WIN32)
    target_link_libraries(gui_spike PRIVATE m)
  endif()
  target_compile_options(gui_spike PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
else()
  message(STATUS "SDL3 not found - building headless tools only (movscan, fladump), not flade")
endif()

# Headless tools over the SDL-free decoder core (no SDL needed).
add_executable(fladump tools/fladump.c
  src/fla.c src/acf.c src/hqr.c src/voc.c src/iso9660.c)

# movscan: open any movie, decode every frame, check audio, digest, exit codes.
add_executable(movscan tools/movscan.c ${DECODER_SRC})
target_include_directories(movscan PRIVATE src)

foreach(t ${FLADE_TARGETS})
  if(NOT WIN32)
    target_link_libraries(${t} PRIVATE m) # tsf/midi math
  endif()
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endforeach()
