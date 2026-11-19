# Standby Screen – Implementation Plan

> Target executor: an AI coding agent working in `d:\psp5_enhance` (psp5 launcher for PSP with ARK-4 / ARK-5).
> Read this whole file before writing code. Follow the phases **in order**; each phase must compile before the next.
> Code style: match the existing code (C99, 4-space indent, short Swedish comments are fine but English is OK too).
> Never break the existing Control Center, sleep, USB, or HOME-watchdog behaviour.

> [!IMPORTANT]
> **Out of scope – do NOT implement:** any change of CPU/bus clock frequency. Do not call `scePowerSetClockFrequency`,
> `scePowerSetCpuClockFrequency`, `scePowerSetBusClockFrequency`, `sctrlHENSetSpeed` or similar, and do not touch
> ARK's `SETTINGS.TXT` / `g_cfw.clock_*` for this feature. The clock stays exactly as the user configured it.

---

## 0. Feature summary (the spec)

A new Control Center item **"Standby Screen"**. When chosen:

1. A full-screen, theme-styled "always-on" screen appears: big clock (HH:MM), date, battery, and – if music is playing or paused – a "Now Playing" card (cover, title, artist, progress) with music controls.
2. **Screen brightness is lowered by exactly one step** (see Phase 2.2).
3. While the screen is shown the PSP **never auto-sleeps or turns the screen off** (neither psp5's own timers nor the system's).
4. **HOME is the only button that unlocks** (leaves the screen). Other buttons only do what is listed in the control table below.
5. The screen offers **Sleep** and **Shut down**.
6. On leaving (HOME, Sleep, Shut down, power switch, exit callback) the **exact brightness that was active before entering is restored** – before anything else happens.

### Controls inside the Standby Screen

| Button | Action |
|---|---|
| HOME | Unlock → restore brightness → back to the menu exactly where the user was |
| ✕ (or ○ if `confirm_circle`) | Play / Pause music (if nothing loaded and tracks exist: `music_play(ui->music_sel)`; if no tracks: `SND_ERROR`) |
| ← / → | `music_prev()` / `music_next()` (only if a track is current) |
| △ | Open/close the small **Power panel** (Sleep / Shut down) |
| Power panel: ← / → | Move between Sleep and Shut down |
| Power panel: ✕ | Sleep: immediate. Shut down: needs a second ✕ (same pattern as `cc_confirm`) |
| Power panel: ○ / △ | Close panel |
| Volume +/- (hardware) | Already handled by the system; the existing OSD (`osd_update`/`osd_draw`) shows the bar – just keep calling them |
| Anything else | Ignored (does **not** unlock) |

> Optional (only if the user confirms later): ↑/↓ change system volume via the kernel `sceImposeSetParam`. **Not part of this plan.**

---

## 1. Codebase facts you must know (verified)

| Topic | Where | Notes |
|---|---|---|
| Control Center items | `src/ui.c` ~L1925–2027 | `enum { CC_MUSIC, CC_SCREENSHOT, CC_SLEEP, CC_USB, CC_FILES, CC_SETTINGS, CC_XMB, CC_RESTART, CC_OFF, CC_COUNT }`, `cc_label()`, `update_cc()` with `static const int actions[CC_COUNT]` **(positional – must be updated together with the enum)** |
| CC drawing | `src/ui.c` ~L4195–4330 | `draw_cc_glyph()`, `cc_colors()`, `draw_cc_music()`, `draw_cc()`; layout uses `step = 48` and `CC_COUNT` |
| UI return codes | `src/ui.h` | `enum { UI_NONE, UI_LAUNCH, UI_EXIT, UI_SETTINGS, UI_ACTION }` |
| Main loop | `src/main.c` ~L510–664 | idle timers (`g_screen_off`, `g_auto_sleep`, `idle`, `dark`), sleep handling (`s_sleeping`, `s_resumed`, `sleep_and_wake`), `run_action()` |
| Input | `src/main.c` `read_pad(in, old, home)` | HOME only arrives when `home=1` (uses `sys_home_buttons()`); calling it also feeds the **HOME watchdog** heartbeat in `system.c` – it must be called at least every ~1 s or HOME is handed back to the system after 3 s |
| Power API | `src/system.c` | `sys_suspend()`, `sys_shutdown()`; kernel calls via `kcall()` + `sctrlHENFindFunction()`; display brightness pointers `s_light_get` / `s_light_set` (NIDs `0x31C4BAA8` / `0x9E3C6DC6`), resolved in `sys_home_init()` |
| Clock / date | `src/ui.c` `static int read_clock(ScePspDateTime*)` ~L2501 | handles PSP-1000/ARK-4 quirk – reuse it, don't re-implement |
| Battery | `src/ui.c` `static void draw_battery(...)` ~L2448 | reuse |
| Helpers (static in ui.c) | `clampf`, `smooth`, `with_alpha`, `mix`, `draw_note`, `draw_play_glyph`, `TEXT_MAIN`, `TEXT_DIM`, `LIGHT`, `DARK`, `MARGIN` | → **implement the Standby UI inside `ui.c`** so these can be reused |
| Music | `src/music.h` | `music_state/current/track/elapsed/duration/toggle_pause/next/prev/play/count/generation` |
| Cover art | `ui->cover`, `ui->cover_for`, `ui->cover_fade`, refreshed by `media_poll(ui)` (static, ui.c ~L1567) | call `media_poll` from the standby update so the cover follows track changes |
| Theme | `src/theme.h` | `theme_cur()`, `theme_backdrop()`, `ui_draw_calm_backdrop()`, `theme_toggle_on()`, `g_wallpaper` |
| Volume OSD | `src/osd.c` | `osd_update(dt)` + `osd_draw()` |
| Strings | `src/lang.h` / `src/lang.c` | new ids go **just before `S_COUNT`**; `lang.c` uses designated initialisers with 10 languages (EN, SV, DE, FR, ES, IT, NL, PT, RU, JA) |
| Sounds | `src/sound.h` | reuse existing ids (no new sounds) |
| Build | `tools/build.sh` (`wsl -d pspdev`) – targets: default (ARK-5), `ark4`, `test` | **all three must build** |
| Test scripts | `tools/tests/*.txt`, `src/testrun.c` | ops: `wait`, `press`, `hold`, `volume`, `battery`, `charging`, `sleep`, `resume`, `shot` |

---

## Phase 1 – Strings

**File:** `src/lang.h`, `src/lang.c`

1. Add to the enum, right before `S_COUNT`:
   - `S_STANDBY` – "Standby Screen"
   - `S_STANDBY_UNLOCK` – "Press HOME to unlock"
   - `S_POWER` – "Power"
2. Add entries in `lang.c` for all 10 languages. "Standby Screen" may stay in English in every language (it is the product name the user chose); translate the other two. Suggested EN/SV:
   - `S_STANDBY_UNLOCK`: "Press HOME to unlock" / "Tryck på HOME för att låsa upp"
   - `S_POWER`: "Power" / "Ström"
3. Reuse existing `S_SLEEP`, `S_SHUTDOWN`, `S_CONFIRM`, `S_PLAY`, `S_PAUSE`, `S_NOTHING_PLAYING`, `S_UNKNOWN_ARTIST`.

✅ Done when: builds, no missing-initializer warnings for the new ids.

---

## Phase 2 – Power layer (brightness, keep-awake)

**Files:** `src/system.h`, `src/system.c`

### 2.1 Public API (add to `system.h`)

```c
/* Standby Screen: sänker ljusstyrkan ett steg och sparar den exakta nivån innan –
 * sys_standby_leave() ställer tillbaka den. Säker att anropa flera gånger
 * (enter två gånger gör inget, leave utan enter gör inget). */
void sys_standby_enter(void);
void sys_standby_leave(void);
int  sys_standby_active(void);
/* Håller PSP:n vaken (systemets viloläge och skärmsläckning). Anropas ~1 gång/s. */
void sys_keep_awake(void);
```

State (static in `system.c`): `s_sb_active`, `s_sb_light` (-1 = unknown).

`sys_standby_enter()`: if already active → return; dim brightness (2.2); `s_sb_active = 1`.
`sys_standby_leave()`: if not active → return; restore brightness; `s_sb_active = 0`.

### 2.2 Brightness – "one step down"

- Use the existing `s_light_get` / `s_light_set` kernel pointers. If they are NULL (no ARK, e.g. PPSSPP) skip dimming silently.
- Read the current level `L` with `kcall(s_light_get, (u32)&level, (u32)&unk)`; save it in `s_sb_light`.
- New level = the next lower entry from the XMB brightness steps. Implement as a small table and pick the largest value `< L`; if `L` is at or below the lowest entry, keep `L` (never go darker than the lowest step / never black):
  ```c
  static const int LIGHT_STEPS[] = { 20, 40, 60, 80, 100 }; /* VERIFY on hardware via standby.log */
  ```
  Fallback if the table can't be trusted: `L * 3 / 4`, minimum 20.
- **Do not** touch `s_light` (used by `sys_display()` for screen-off) – keep a separate variable.
- Restore exactly `s_sb_light` on leave.
- Small debug log appended to `<g_data_dir>/standby.log` (cap ~4 KB, never while USB mode is active): model, original level `L`, new level, result of the set call. Needed to correct `LIGHT_STEPS` to the real values each model reports.

### 2.3 Keep awake

`sys_keep_awake()` → `scePowerTick(PSP_POWER_TICK_ALL)` (include `<psppower.h>`; `-lpsppower` is already linked). The caller calls it once per second while the standby screen is up.

✅ Done when: all three builds compile; nothing calls these functions yet.

---

## Phase 3 – Control Center entry

**File:** `src/ui.c`, `src/ui.h`

1. `src/ui.h`: add a return code at the end of the enum: `UI_STANDBY`.
2. Enum: insert `CC_STANDBY` **right after `CC_SLEEP`**:
   `CC_MUSIC, CC_SCREENSHOT, CC_SLEEP, CC_STANDBY, CC_USB, CC_FILES, CC_SETTINGS, CC_XMB, CC_RESTART, CC_OFF, CC_COUNT`
3. `cc_label()`: `case CC_STANDBY: return L(S_STANDBY);`
4. `update_cc()`: update the positional `actions[CC_COUNT]` array (add a `0` in the new slot!). Add:
   ```c
   case CC_STANDBY:
       ui->cc_open = 0;
       ui->cc_anim = 0;
       sound_play(SND_CC_CLOSE);
       return UI_STANDBY;
   ```
5. `draw_cc_glyph()`: add a clock icon for `CC_STANDBY` (ring: `gfx_circle(r=8.5,c)` + `gfx_circle(r=6.5,bg)`; hands: two `gfx_round_rect` bars from centre, e.g. up 5 px and right 4 px, width 1.8).
6. Layout: with 10 items `step = 48` gives 432 px – too tight on 480 px. Make the step depend on the count, e.g. `const float step = CC_COUNT > 9 ? 44 : 48;` and check the label clamp still works. Take a screenshot (Phase 7) to confirm nothing overlaps the screen edges.

✅ Done when: the new icon is visible/selectable; choosing it returns `UI_STANDBY` (main.c ignores it for now).

---

## Phase 4 – Standby UI (state, update, draw) inside `ui.c`

### 4.1 State in `Ui` (`src/ui.h`)

```c
/* Standby Screen */
float sb_time;       /* sekunder sedan skärmen öppnades (intoning) */
float sb_leave;      /* >0: tonar ut innan main.c lämnar läget */
int   sb_power_open; /* △-panelen Sleep / Shut down */
int   sb_power_sel;  /* 0 = Sleep, 1 = Shut down */
int   sb_confirm;    /* Shut down: ✕ en gång till */
float sb_power_anim;
```

### 4.2 API (`src/ui.h`)

```c
enum { SB_NONE, SB_UNLOCK, SB_SLEEP, SB_SHUTDOWN };
void ui_standby_open(Ui *ui);                                  /* nollställer sb_* */
int  ui_standby_update(Ui *ui, const UiInput *in, float dt);   /* SB_* */
void ui_draw_standby(Ui *ui);                                  /* hel bildruta: gfx_begin … gfx_end */
```

### 4.3 `ui_standby_update`

- `ui->time += dt; ui->sb_time += dt;` and call `media_poll(ui)` (keeps cover up to date).
- Ignore all input during the first 0.3 s (the ✕ that chose the item must not toggle music).
- `PSP_CTRL_HOME` pressed → return `SB_UNLOCK` (play `SND_CC_OPEN` or `SND_BACK`).
- If `sb_power_open`: ←/→ move (`SND_MOVE`), ○/△ close (`SND_CLOSE_MENU`), ✕: Sleep → `SB_SLEEP`; Shut down → first press sets `sb_confirm=1` (`SND_SELECT`), second → `SB_SHUTDOWN`. Reset `sb_confirm` on move/close and after 3 s.
- Else: ✕ = play/pause (same logic as `CC_MUSIC` in `update_cc`), ←/→ = `music_prev/next` if `music_current() >= 0`, △ = open power panel (`SND_OPEN`).
- Respect `ui->confirm_circle` for ✕/○ like the rest of the UI.

### 4.4 `ui_draw_standby` – look & feel (must match the system theme)

Layout for 480×272:
1. `gfx_begin(CLEAR_COLOR)`; background = `ui_draw_calm_backdrop(theme_cur(), ui->time * 0.3f, ui->accent, 1)` (slow animation, uses the exact active theme or user wallpaper).
2. **Clock** centred, y ≈ 95: `HH:MM` via `read_clock()`; large size (try `text_draw(..., 2.4f, 0, TEXT_MAIN, ALIGN_CENTER, ...)`, check intraFont sharpness on screenshot; if blurry use the largest crisp size). Colon may blink once per second (subtle, alpha 0.4↔1).
3. **Date** under the clock (y ≈ 122), `TEXT_DIM`, e.g. `"%02d/%02d/%04d"` from `ScePspDateTime` (day/month/year – keep simple, no localisation needed).
4. **Battery** top-right using `draw_battery()` + percentage (same as `ui_draw_top_bar`, without tabs).
5. **Now Playing card** (only if `music_current() >= 0 && music_state() != MUSIC_STOPPED`), compact square-like card centred under the clock (`168×140` at `y = 94`): cover art (`44×44`), title, artist, progress bar + timestamps, and transport controls (`[◀◀] (▶/||) [▶▶]`).
6. **Bottom hint row** (y ≈ 252): HOME icon/text `L(S_STANDBY_UNLOCK)` centred; on the right `△ L(S_POWER)` using `gfx_button(..., BTN_TRIANGLE, ...)`.
7. **Power panel** (when `sb_power_anim > 0`): small rounded panel above the hint row with two items (moon glyph + `S_SLEEP`, power glyph + `S_SHUTDOWN`; reuse `draw_cc_glyph(CC_SLEEP/CC_OFF, ...)`), white highlight on the selected item like the Control Center, label shows `L(S_CONFIRM)` when `sb_confirm`.
8. `osd_draw()` (volume bar).
9. Fade-in from black over 0.25 s (`sb_time`), fade-out with `sb_leave`.
10. `gfx_end()`.

✅ Done when: compiles; functions unused until Phase 5.

---

## Phase 5 – Main loop integration (`src/main.c`)

Add `static int standby_mode(EntryList *list, Ui *ui, u32 *old)` (returns `SB_*`) and call it when `ui_update` returns `UI_STANDBY`:

```c
if (action == UI_STANDBY) {
    int r = standby_mode(&list, &ui, &old);
    last = sceKernelGetSystemTimeWide();
    idle = 0; dark = 0;
    if (r == SB_SLEEP) { /* same as the CC sleep path: sys_suspend() + keep screen black */ }
    if (r == SB_SHUTDOWN) { run_action(SA_SHUTDOWN, &list, &ui); }
    continue;
}
```

`standby_mode` loop – rules:
1. On entry: `ui_standby_open(ui)`, `preview_pause(1)`, `sys_standby_enter()`, `sys_keep_awake()`.
2. Each iteration:
   - `read_pad(&in, old, g_control_center)` – **every iteration**, keeps the HOME watchdog alive. (Standby is only reachable from the Control Center, so `g_control_center` is 1 and HOME reaches us.)
   - compute `dt` like the main loop (clamp 0.05 s), `osd_update(dt)`.
   - every ≥1 s: `sys_keep_awake()`.
   - **psp5 idle timers are NOT evaluated here** (no screen-off, no auto-sleep).
   - if `!s_running` or `s_sleeping` (power switch / exit callback) → break with `SB_NONE` (main loop will run `sleep_and_wake` as usual).
   - `r = ui_standby_update(ui, &in, dt)`; if `r != SB_NONE` start a 0.2 s fade-out (`ui->sb_leave`), then break with `r`.
   - **Throttle drawing to save battery:** redraw only when ≥100 ms have passed since the last frame, or input/music state/minute changed, or a fade/panel animation is running. Otherwise `sceKernelDelayThread(33000)`. (`gfx_end` already waits for vblank.)
3. On exit, **always** (single exit point, no early `return` before this):
   - `sys_standby_leave()` – restores brightness **before** sleep/shutdown/anything else.
   - `preview_pause(0)`.
   - `*old = in.held` so the HOME press that unlocked does not reopen the Control Center.
4. Safety nets elsewhere in `main.c`:
   - Before `launch_entry(...)`, in `run_action()` (top), and before `sys_exit_xmb()` at the end of `main`: call `sys_standby_leave()` (no-op if not active).
   - In the `s_sleeping || s_resumed` block at the top of the main loop: call `sys_standby_leave()` too (covers the power switch while in standby).

✅ Done when: entering shows the screen, HOME returns to the menu, music controls work, sleep/shutdown work, brightness dims and comes back exactly.

---

## Phase 6 – Build

Run all three (via WSL, outside the sandbox if required):

```
wsl -d pspdev -- bash /mnt/d/psp5_enhance/tools/build.sh
wsl -d pspdev -- bash /mnt/d/psp5_enhance/tools/build.sh ark4
wsl -d pspdev -- bash /mnt/d/psp5_enhance/tools/build.sh test
```

Zero new warnings (`-Wall`).

---

## Phase 7 – Tests

### 7.1 PPSSPP (test build, `tools/ppsspp-test.ps1`)
1. Add `tools/tests/standby.txt`. If `testrun.c` has no `"home"` button name, add `{ "home", PSP_CTRL_HOME }` to its button table.
   Script idea: `wait 150` → `press home` → navigate to the Standby icon (`press right` ×3 from Music) → `shot 01-cc-standby` → `press cross` → `wait 40` → `shot 02-standby` → `press triangle` → `shot 03-power-panel` → `press circle` → `press cross` (music) → `wait 60` → `shot 04-standby-music` → `press home` → `wait 30` → `shot 05-back-menu`.
2. Check screenshots: CC row fits (10 icons), clock readable/crisp, theme colours, card, hints, power panel.
3. Repeat with 2–3 themes (`tools/tests/theme.txt` shows how) and with a wallpaper.

### 7.2 Real hardware checklist (ARK-5 and ARK-4, ideally PSP-1000/2000/3000/Go)
- [ ] Brightness drops one step and returns exactly; original level in `standby.log` → correct `LIGHT_STEPS` if needed.
- [ ] Music (MP3 and M4A) keeps playing smoothly on the screen.
- [ ] Leave the screen on 10+ min with system auto-sleep enabled and psp5 `autosleep`/`screenoff` enabled → PSP stays awake, screen stays on.
- [ ] HOME unlocks; other buttons don't. HOME watchdog never hands HOME to the system while on the screen.
- [ ] Sleep from the panel → wakes to the boot screen as today, brightness is the user's level afterwards.
- [ ] Shut down needs two ✕.
- [ ] Power switch while on the screen → normal sleep/wake, brightness restored.
- [ ] CPU clock is unchanged by the feature (same value before, during and after).
- [ ] Control Center, settings, USB, file manager, game launch unaffected.

---

## Phase 8 – Docs

- `README.md` (+ `docs/README.sv.md` if it lists features): add "Standby Screen" under Control Center features with one paragraph (clock, music controls, keeps the PSP awake, HOME to unlock).

---

## Risks / open points

| Risk | Mitigation |
|---|---|
| Brightness values differ per model | table + log + "never below lowest step" rule |
| Brightness not restored after an unexpected exit | single exit point + safety-net calls in `main.c` (Phase 5.4) |
| CC row overcrowded | dynamic `step` + screenshot check |
| HOME handed back to system by the watchdog | call `read_pad` every loop iteration even when not redrawing |
