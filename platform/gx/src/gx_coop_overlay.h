// Split-screen co-op's card (gx_coop_overlay.cpp): how to join, leave and take
// over, near the top of the game's picture or of a player's view, set from any
// thread with GXPC_CoopCard (sms_gx/gx_pc.h).
#ifndef SMS_GX_COOP_OVERLAY_H
#define SMS_GX_COOP_OVERLAY_H

namespace gx {

#ifdef SMS_GX_HAVE_SDL2
// On the GL thread, between presenting the frame and the swap.
void coopOverlayRender(int winW, int winH);
#else
inline void coopOverlayRender(int, int) {}
#endif

}  // namespace gx

#endif
