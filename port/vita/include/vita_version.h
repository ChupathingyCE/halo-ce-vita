/*
VITA_VERSION.H

The version the Vita side says it is: the game's (its VERSION) and the
Vita's build count after it, "0.9.2 (build 1)", as the other Warthog ports
say theirs; port/vita/build.py passes it as HALO_VERSION.
*/

#ifndef VITA_VERSION_H
#define VITA_VERSION_H

#ifdef HALO_VITA_VERSION_TEXT
#define HALO_VITA_VERSION HALO_VITA_VERSION_TEXT
#else
#define HALO_VITA_VERSION "unknown"
#endif

#endif
