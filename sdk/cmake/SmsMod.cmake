# SmsMod.cmake: build native mods for the Super Mario Sunshine PC port
# (docs/MODDING.md). Include it, then for each mod:
#
#   sms_add_mod(<folder with mod.ini> <sources...>)
#
# builds the mod's library with only sdk/include on its include path and
# lays the mod out as the game loads it:
#   ${SMS_MODS_OUT}/<id>/mod.ini
#   ${SMS_MODS_OUT}/<id>/bin/<platform>/<library>.so|.dll|.dylib
#   ${SMS_MODS_OUT}/<id>/<everything else in the mod's folder but sources>
#
#   sms_add_script_mod(<folder with mod.ini>)
#
# lays out a script mod (runtime = lua in its mod.ini): its files as they are.
#
# SMS_MODS_OUT defaults to <build>/mods; point it at the game's mods/ folder
# to install. The library must match the game: a 32-bit game (the default on
# Linux, build/linux-32) needs -DSMS_MOD_ARCH=32 (or a 32-bit toolchain).
include_guard(GLOBAL)

set(SMS_MOD_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "The mod SDK (sdk/)")
set(SMS_MODS_OUT "${CMAKE_BINARY_DIR}/mods" CACHE PATH "Where built mods are laid out")
set(SMS_MOD_ARCH "" CACHE STRING "32 or 64 to force the pointer size (Linux, gcc/clang)")

if(SMS_MOD_ARCH STREQUAL "32")
  add_compile_options(-m32)
  add_link_options(-m32)
  set(_sms_mod_bits 32)
elseif(SMS_MOD_ARCH STREQUAL "64")
  add_compile_options(-m64)
  add_link_options(-m64)
  set(_sms_mod_bits 64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_sms_mod_bits 32)
else()
  set(_sms_mod_bits 64)
endif()

# The folder name the game looks in (ext/modapi/host/mod_runtime.cpp platformDir).
if(WIN32)
  if(_sms_mod_bits EQUAL 32)
    set(SMS_MOD_PLATFORM windows-x86)
  else()
    set(SMS_MOD_PLATFORM windows-x86_64)
  endif()
elseif(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"))
    set(SMS_MOD_PLATFORM macos-arm64)
  else()
    set(SMS_MOD_PLATFORM macos-x86_64)
  endif()
else()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(SMS_MOD_PLATFORM linux-arm64)
  elseif(_sms_mod_bits EQUAL 32)
    set(SMS_MOD_PLATFORM linux-x86)
  else()
    set(SMS_MOD_PLATFORM linux-x86_64)
  endif()
endif()

# A key's value in a mod.ini.
function(_sms_mod_ini file key out default)
  set(value "${default}")
  file(STRINGS "${file}" lines REGEX "^[ \t]*${key}[ \t]*=")
  if(lines)
    list(GET lines 0 line)
    string(REGEX REPLACE "^[ \t]*${key}[ \t]*=[ \t]*" "" value "${line}")
    string(STRIP "${value}" value)
  endif()
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(sms_add_mod dir)
  get_filename_component(dir "${dir}" ABSOLUTE)
  get_filename_component(folder "${dir}" NAME)
  _sms_mod_ini("${dir}/mod.ini" id id "${folder}")
  _sms_mod_ini("${dir}/mod.ini" library lib "${id}")
  set(target "smsmod_${id}")
  string(MAKE_C_IDENTIFIER "${target}" target)
  set(out "${SMS_MODS_OUT}/${id}")
  add_library(${target} MODULE ${ARGN})
  target_include_directories(${target} PRIVATE "${SMS_MOD_SDK_DIR}/include")
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${lib}"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${out}/bin/${SMS_MOD_PLATFORM}"
    RUNTIME_OUTPUT_DIRECTORY "${out}/bin/${SMS_MOD_PLATFORM}")
  foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set_target_properties(${target} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${CFG} "${out}/bin/${SMS_MOD_PLATFORM}"
      RUNTIME_OUTPUT_DIRECTORY_${CFG} "${out}/bin/${SMS_MOD_PLATFORM}")
  endforeach()
  if(MINGW)
    # no libgcc/libstdc++ DLLs to ship beside the mod
    target_link_options(${target} PRIVATE -static-libgcc $<$<COMPILE_LANGUAGE:CXX>:-static-libstdc++>)
  endif()
  # the mod's folder, its sources left out
  file(GLOB_RECURSE files RELATIVE "${dir}" CONFIGURE_DEPENDS "${dir}/*")
  foreach(f ${files})
    if(NOT f MATCHES "\\.(c|cc|cpp|cxx|h|hpp)$" AND NOT f MATCHES "^CMakeLists.txt$")
      configure_file("${dir}/${f}" "${out}/${f}" COPYONLY)
    endif()
  endforeach()
  message(STATUS "sms mod ${id}: ${out} (${SMS_MOD_PLATFORM})")
endfunction()

# A script mod: nothing to build, its folder copied as it is (scripts included).
function(sms_add_script_mod dir)
  get_filename_component(dir "${dir}" ABSOLUTE)
  get_filename_component(folder "${dir}" NAME)
  _sms_mod_ini("${dir}/mod.ini" id id "${folder}")
  set(out "${SMS_MODS_OUT}/${id}")
  file(GLOB_RECURSE files RELATIVE "${dir}" CONFIGURE_DEPENDS "${dir}/*")
  foreach(f ${files})
    if(NOT f MATCHES "^CMakeLists.txt$")
      configure_file("${dir}/${f}" "${out}/${f}" COPYONLY)
    endif()
  endforeach()
  message(STATUS "sms script mod ${id}: ${out}")
endfunction()
