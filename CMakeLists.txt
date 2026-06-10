cmake_minimum_required(VERSION 3.16)
project(flade C)

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

find_package(SDL3 CONFIG REQUIRED)

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

add_executable(flade
  src/main.c
  src/movie.c
  src/player.c
  src/iso9660.c
  src/hqr.c
  src/voc.c
  src/fla.c
  src/acf.c
  src/audio.c
  src/midi.c
  src/xmidi.c
  src/smk.c
  src/libsmacker/smacker.c
  src/libsmacker/smacker_compat.c
  src/libsmacker/smk_bitstream.c
  src/libsmacker/smk_hufftree.c)

target_link_libraries(flade PRIVATE SDL3::SDL3)
if(NOT WIN32)
  target_link_libraries(flade PRIVATE m) # tsf/midi math
endif()

# Vendored libsmacker (LGPL) - build it without our strict warnings.
set_source_files_properties(
  src/libsmacker/smacker.c
  src/libsmacker/smacker_compat.c
  src/libsmacker/smk_bitstream.c
  src/libsmacker/smk_hufftree.c
  PROPERTIES COMPILE_OPTIONS "-w")

# Headless tools over the SDL-free decoder core (no SDL needed).
add_executable(fladump tools/fladump.c
  src/fla.c src/acf.c src/hqr.c src/voc.c src/iso9660.c)

# movscan: open any movie, decode every frame, check audio, digest, exit codes.
add_executable(movscan tools/movscan.c ${DECODER_SRC})
target_include_directories(movscan PRIVATE src)

foreach(t flade fladump movscan)
  if(NOT WIN32)
    target_link_libraries(${t} PRIVATE m)
  endif()
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endforeach()
