cmake_minimum_required(VERSION 3.20)
project(sms_port C CXX)

set(SMS_DECOMP ${CMAKE_CURRENT_SOURCE_DIR}/decomp)
set(SMS_PORT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Target word size -------------------------------------------------------
# The game assumes 32-bit pointers (pointer-in-u32 slots, in-place relocated
# resource structs). Build -m32 when the host has multilib; otherwise fall back
# to -m64 so compile work can proceed.
set(SMS_ARCH "auto" CACHE STRING "auto, 32 or 64")
if(SMS_ARCH STREQUAL "auto")
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-m32")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-m32")
  check_cxx_source_compiles("#include <cstdio>\nint main(){std::puts(\"\");return 0;}" SMS_HAVE_M32)
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(SMS_HAVE_M32)
    set(SMS_ARCH 32)
  else()
    set(SMS_ARCH 64)
    message(WARNING "-m32 does not link here (install gcc-multilib g++-multilib); building -m64")
  endif()
endif()
add_compile_options(-m${SMS_ARCH})
add_link_options(-m${SMS_ARCH})
message(STATUS "SMS port: building -m${SMS_ARCH}")

# --- Decomp translation units -----------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_sources.py
          ${SMS_DECOMP} ${CMAKE_BINARY_DIR}/decomp_sources.cmake
  RESULT_VARIABLE _gen_rc)
if(NOT _gen_rc EQUAL 0)
  message(FATAL_ERROR "tools/gen_sources.py failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SMS_DECOMP}/configure.py)
include(${CMAKE_BINARY_DIR}/decomp_sources.cmake)

# Port patches (decomp-patches/*.patch) are applied to copies of the touched
# files under the build tree; those copies replace the originals in the
# source lists.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches.cmake)

set(SMS_COMMON_DEFS GEKKO TARGET_PC VERSION_GMSE01 BUILD_VERSION=2 NDEBUG=1)
set(SMS_GAME_FLAGS
  -w -fno-strict-aliasing -fwrapv -fno-omit-frame-pointer
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++03 -fno-gnu-keywords -fpermissive -fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:C>:-std=gnu99 -fcommon>
  -include ${SMS_PORT_SRC}/port_compat.h)
if(SMS_ARCH STREQUAL "64")
  # Keep static data below 4 GiB so pointer<->u32 round trips of globals survive.
  list(APPEND SMS_GAME_FLAGS -fno-pie)
endif()

set_source_files_properties(${SMS_DECOMP_CXX_SOURCES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${SMS_DECOMP_PCH_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;${SMS_DECOMP}/include/SMS.pch")

add_library(sms_game STATIC ${SMS_DECOMP_CXX_SOURCES} ${SMS_DECOMP_C_SOURCES})
target_include_directories(sms_game BEFORE PRIVATE
  ${SMS_PORT_SRC}/port_include ${SMS_PATCHED_INCLUDE_DIR} ${SMS_DECOMP}/include)
target_compile_definitions(sms_game PRIVATE ${SMS_COMMON_DEFS})
target_compile_options(sms_game PRIVATE ${SMS_GAME_FLAGS})

# --- Platform layer ---------------------------------------------------------
file(GLOB_RECURSE SMS_PLATFORM_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)
add_executable(sms ${SMS_PORT_SRC}/port_main.cpp ${SMS_PLATFORM_SOURCES})
target_include_directories(sms PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/platform ${SMS_PORT_SRC}
  ${SMS_PORT_SRC}/port_include ${SMS_DECOMP}/include)
target_compile_definitions(sms PRIVATE ${SMS_COMMON_DEFS})
target_compile_options(sms PRIVATE -fno-strict-aliasing -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>)
if(SMS_ARCH STREQUAL "64")
  target_compile_options(sms PRIVATE -fno-pie)
  target_link_options(sms PRIVATE -no-pie)
endif()
find_package(Threads REQUIRED)
target_link_libraries(sms PRIVATE -Wl,--whole-archive sms_game -Wl,--no-whole-archive Threads::Threads m)
