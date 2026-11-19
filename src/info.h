/* PSP5 REMAKE – Game / App Information (adapted from psp5-authentic info.c) */
#ifndef INFO_H
#define INFO_H

#include "ui.h"

/* The information screen of a game or app (options menu -> Information): name, type, where it
 * is, how big it is (an EBOOT game: the whole folder, counted in a thread so nothing freezes),
 * when it was changed, when psp5 last started it, the packing of a CSO/ZSO/DAX and the few
 * PARAM.SFO entries that matter. It reads files only when opened and never writes anything.
 * `list_index` is the game's index in the game list (for its picture); `confirm_circle` is the
 * PSP's button assignment (back = the other button). */
void info_open(const Entry *e, int list_index, int confirm_circle);
/* Once per frame. 1 = close the screen. */
int info_update(const UiInput *in, float dt);
void info_draw(void);
/* Stops the background count of the folder size. Safe to call at any time. */
void info_close(void);

#endif
