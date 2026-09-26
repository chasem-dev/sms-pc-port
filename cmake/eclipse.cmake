# -DSMS_ECLIPSE=ON builds Super Mario Eclipse into the port (docs/ECLIPSE.md).
#
# Eclipse and BetterSunshineEngine (and the SunshineHeaderInterface headers
# they are written against) are fetched at configure time at pinned
# revisions, never kept in this repository, fixed up mechanically
# (platform/mods/eclipse/fixup_sources.py) and built with clang into one
# library (platform/mods/eclipse/lib). Their patches register with the port's
# code-mod registry (platform/mods/modhooks.cpp) and reach the game through
# hooks in the decomp source (decomp-patches/modhook-*.patch).
#
# 32-bit only for now: their headers describe the GameCube's layout, which
# the 32-bit port reproduces (decomp-patches/layout-01-*.patch).

if(NOT SMS_ARCH STREQUAL "32")
  message(FATAL_ERROR "SMS_ECLIPSE needs the 32-bit build (SMS_ARCH=32)")
endif()
find_program(SMS_CLANGXX NAMES clang++)
find_program(SMS_CLANG NAMES clang)
if(NOT SMS_CLANGXX OR NOT SMS_CLANG)
  message(FATAL_ERROR "SMS_ECLIPSE needs clang/clang++ (Eclipse's sources are written for clang)")
endif()
find_package(Git REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SMS_ECLIPSE_SRC_DIR "${CMAKE_BINARY_DIR}/eclipse-src" CACHE PATH
  "Where the Eclipse, BetterSunshineEngine and SunshineHeaderInterface sources are fetched to")

# name  url  revision
set(_eclipse_repos
  "eclipse|https://github.com/JoshuaMKW/super-mario-eclipse|52749795113f415b97d02392c45385982daa70bb"
  "bse|https://github.com/JoshuaMKW/BetterSunshineEngine|fd6273014545ac0174fa54fada02edd9212f63d8"
  "shi|https://github.com/JoshuaMKW/SunshineHeaderInterface|a0d858951e7fb22dce5304aa5c50287ecb0d6862")

foreach(r ${_eclipse_repos})
  string(REPLACE "|" ";" r "${r}")
  list(GET r 0 _name)
  list(GET r 1 _url)
  list(GET r 2 _rev)
  set(_dir "${SMS_ECLIPSE_SRC_DIR}/${_name}")
  set(_have "")
  if(EXISTS "${_dir}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} -C "${_dir}" rev-parse HEAD
      OUTPUT_VARIABLE _have OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  if(NOT _have STREQUAL _rev)
    message(STATUS "SMS_ECLIPSE: fetching ${_name} ${_rev}")
    file(MAKE_DIRECTORY "${_dir}")
    execute_process(COMMAND ${GIT_EXECUTABLE} init -q "${_dir}")
    execute_process(COMMAND ${GIT_EXECUTABLE} -C "${_dir}" fetch -q --depth 1 "${_url}" "${_rev}"
      RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "SMS_ECLIPSE: could not fetch ${_url} at ${_rev}")
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} -C "${_dir}" -c advice.detachedHead=false checkout -q -f FETCH_HEAD
      RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "SMS_ECLIPSE: could not check out ${_name}")
    endif()
  endif()
endforeach()

execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/platform/mods/eclipse/fixup_sources.py
  "${SMS_ECLIPSE_SRC_DIR}/eclipse" "${SMS_ECLIPSE_SRC_DIR}/bse" "${SMS_ECLIPSE_SRC_DIR}/shi"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "SMS_ECLIPSE: fixup_sources.py failed")
endif()

include(ExternalProject)
set(_eclipse_lib "${CMAKE_BINARY_DIR}/eclipse-build/libsms_eclipse.a")
ExternalProject_Add(sms_eclipse_build
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/mods/eclipse/lib
  BINARY_DIR ${CMAKE_BINARY_DIR}/eclipse-build
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DCMAKE_C_COMPILER=${SMS_CLANG} -DCMAKE_CXX_COMPILER=${SMS_CLANGXX}
    -DECLIPSE_SRC=${SMS_ECLIPSE_SRC_DIR}/eclipse -DBSE_SRC=${SMS_ECLIPSE_SRC_DIR}/bse
    -DSHI_SRC=${SMS_ECLIPSE_SRC_DIR}/shi -DPORT_MODS=${CMAKE_CURRENT_SOURCE_DIR}/platform/mods
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${_eclipse_lib})
# The mods' plain new/delete go to the game's heaps, like the game's own
# (see the sms_game rename above), and their static constructors run when the
# modules load (sms_mod_start), not at process start: Kuribo runs a module's
# constructors when it loads it, and BetterSunshineEngine's start threads and
# allocate from the game's heaps.
ExternalProject_Add_Step(sms_eclipse_build rename_new
  COMMAND ${SMS_OBJCOPY} --redefine-syms=${CMAKE_BINARY_DIR}/game_new_syms.txt
    --rename-section .ctors=sms_mod_ctors,alloc,load,data ${_eclipse_lib}
  DEPENDEES build)

# Functions the mods call out of line that the decomp only has inline, and
# SDK functions the retail game never needed: built with the game's flags.
target_sources(sms_game PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/mods/eclipse/port_shims.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/mods/eclipse/sdk_extras.cpp)

add_dependencies(sms sms_eclipse_build)
target_compile_definitions(sms PRIVATE SMS_ECLIPSE=1)
target_link_libraries(sms PRIVATE -Wl,--whole-archive ${_eclipse_lib} -Wl,--no-whole-archive)
set_property(TARGET sms APPEND PROPERTY LINK_DEPENDS ${_eclipse_lib})
# Names the mods use for things the decomp spells otherwise: retail globals
# under their map names, and functions whose u32 is unsigned int there and
# unsigned long here (the same type on the 32-bit port).
target_link_options(sms PRIVATE
  -Wl,--defsym=gStageBGM=_ZN10MSMainProc11MSStageInfo8stageBgmE
  -Wl,--defsym=gAudioVolume=_ZN5MSBgm12smMainVolumeE
  -Wl,--defsym=waterColor=gModelWaterManagerWaterColor
  -Wl,--defsym=_ZN7JKRHeap5allocEjiPS_=_ZN7JKRHeap5allocEmiPS_
  -Wl,--defsym=_ZN13JKRMemArchiveC1EPvj15JKRMemBreakFlag=_ZN13JKRMemArchiveC1EPvm15JKRMemBreakFlag)
message(STATUS "SMS port: Super Mario Eclipse built in (sources in ${SMS_ECLIPSE_SRC_DIR})")
