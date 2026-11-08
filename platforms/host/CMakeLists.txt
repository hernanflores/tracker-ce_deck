cmake_minimum_required(VERSION 3.20)
project(tracker_ce_host LANGUAGES C CXX)

# Desktop (SDL3) build of the Tracker Mini firmware. Driven by `make host` from the repo root.

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
get_filename_component(REPO_ROOT ${REPO_ROOT} ABSOLUTE)
set(CORE_DIR ${REPO_ROOT}/core)
set(UTIL_DIR ${REPO_ROOT}/util)
set(SYNTH_DIR ${REPO_ROOT}/synth)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(FW_EDITION CE)
set(FW_MAJOR 1)
set(FW_MINOR 0)
set(FW_PATCH 0)
set(FW_BETA 255)

# ---- core library (same source dirs as core/makefile) ----
set(CORE_SRC_DIRS source libs interface effector FileManager Exporter AudioEngine synth)
set(CORE_CPP "")
foreach(d ${CORE_SRC_DIRS})
  file(GLOB_RECURSE found CONFIGURE_DEPENDS ${CORE_DIR}/${d}/*.cpp)
  list(APPEND CORE_CPP ${found})
endforeach()
file(GLOB_RECURSE CORE_C CONFIGURE_DEPENDS ${CORE_DIR}/libs/*.c)

# Tracker.cpp etc. use hardware-only code excluded on host; list exclusions here.
set(CORE_EXCLUDE "")
foreach(x ${CORE_EXCLUDE})
  list(FILTER CORE_CPP EXCLUDE REGEX "${x}")
endforeach()

set(CORE_INCLUDES
  include config source synth libs libs/Audio libs/ElapsedMillis libs/SerialFlash libs/SPI
  libs/Audio/utility libs/FT812 libs/fonts libs/NewDisplay interface effector FileManager
  Exporter AudioEngine libs/Audio/Polyverb AudioEngine/ConstantValues AudioEngine/MasterEnhancer)
list(TRANSFORM CORE_INCLUDES PREPEND ${CORE_DIR}/)

set(HOST_FLAGS -funsigned-char -ffunction-sections -fdata-sections -Wno-deprecated-declarations
  -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers)
set(HOST_DEFS
  PLATFORM_IMX PLATFORM_HOST TRACKER_MINI
  AUDIO_PERFORMANCE_INFO_ENABLED MEMORY_USAGE_INFO_ENABLED
  FW_EDITION_NAME="${FW_EDITION}" FW_VER_MAJOR=${FW_MAJOR} FW_VER_MINOR=${FW_MINOR}
  FW_VER_PATCH=${FW_PATCH} FW_VER_BETA=${FW_BETA} BUILD_ID="host")

add_library(tracker_core STATIC ${CORE_CPP} ${CORE_C})
target_include_directories(tracker_core PUBLIC ${CORE_INCLUDES}
  ${UTIL_DIR}/include ${UTIL_DIR}/3rdparty ${SYNTH_DIR}/include)
target_compile_definitions(tracker_core PUBLIC ${HOST_DEFS})
target_compile_options(tracker_core PUBLIC ${HOST_FLAGS})
target_compile_options(tracker_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions>)

# ---- host platform + executable ----
set(IMX_FT812_DIR ${REPO_ROOT}/platforms/imx/3rdparty/FT812)
file(GLOB HOST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/eve/*.cpp)
list(FILTER HOST_SRC EXCLUDE REGEX "BtEmuDevice")
# Bridgetek's emulator is optional and proprietary: only built against a user-provided header dir.
set(BT8XXEMU_INCLUDE_DIR "" CACHE PATH "Directory containing Bridgetek's bt8xxemu.h (enables --display=bteve)")
if(BT8XXEMU_INCLUDE_DIR)
  list(APPEND HOST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/eve/BtEmuDevice.cpp)
endif()
# the IMX FT812 command/API layers are compiled unchanged on top of the host MCU layer
list(APPEND HOST_SRC ${IMX_FT812_DIR}/API_Layer.cpp ${IMX_FT812_DIR}/EVE_Layer.cpp)
find_package(SDL3 REQUIRED CONFIG)
add_executable(tracker-host ${HOST_SRC})
target_link_libraries(tracker-host PRIVATE SDL3::SDL3)
if(BT8XXEMU_INCLUDE_DIR)
  target_include_directories(tracker-host PRIVATE ${BT8XXEMU_INCLUDE_DIR})
  target_compile_definitions(tracker-host PRIVATE HOST_HAS_BTEMU)
endif()
# stubs/ must come first: it provides the SpiBus.h that FT812.h includes
target_include_directories(tracker-host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR} ${IMX_FT812_DIR})
target_compile_options(tracker-host PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions>)
option(HOST_WHOLE_ARCHIVE "Link every core object (find unresolved symbols)" OFF)
if(HOST_WHOLE_ARCHIVE)
  if(APPLE)
    target_link_libraries(tracker-host PRIVATE -Wl,-force_load,$<TARGET_FILE:tracker_core>)
  else()
    target_link_libraries(tracker-host PRIVATE -Wl,--whole-archive tracker_core -Wl,--no-whole-archive)
  endif()
else()
  target_link_libraries(tracker-host PRIVATE tracker_core)
endif()
if(APPLE)
  target_link_options(tracker-host PRIVATE -Wl,-dead_strip)
else()
  target_link_options(tracker-host PRIVATE -Wl,--gc-sections)
  target_link_libraries(tracker-host PRIVATE pthread dl)
endif()

# ---- unit tests (ctest) ----
enable_testing()
add_executable(irqgate_test tests/irqgate_test.cpp source/IrqGate.cpp)
target_include_directories(irqgate_test PRIVATE include)
target_link_libraries(irqgate_test PRIVATE tracker_core)
add_test(NAME irqgate COMMAND irqgate_test)

add_executable(dspinst_test tests/dspinst_test.cpp)
target_include_directories(dspinst_test PRIVATE ${CORE_DIR}/libs/Audio/utility)
target_compile_definitions(dspinst_test PRIVATE PLATFORM_HOST)
add_test(NAME dspinst COMMAND dspinst_test)

add_test(NAME export_smoke COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/export_smoke.sh $<TARGET_FILE:tracker-host>)
set_tests_properties(export_smoke PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")

# ---- eve-replay: replays recorded display traces into a backend ----
set(EVE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/eve/EveSpiDecoder.cpp ${CMAKE_CURRENT_SOURCE_DIR}/eve/PassiveEveDevice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/eve/TraceTee.cpp ${CMAKE_CURRENT_SOURCE_DIR}/eve/BtEmuStub.cpp)
if(BT8XXEMU_INCLUDE_DIR)
  list(APPEND EVE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/eve/BtEmuDevice.cpp)
endif()
add_executable(eve-replay tools/eve_replay.cpp ${EVE_SRC})
target_include_directories(eve-replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE_DIR}/libs/FT812)
if(BT8XXEMU_INCLUDE_DIR)
  target_include_directories(eve-replay PRIVATE ${BT8XXEMU_INCLUDE_DIR})
  target_compile_definitions(eve-replay PRIVATE HOST_HAS_BTEMU)
  if(NOT APPLE)
    target_link_libraries(eve-replay PRIVATE dl)
  endif()
endif()

add_executable(eve_test tests/eve_test.cpp ${EVE_SRC})
target_include_directories(eve_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(BT8XXEMU_INCLUDE_DIR)
  target_include_directories(eve_test PRIVATE ${BT8XXEMU_INCLUDE_DIR})
  target_compile_definitions(eve_test PRIVATE HOST_HAS_BTEMU)
  if(NOT APPLE)
    target_link_libraries(eve_test PRIVATE dl)
  endif()
endif()
add_test(NAME eve_decoder COMMAND eve_test)
add_test(NAME eve_roundtrip COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/eve_roundtrip.sh $<TARGET_FILE:tracker-host> $<TARGET_FILE:eve-replay>)
