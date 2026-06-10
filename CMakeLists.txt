cmake_minimum_required(VERSION 3.16)
project(flade C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 CONFIG REQUIRED)

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
  src/xmidi.c)

target_link_libraries(flade PRIVATE SDL3::SDL3)
if(NOT WIN32)
  target_link_libraries(flade PRIVATE m) # tsf/midi math
endif()

# Headless verification harness for the SDL-free decoder core (no SDL needed).
add_executable(fladump
  tools/fladump.c
  src/fla.c
  src/acf.c
  src/hqr.c
  src/voc.c
  src/iso9660.c)

foreach(t flade fladump)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
endforeach()
