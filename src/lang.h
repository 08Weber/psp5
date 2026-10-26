/* psp5 – made by 08Weber */
#ifndef LANG_H
#define LANG_H

/* Språk. LANG_AUTO följer PSP:ns systemspråk (svenska finns inte som systemspråk
 * på PSP, så det väljs manuellt i inställningarna). */
typedef enum {
    LANG_AUTO, LANG_EN, LANG_SV, LANG_DE, LANG_FR, LANG_ES, LANG_IT, LANG_NL, LANG_PT, LANG_RU, LANG_JA,
    LANG_COUNT
} LangId;

typedef enum {
    S_TAB_GAMES, S_TAB_APPS, S_LAUNCH, S_HINT_TAB, S_SETTINGS,
    S_NO_GAMES, S_NO_APPS, S_HINT_GAMES, S_HINT_APPS, S_NO_ARK, S_LOADING, S_SAVED, S_LAUNCH_FAILED,
    S_USB_MODE, S_USB_CONNECTED, S_USB_WAITING, S_USB_EJECT, S_USB_EXIT,
    S_SELECT, S_BACK, S_YES, S_NO, S_CONFIRM, S_OFF, S_AUTO, S_STANDARD, S_ALL,
    S_CAT_PERF, S_CAT_SYSTEM, S_CAT_PLUGINS, S_CAT_POWER,
    S_DESC_PSP5, S_DESC_PERF, S_DESC_PLUGINS,
    S_BOOT_PSP5, S_SOUNDS, S_LANGUAGE, S_IS_LAUNCHER, S_CLEAR_CACHE, S_VERSION,
    S_CPU_GAME, S_CPU_XMB, S_EXTRA_MEM, S_MS_CACHE, S_ISO_CACHE,
    S_USB_CHARGE, S_WPA2, S_SKIP_LOGOS, S_HIDE_PICS, S_HIDE_MAC, S_HIDE_DLC, S_NO_LED, S_NO_UMD,
    S_NO_ANALOG, S_VITA_MUTE, S_QA_FLAGS, S_UMD_REGION, S_XMB_REGION, S_NO_PLUGINS,
    S_TO_XMB, S_SLEEP, S_RESTART, S_SHUTDOWN, S_RECOVERY,
    S_LOGOS_GAME, S_LOGOS_BOOT,
    S_R_JAPAN, S_R_AMERICA, S_R_EUROPE, S_R_KOREA, S_R_UK, S_R_LATAM, S_R_AUSTRALIA, S_R_HONGKONG,
    S_R_TAIWAN, S_R_RUSSIA, S_R_CHINA,
    S_RL_PSP, S_LANG_AUTO,
    S_TAB_LIBRARY, S_LIB_ALL, S_SORT, S_SORT_NAME, S_SORT_RECENT, S_SORT_XMB,
    S_HIDE_DUPES, S_BATTERY, S_BAT_ICON, S_BAT_PERCENT, S_BAT_TIME, S_PREVIEWS, S_WALLPAPER, S_BOOT_ANIM, S_HEALTH,
    S_TAB_MUSIC, S_NO_MUSIC, S_HINT_MUSIC, S_NOW_PLAYING, S_NOTHING_PLAYING, S_PLAY, S_PAUSE, S_TRACK,
    S_UNKNOWN_ARTIST, S_LOADING_MUSIC, S_SAFE_START, S_NO_PAUSE, S_HIB_BLOCK,
    S_CAT_THEME, S_DESC_THEME, S_THEME_ART,
    S_MOVE_TO_GAMES, S_MOVE_TO_APPS, S_MOVED_TO_GAMES, S_MOVED_TO_APPS, S_OPTIONS,
    S_FILES, S_F_OPEN, S_F_MARK, S_F_COPY, S_F_MOVE, S_F_PASTE, S_F_RENAME, S_F_DELETE, S_F_NEWDIR, S_F_CANCEL,
    S_F_EMPTY, S_F_ITEMS, S_F_COPYING, S_F_MOVING, S_F_DELETING, S_F_DONE, S_F_FAILED, S_F_NO_SPACE,
    S_F_CANCELLED, S_F_FREE, S_F_MS, S_F_INTERNAL, S_F_BAD_NAME,
    S_CATEGORIES, S_NO_CATEGORIES, S_SHUFFLE_ON, S_SHUFFLE_OFF, S_SHUFFLE, S_SORT_PLAYED, S_SORT_CUSTOM, S_PIN, S_UNPIN,
    S_SCREEN_OFF, S_AUTO_SLEEP, S_SET_WALLPAPER,
    S_WALLPAPER_SET,
    S_COUNT
} StrId;

/* Valt språk (LANG_AUTO … LANG_JA), sparas i psp5.cfg */
extern unsigned char g_lang_setting;

/* Bestämmer aktuellt språk från inställningen och systemspråket. */
void lang_apply(void);
/* Text på aktuellt språk */
const char *L(StrId id);
/* Språkets eget namn ("Svenska", "Deutsch", …), eller "Automatiskt" på aktuellt språk för LANG_AUTO */
const char *lang_name(int id);
/* Kortkod för psp5.cfg ("auto", "en", "sv", …) */
const char *lang_code(int id);
int lang_from_code(const char *code);
/* 1 om aktuellt språk behöver det japanska typsnittet (japanska, ryska) */
int lang_needs_cjk(void);

#endif
