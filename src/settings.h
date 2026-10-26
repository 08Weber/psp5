/* psp5 – made by 08Weber */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "ui.h"

/* Det inställningsskärmen ber huvudprogrammet att göra */
enum {
    SA_NONE, SA_CLOSE, SA_USB, SA_XMB, SA_RESTART, SA_SUSPEND, SA_SHUTDOWN, SA_RECOVERY, SA_CLEAR_CACHE, SA_FILES,
};

/* Läser ARK-inställningar och plugins och visar första kategorin. */
void settings_open(void);
int settings_update(const UiInput *in, float dt);
void settings_draw(void);
/* Sparar ändrade filer. Returnerar 1 om CFW-inställningar ändrades (gäller efter omstart). */
int settings_commit(void);

#endif
