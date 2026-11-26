// gx_api.cpp lays PTR32 slots over GXTexObj and GXTlutObj, so the objects
// keep the game's layout on a 64-bit host; a slot given an address that does
// not fit 32 bits calls port_ptr32_trap, which the game links from
// platform/port_runtime.cpp. The standalone tests are built without the port,
// so they bring their own.
#include <cstdio>
#include <cstdlib>

extern "C" void port_ptr32_trap(const void* p) {
    fprintf(stderr, "PTR32: address %p does not fit a 32-bit slot\n", p);
    abort();
}
