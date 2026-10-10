# psp5

**Made by Mille Weber**

I love the PSP, but going back to the XMB after using a PS5 always feels a bit dated. So I built
psp5: a launcher for the PSP that tries to recreate the look and feel of the **PS5 user interface** –
the row of game tiles at the top, the big artwork filling the screen behind them, the clean dark settings
pages – on a 480×272 screen from 2004.

psp5 runs on PSPs with the **ARK-5** custom firmware. You can start it like any other homebrew, or let it
take over as ARK's own launcher so your PSP boots straight into it.

Everything you see is drawn by psp5 itself. No logos, icons, sounds or other assets from Sony are used –
it's my own take on that style.

| Settings | Performance |
|---|---|
| ![Settings](docs/screenshots/settings.png) | ![Performance settings](docs/screenshots/settings-performance.png) |
| ![System settings](docs/screenshots/settings-system.png) | ![Power and USB](docs/screenshots/settings-power.png) |
| ![Startup screen](docs/screenshots/boot.png) |                                                         |

## What it does

psp5 starts with its own startup screen: points of light swirl in toward the middle, meet in a flash and leave
the psp5 logo behind while a soft chord plays (any button skips it, and you can turn it off in settings).
Everything is drawn and synthesized in code – no borrowed logos or sounds.

Then you get your games in a row along the top. The selected one is large with a thin light
frame around it, and its artwork fades in behind everything. Your PSP games (ISO, CSO and ZSO) live under
**Games** together with PSN and PS1 titles, and homebrew sits under **Apps** – flip between them with L and R.
Next to them is the **Library**: everything you have, games and apps together, in a grid that's easy to scan
through. Press □ there to sort by name or by what you added most recently.

Pressing ✕ (or START) starts the game the same way ARK-5's own menu does, so updates, patched ISOs and
PS1 games all work. Before a game launches psp5 shuts itself down completely and frees all of its memory,
so the game gets the whole PSP.

Press △ for the settings, laid out like the PS5's settings pages. From there you can change ARK's CFW
options (CPU speed, extra memory, caches, USB charging and so on), turn plugins on and off, connect over
USB, restart, shut down, jump to the XMB or open ARK's recovery menu.

A few other things I cared about:

- **It's quick.** Only the tiles near your selection are loaded, in the background, and all artwork is cached
  as small ready-made textures, so after the first start it opens almost instantly.
- **It has sounds.** Soft ticks when you move around, a swipe between tabs and a little chord when a game
  starts. I synthesize them in code, so there are no audio files – you can listen to them in
  [`docs/sounds/`](docs/sounds), or turn them off in settings.
- **It speaks your language.** psp5 follows the PSP's system language: English, German, French, Spanish,
  Italian, Dutch, Portuguese, Russian and Japanese. Swedish is there too – the PSP itself doesn't offer
  Swedish, so you pick it under *Settings → psp5 → Language*.
- **It never touches flash0**, and you can always get back to the normal XMB.

## Installing

Everything you need to get psp5 onto your PSP – as a normal app or as ARK's launcher – is in
**[INSTALL.md](INSTALL.md)**. The download is on the [Releases](../../releases) page.

## Buttons

| Button | What it does |
|---|---|
| ← → or the analog stick | move between games (hold to go faster) |
| ↑ ↓ | move between rows in the Library |
| L / R | switch between Games, Apps and Library |
| □ | sort the Library (name / recently added) |
| ✕ or START | start the selected game |
| △ | settings |
| any button | skip the startup screen |
| ○ | back |
| HOME | quit to the XMB |

If something ever goes wrong, hold **SELECT** while the PSP boots – ARK then skips its settings and you land
in the normal XMB.

## Covers

psp5 uses each game's own icon and background automatically. If you'd like your own covers, there's a small
tool for your computer that prepares them – see the covers section in [INSTALL.md](INSTALL.md).

## Status

I've been using psp5 on my own **PSP-2000 with ARK-5**, and launching games, artwork and covers all work
there. The settings screen, USB mode and the language support are newer, so let me know if something
behaves oddly. Other PSP models should be fine but I haven't been able to try them. The PSP Go and PS Vita
aren't supported.

## Building it yourself

psp5 is written in C and built with the [pspdev](https://github.com/pspdev/pspdev) toolchain plus
[psp-cfw-sdk](https://github.com/pspdev/psp-cfw-sdk), which provides ARK's headers and libraries. With pspdev
installed it's just:

```sh
make
```

On Windows I build it inside WSL – `tools/setup-wsl.sh` installs the toolchain and `tools/build.sh` builds
`dist/PSP/GAME/PSP5/EBOOT.PBP`.

## Thanks

- The [ARK-5](https://github.com/PSP-Arkfive) team – psp5 starts games and stores settings the same way
  their arkMenu does, and uses ARK's libraries through psp-cfw-sdk.
- [pspdev](https://github.com/pspdev) for the toolchain, PSPSDK and packages.
- intraFont, libpng, zlib, LZ4 and libcisoread.

## License

GPL-3.0 – see [LICENSE](LICENSE).

psp5 is an unofficial fan project by Mille Weber. It is not affiliated with or endorsed by Sony Interactive
Entertainment. "PSP", "PS5" and "PlayStation" are trademarks of Sony Interactive Entertainment Inc.
