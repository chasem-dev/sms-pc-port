# sms-port

Native PC port of Super Mario Sunshine (GMSE01), built from the matching decompilation.

- `decomp/` — the decompilation (git submodule, branch `local/decomp-progress`). Game and JSystem source come from here; its GameCube build stays byte-identical to retail and is the reference.
- `platform/` — host replacements for the Dolphin SDK surface the game calls (GX, OS, DVD, PAD, VI, CARD, AI/DSP/AR). See `decomp/docs/progress/port-scope/REPORT.md` for the API list.
- `src/` — port entry point, compat header, glue.
- Assets are never stored here: the port reads the user's own disc image / extracted disc at run time.

PC-only changes to game source go into the decomp behind `#ifdef TARGET_PC` (or compile identically under MWCC), verified there by `ninja changes_all` and the DOL hash.
