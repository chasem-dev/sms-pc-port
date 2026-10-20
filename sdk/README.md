# sdk/: making native mods

Everything a mod needs from this repository. The manual, with every event and function, is [docs/MODDING.md](docs/MODDING.md).

| | |
| --- | --- |
| [`include/sms_mod.h`](include/sms_mod.h) | The API: one C header, every function and payload documented where it is declared. A mod needs nothing else. |
| [`include/sms_mod.hpp`](include/sms_mod.hpp) | A header-only C++11 layer on it: lambdas for handlers, payloads by type, a class for the mod. |
| [`cmake/SmsMod.cmake`](cmake/SmsMod.cmake) | `sms_add_mod(<folder> <sources>)`: builds a mod and lays it out as the game loads it (`<id>/mod.ini`, `<id>/bin/<platform>/<library>`). |
| [`examples/hello`](examples/hello) | C++, the smallest useful mod: names the stage, counts coins, a key and a setting. Start here. |
| [`examples/trainer-hud`](examples/trainer-hud) | C, most of the API: a speed panel and graph, a trail and landing ring in the world, coin pop-ups, a field of view setting, invincibility, moon jump, images, options, keys, another mod's events. |
| [`examples/lua`](examples/lua) | C, the **lua** mod: embeds Lua 5.4 (downloaded at configure time, checked against its release hash) and runs Lua script mods. |
| [`examples/lua-challenges`](examples/lua-challenges) | **Lua**, a script mod: a challenge against the clock each time a stage loads, with a panel, world markers, sounds, settings, a key, a module of its own, and the C stage-timer's events. Start here for Lua. |
| [`examples/stage-timer`](examples/stage-timer) | C, made to sit beside others: a timer panel that stacks under the trainer HUD's, damage watched at `MONITOR` priority, best times in its settings, a custom event other mods can hear. |

## Building the examples

```sh
cmake -S sdk/examples -B build/mods-examples -DSMS_MODS_OUT=$PWD/mods -DSMS_MOD_ARCH=32
cmake --build build/mods-examples
```

They are then in `mods/hello`, `mods/trainer-hud`, `mods/stage-timer`, `mods/lua` and `mods/lua-challenges`, and the next start of the game loads them; F1 in the game shows them.
The lua mod downloads Lua's sources from lua.org when CMake configures; `-DSMS_LUA_SOURCE_DIR=<lua-5.4.x>` uses a copy instead.
`-DSMS_MOD_ARCH=32` matches the default Linux game (`build/linux-32`, a 32-bit program). Leave it out for a 64-bit game, on Windows (MSYS2 MinGW) and on macOS.

## A Lua script mod of your own

With the lua mod installed, a folder in `mods/` with a `mod.ini` saying `runtime = lua` and a `main.lua` is a mod: nothing to build.
Copy `examples/lua-challenges` to start; the Lua API is in [docs/MODDING.md](docs/MODDING.md#lua-scripts).
Run the game with `SMS_NATIVE_MODS_HOT=1` and each save of the script loads it again.

## A native mod of your own

Copy `examples/hello` somewhere, give it your own `id` in `mod.ini`, and build it with a `CMakeLists.txt` of its own:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_mod CXX)
include(/path/to/sms-pc-port/sdk/cmake/SmsMod.cmake)
sms_add_mod(${CMAKE_CURRENT_SOURCE_DIR} my_mod.cpp)
```

or copy `include/` into your project and build the library any way you like: it must export `sms_mod_api_version` (`SMS_MOD_DEFINE()`) and `sms_mod_load`, and go in `mods/<id>/bin/<platform>/` beside the `mod.ini`.
To release it, ship the folder with a library for each platform you built (`linux-x86`, `linux-x86_64`, `windows-x86_64`, `macos-arm64`, `macos-x86_64`).
