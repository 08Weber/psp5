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

## Version 1.6.0

Önskemål från Reddit. Det som inte alla vill ha är av som standard.

- **Förhandsvisning som i XMB** (`src/preview.c`, Inställningar → psp5 → Spelförhandsvisning, av som standard):
  står man stilla på ett kort i Spel eller Appar i 0,9 s spelas spelets ICON1.PMF i kortet (`sceMpeg`, 8888,
  alfa sätts till fullt) och SND0.AT3 (`sceAtrac`, egen ljudkanal). Styrtråd + en tråd för video och en för ljud;
  musikspelaren går före. Pausas i inställningarna och vid viloläge, stoppas före spelstart. Offset och storlek
  för ICON1/SND0 sparas i `Entry` vid skanningen (PBP-huvudet, ISO:ns `PSP_GAME/`, UMD-skivan).
- **Dubbletter döljs** (`is_dupe` i `ui.c`, `hidedupes=1`): samma spel-ID och titel visas en gång. UMD-skivan räknas inte.
- **DAX** skannas i `ISO/` (libcisoread läser formatet, ARK startar det).
- **Minis:** ikoner smalare än 1,5:1 sträcks inte – de centreras på en suddig, mörkare kopia (`fit_icon`).
  Cachade ikoner tas bort en gång (`cache/icons2`).
- **Batteri:** procent eller tid kvar bredvid batteriet (`battery=1/2`).
- **Egen bakgrundsbild** (`wallpaper=`): en bild ur `PICTURE` (en nivå undermappar) ersätter temats yta när spelet
  saknar egen bild. Beskärs till skärmen och sparas i cachen (`.wal`).
- **Klockan på PSP-1000/ARK-4** efter att batteriet tagits ur: om `sceRtcGetCurrentClockLocalTime` ger fel räknas
  tiden med systemets tidszon och sommartid, i sista hand den råa klockan (`read_clock`).
- **Toppraden** dimmas inte längre i Bibliotek och Musik (hinnan ritas före toppraden).
- `tools/ppsspp-test.ps1` tar `-Exe` och `-Memstick` för en portabel PPSSPP, och CI laddar upp testbygget.

## Version 1.5.4

- **PSP Go: spel startar från internminnet** (#6, #8, #11): startades psp5 från internminnet pekar ARK om `ms0:`
  dit, men bara under psp5:s session. Då skannas `ef0:` direkt, så att spelen startas med Go-lägena. Homebrew på
  `ef0:` startas med 0x152 som i arkMenu (där `redirect_ms0` är på som standard).
- **Launcherläget** slås på igen direkt när inställningarna öppnas (#8), inte först efter första spelstarten.
- **USB-läget** stängs inte direkt längre: knappen som öppnade det ignoreras även när ○ bekräftar, och ett fel
  från `sceUsbActivate` ignoreras som i arkMenu (#10).
- **Snabbare bakgrund:** bilder ritas i remsor om 64 texlar i stället för en stor sprite (#9).
- **Sortering som i XMB** (#7): Inställningar → psp5 → Sortera. Nyaste mappen först, alltså ordningen Homebrew
  Sorter sätter (`sortxmb=1` i `psp5.cfg`).
- **Öppningsljudet** spelas efter att inställningarna lästs in, inte under (#2).
- **Toppraden:** kugghjulet är borta och laddningsblixten är större.

## Version 1.5.3

- **Kategorimappar:** en mapp i `PSP/GAME` utan `EBOOT.PBP` (t.ex. `GAME/PS1/<spel>` eller
  `GAME/Emulators/TempGBA`) genomsöks en nivå ner, som arkMenu gör (`scan_game_dir`, `depth`). Förut syntes
  PS1-spel och appar i sådana mappar inte alls.

## Version 1.5.2

- **Flytta mellan Spel och Appar:** □ på ett kort i Spel eller Appar öppnar en liten alternativmeny (`draw_menu`)
  med kortets namn och *Flytta till Spel/Appar*. ✕ väljer, ○ eller □ stänger, så inget flyttas av misstag. Sparas
  i `<data>/moved.txt`, en rad per kort med `entry_cover_key` (mappnamnet för EBOOT, spel-ID för ISO). UMD-skivan
  har ingen meny. Tipset "□ Alternativ" står svagt bredvid Starta-knappen.
- **Filhanterare** (`src/files.c`, Inställningar → Ström och USB): körs som USB-läget, med resten av psp5 stängt
  (`files_mode` i `main.c`), och spellistan läses om efteråt. ms0: och på PSP Go även ef0: (enhetslista överst),
  aldrig flash0. Mappar först, sorterat på namn; □ markerar, △ (eller ✕ på en fil) öppnar menyn: Klistra in här,
  Kopiera, Flytta, Byt namn, Radera, Ny mapp. Kopiera/flytta/radera körs i en egen tråd (`worker`) med förlopp och
  ○ för att avbryta; en avbruten eller misslyckad kopia städas bort, och ledigt utrymme kontrolleras först
  (`sceIoDevctl` 0x02425818). Flytt på samma enhet är ett `sceIoRename`, annars kopiera + radera. Finns namnet
  redan blir det "namn (2)". Radera frågar alltid, med Avbryt förvalt. Namn skrivs med PSP:ns tangentbord
  (`sceUtilityOsk*`, via `gfx_end_with`). Ledigt utrymme visas med en tunn stapel uppe till höger.

## Version 1.5.1

- **Volymindikator i PS5-stil** (`src/osd.c`): volymknapparna går direkt till systemet, så psp5 läser volymen
  (0–30) och ljud av/på med kärnans `sceImposeGetParam` – ARK hittar funktionen (`sctrlHENFindFunction`, ARK
  översätter NID:en 0x531C9778 för 6.60/6.61, annars prövas 0xDC3BECFF) och kubridges `kuKernelCall` anropar
  den. Ändras värdet glider en stapel in överst i 1,6 s, i alla flikar, inställningarna och USB-läget. Utan ARK
  (PPSSPP) visas inget.
- **Spola i musiken** (`seek_to` i `src/music.c`): kort tryck på ← / → byter låt när knappen släpps, håll i
  0,35 s för att spola (8, sedan 20 och 45 sekunder per sekund). Avkodaren startas om vid rätt byte i filen:
  Xing-tabellen för VBR, annars proportionellt, och sedan första giltiga bildrutan (samma version, lager och
  frekvens som låtens första, och en giltig bildruta direkt efter). De första 8 bildrutorna efter ett hopp får
  misslyckas utan att låten byts: PSP:ns avkodare svarar `MP3_END` (0x80671402) i stället för ett fel på en
  bildruta som saknar data från bildrutorna före (bitreservoaren). Spolningen stannar en sekund före slutet.
- **Omslag i musiken** (`art_load_cover`, `music_cover_data`): ur ID3-taggens APIC/PIC (helst framsidan, v2.2–2.4
  med unsynchronisation), annars `cover.jpg`, `folder.jpg`, `front.jpg`, `album.jpg`, `cover.png` eller
  `folder.png` i albummappen. JPEG avkodas med libjpeg-turbo (skalas ner redan i avkodaren), PNG med libpng.
  64×64 med rundade hörn, sparas i `cache/` per album och laddas i laddartråden. `loader_stop` körs nu före
  `music_term`, eftersom laddaren läser musiklistan.
- **Laddningsanimation:** medan batteriet laddas sveper fyllningen från nivån upp till fullt, med en blixt bredvid.
- Testskript `tools/tests/media.txt` och nya testkommandon: `hold KNAPP:N`, `volume N`, `charging 0/1`, `battery N`.

## Version 1.5.0

- **Teman** (`src/theme.c`, ny flik *Tema* i inställningarna): PS5 (som förut), PS5:s konsolskal (Midnight
  Black, Cosmic Red, Nova Pink, Starlight Blue, Galactic Purple, Deep Earth-serien Volcanic Red/Cobalt Blue/
  Sterling Silver och Chroma Indigo/Teal/Pearl) och PS1–PS4 som PS5:s inställning *Utseende*: PS1 grå med
  handkontrollens symboler, PS2 mörk med blå torn och klot, PS3 med XMB-vågen och PS4 blå med vågor och
  partiklar. Allt ritas i kod. Markerat tema förhandsvisas i inställningarna, ✕ väljer det. Temat färgar
  bakgrunden (när spelet saknar bild), inställningarna, USB-läget, laddningsskärmen, platshållarkorten,
  reglagen, musikens förlopp och laddningsprickarna. *Spelens bilder i bakgrunden* kan stängas av – då syns
  temat alltid och inga bakgrundsbilder laddas. Sparas i `psp5.cfg` (`theme=`, `themeart=`).
- **Snabbare ritning:** inställningarna bröt om kategoribeskrivningen (många `text_width`) för varje rad i varje
  bildruta – nu bara när texten byts. Flikarnas och knapptipsens bredd mäts bara om när något ändras.
  `gfx.c` binder inte om samma textur (och tömmer inte texturcachen) för varje form i rad. Ytan under
  spelets bild ritas inte när bilden täcker hela skärmen. Ikonens medelfärg räknas direkt ur texturen
  i stället för på en kopia (46 KB mindre att kopiera per ikon).
- **Rättat:** en bakgrundsbild som laddades klart precis när markeringen flyttade kastades och laddades sedan
  aldrig igen om man gick tillbaka till spelet (`s_bg_last` i `src/loader.c`). En trasig katalogpost i en ISO
  (för kort post eller för långt namn) kunde läsas utanför sektorbufferten (`src/iso.c`).
- Testskript `tools/tests/theme.txt`; `settings.txt` och `lang.txt` har ett steg till för den nya fliken.

## Version 1.4.0

- **PSP Go** – samma EBOOT på alla modeller: `sys_set_root` (`src/system.c`) väljer `g_root` efter argv[0].
  Startas psp5 från `ef0:` (PSP Go:s internminne) ligger psp5:s filer (`ef0:/PSP5`), standardmappen för ARK och
  `SEPLUGINS` där, och tipstexterna visar `ef0:/`. Spel (`ISO`, `PSP/GAME`) och musik (`MUSIC`) söks alltid både
  på `ms0:` och `ef0:`, som arkMenu – så även ett Memory Stick Micro i en PSP Go. Spel på `ef0:` startas med ARK:s
  apityper för internminnet (0x125/0x126 ISO och PSN, 0x155 PS1; homebrew som vanligt 0x141), och uppdateringar
  (`PBOOT.PBP`) söks på båda. Ingen UMD-tråd på PSP Go (modell 4). Tidigare letade psp5 bara på `ms0:`, så
  spel i `ef0:/PSP/GAME` och musik i `ef0:/MUSIC` syntes inte på PSP Go.
  Release-zipparna `psp5-v1.4.0-PSPGo-ARK5.zip` och `-PSPGo-ARK4.zip` har samma EBOOT som ARK5/ARK4-zipparna,
  bara med en READ ME för PSP Go (`tools/make_release.py`).
- **Inställningar på PSP Go** (`src/settings.c`, modell 4): *Stäng av Pausa spel* (`disablepause`) och
  *Behåll det pausade spelet* (`hibblock`) visas, *Stäng av UMD-enheten* döljs – som i arkMenu.
- **Bygge i GitHub Actions** (`.github/workflows/build.yml`): båda versionerna och alla fyra zipparna.

## Version 1.3.3

- **Startskydd mot semi-brick** (`src/main.c`): `ms0:/PSP5/starting` skapas först av allt vid start och innehåller
  antalet starter i rad som inte blev klara. Filen tas bort när menyn har gått i 5 s och musiklistan är inläst,
  eller när ett spel startas eller något väljs i inställningarna.
  - 1 misslyckad start: **felsäker start** – ingen startskärm, ingen musik, ingen UMD-tråd, `ms0:/PSP5/cache` töms
    och en toast visas.
  - 2 i rad som ARK:s launcher: psp5 går direkt till XMB (`sys_exit_xmb`, launcherläget pausas) och skriver 1 i
    filen, så att nästa start som launcher blir felsäker i stället för XMB igen.
  - Pausat launcherläge slås på igen först när en start har blivit klar (`sys_resume_launcher`), inte direkt i `sys_init`.
- **Viloläge utan grafik:** när PSP:n somnar ritas två svarta bildrutor, sedan väntar psp5 med `sceKernelDelayThread`
  i stället för att rita hela tiden medan PSP:n somnar och vaknar. Ingen startskärm efter viloläge i felsäkert läge.
- `sctrlArkSetConfig` anropas bara med en konfiguration som gick att läsa (`IS_ARK_CONFIG`).
- HOME-menyn i spel: psp5 finns inte i minnet då (LoadExec startar om systemet) och startar spel med exakt samma
  anrop som arkMenu. Hjälptext om plugins och ARK:s kombination L + R + START + ↓ i INSTALL.md.

## Version 1.3.2

- **Ingen meny innan startskärmen efter viloläge:** "vaknat klart" (`PSP_POWER_CB_RESUME_COMPLETE`) kommer ungefär
  en sekund efter att skärmen tänts, och fram till dess syntes menyn. Nu ritas skärmen svart redan när PSP:n
  somnar (`PSP_POWER_CB_SUSPENDING`) och hålls svart tills den har vaknat klart, sedan startskärmen
  (`sleep_and_wake` i `src/main.c`, högst 4 s vaken utan signal). Musiken pausas när PSP:n somnar.
  Testskript: `tools/tests/resume.txt` (`sleep 1`, `resume 1`).

## Version 1.3.1

- **Ingen startskärm efter ett spel:** som ARK:s launcher startas psp5 om när ett spel avslutas. Före spelstart
  skapas `ms0:/PSP5/ingame`; finns filen när psp5 startar tas den bort och startskärmen hoppas över.
  Som vanlig app skapas filen inte (då går systemet till XMB efter spelet).
- **Startskärm efter viloläge:** en strömcallback (`scePowerRegisterCallback`, `PSP_POWER_CB_RESUME_COMPLETE`)
  visar startskärmen igen när PSP:n väcks med sidoknappen. Startljudet räknas fram på nytt i ljudtråden
  (`sound_prepare_boot`), musik pausas under tiden och korten glider in igen (`ui_replay_intro`).
  Testskript: `tools/tests/resume.txt` (`resume 1` låtsas att PSP:n vaknade).
- **UMD-skivan först under Spel** (`src/umd.c`): en tråd tittar efter skivan som arkMenu gör (sceUmdCheckMedium,
  sceUmdActivate, vänta på PSP_UMD_READY) och läser `disc0:/PSP_GAME/PARAM.SFO`. Skivan läggs in först i listan
  när den sätts i och tas bort när den tas ut, och markeringen ligger kvar på samma spel. Den sparas aldrig i listcachen.
  Start: apitype 0x120 med `disc0:/PSP_GAME/SYSDIR/EBOOT.BIN`, eller 0x160 med `ms0:/PSP/GAME/<DISC_ID>/PBOOT.PBP`
  om en uppdatering finns. I testbygget fungerar `ms0:/PSP5/fakeumd/` som skiva (testskript `tools/tests/umd.txt`).

## Version 1.3

- **ARK-4 och ARK-5, PSP-1000/2000/3000:** två byggen – `tools/build.sh` (ARK-5, länkar ArkCtrl) och
  `tools/build.sh ark4` (ARK-4, `-DPSP5_ARK4`, utan ArkCtrl → `dist-ark4/`). Båda känner av ARK-versionen
  (`sctrlSEGetVersion()`: 4/5) och modellen (`kuKernelGetModel()`: 0 = PSP-1000) vid start. På ARK-4 läses
  ARK-konfigurationen via en egen SystemCtrlForUser-import (`src/ark4stub.S`) och SETTINGS.TXT skrivs i
  ARK-4:s format (overclock/defaultclock/powersave, highmem/mscache på/av). PSP-1000 döljer extra minne och
  USB-laddning.
- **"ARK not found"-felet:** `sctrlHENGetVersion()` ger bara mellanversionen (0 på ARK 5.0.x) – nu används
  huvudversionen.
- **Musik:** fliken Musik spelar MP3 från `ms0:/MUSIC` (sceMp3, egen tråd, `src/music.c`), som XMB:s spelare.
- **Startskärm som PS5:** grå logga på svart → hälsovarning med mörkblå bokeh → gyllene damm → ljuskägla.
- Heapen lämnar 7 MB fritt (ARK:s RECOVERY.PRX behöver 4 MB).

## Startskärm och bibliotek (1.2.0)

- **Startskärm:** ljuspartiklar virvlar in mot mitten, möts i en blixt och lämnar psp5-loggan kvar medan ett
  eget ackord spelas. Allt ritas och räknas fram i `src/boot.c` / `src/sound.c`. Valfri knapp hoppar över;
  stängs av under Inställningar → psp5 → Startanimation (`bootanim=0` i `ms0:/PSP5/psp5.cfg`).
- **Bibliotek:** tredje fliken (L/R). Alla spel och appar i ett rutnät med 5 kolumner, ↑↓←→ flyttar,
  □ växlar sortering (namn / senast tillagda).

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
- **psp5:** autoboot, systemljud, startanimation, språk, sortering av Spel och Appar (namn / som i XMB –
  följer t.ex. Homebrew Sorter), dölj dubbletter, batteri (ikon / procent / tid kvar), spelförhandsvisning,
  rensa bildcache, version
- **Tema:** spelens bilder i bakgrunden, egen bakgrundsbild ur `PICTURE`, PS5-färgerna och PS1–PS4
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
- UMD-skivan i enheten: först under **Spel** och i **Bibliotek**

Spel startas med samma systemctrl-anrop som ARK-5:s arkMenu (`src/launch.c`), inklusive
installerade uppdateringar (`PBOOT.PBP`), patchade ISO:er (`EBOOT.OLD`) och okrypterad `BOOT.BIN`.
Före start frigörs allt: laddartråd, texturer, typsnitt, GU och spellistan.

## Bilder och cache
Kort- och bakgrundsbilder letas i den här ordningen:
1. `ms0:/PSP5/covers/<nyckel>.ico|.bg` – från `tools/covers.py` (nyckel = DISC_ID för ISO och UMD, mappnamn för EBOOT)
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
