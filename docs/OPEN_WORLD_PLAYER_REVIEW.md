# Open-world player experience review

## Static scenery continuity (v11)

Palms now remain visible before a neighboring level becomes active: 23 Plaza palms, including the cliff palms, and five Pinna Beach palms use the destination episode's saved placement. The Plaza's Shine monument also remains visible. The same preview is used while looking back from the Harbor connection. Recorded native views were checked for grounded trees and shore support.

The 32-bit, 60 fps ferry round trip and walking round trip passed with audio and native state carried across both directions. The 64-bit saved-settings run also reached Pinna Park and proved controller movement inside. The updated phone preview is `sunshine-coastal-scenery-v11.png` and `.mp4`; the video keeps both full recorded round trips and their loading holds.

Moving boats, park rides, and other scene actors can still appear at an exchange. The low return camera can show the upper Ferris cabins without enough of their supporting ride in frame; a wider native view confirmed that the geometry is present. This pass does not establish seamless moving-ride continuity or unaided route discovery.

## Rider and return-load polish (v10)

Mario now keeps a natural riding pose offshore instead of leaning continuously as though turning sharply. The ferry carries the torso lean across the exchange; ordinary walking crossings also preserve facing and lean. The shared ocean animation pauses with the held view.

Returning to the Plaza no longer waits for an extra fixed billboard-video buffering delay during a coastal exchange. The video still initializes and prepares normally. The recorded 64-bit round trip with native movie decoding enabled resumed at 183 ms outbound and 151 ms returning. These are local replay measurements, not guaranteed timings.

A walking replay also caught the camera dropping behind the Plaza seawall after returning. The approach now recovers low or obstructed views and keeps Mario visible on the promenade. The recorded 32-bit round trip and stationary ending show a readable, stable view. Native conversations and L-button cameras retain control.

The short background blend remains. Removing it exposed a visible return-view jump in the recorded test, so that experiment was rejected. This pass improves rider stability and the return hold; it does not eliminate every scenery pop-in or establish unaided route discovery.

## Current coastal layout (v9)

The Harbor now occupies a consistent position beside the Plaza coast. Both shores show the neighboring map and both ends of the walking connection. The approaches follow broad curves, and the covered passage climbs between the native waterfront heights. Two-sided signs identify the destination on entry and the current area on arrival.

The Blooper boards from the red-roofed bell-tower promenade. Mario hops down to the waiting Blooper, and hops ashore on the return trip. The departure camera rises above the seawall and avoids the bell dome. Offshore framing keeps the destination visible; the final beach approach turns toward the amusement park entrance. The route passes around the small island and exchanges maps offshore.

The scenery pass removes obsolete backdrops, unsupported preview fittings, and exposed backing surfaces. The neighboring Harbor uses its native hills, and the newly exposed quay has a masonry face. A shared distant water surface joins the native near-shore waves. The original Plaza secret-pipe platforms and their supporting cliff were preserved.

This remains a prototype. Static map previews do not reproduce every native actor, so some objects still appear at an exchange. The brief loading hold and small camera/pose differences have not disappeared. The Harbor exterior remains a constructed connection, and ordinary-spawn captures do not establish that a first-time player can discover it unaided.

Recorded walking and ferry round trips, an eight-crossing 32-bit stress run, a reversal after the offshore swap, and a saved-settings journey through the native park gate passed. The relocated Harbor sign no longer blocks the reviewed arrival camera. Detailed configurations and local timings are in the [development notes](OPEN_WORLD_DEVELOPMENT.md).

The current player guide describes the new entrances. Earlier findings below are retained as versioned history, not as a description of the current route locations.

## v8 architecture review

The previous pass introduced native roofing, masonry foundations, wall ribs, and interior shading. It improved readability but retained the old route location. Its walking, boat-clearance, and swimming evidence remains in `build/open-world/v8/`.

## v7 review findings

An uninterrupted crossing review exposed a route problem that the arrival checks missed: the ferry drove across a small sand island.
The departure curve now passes around it, and the native Plaza collision survey found no raised-ground intersections at the centerline or either 350-unit steering limit after departure.
The chase camera follows travel directly while smoothing its viewing offset, preventing the rider from receding toward the top of the picture at speed.

Both maps use identical sky resources, but their different coordinate directions made the clouds rotate at the exchange.
The shared sky orientation and carried cloud phase allow the previous 0.6-second dissolve to be shortened to about 0.067 seconds.
The brief blend softens remaining small scenery and pose changes without leaving the previous long trails of double images.
The load hold remains until the restored rider pose has passed through native drawing.
Turning back now eases the orbit angle at a steady distance, addressing the camera moving too close to Mario during reversal.
The ferry holds its camera field of view through the first destination frame and blends back on landing, preventing a brief 60 fps zoom-in.
The distant map also follows the destination episode.

Restoring the entire Plaza backdrop was rejected after seeing oversized cliffs intersect the park approach.
A selective mainland mesh filter preserves the near cliffs while replacing the distant western terrain with the native Pinna island.
This is still a partial geographic integration: small actor pop-ins and the flat city approach need more work.

The boat concern was checked separately from the scenery impression.
All three moving native Plaza boats completed full circuits in the dry Plaza with no contact against the added geometry, including a 100-unit expanded hull and the boats' actual tilt.
The fixed-camera footage still shows the connection as a long, exposed structure with a cut-off end.
Boat clearance is verified for that episode; the Harbor entrance's appearance and placement are not resolved.

Final v7 validation includes the recorded 64-bit round trip at 30 fps and a recorded 32-bit reversal after crossing at 60 fps with audio.
Both retained health, water, speed, and controllable travel, with no black ferry frames.
The saved-settings park-entry replay also reached the native gate and moved inside the park; the subsequent camera-orbit refinement was checked in the recorded ferry replays.
The final orbit reversal keeps Mario at a steady viewing distance.
The short blend still briefly mixes the two views; it does not make missing distant actors into a continuous simulation.
Evidence: `build/open-world/v7/orbit64-final`, `orbit32-final`, `park-final64`, and `boats-complete`.
The phone preview is `sunshine-player-polish-v7.png` and its uninterrupted `.mp4` in FileBrowser's `Render-Previews` folder.

## What was reviewed

Reviewed native gameplay captures across the complete Plaza–Pinna Beach–park journey, the return ferry footage, and the Plaza–Harbor walking route.
A fresh controller replay reached Pinna Park and moved inside it using a private copy of the save.
This is a visual review of native rendered gameplay and scripted controller replays, not an unaided first-time player usability session.
The route-finding test begins beside the ferry, so it cannot establish whether a new player would discover it.

Baseline evidence: `build/open-world/player-review-baseline/`.
Detailed earlier ferry captures: `build/open-world/v5/park-entry64-full/` and `release64-final/`.
Harbor footage: `concepts/delfino-coastal-curve-v4.mp4` and its matching PNG.
Evidence images are local, extracted from actual game captures; they are not concept art.

## Original v5 review priorities (see v6–v8 updates above)

| Priority | What the player sees or feels | Evidence and next change | What would count as finished |
| --- | --- | --- | --- |
| 1 | Palm trees and pieces of scenery hang over the ocean immediately after departure. | Confirmed in baseline field 4040. Audit the ground and scenery shown from the new offshore camera, including the departure shore. | No unsupported trees, buildings, or cut-off land in either direction, including when turning back. |
| 1 | Beach objects appear in water, then the sand appears at landing. | Confirmed in baseline fields 4600 and 4620. Preserve the visible shore and shallow-water boundary throughout the approach; check background filtering and the added ocean together. | The beach is visibly solid before Mario reaches it, and objects remain grounded through landing. |
| 1 | The moving journey becomes a frozen picture. | Fresh baseline trigger-to-arrival measurement: 2.105 seconds. Earlier recorded runs measured 3.790 and 5.980 seconds on different build configurations. These are diagnostic timings, not a controlled performance comparison. Profile construction and first display separately before choosing an optimization. | No perceptible stop during ordinary travel on the intended player configuration. Keeping the old picture visible does not satisfy this. |
| 2 | The distant park view changes into a noticeably different close view. | Confirmed across the offshore handoff and approach. Test an earlier exchange over open water, with matching coastline, scale, silhouette, and camera heading. | A viewer can follow the same landmark from departure through arrival without seeing the island change identity. |
| 2 | Mario becomes very small against a largely empty ocean; the ride lacks a strong sense of speed. | Confirmed in the middle of the ferry recording. Evaluate closer framing, heading response, wake/spray, and clearer forward sightlines in motion. | Mario and the Blooper are readable at ordinary viewing size, steering is legible, and the destination remains visible. |
| 2 | Landing faces local scenery and enemies, leaving the park entrance off to the left. | Confirmed in baseline field 4620 and the subsequent beach walk. Improve landing orientation and provide a visible route to the entrance. | The next destination is apparent without opening instructions, and the player has time to orient before an enemy reaches them. |
| 2 | The Harbor route feels attached to the quay rather than integrated into its traffic and shoreline. | Existing footage shows the quay join but does not prove moving-boat clearance. Survey the complete boat route before relocating or reshaping the entrance. | Boats complete their full route without touching the addition; the player can approach naturally from both shores. The v7 circuit review subsequently verified clearance; geographic fit remains open. |
| 3 | The walkway reads as a long, bright, blank corridor with a large flat roof outside. | Confirmed in the v4 approach/interior captures. Shorten the enclosed visual stretch, reduce dominating roof surfaces, and improve the rhythm and shading of the architecture. | The route fits Sunshine's waterfront scale and materials from outside and inside, and its turns feel geographically motivated. |
| 3 | Finding and using the new connections depends on prior knowledge. | The ferry replay starts at the boarding point; first-time discovery has not been tested. Review an ordinary Plaza spawn, approach signs from both directions, and the visible waiting Blooper. | Someone unfamiliar with the mod can find, board, reach the park, and return using only in-game cues. |

## Initial instruction polish (v5 baseline)

- Split ferry destination and controls into two shorter lines, clear of the FLUDD gauge.
- Added a translucent backing so instructions remain legible over bright sand and water.
- Named the actual ferry destination **Pinna Beach**, since the park entrance is a separate onward journey.
- Added a brief arrival message directing players left along the beach toward the park entrance.
- Suppressed the boarding prompt during cooldown and while Mario is carrying something, being carried, or riding Yoshi, matching the existing boarding restrictions.

These changes improve instructions and orientation only.
The floating scenery, shoreline discontinuity, loading pause, camera framing, and Harbor placement are still open.

Validation of this small change:

- Rebuilt both Linux executables and standalone bundles; each bundle's executable prefix matches its rebuilt executable.
- The final 64-bit, 30 fps replay reaches the beach, walks through the native park gate, and demonstrates movement inside the park.
- The final 32-bit, 60 fps replay completes both ferry directions with audio, steering, and hopping.
- Inspected the rendered instructions on water and sand, plus the Plaza return view.
- Final first-load measurements remain 2.228 seconds and 5.950 seconds respectively: the instruction change does not improve loading.

Final replay evidence: `build/open-world/player-review-final64/` and `player-review-final32/`.
The illustrated review is `concepts/open-world-player-review-v1.png`, also copied into FileBrowser's `Render-Previews` folder.

## How to judge the next version

Review an uninterrupted recording at normal playback speed, including departure, the full ride, landing, the walk into the park, and the return trip.
Include a turn-back near the exchange, standing still at each landing, and looking toward the departure shore.
Keep freezes and visual discontinuities in the evidence.
Use an ordinary start to assess discovery separately from the scripted route replay.
Check both supported frame rates and the player's normal resolution and texture settings before making performance claims.
Prioritize these observations over successful state-transfer assertions or small frame-difference scores.
