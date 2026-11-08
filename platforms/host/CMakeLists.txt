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
file(GLOB HOST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
add_executable(tracker-host ${HOST_SRC})
target_include_directories(tracker-host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
