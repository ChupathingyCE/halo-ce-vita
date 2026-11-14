/*
FORGE_MODES.H

The game modes made in Forge (the design:
docs/superpowers/specs/2026-11-05-forge-warzone-firefight-design.md):
FIREFIGHT (co-op against the AI's waves, rounds and sets) and WARZONE (red
and blue against each other and the AI, its bases, bosses and cores). Their
markers are Forge objects (forge_mode_markers.inc), saved in the map variant
(forge_files.h version 5).
*/

#ifndef __FORGE_MODES_H
#define __FORGE_MODES_H

/* ---------- constants */

/* the modes' names, as the menus and the HUD say them (the owner, Nov 5:
one lang key each) */
#define FORGE_FIREFIGHT_NAME "FIREFIGHT"
#define FORGE_WARZONE_NAME "WARZONE"

#endif
