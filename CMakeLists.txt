cmake_minimum_required(VERSION 3.20)
project(topgear_pc C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(TG_BUILD_FRONTEND "Build the SDL3 game executable" ON)
option(TG_FETCH_SDL "Download and statically link SDL3 if it is not installed" ON)
set(TG_TEST_ROM "" CACHE FILEPATH "Your Top Gear (USA) ROM, for ROM-dependent tests")

if(MSVC)
  add_compile_options(/W3 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall)
endif()

# Reference emulator (LakeSnes, MIT). Runs the original ROM: today as the whole game,
# later as the fallback for untranslated routines and as the lockstep referee.
file(GLOB LAKESNES_SOURCES third_party/lakesnes/*.c)
add_library(lakesnes STATIC ${LAKESNES_SOURCES})
target_include_directories(lakesnes PUBLIC third_party/lakesnes)
if(NOT MSVC)
  target_compile_options(lakesnes PRIVATE -w)
endif()

add_library(tgcore STATIC
  src/coverage.c
  src/game.c
  src/input_script.c
  src/rom.c
  src/sha256.c
)
target_include_directories(tgcore PUBLIC src)
target_link_libraries(tgcore PUBLIC lakesnes)
if(NOT WIN32)
  target_link_libraries(tgcore PUBLIC m)
endif()

add_executable(tg_headless tools/tg_headless.c)
target_link_libraries(tg_headless PRIVATE tgcore)

if(TG_BUILD_FRONTEND)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    if(NOT TG_FETCH_SDL)
      message(FATAL_ERROR "SDL3 not found; install it or enable TG_FETCH_SDL")
    endif()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-3.2.24.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)
  endif()
  add_executable(topgear WIN32 src/main.c)
  target_link_libraries(topgear PRIVATE tgcore SDL3::SDL3)
  if(MINGW)
    target_link_options(topgear PRIVATE -static -static-libgcc)
  endif()
endif()

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE tgcore)
add_test(NAME core COMMAND test_core)
if(TG_TEST_ROM)
  add_test(NAME rom_boot COMMAND test_core --rom ${TG_TEST_ROM})
  # Two minutes of the untouched attract sequence, ending in the split-screen demo race.
  # Pinned hashes catch any change in game behaviour or rendering.
  add_test(NAME attract_demo COMMAND tg_headless --rom ${TG_TEST_ROM} --frames 7200
    --expect-wram eb074942bd3f6b5efa7402979c2d6e56e4373739112cc711cfe448f4a8291cba
    --expect-frame fc8fc8246cc971806acc2607897b4d99093d1b48884d9aa682271d1036f81eca)
  # Menus, a full scripted 1-player race and the results screen.
  add_test(NAME race_1p COMMAND tg_headless --rom ${TG_TEST_ROM} --frames 11000
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/race_1p.txt
    --expect-wram 08f94c7b56cb0b8ac33b9c557c5c0fdd25c7ab80ee479d374a45a6253b5b2a8a
    --expect-frame 94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9)
  # Chris's 15-minute session recorded on Windows. Slow, so labelled "long":
  # ctest -L long to run it, ctest -LE long to skip it.
  add_test(NAME chris_session_01 COMMAND tg_headless --rom ${TG_TEST_ROM} --frames 54853
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_01.txt
    --expect-wram 4ff4ffc25d58655992eaa28e1c404ff7f3a73e815903af5e62361bd135f76cd8)
  set_tests_properties(chris_session_01 PROPERTIES LABELS long TIMEOUT 1800)
  # Disassemble, reassemble with ca65/ld65 and require a byte-identical ROM.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  find_program(CA65 ca65)
  find_program(LD65 ld65)
  if(Python3_FOUND AND CA65 AND LD65)
    add_test(NAME disasm_roundtrip COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_disasm.py
      --rom ${TG_TEST_ROM} --out ${CMAKE_BINARY_DIR}/disasm)
  endif()
endif()
