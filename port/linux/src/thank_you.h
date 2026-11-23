/*
THANK_YOU.H

1.1.3's thank-you note from the owner (thank_you.c): shown once on each
install at the main menu, and again from the settings panel's Thank you row.
*/

#ifndef THANK_YOU_H
#define THANK_YOU_H

/* the note's title and text, in the player's language */
const char *thank_you_title(void);
const char *thank_you_text(void);

/* shows the note (platform_show_message), `why` in halo.log */
void thank_you_show(const char *why);

/* (the game's thread, each frame: system_link_shortcut.c) `ready`: the main
menu up with nothing over it; the first time it has been for a few seconds,
the note shown and remembered (thank_you.txt in the data folder) */
void thank_you_update(int ready, unsigned long now_milliseconds);

#endif
