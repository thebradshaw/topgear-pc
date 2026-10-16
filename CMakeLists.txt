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
  src/lockstep.c
  src/port/port.c
  src/port/hud.c
  src/port/race.c
  src/port/registry.c
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
  # Recording tests run with --port verify: every call to a translated routine is checked
  # against the original (src/lockstep.h), and any difference fails the test.
  add_test(NAME rom_boot COMMAND test_core --rom ${TG_TEST_ROM})
  # Two minutes of the untouched attract sequence, ending in the split-screen demo race.
  # Pinned hashes catch any change in game behaviour or rendering.
  add_test(NAME attract_demo COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 7200
    --expect-wram eb074942bd3f6b5efa7402979c2d6e56e4373739112cc711cfe448f4a8291cba
    --expect-frame fc8fc8246cc971806acc2607897b4d99093d1b48884d9aa682271d1036f81eca)
  # Menus, a full scripted 1-player race and the results screen.
  add_test(NAME race_1p COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 11000
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/race_1p.txt
    --expect-wram 08f94c7b56cb0b8ac33b9c557c5c0fdd25c7ab80ee479d374a45a6253b5b2a8a
    --expect-frame 94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9)
  # Chris's 15-minute session recorded on Windows. Slow, so labelled "long":
  # ctest -L long to run it, ctest -LE long to skip it.
  add_test(NAME chris_session_01 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 54853
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_01.txt
    --expect-wram 4ff4ffc25d58655992eaa28e1c404ff7f3a73e815903af5e62361bd135f76cd8)
  set_tests_properties(chris_session_01 PROPERTIES LABELS long TIMEOUT 1800)
  # Scripted recordings that reach the other game modes: a 2-player race, and the first
  # race of each country unlocked with its password, and a race on each harder skill level. Each takes a while, so labelled "long".
  foreach(rec IN ITEMS
      "race_2p|14000|af867184fd380ccfc5327252cdb15d44ee7b6ce23c06b7aaa668bac6bf5f8592|ca02793076b8845454d3cc4d949bda5bc49fe16d063e310331f86318ed644ba7"
      "country_s_america|9762|299d275f2492573fc26ca109689ecd4422d5e7e7bdeb4997fca83cef3332213f|af587dd1ef071b5d55271b8b80aa345a5a30715d6f0bcff295af4bfa6c138d78"
      "country_japan|10242|89cf229c909c271462147467800dfa3ddebb4fa75f871746fa0c91ba6e8d8a20|c9225e4c522e860785001a9310eb0197082544ea3ccff226332aea03f721beb6"
      "country_germany|10402|85168033966361e7cb9a92e44f65e31dbaa0c9165935e93317ba93970151a4a2|b87c5f9d370169fe3f4bf8d42be01d63e7d20f9ce7727036c401ca4600f03af3"
      "country_scandinavia|10602|00697c0c070b2350a196a6a60be443d7f20743104dfc7de3265e79e9c6499754|c81af3c92b1ebaefd42d36be63e180765e2f96e4acb483ed51aa3e8e9facf4bd"
      "country_france|10562|8cd9a88843f7c213dfb411ee86b251c072163cbe24dcb3bca46c9e0ae6e085e6|2cbbce0c1b6fc7de74bf4e59a1e598ceb17949066756fc32f645a36c389eeee6"
      "country_italy|10522|4402fc56aed633d28a8d45a181ed0a634d4ee3b5680e9f7805ca377877d2e86a|0cd8c2b0ed333398a9122c100d3de1cfa42fc5fab9974732045cc66e39373d29"
      "country_uk|10242|b9dc85e182f4d1d6c0c33712e8405030e380c15526a343071e7b02214cf091d5|d783491c8f629c6fdea76648c4d59b31692a5fa7dcfe9a427c3b067e459824ba"
      "skill_professional|11432|71dd6a46a8b858aeb363354fee2df784005d192a38f58c6448af9377560897bb|94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9"
      "skill_championship|11540|f9a5a1c7b220f84e72bbf16cb828384de6c29c0fd4ffa5dcf330db7412cee631|94766e6c901d25c1fdb0f14cf24d3b8c78be1e25318146f0f3905f6d904537c9"
    )
    string(REPLACE "|" ";" rec "${rec}")
    list(GET rec 0 name)
    list(GET rec 1 frames)
    list(GET rec 2 wram)
    list(GET rec 3 frame)
    add_test(NAME ${name} COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames ${frames}
      --input ${CMAKE_SOURCE_DIR}/tests/inputs/${name}.txt --expect-wram ${wram} --expect-frame ${frame})
    set_tests_properties(${name} PROPERTIES LABELS long TIMEOUT 1800)
  endforeach()
  # Disassemble, reassemble with ca65/ld65 and require a byte-identical ROM.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  find_program(CA65 ca65)
  find_program(LD65 ld65)
  if(Python3_FOUND AND CA65 AND LD65)
    add_test(NAME disasm_roundtrip COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_disasm.py
      --rom ${TG_TEST_ROM} --out ${CMAKE_BINARY_DIR}/disasm)
  endif()
endif()
