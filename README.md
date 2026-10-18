# OMORI for PSP

An unofficial PSP port of OMORI (version 1.0.8), with an engine written from scratch.

This repository contains **no game content**. You bring your own copy of the game, and the builder turns it into a
single `EBOOT.PBP` for a PSP or PPSSPP. Every image, sound and line of dialogue comes from your copy. Please
[buy the game on Steam](https://store.steampowered.com/app/1150690/OMORI/); the port is useless without it.

The port has been tested in PPSSPP at 333 MHz: every map loads, every enemy group's battle starts, and the
minigames play through. It hasn't been tried on a real PSP yet, so reports from hardware are welcome.

## What you need

- OMORI version 1.0.8 from Steam. The builder reads the version from the game's title screen and refuses other
  versions.
- A PSP-2000, PSP-3000, PSP Go or PSP Street (E1000) with custom firmware, or PPSSPP. The EBOOT asks for the 64 MB
  memory mode and the engine keeps a 16 MB image cache on the PSP, so the PSP-1000 (32 MB) can't run it.
- About 2 GB free on the memory stick. Music and sounds are copied over as they are, so the EBOOT is big.

## How to use the builder

**In the browser:** build the page with `make web` and serve `build/web` (`make web-serve` does both and serves it
on http://localhost:8080). Drop in your game folder (or a .zip of it), press Build, and download the result. Your
game files never leave your computer. The page needs a current version of Chrome, Edge, Firefox or Safari, because
the builder uses WebAssembly exception handling. The browser keeps a copy of the game and writes the EBOOT to its
own storage, so it needs about 4 GB of free disk space. The movies are converted on all of your processor's cores at
once, and the whole build takes about four minutes on a decent computer.

The page itself ships none of OMORI's files. Once you give it your copy, it dresses itself up with the title screen,
a few White Space sprites, sounds, the game's font and a few lines of dialogue, all read from that copy inside your
browser. Nothing is uploaded, and nothing from the game is stored in this repository or served with the page.

**On the desktop:** run the builder without arguments (or double-click it). It looks for OMORI in your Steam
library, asks for the folder if it can't find it, and then asks where to save. From a terminal you can also pass
the folder yourself:

```
omori-builder path/to/OMORI -o out
```

Pick the folder that has `OMORI.exe`, `Launch_OMORI.bat` and `www` in it (on Steam: right-click the game, then
Manage, then Browse local files). The game's files are encrypted, and the key comes from `Launch_OMORI.bat` or
Steam's launch options. If it can't be found, pass it with `--key`. `omori-builder --help` lists every option.

Either way you end up with `PSP/GAME/OMORI/EBOOT.PBP`. Copy the `PSP` folder to the root of your memory stick, or
open the EBOOT in PPSSPP. Saves go into `PSP/GAME/OMORI/saves` and are kept when you update.

## Controls

The buttons follow the game's own gamepad layout.

| PSP | Action |
|---|---|
| D-pad | Move, select |
| Cross | Confirm, interact, advance text |
| Circle / Start | Cancel, open the menu |
| Triangle | Tag (change the party leader) |
| Square (hold) | Run |
| L / R | Page up / down in lists, and the map shortcuts the game gives the Q and W keys |
| Select | The game's tab action |

The analog stick isn't used.

## Differences from the PC version

Steam achievements are not available.

The window-shake effect, which moves the game window on PC, has no PSP equivalent.

Keyboard-specific options (key rebinding and the choice between keyboard and gamepad prompts) and the screen
resolution and fullscreen options are not available.

The game is drawn at 360x270 in the middle of the PSP screen, with thin black borders, and the builder scales the
artwork down to match. Movies are converted to a smaller format and look softer than on PC.

A sound or song that hasn't played yet in the session is decoded in the background, so it can start a moment late
the first time.

## How it works

- `engine/` is a C++ port of the RPG Maker MV runtime (maps, events, interpreter, pictures, screen effects) plus
  re-implementations of the plugins and custom systems OMORI uses, and a JavaScript interpreter for the
  game's scripts. It runs on the PSP (GU, sceAudio) and on the desktop (SDL2 with a software
  renderer that matches the PSP output, used for testing).
- `tools/builder/` is the builder. It decrypts your game files, scales the 640x480 artwork to 360x270 for the
  480x272 screen, packs maps, events and dialogue, converts the WebM movies, renders the game's fonts and stores it
  all inside the `EBOOT.PBP` next to the prebuilt engine. It refuses other game versions and warns when a copy
  looks cracked.
- `web/` is the in-browser builder: the same code compiled to WebAssembly.

## Building from source

You need `make`, `cmake`, a C and C++ compiler and `curl`. The PSP toolchain, zig (for `make release`), wasi-sdk
and the tailwindcss cli (both for `make web`) are downloaded into `_deps/` on first use.

```
make toolchain        # download the pspdev toolchain
make engine           # build the PSP engine into build/psp/EBOOT.PBP
make builder          # build the builder for this machine (embeds the engine)
make run              # run the builder (GAME=path/to/game and OUT=folder are optional)
make test             # run the builder unit tests
make desktop          # desktop test build (needs SDL2, libvorbis, zlib); make play runs it
make web              # build the in-browser builder into build/web
make release          # cross-compile the builder for Linux, Windows and macOS
make help             # every target
```

Plain `make` builds the engine and the builder.

## License

All rights reserved; this is not open source. You may build and run it for your own personal use, but you may not
modify, redistribute or publish it or builds of it. That's only so the port can't be used to pirate the game or to
pass around malware under its name. See [LICENSE](LICENSE), which also lists the third-party parts and their own
licenses: miniz and fflate (MIT), stb (MIT or public domain), libvpx and Opus (BSD), minimp3 (CC0), the LLVM
compiler-rt soft-float routines (Apache 2.0 with LLVM exceptions), the Noto
fallback fonts (SIL Open Font License, see `tools/builder/third_party/fallback_fonts/OFL.txt`) and the RPG Maker
MV corescript (MIT).

OMORI and everything in it (characters, artwork, music, sounds, fonts and writing) is (c) OMOCAT, LLC, all rights
reserved. This project is not affiliated with or endorsed by OMOCAT.
