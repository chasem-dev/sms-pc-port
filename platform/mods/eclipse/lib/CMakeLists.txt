# Builds BetterSunshineEngine and Super Mario Eclipse (fetched sources, see
# cmake/eclipse.cmake) into one static library for the 32-bit port, with
# clang (their sources are written for it) and the port's shim headers in
# place of the Kuribo/Kamek SDKs and PowerPC-only macros.
cmake_minimum_required(VERSION 3.16)
project(sms_eclipse C CXX)

foreach(v ECLIPSE_SRC BSE_SRC SHI_SRC PORT_MODS)
  if(NOT ${v})
    message(FATAL_ERROR "${v} is required")
  endif()
endforeach()

# The same file sets their own CMake builds glob (Eclipse: src/ one level
# deep; BetterSunshineEngine: its listed directories), minus what the port
# provides natively: BSE's Dolphin SDK reimplementations (libs/dolphin), its
# C runtime (cstd, except the two string helpers) and PowerPC assembly.
set(_bse_dirs src src/camera src/collision src/debug src/level_select src/libs src/moveset
  src/objects src/patches src/patches/collision src/physics src/shine src/sunscript src/toolbox src/yoshi)
set(BSE_SOURCES ${BSE_SRC}/src/cstd/stdlib.c)
foreach(d ${_bse_dirs})
  file(GLOB _f ${BSE_SRC}/${d}/*.c ${BSE_SRC}/${d}/*.cpp)
  list(APPEND BSE_SOURCES ${_f})
endforeach()
file(GLOB ECLIPSE_SOURCES ${ECLIPSE_SRC}/src/*.c ${ECLIPSE_SRC}/src/*.cpp ${ECLIPSE_SRC}/src/*/*.c ${ECLIPSE_SRC}/src/*/*.cpp)

set(_shi ${SHI_SRC}/include ${SHI_SRC}/include/JSystem ${SHI_SRC}/include/Dolphin ${SHI_SRC}/include/SMS
  ${SHI_SRC}/include/Kamek ${SHI_SRC}/include/Kuribo)
set(_defs NTSCU KURIBO_NO_TYPES "BETTER_SMS_VERSION=\"v2.0\"" BETTER_SMS_CUSTOM_MUSIC=1 BETTER_SMS_EXTRA_SHINES=1
  BETTER_SMS_EXTRA_OBJECTS=1 BETTER_SMS_EXTRA_COLLISION=1 BETTER_SMS_EXTENDED_RENDER_DISTANCE=1
  BETTER_SMS_SLOT_B_SUPPORT=1 BETTER_SMS_SHADOW_MARIO_HEALTHBAR=1 "SUPER_MARIO_ECLIPSE_VERSION=\"v1.1\"")
# -fno-use-init-array: static constructors go to .ctors, which the port
# renames (cmake/eclipse.cmake) and runs when the modules load, as Kuribo does.
set(_opts -m32 -O2 -g -w -fms-extensions -fno-exceptions -fno-threadsafe-statics -fno-strict-aliasing
  -fno-use-init-array
  -include ${PORT_MODS}/eclipse/shim/sms_mod_prelude.h
  $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20> $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)

add_library(bse_objs OBJECT ${BSE_SOURCES})
target_include_directories(bse_objs PRIVATE ${PORT_MODS}/eclipse/shim ${PORT_MODS}/include
  ${BSE_SRC}/include/BetterSMS ${BSE_SRC}/src ${BSE_SRC}/include ${_shi})
target_compile_definitions(bse_objs PRIVATE ${_defs})
target_compile_options(bse_objs PRIVATE ${_opts})

add_library(eclipse_objs OBJECT ${ECLIPSE_SOURCES})
target_include_directories(eclipse_objs PRIVATE ${PORT_MODS}/eclipse/shim ${PORT_MODS}/include
  ${ECLIPSE_SRC}/include ${BSE_SRC}/include ${_shi})
target_compile_definitions(eclipse_objs PRIVATE ${_defs})
target_compile_options(eclipse_objs PRIVATE ${_opts})

add_library(sms_eclipse STATIC $<TARGET_OBJECTS:bse_objs> $<TARGET_OBJECTS:eclipse_objs>)
