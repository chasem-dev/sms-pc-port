# Extensions (ext/README.md): the registry and each extension's game half are
# game code (sms_game, the game's flags and headers); their other halves
# (networking, anything that needs the host's libraries) are platform code
# (sms). An extension is a folder ext/<name>/ with game/ and, optionally,
# net/ and sync/ sources; tests/ folders are left out.
target_sources(sms_game PRIVATE ${CMAKE_CURRENT_LIST_DIR}/core/port_ext.cpp)
file(GLOB SMS_EXT_GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*/game/*.cpp)
target_sources(sms_game PRIVATE ${SMS_EXT_GAME_SOURCES})
file(GLOB SMS_EXT_HOST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*/net/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*/sync/*.cpp)
target_sources(sms PRIVATE ${SMS_EXT_HOST_SOURCES})
target_include_directories(sms PRIVATE ${CMAKE_CURRENT_LIST_DIR})
if(WIN32)
  # multiplayer's sockets
  target_link_libraries(sms PRIVATE ws2_32)
endif()

# Both sides of the multiplayer extension must run the same game build: the
# port's commit (as the launcher names a game), and Eclipse marked.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
  OUTPUT_VARIABLE _sms_ext_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _sms_ext_git)
if(NOT _sms_ext_git EQUAL 0 OR NOT _sms_ext_commit)
  set(_sms_ext_commit "unknown")
endif()
string(SUBSTRING "${_sms_ext_commit}" 0 12 _sms_ext_build)
if(SMS_ECLIPSE)
  string(APPEND _sms_ext_build "-eclipse")
endif()
set_property(SOURCE ${SMS_EXT_HOST_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS "SMS_GAME_BUILD=\"${_sms_ext_build}\"")
# for test tools that join this build (ext/multiplayer/tools/mp_shots.py)
file(WRITE ${CMAKE_BINARY_DIR}/multiplayer-build.txt "${_sms_ext_build}\n")
