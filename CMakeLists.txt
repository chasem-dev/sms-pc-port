cmake_minimum_required(VERSION 3.20)
project(sms_port C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  # String literals are Shift-JIS on disc and in the MWCC build (sjiswrap).
  -finput-charset=UTF-8 -fexec-charset=CP932
  # 39 decomp functions fall off the end of a non-void body (retail returned
  # whatever was in r3). At -O0 this returns garbage like MWCC instead of
  # trapping; at -O1+ it is real UB (see build/noreturn.txt).
  -fno-unreachable-traps
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++03 -fno-gnu-keywords -fpermissive -fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:C>:-std=gnu99 -fcommon>
  -include ${SMS_PORT_SRC}/port_compat.h)
if(SMS_ARCH STREQUAL "64")
  # Keep static data below 4 GiB so pointer<->u32 round trips of globals survive.
  list(APPEND SMS_GAME_FLAGS -fno-pie)
endif()

set_source_files_properties(${SMS_DECOMP_CXX_SOURCES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${SMS_DECOMP_CXX_SOURCES} ${SMS_DECOMP_C_SOURCES} PROPERTIES
  OBJECT_DEPENDS ${SMS_PATCH_HEADER_STAMP})
set_source_files_properties(${SMS_DECOMP_PCH_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-include;${SMS_DECOMP}/include/SMS.pch")

option(SMS_WITH_GX "Build and link platform/gx (the graphics layer)" ON)
if(SMS_WITH_GX AND SMS_ARCH STREQUAL "32")
  # Only the i386 runtime libraries are installed (no :i386 -dev packages):
  # compile against the amd64 SDL2/EGL headers and link the .so files directly.
  set(_i386 /usr/lib/i386-linux-gnu)
  if(EXISTS ${_i386}/libSDL2-2.0.so.0 AND NOT TARGET SDL2::SDL2)
    set(_shim ${CMAKE_BINARY_DIR}/sdl2-m32)
    file(WRITE ${_shim}/SDL2/_real_SDL_config.h
      "/* 32-bit build against the amd64 SDL2 headers. */\n"
      "#include \"/usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h\"\n"
      "#undef SIZEOF_VOIDP\n#define SIZEOF_VOIDP 4\n")
    add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
    set_target_properties(SDL2::SDL2 PROPERTIES
      IMPORTED_LOCATION ${_i386}/libSDL2-2.0.so.0
      INTERFACE_INCLUDE_DIRECTORIES "${_shim};/usr/include/SDL2")
    set(CMAKE_DISABLE_FIND_PACKAGE_SDL2 ON)
  endif()
  if(EXISTS ${_i386}/libEGL.so.1)
    set(SMS_GX_EGL_LIB ${_i386}/libEGL.so.1 CACHE FILEPATH "" FORCE)
    set(SMS_GX_EGL_INCLUDE /usr/include CACHE PATH "" FORCE)
  endif()
endif()
if(SMS_WITH_GX AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/platform/gx/CMakeLists.txt)
  add_subdirectory(platform/gx)
endif()

add_library(sms_game STATIC ${SMS_DECOMP_CXX_SOURCES} ${SMS_DECOMP_C_SOURCES})
target_include_directories(sms_game BEFORE PRIVATE
  ${SMS_PORT_SRC}/port_include ${SMS_PATCHED_INCLUDE_DIR} ${SMS_DECOMP}/include)
if(TARGET sms_gx AND SMS_GX_OVERRIDE_INCLUDE_DIR)
  # GXVert.h vertex writers become functions implemented by sms_gx.
  target_include_directories(sms_game BEFORE PRIVATE ${SMS_GX_OVERRIDE_INCLUDE_DIR})
endif()
target_compile_definitions(sms_game PRIVATE ${SMS_COMMON_DEFS})
target_compile_options(sms_game PRIVATE ${SMS_GAME_FLAGS})

# The game replaces the global operator new/delete with JKRHeap allocation,
# which would also capture libstdc++ and the platform layer (and crash before
# the first JKRHeap exists). Rename the game's definitions and every game
# reference to them, so only game code allocates from JKR heaps.
if(SMS_ARCH STREQUAL "32")
  set(_sz j)
else()
  set(_sz m)
endif()
file(WRITE ${CMAKE_BINARY_DIR}/game_new_syms.txt
  "_Znw${_sz} __sms_game_new\n_Zna${_sz} __sms_game_new_array\n_ZdlPv __sms_game_delete\n_ZdaPv __sms_game_delete_array\n")
add_custom_command(TARGET sms_game POST_BUILD
  COMMAND objcopy --redefine-syms=${CMAKE_BINARY_DIR}/game_new_syms.txt $<TARGET_FILE:sms_game>
  COMMENT "Renaming the game's global operator new/delete")

# --- Platform layer ---------------------------------------------------------
file(GLOB_RECURSE SMS_PLATFORM_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp)
# platform/gx is owned by the graphics layer and builds itself: if it provides
# platform/gx/CMakeLists.txt defining a library target `sms_gx`, it is linked
# in and its strong GX* definitions replace the weak stubs.
list(FILTER SMS_PLATFORM_SOURCES EXCLUDE REGEX "/platform/gx/")
add_executable(sms ${SMS_PORT_SRC}/port_main.cpp ${SMS_PLATFORM_SOURCES})
target_include_directories(sms PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/platform ${SMS_PORT_SRC}
  ${SMS_PORT_SRC}/port_include ${SMS_DECOMP}/include)
target_compile_definitions(sms PRIVATE ${SMS_COMMON_DEFS})
target_compile_options(sms PRIVATE -fno-strict-aliasing -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>)
# dsptask.c points a DSP task at the fixed low-memory address 0x80004600.
target_link_options(sms PRIVATE -Wl,--defsym=lbl_80004600=0x80004600)
if(SMS_ARCH STREQUAL "64")
  target_compile_options(sms PRIVATE -fno-pie)
  target_link_options(sms PRIVATE -no-pie)
endif()
find_package(Threads REQUIRED)
target_link_libraries(sms PRIVATE -Wl,--whole-archive sms_game -Wl,--no-whole-archive Threads::Threads m ${CMAKE_DL_LIBS})
if(TARGET sms_gx)
  target_link_libraries(sms PRIVATE -Wl,--whole-archive sms_gx -Wl,--no-whole-archive)
endif()
