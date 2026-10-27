/* psp5 – made by 08Weber */
#ifndef OSD_H
#define OSD_H

#include <psptypes.h>

/* Volymindikator och skärmbildsindikator i PS5-stil: syns en stund överst när
 * systemvolymen ändras eller en skärmbild sparas, var man än är i psp5. */

/* Letar upp systemets volymfunktion (kräver ARK). Anropas efter sys_init. */
void osd_init(void);
/* Läser volymen och räknar animationen – en gång per bildruta. */
void osd_update(float dt);
/* Ritar indikatorn om den syns – sist i bildrutan, före gfx_end. */
void osd_draw(void);

/* Begär att nästa färdigritade bildruta sparas som PNG i PICTURE/PSP5/. */
void osd_request_screenshot(void);
/* Anropas från gfx_end_with när bildrutan är färdigritad i VRAM. */
void osd_capture(const u32 *vram, int stride);
/* 1 en gång efter att en ny skärmbild har sparats (så att Bild-fliken kan uppdateras). */
int osd_take_screenshot_done(void);

#endif
