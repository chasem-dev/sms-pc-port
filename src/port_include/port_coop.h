// Split-screen co-op between the controllers (platform/pad/pad.cpp) and the
// game's side (ext/coop/game/coop.cpp): who joined and left, and what the
// join card shows.
#ifndef PORT_COOP_H
#define PORT_COOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The card platform/gx draws (GXPC_CoopCard, sms_gx/gx_pc.h, which says what each field is).
#ifndef GXPC_COOP_CARD_DEFINED
#define GXPC_COOP_CARD_DEFINED
typedef struct GXPCCoopCard {
    char badge[4];
    uint32_t accent;
    char title[64];
    char lines[2][96];
    float progress;
    int view, stacked;
} GXPCCoopCard;
#endif

// port_coop_take_events: what happened since it was last asked
enum {
	PORT_COOP_EVENT_JOINED     = 1 << 0, // a controller joined as player 2
	PORT_COOP_EVENT_LEFT       = 1 << 1, // player 2 left (with LEFT_HELD or LEFT_GONE, or the game let them go)
	PORT_COOP_EVENT_LEFT_HELD  = 1 << 2, //   they held their Start
	PORT_COOP_EVENT_LEFT_GONE  = 1 << 3, //   their controller went and none came in time
	PORT_COOP_EVENT_QUEUED     = 1 << 4, // a guest's Start asked to join while the stage could not take them
	PORT_COOP_EVENT_CANCELLED  = 1 << 5, // ...and was held: they won't join after all
	PORT_COOP_EVENT_TAKEOVER   = 1 << 6, // a guest held Start: it is player 1's controller now
	PORT_COOP_EVENT_OWNER_LOST = 1 << 7, // player 1's controller went
	PORT_COOP_EVENT_OWNER_BACK = 1 << 8, // ...and another controller is theirs now
	PORT_COOP_EVENT_P2_GONE    = 1 << 9, // player 2's controller went: their place waits
	PORT_COOP_EVENT_P2_BACK    = 1 << 10, // ...and a controller took it
};

// What the controllers are doing now, for the join card.
typedef struct PortCoopStatus {
	int guest;          // a controller that is no one's was played with a moment ago
	int guestOwnerless; // ...while player 1 has no controller of their own (the keyboard)
	float guestHold;    // its Start held: how far to taking over as player 1 (0..1)
	int queued;         // a join asked for, waiting for the stage
	float queuedHold;   // its Start held: how far to taking it back (0..1)
	float p2Hold;       // player 2's Start held: how far to leaving (0..1)
	float p2Gone;       // seconds left for player 2's controller to come back, or < 0
	int ownerLost;      // player 1's controller went and no other is theirs yet
	char guestStart[12]; // the label on the guest's (or the queued controller's) Start: START, OPTIONS, +, MENU
	char p2Start[12];    // ...on player 2's
} PortCoopStatus;

int port_coop_enabled(void);
int port_coop_player2(void);
// The game's state: 0 the menus (title, file select: every controller plays
// as controller 1), 1 a stage that cannot take a player now (loading, a
// cutscene, paused), 2 a stage being played; `built` whether this stage has
// a second player made.
void port_coop_set_mode(int mode, int built);
// Whether a stage loading now should make a second player: a controller
// other than player 1's is there, or one has joined or asked to.
int port_coop_armed(void);
void port_coop_leave(void);
unsigned port_coop_take_events(void);
void port_coop_status(PortCoopStatus* out);
void port_coop_join_virtual(void);

#ifdef __cplusplus
}
#endif

#endif
