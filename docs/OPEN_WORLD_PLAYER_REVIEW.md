# Open-world player experience review

The connection works, but the current journey still looks unfinished to a player.
Successful arrival, preserved health, and an absence of black frames do not establish that the world feels continuous.
The highest priority is a believable departure and arrival, followed by uninterrupted movement and clear orientation.

**v6 follow-up:** the missing beach sand has been restored, the map exchange moved offshore, and the unrelated cannon departure sequence removed from ferry travel.
Recorded outbound trigger-to-arrival time dropped to 59 ms in the 64-bit replay and 67 ms in the 32-bit, 60 fps replay with audio.
A subsequent run with saved graphics settings and the installed texture pack measured 115 ms outbound and 173 ms return until the destination frame was ready to display.
The findings below describe the reviewed v5 baseline; return-skyline continuity and Harbor placement remain open. Boat clearance was subsequently verified in v7, as described below.

## v8 architecture review

Reviewed the working Harbor route from inside the passage and from a fixed waterfront camera, plus native views of alternative Plaza and Harbor shoreline locations.
A relocation beside the Plaza bell platform and the Harbor's eastern quay was rejected: the same long shell still projected over the water, and the Harbor end intersected the native cliff backdrop.
The working entrance positions remain in place. A relocation needs a coherent terrain and scenery solution, not another coordinate change.

The retained architecture pass uses native roof tiles, masonry extending to the water, continuous stone ribs down the passage walls, and softer interior shading.
Mario and the curve ahead are easier to distinguish from the ceiling and walls.
The exterior still exposes the route's abrupt end; new materials do not resolve its geographic fit.
The first-time discovery review and missing scenery across the ferry exchange remain open.
A close review of the Harbor-to-Plaza handoff also shows a small camera/pose change when drawing resumes; passing the continuity threshold does not make that change invisible.

The player guide now puts launching, visible route locations, Blooper controls, and current limitations before build instructions.
Implementation history and old performance measurements have moved to `OPEN_WORLD_DEVELOPMENT.md`, where their version is explicit.
This prevents obsolete multi-second loading notes from appearing as current launch guidance.

Native walking replays passed both directions at 30 and 60 fps, including water consumption and audio on the 32-bit run.
The 32-bit capture completed all checks before its harness exited with a termination signal; its saved captures and log were validated separately.
All three native boat routes also completed full circuits with no contact against the expanded masonry; the existing 100-unit hull margin was retained.
A subsequent swimming replay confirmed that Mario stops against the new foundation instead of passing through its visible wall.
Evidence: `build/open-world/v8/architecture64`, `architecture32`, `architecture-exterior`, `boats-final`, `swim-foundation`, and `transition-final32`.
The review image and continuous walking capture are `harbor-architecture-review-v8.png` and `.mp4` in FileBrowser's `Render-Previews` folder.

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
