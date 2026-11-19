/* PSP5 REMAKE – when each game was last launched by psp5 (ported from psp5-authentic lastplayed.c) */
#ifndef LASTPLAYED_H
#define LASTPLAYED_H

#include <psptypes.h>
#include "scan.h"

/* Times are packed local times (datetime.h dt_pack(): year-1980 << 26 | ...), 0 = not known.
 * Only psp5 itself writes them: lastplayed_note_launch() when it starts a game. A game started
 * from the XMB or before this feature existed has no time (nothing is invented).
 * Stored in <data dir>/lastplayed.txt, one line per game: "<time> <key>" (key = entry_cover_key()). */
void lastplayed_load(const char *dir);
u32 lastplayed_now(void);
/* 0 when not known. A disc without a disc ID has none. */
u32 lastplayed_time(const Entry *e);
void lastplayed_note_launch(const Entry *e);

#endif
