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
  src/fiber.c
  src/game.c
  src/input_script.c
  src/lockstep.c
  src/port/port.c
  src/port2/registry.c
  src/port3/registry.c
  src/port3/dsp_objects.c
  src/port3/engine.c
  src/port3/player_setup.c
  src/port3/track_position.c
  src/port3/road_ahead.c
  src/port3/road_edges.c
  src/port3/camera_follow.c
  src/port3/sprite_finish.c
  src/port3/tile_scroll.c
  src/port3/countdown.c
  src/port3/player_record.c
  src/port3/road_pieces.c
  src/port3/engine_sound.c
  src/port3/horizon_scroll.c
  src/port3/player_frame.c
  src/port3/car_stats.c
  src/port3/objects.c
  src/port3/road.c
  src/port3/windows.c
  src/port3/scenery.c
  src/port3/positions.c
  src/port3/bars.c
  src/port3/splits.c
  src/port3/hdma_tables.c
  src/port3/race_order.c
  src/port3/scenery_loop.c
  src/port2/video.c
  src/port2/road.c
  src/port2/objects.c
  src/port2/objpass.c
  src/port2/ghost.c
  src/port2/colours.c
  src/port2/drive.c
  src/port2/cars.c
  src/port2/stream.c
  src/port2/windows.c
  src/port2/nmi.c
  src/port2/race.c
  src/port2/setup.c
  src/port2/decomp.c
  src/port2/load.c
  src/port2/menu.c
  src/port2/menu_flow.c
  src/port2/sprites.c
  src/port/cars.c
  src/port/decompress.c
  src/port/drive.c
  src/port/render.c
  src/port/ghost.c
  src/port/setup.c
  src/port/tilecache.c
  src/port/hud.c
  src/port/menu_leaves.c
  src/port/menu_text.c
  src/port/menu_results.c
  src/port/menu_flow.c
  src/port/messages.c
  src/port/irq.c
  src/port/program.c
  src/port/race.c
  src/port/registry.c
  src/port/sprites.c
  src/port/uploads.c
  src/port/visible.c
  src/port/waits.c
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
  # The launcher: choose the ROM and the options, then start topgear.exe.
  add_executable(topgear-launcher WIN32 src/launcher.c src/rom.c src/sha256.c)
  target_include_directories(topgear-launcher PRIVATE src)
  target_link_libraries(topgear-launcher PRIVATE SDL3::SDL3)
  # The launcher's icon (assets/launcher-icon.ico, made by tools/gen_launcher_icon.py from the
  # box art) and its logo (assets/logo.bmp, built in).
  set(TG_LAUNCHER_ICON "${CMAKE_SOURCE_DIR}/assets/launcher-icon.ico" CACHE FILEPATH "Icon (.ico) for topgear-launcher.exe")
  if(WIN32 AND EXISTS "${TG_LAUNCHER_ICON}")
    file(WRITE ${CMAKE_BINARY_DIR}/launcher_icon.rc "1 ICON \"${TG_LAUNCHER_ICON}\"\n")
    target_sources(topgear-launcher PRIVATE ${CMAKE_BINARY_DIR}/launcher_icon.rc)
    set_source_files_properties(${CMAKE_BINARY_DIR}/launcher_icon.rc PROPERTIES OBJECT_DEPENDS "${TG_LAUNCHER_ICON}")
  endif()
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/gen/launcher_logo.h
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/assets/logo.bmp -DOUT=${CMAKE_BINARY_DIR}/gen/launcher_logo.h
      -DNAME=kLogoBmp -P ${CMAKE_SOURCE_DIR}/cmake/bin2h.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/logo.bmp ${CMAKE_SOURCE_DIR}/cmake/bin2h.cmake)
  target_sources(topgear-launcher PRIVATE ${CMAKE_BINARY_DIR}/gen/launcher_logo.h)
  target_include_directories(topgear-launcher PRIVATE ${CMAKE_BINARY_DIR}/gen)
  if(MINGW)
    target_link_options(topgear PRIVATE -static -static-libgcc)
    target_link_options(topgear-launcher PRIVATE -static -static-libgcc)
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
  # Chris's later sessions, recorded on Windows: a 5-minute one with a race, and an
  # 85-minute one with about twenty races.
  add_test(NAME chris_session_02 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 17047
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_02.txt
    --expect-wram a7367821082e64bf84eee43439ddcc1dc23bea23a99d868e25e1acecd714b908)
  add_test(NAME chris_session_03 COMMAND tg_headless --rom ${TG_TEST_ROM} --port verify --frames 304552
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/chris_session_03.txt
    --expect-wram bc050a7e61dd3e96502383ceb71374eba269ceea30be1c99ccbc89c726722056)
  set_tests_properties(chris_session_02 PROPERTIES LABELS long TIMEOUT 1800)
  set_tests_properties(chris_session_03 PROPERTIES LABELS long TIMEOUT 7200)
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
  # The race loop in C against the emulated one, allowing for its faster loading.
  if(Python3_FOUND)
    add_test(NAME race_loop_c COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/shift_compare.py
      --bin $<TARGET_FILE:tg_headless> --rom ${TG_TEST_ROM} --input ${CMAKE_SOURCE_DIR}/tests/inputs/race_2p.txt --frames 14000)
    set_tests_properties(race_loop_c PROPERTIES LABELS long TIMEOUT 3600)
  endif()
  if(Python3_FOUND AND CA65 AND LD65)
    add_test(NAME disasm_roundtrip COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_disasm.py
      --rom ${TG_TEST_ROM} --out ${CMAKE_BINARY_DIR}/disasm)
  endif()
endif()

# Top Gear 2 and Top Gear 3000: their ROMs (and 3000's DSP-4 firmware) turn on these tests.
set(TG2_TEST_ROM "" CACHE FILEPATH "Your Top Gear 2 (USA) ROM, for its tests")
if(TG2_TEST_ROM)
  # The first race with every translated Top Gear 2 routine checked against the original.
  add_test(NAME tg2_first_race COMMAND tg_headless --rom ${TG2_TEST_ROM} --port verify --frames 16000
    --input ${CMAKE_SOURCE_DIR}/tests/inputs/topgear2/first_race.txt
    --expect-wram ffff75b4e055dd06cdc3ad1c55c6319ab9445629a1fd4f88e0a1659f4b8ad301
    --expect-frame b40950a0da02055a8c289e3acccbf8d1ef3b32faa341e339ef8cddae6c97b011)
  # A two-player race, and races on other tracks (the recordings poke the country and race
  # in before the start), each checked the same way: name|frames|wram hash|frame hash.
  foreach(rec
      "race_2p|14000|db068cfb1cd1cf0e8d6cb108d84160bf9861f68dd9ae45b8ca4abdf5d967ab3d|489e9b90682d0e12fe5bbf5fc2b85c3430940d9415c0ad59b37a5d34088734c4"
      "country02_race01|12000|07904fe66091064ca5c91293405846581a569555ea661187c3837aac1c215d26|49d61d8fb6d5bcf879b04b80c84f2f38f410f17609776828682ab51a195e923c"
      "country08_race03|12000|583c781d5bdbc5dfde779513689bcfe3159b13f6fbad0ed0161fb7afbff051a9|7be41d011dd1e5ea755cac2c145bcdca3dd33d2b14b113952c030b675d2e7a40"
      "country0B_race00|12000|fb7690a53ed5f96bb1c8554f113f8ffa8f54126da367dadfbe7142f8fc32fdd1|ca02793076b8845454d3cc4d949bda5bc49fe16d063e310331f86318ed644ba7"
      "country0E_race02|12000|1cc9a9ed5bff9c17be46c0e39e8510da3bd832540ce097cf026dd5b3612b0cd0|0f7db8ac483df117b984a5b3cd51a1240fa6e8dc1ae2e9533a3d10c3461ced52"
      "country07_race02|12000|648326920da23c9d71662c26773ae915bfd2775fd3f78704626c56c93781f0d0|e880b9c40fdce2f5e234fbf0a3beb1e02f67834fa9a21e022fe78da1a396eba7"
      "country0D_race03|12000|c47e38bf104df9267367ac115b73454df206dd7d03dac960673a73d96df7f36c|c85ceb69456d71dbe92c70673eb279b8318c2159d81932f9fa35a95348a81732"
      "country0F_race03|12000|4294708c46a15ea848b8584bc7614dae76fb286511da12172660c5c11faa4995|a08fb728040b4f7c9e058f6299a165e4e1116f19c0c0cd7efcca764c3cee9720"
      "country0A_race01|12000|e668fc558ee9e759a7b18a0b6cf6d466c7030bc1286ea66181b35c8b76c9ec27|ca02793076b8845454d3cc4d949bda5bc49fe16d063e310331f86318ed644ba7"
      "country03_race00|12000|2f2ab5f52a112232465921fba813852869bf00c0d03e356f0100fa49f1188bba|758401c1a885b6be74a674773b8e407a7116efb69616a9d2a5309a374f8a28de"
      "country00_race01|12000|5bd9913bc3ae4478f33d01fe895f81b6dc0b516d577e46fd8868988a63eb05c3|fb8b671a51ababd41681d7db39f9b55080ac67c622bb842b4025d9ef4d716141"
      "country05_race03|12000|60d34aa589f480b35d63ac62883bcfb527d535645d6ffeae69d6a180124bcb84|401c3821f074c404178dfb5f2e3acf677696a3a7406c4af5f677c48d91e4035c"
      "pause_1p|3600|70d91ef82ae5a84b6a064b74c93633ce4004dafa7e038763a61920d293c2996c|0f4094f81284e439edafc4966921bd10d2e4d044cdb5e3496ff3dedd335bbc6d"
      "pause_2p|3600|8a2bdf73ca97cabdf464f3a91dbede4293c44d4b5941831045b9d5bb7e2101d8|a31037e88482de96fdf099d958465ad526ff3ae0975d85f0ccd3a9df006de1d3"
      "demo_grid|2600|c30f9f9148188278693f735821982a17a86dbcd5810955056c997a20e0f04a9e|114427095ffed864bda88a275e9e3fd989785352706202bfd8b4abb5ef349133"
      "menu_tour|8200|4f2d60c195bd70d6dfa9e1835b14ce381d1052171c99e9ef942a9066e74fc812|190377111149d1680faeb235d0f8f8e8cbb00de7b35fedfbb6711cd9eff2b7d2"
      "password_good|2400|664abac582137459438f037144ac75e6b67bb19fa618ca9db84c22690a4b3ddf|e734327ca6d1ce0ab0cf632659590bbe7c33a99db4364b74eb0705c82c7d5da8"
      "password_bad|2700|daa3a338ea85d3ec155ff6709306269b60efe8fe6a155e7e11bcce6e8d10ce04|7413f39537a9924b84073f5394618278096af89d112f0749c533765e2adcc49c"
      "season_standings|17000|f07a959d7f99e461f63a95dc2c8e270e7e774a558352fb1d9e713a694e3093c0|becfa631b376e1b6290fb5fc18bc660a9cf663297974046e9fdf4e114ef7dfc1"
      "paint_shop|9400|e676f18fa1343c5be15e5ce38678fd7ccd5a17f0b20f9defa3c2e2272d3689ec|6276fa34915ca051b57e369b95c58e13e4694653cadee9757ec75b1b0178fb75"
      "season_end|17500|8a0bc1b41c4c8586c2fdaac0eaa6f5dd5caff8626b3f13d8db0570c09c758009|c945c73a51b5eef2d46e60139566ba63223399659495aec885ab0ccff3d63f03"
      )
    string(REPLACE "|" ";" f "${rec}")
    list(GET f 0 name)
    list(GET f 1 frames)
    list(GET f 2 wram)
    list(GET f 3 frame)
    add_test(NAME tg2_${name} COMMAND tg_headless --rom ${TG2_TEST_ROM} --port verify --frames ${frames}
      --input ${CMAKE_SOURCE_DIR}/tests/inputs/topgear2/${name}.txt --expect-wram ${wram} --expect-frame ${frame})
  endforeach()
endif()
set(TG3_TEST_ROM "" CACHE FILEPATH "Your Top Gear 3000 (USA) ROM, for its tests")
set(TG3_TEST_DSP "" CACHE FILEPATH "The DSP-4 firmware (dsp4.bin, 8192 bytes), for Top Gear 3000's tests")
if(TG3_TEST_ROM AND TG3_TEST_DSP)
  # Top Gear 3000's first race with every translated routine checked against the original.
  add_test(NAME tg3_first_race COMMAND tg_headless --rom ${TG3_TEST_ROM} --dsp ${TG3_TEST_DSP} --port verify
    --frames 14000 --input ${CMAKE_SOURCE_DIR}/tests/inputs/topgear3000/first_race.txt
    --expect-wram 9a4783b8c28a4eeb6a711bb56fdd294dffabe4e2913666150f8c68322a75d1e2
    --expect-frame 26110be1ee260b1507e3a0105d976e578199e67f71fa5425f9a9f6495d1ce775)
endif()
