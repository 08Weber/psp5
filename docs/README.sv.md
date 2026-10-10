# psp5 – egen launcher för PSP med ARK-5

Mörk launcher med en rad spelkort, spelets bild som bakgrund och flikar för spel och appar.
All grafik är egen. Launchern ligger som vanlig app i `ms0:/PSP/GAME/PSP5` och rör aldrig flash0,
så XMB finns alltid kvar (HOME i psp5 = tillbaka till XMB).

## psp5 som ARK:s launcher
psp5 ersätter ARK-5:s egen launcher (arkMenu): filen `ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP` är psp5.
ARK:s "Custom Launcher" i XMB startar då psp5, och med **Inställningar → psp5 → Starta psp5 vid uppstart**
startar PSP:n direkt i psp5 i stället för XMB.

- **Nödutgång:** håll **SELECT** när PSP:n startar – ARK hoppar då över SETTINGS.TXT och du kommer till XMB.
  Håll **START** för att hoppa över alla plugins.
- **Gå till XMB** (Inställningar → Ström och USB) stänger av launcherläget för den sessionen.
- **Tillbaka till arkMenu:** kopiera `ARK_01234/VBOOT_ARKMENU.PBP` till `VBOOT.PBP` (kopia finns även i `backup/`).
- En ARK-uppdatering kan skriva över `VBOOT.PBP` – kopiera då in `dist/PSP/GAME/PSP5/EBOOT.PBP` som `VBOOT.PBP` igen.

## Språk
Alla texter finns i `src/lang.c` (engelska, svenska, tyska, franska, spanska, italienska, nederländska,
portugisiska, ryska, japanska). Språket följer PSP:ns systemspråk; svenska väljs manuellt under
Inställningar → psp5 → Språk (sparas i `ms0:/PSP5/psp5.cfg`).

## Ljud
Egna gränssnittsljud (`src/sound.c`) räknas fram vid start – inga ljudfiler och inga ljud från någon konsol.
Tick vid bläddring, swish vid flikbyte, toner för välj/tillbaka och ett ackord när ett spel startar.
Av/på under Inställningar → psp5 → Systemljud (sparas i `ms0:/PSP5/psp5.cfg`). WAV-förhandslyssning i `docs/sounds/`.

## Inställningar (△)
Ser ut som PS5:s inställningar och läser/skriver samma filer som arkMenu:
- **psp5:** autoboot, rensa bildcache, version
- **Prestanda:** CPU i spel / XMB, extra minne, minneskortscache, ISO-cache
- **System:** USB-laddning, WPA2, logotyper, dölj bilder/MAC/DLC, lampor, UMD, analogspak, regioner m.m.
- **Plugins:** slå av/på rader i `ARK_01234/PLUGINS.TXT` och `SEPLUGINS/PLUGINS.TXT`
- **Ström och USB:** USB-läge, XMB, viloläge, starta om, stäng av, återställningsmeny

CFW-inställningarna (`SETTINGS.TXT`) skrivs i exakt samma format som arkMenu och gäller efter omstart.
Okända rader och kommentarer behålls.

## Installera på PSP:n
1. Koppla in PSP:n med USB.
2. Kopiera `dist/PSP` till minneskortets rot (så att filen hamnar i `PSP/GAME/PSP5/EBOOT.PBP`).
3. Som ARK-launcher: kopiera samma fil till `PSP/SAVEDATA/ARK_01234/VBOOT.PBP` (spara arkMenu som `VBOOT_ARKMENU.PBP` först).
4. (Valfritt) Förbered omslag: `py tools\covers.py E:\` (E: = PSP:n).
5. Starta "psp5" från XMB → Spel → Memory Stick, eller via ARK:s Custom Launcher.

## Kontroller
| Knapp | Funktion |
|---|---|
| ← → / spaken | bläddra (håll för att gå snabbare) |
| L / R | byt flik (Spel / Appar) |
| ✕ eller START (○ med japansk knappinställning) | starta |
| △ | inställningar |
| HOME | tillbaka till XMB (systemets egen fråga) |

## Vad som visas
- `ms0:/ISO` (+ en nivå undermappar): `.iso`, `.cso`, `.zso` – fliken **Spel**
- `ms0:/PSP/GAME/*/EBOOT.PBP`: PSN- och PS1-spel under **Spel**, övrigt under **Appar**

Spel startas med samma systemctrl-anrop som ARK-5:s arkMenu (`src/launch.c`), inklusive
installerade uppdateringar (`PBOOT.PBP`), patchade ISO:er (`EBOOT.OLD`) och okrypterad `BOOT.BIN`.
Före start frigörs allt: laddartråd, texturer, typsnitt, GU och spellistan.

## Bilder och cache
Kort- och bakgrundsbilder letas i den här ordningen:
1. `ms0:/PSP5/covers/<nyckel>.ico|.bg` – från `tools/covers.py` (nyckel = DISC_ID för ISO, mappnamn för EBOOT)
2. `ms0:/PSP5/cache/` – skapas av launchern själv första gången ett spel visas
3. ICON0/PIC1 inuti spelet

Bara korten nära markeringen laddas (i en bakgrundstråd), och bakgrunden laddas först när
markeringen stått still en kort stund. Spellistan cachas i `ms0:/PSP5/cache/list.bin`, så oförändrade
filer läses inte om. Ta bort `ms0:/PSP5/cache` (eller Inställningar → Rensa bildcache) om något ser fel ut – den byggs upp igen.

### Egna omslag
Lägg bilder i `tools/covers-src/` (eller ange `--src`), namngivna efter spel-ID eller titel:
`ULUS10041.jpg` eller `Mitt Spel.png` → kortbild, `ULUS10041_bg.jpg` → bakgrund.
Bilderna beskärs automatiskt. Flaggor: `--force` (skriv om alla), `--clean` (ta bort omslag
för spel som inte finns kvar), `--dry-run`.

## Bygga (Windows + WSL1)
Verktygskedjan (pspdev + psp-cfw-sdk) ligger i WSL1-distron `pspdev` (`C:\WSL\pspdev`).
WSL1 används med flit – WSL2 slår på hypervisorn, som krockar med VMware.

```
wsl -d pspdev --exec bash /mnt/c/path/to/psp5/tools/build.sh
powershell -ExecutionPolicy Bypass -File tools\run-ppsspp.ps1      # testa i PPSSPP
```

| Skript | Gör |
|---|---|
| `tools/setup-wsl.sh` | installerar pspdev + paket i WSL (körs som root) |
| `tools/build.sh` | bygger till `dist/PSP/GAME/PSP5/EBOOT.PBP` |
| `tools/make_icon.py` | ritar psp5:s egen ICON0/PIC1 till `res/` |
| `tools/make_testdata.py` | påhittade testspel (iso/cso/zso/EBOOT) i `testdata/` |
| `tools/run-ppsspp.ps1` | kopierar till PPSSPP:s minneskort och startar |
| `tools/ppsspp-capture.ps1` | skickar tangenter till PPSSPP och tar skärmdump |
| `tools/covers.py` | förbereder omslag på minneskortet |
| `tools/build.sh test` | testbygge med skriptat testläge (`src/testrun.c`) till `dist-test/` |
| `tools/ppsspp-test.ps1` | kör ett testskript (`tools/tests/*.txt`) i PPSSPP och sparar skärmdumpar – utan tangenttryck |

PPSSPP saknar ARK:s systemctrl, så där visas "ARK hittades inte" när man trycker ✕.
Själva spelstarten måste provas på PSP:n.

## Källkod
| Fil | Innehåll |
|---|---|
| `src/main.c` | start, huvudloop, frigöring före spelstart |
| `src/ui.c` | layout, animationer, flikar |
| `src/gfx.c` | liten GU-renderare + intraFont-text |
| `src/loader.c` | bakgrundstråd som laddar bilder |
| `src/art.c`, `src/image.c` | omslag/cache, PNG, skalning, oskärpa, NTX-format |
| `src/scan.c`, `src/iso.c`, `src/sfo.c` | skanning, ISO/CSO/ZSO, PARAM.SFO |
| `src/launch.c` | start via ARK-5 systemctrl |
| `src/settings.c` | inställningsskärmen |
| `src/arkcfg.c`, `src/plugins.c` | ARK:s SETTINGS.TXT och PLUGINS.TXT |
| `src/system.c` | USB, ström, XMB, återställningsmeny |
