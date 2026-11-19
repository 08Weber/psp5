/* PSP5 REMAKE – Saved Data Manager */
#ifndef SAVEDATA_H
#define SAVEDATA_H

#include "ui.h"

enum { SAVE_DEV_MS0, SAVE_DEV_EF0 };
enum { SAVE_OK, SAVE_INCOMPLETE, SAVE_CORRUPTED, SAVE_PROTECTED };
enum { SAVE_SORT_DATE, SAVE_SORT_NAME, SAVE_SORT_SIZE, SAVE_SORT_COUNT };
enum { SAVE_DEL_OK, SAVE_DEL_PROTECTED, SAVE_DEL_INVALID, SAVE_DEL_FAILED };

/* Opens the Saved Data Manager.
 * If filter_game_id is non-NULL and non-empty, starts in filtered mode for that game.
 * confirm_circle follows sys_confirm_is_circle(). */
void savedata_open(const char *filter_game_id, const char *filter_game_title, int confirm_circle);

/* Once per frame. Returns 1 when the screen should close. */
int savedata_update(const UiInput *in, float dt);
void savedata_draw(void);

/* Stops background threads and frees all loaded data and icon textures. */
void savedata_close(void);

/* Safely deletes a specific saved data folder inside ms0:/PSP/SAVEDATA/ or ef0:/PSP/SAVEDATA/.
 * Refuses protected ARK folders (e.g. ARK_01234) and any path outside PSP/SAVEDATA/. */
int savedata_delete_folder(const char *path);

#endif
