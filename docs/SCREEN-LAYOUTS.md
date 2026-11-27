# Screen layout review (GMSE01)

Screen edges is opt-in. Window spends the extra rows or columns from the drawable; the HUD is not enlarged. Fixed wide formats only have extra columns. Original 4:3, 16:9 area and Horizontal edges keep their drawing paths. Window at exact 16:9 uses the reference layout. GuideWiden already owns its full-width Guide art and hit geometry.

Reviewed against the disc archive directories, pane hierarchy and the drawing owners in `decomp/src/GC2D` and `src`. Names are filenames, not guesses from the current quadrant. Run `python3 tools/check_layouts.py --disc path/to/GMSE01.iso`; without a disc the check covers source references only.

| Resource | Class | Rule in Screen edges |
|---|---|---|
| `standard_1.blo` | gameplay HUD | Counters top left, lives top centre, health top right, FLUDD/juice bottom right, timed and special counters bottom left, D.E.B.S. bottom. See `hudDock`. |
| `big_tx_1.blo` | content | GO, SHINE and TOO BAD letter animation stays centred. |
| `message_2.blo` | content | Dialogue and choices stay together. |
| `message_board_1.blo` | content | Keep the sign and its text together. |
| `pause_1.blo` | menu | Dock the bottom episode group and native port hints; keep PAUSE, choices and cursor centred. |
| `load.blo` | menu | Dock the header and instructions; keep card choices and dialogs centred. |
| `load_score.blo` | content | Scores stay in their table. |
| `option.blo` | menu | Dock the heading; keep the selection and value previews in the reference frame. |
| `scenario_select_1.blo` | menu | Top and bottom masks span width; dock the stage heading. Keep episode and Shine choices centred. |
| `guide_1.blo` | menu | Leave stock GuideWiden in charge: paper, tabs, island, cursor and location panels stay registered. |
| `scenario_demo_1.blo` | full-screen | Leave the masks and the stage name to the existing stage-entry renderer. |
| `save.blo` | content | Save confirmation and buttons stay centred. |
| `save_end.blo` | content | Game-end save dialog stays centred. |
| `demo_1.blo` | content | Subtitle stays with movie. |
| `demo_2.blo` | content | Alternate subtitle stays with movie. |
| `ending_1.blo` | content | End card stays in the movie frame. |
| `title_1.blo` | content | Animated title and start prompt keep their composition. |
| `title.blo` | content | Menu director title stays centred. |
| `nintendo.blo` | content | Logo stays in native frame. |
| `title5.blo` | content | Title archive variant, retain native frame. |
| `title6.blo` | content | Title archive variant, retain native frame. |
| `test_save_1.blo` | content | Disc archive only; no source consumer, retain native frame. |
| `menu_2.blo` | content | Disc archive only; no source consumer, retain native frame. |

| Native owner | Class | Rule |
|---|---|---|
| `SettingsMenuDraw` | menu | Dock the heading, position and help; centre rows. |
| `SaveStateMenuDraw` | menu | Dock heading and help; centre list and previews. |
| `PORT_SETTINGS_PAGE_GRAPHICS` | menu | Centred settings list. |
| `PORT_SETTINGS_PAGE_DISPLAY` | menu | Centred settings list. |
| `PORT_SETTINGS_PAGE_GAMEPLAY` | menu | Centred settings list. |
| `PORT_SETTINGS_PAGE_CONTROLS` | menu | Centred settings list. |
| `PORT_SETTINGS_PAGE_HUD` | menu | Centred settings list. |

| Non-BLO owner | Class | Rule |
|---|---|---|
| `PauseTrackerDraw` | menu | Dock the tracker at bottom left as a group. |
| `RadarDraw` | gameplay HUD | Fit the radar and caption in one lane; its map symbols move with it. |
| `drawFullMap` | content | Keep the map and its world markers registered. |
| `GXPC_SetSplitView` | split-screen | The split viewport maps each player's draw after its HUD transform. |
| `GXPC_SetStretch2D` | full-screen | Fades, flashes and loading cover the expanded frame. |
| `THP` | content | Native movie composition; black outside. |
| `Nameplates` | world-attached | Follow the world projection, not the HUD anchors. |

Non-BLO owners: `settings_menu.cpp` docks the heading, position and help/footer but not the rows. `savestate_menu.cpp` keeps previews and list centred. Pause hints and `pause_tracker.cpp` belong at opposite corners. Radar belongs to its HUD corner; radar markers and multiplayer nameplates projected from the world are world-attached. Co-op uses a split-screen viewport after the HUD transform, not a second layout pass. Fades, flash and loading are full-screen; THP content stays in its native frame with black outside.
