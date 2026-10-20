# The example native mods, built apart from the game against sdk/include only:
#
#   cmake -S sdk/examples -B build/mods-examples -DSMS_MOD_ARCH=32 -DSMS_MODS_OUT=$PWD/mods
#   cmake --build build/mods-examples
#
# (SMS_MOD_ARCH=32 for the default 32-bit Linux game; leave it out for a
# 64-bit game, Windows or macOS.)
cmake_minimum_required(VERSION 3.16)
project(sms_example_mods C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SmsMod.cmake)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
if(NOT MSVC)
  add_compile_options(-Wall -Wextra -O2)
endif()

sms_add_mod(hello hello/hello.cpp)
sms_add_mod(trainer-hud trainer-hud/trainer_hud.c)
sms_add_mod(stage-timer stage-timer/stage_timer.c)
if(NOT WIN32)
  target_link_libraries(smsmod_trainer_hud PRIVATE m)
endif()

# The lua mod embeds Lua 5.4: its sources from lua.org (checked against the
# release's hash), or a copy given with -DSMS_LUA_SOURCE_DIR=<lua-5.4.x>.
set(SMS_LUA_SOURCE_DIR "" CACHE PATH "Lua 5.4 sources (the folder with src/); empty: download")
if(NOT SMS_LUA_SOURCE_DIR)
  include(FetchContent)
  FetchContent_Declare(sms_lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(sms_lua) # no CMakeLists.txt in it: only fetched
  set(SMS_LUA_SOURCE_DIR ${sms_lua_SOURCE_DIR})
endif()
file(GLOB _lua_sources ${SMS_LUA_SOURCE_DIR}/src/*.c)
list(FILTER _lua_sources EXCLUDE REGEX "/(lua|luac)\\.c$")
sms_add_mod(lua lua/lua_host.c ${_lua_sources})
target_include_directories(smsmod_lua PRIVATE ${SMS_LUA_SOURCE_DIR}/src)
if(UNIX)
  target_compile_definitions(smsmod_lua PRIVATE LUA_USE_POSIX)
  target_link_libraries(smsmod_lua PRIVATE m)
endif()
# Lua's own sources, not ours: their warnings are not this build's to fix
if(NOT MSVC)
  set_source_files_properties(${_lua_sources} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Lua script mods: nothing to build
sms_add_script_mod(lua-challenges)
