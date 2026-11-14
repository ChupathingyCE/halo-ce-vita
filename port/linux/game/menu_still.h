#ifndef __MENU_STILL_H
#define __MENU_STILL_H

/* (port) the live background's still, the loading screen's picture when the
player has no retail default.xbe one (menu_still.c) */

#define MENU_STILL_WIDTH 320
#define MENU_STILL_HEIGHT 240

/* whether the still is kept (t:\menu_still.bin) */
int menu_still_cached(void);
/* (menu_backdrop.c, the live background up and settled at the main menu)
the next frame read back without the menus' widgets, when none is kept */
void menu_still_request(void);
/* (render_ui_widgets) TRUE: this frame's widgets left out - the still's */
int menu_still_hide_interface(void);
/* (the devices' presents) TRUE once: this present's back buffer is the
still's, given to menu_still_store (on the Vita by its worker) */
int menu_still_present_wanted(void);
/* the back buffer's pixels, B G R A bytes, rows from the top, pitch bytes
apart: box-filtered to 320x240 */
void menu_still_store(const unsigned char *pixels, unsigned long pitch, unsigned long width, unsigned long height);
/* (menu_backdrop.c, every frame at the main menu) the still read written */
void menu_still_update(void);
/* the kept still into the loading texture's 320x240 A8B8G8R8 rows, pitch
bytes apart: TRUE if it was there */
int menu_still_load(unsigned long *pixels, long pitch);

#endif
