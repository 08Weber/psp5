/* psp5 – made by 08Weber */
#ifndef OSD_H
#define OSD_H

/* Volymindikator i PS5-stil: syns en stund överst när systemvolymen ändras eller
 * ljudet stängs av, var man än är i psp5. */

/* Letar upp systemets volymfunktion (kräver ARK). Anropas efter sys_init. */
void osd_init(void);
/* Läser volymen och räknar animationen – en gång per bildruta. */
void osd_update(float dt);
/* Ritar indikatorn om den syns – sist i bildrutan, före gfx_end. */
void osd_draw(void);

#endif
