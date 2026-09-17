# Super Hang-On MD Recomp

A recompilation of **Super Hang-On** for the Sega Mega Drive (Genesis) that
runs natively on Linux, Windows and macOS (Apple Silicon), and in browsers
through the WebAssembly build.

## Why the Mega Drive version and not the arcade one?

Because it was one of my favourite games as a kid, and it has the wonderful
**Original Mode**: you start on an undrivable scooter and, race after race,
beating your rivals lets you upgrade the parts of your bike, sign sponsors and
hire new mechanics. The only problem with the original game is that it runs at
a rock-solid unstable 30 fps, and the input lag isn't great either :D

So I wanted to give one of my favourite games the treatment it deserves.

## Features

- **60 fps** game logic and rendering — not interpolated frames: the game
  itself runs twice per original tick
- **120 fps** for 120 Hz screens (FRAME RATE in the settings menu): the switch
  takes effect at once, even in the middle of a race
- Reduced input lag
- 16:9 and 21:9 widescreen races
- CRT filters (scanlines, aperture grille, slot mask, shadow mask) with
  adjustable mask, glow, curvature, vignette, sharpness and brightness
- Save states (multiple slots)
- Rewind (up to 20 seconds)
- **Relax Mode**: no timer, no rival bikes, no score, just a free ride through
  every continent without interruptions, the music and the wind in your hair :)
- Rankings and records kept between sessions
- Settings menu: fullscreen or window, window size, render resolution,
  scaling (integer, fit, stretch), TV or square pixels, vsync, volume
- Frame rate counter
- Keyboard, game controller and browser touch support; controls are remappable in `controls.ini`
- **Analog steering**: with the left stick the bike leans as far as you push,
  instead of the all-or-nothing of the D-pad (the D-pad and the keys still
  steer fully)
- Original sound: the game's own Z80 sound driver with cycle-accurate YM2612
  and PSG emulation

## How to play

1. Download the release for your system and extract it.
2. Copy your own dump of the Mega Drive game into the same folder as the
   executable, named `baserom.md`. It must be the No-Intro
   *Super Hang-On (Japan, USA) (En,Ja)* dump:

   | | |
   |---|---|
   | SHA-1 | `ecfd7b3bf4dcbee472ddf2f9cdbe968a05b814e0` |
   | CRC32 | `cb2201a3` |
   | Size | 524,288 bytes |

3. Run `shangon` (Linux), `shangon.exe` (Windows) or `./shangon` from Terminal
   (macOS). On Linux SDL2 must be installed (Debian/Ubuntu:
   `sudo apt install libsdl2-2.0-0`); the Windows and macOS releases include it.

The ROM is not included and never will be. The release contains no game code
either: at the first start it is translated from your ROM and kept in
`shangon.cache` (`shangon120.cache` at 120 fps), so the first start takes a
moment longer. The cache,
settings, controls, save states and records are stored next to the
executable, so its folder must be writable.

### Browser build

The `superhangon-wasm.zip` release is the browser version. Extract it and
serve the directory over HTTP(S), for example:

```sh
python3 -m http.server 8000
```

Open `http://localhost:8000/shangon.html`, choose the same 524,288-byte ROM
in the page, and press **Choose ROM**. The browser stores that ROM, settings,
controls, save states and records in its own local storage; it is reused on
later visits. Browser gamepads, keyboard controls and the on-screen touch
buttons work without installing an app. WebGL2 is required.

## Controls

| Action | Keyboard | Xbox controller | PlayStation controller |
|---|---|---|---|
| Steer | Arrow keys | D-pad / left stick (analog) | D-pad / left stick (analog) |
| Accelerate (B) | X | A / right trigger | Cross / R2 |
| Brake (A) | Z | X / left trigger | Square / L2 |
| Turbo (C) | C | B | Circle |
| Start | Enter | Start | Options |
| Settings menu | Esc / F1 | Back | Share (Create) |
| Rewind (hold) | Backspace | Left shoulder | L1 |
| Save state | F5 | | |
| Load state | F8 | | |
| Previous / next slot | F6 / F7 | | |
| Fullscreen | F11 | | |
| Frame rate counter | F3 | | |

All bindings can be changed in `controls.ini`, created on the first run.

## Known issues

- I'm not happy with the CRT filters yet, but I'm working on them (I swear!).
- Something I haven't found yet, for sure.

## To do

- More responsive bike handling
- Sound in the demo mode
- In-game music selection

## Credits

- Sega, and in particular Yu Suzuki and his team, and Katsuhiro Hayashi,
  Shigeru Ohwada and Koichi Namiki for the wonderful soundtrack
- Nuked-OPN2 by nukeykt (YM2612 emulation, LGPL-2.1)
- floooh/chips by Andre Weissflog (Z80 emulation, zlib)
- SDL2
- Retrobigini (https://www.instagram.com/retrobigini) and pierinolartista
  (https://www.twitch.tv/pierinolartista) for beta testing

## License

The code of this project is released under the MIT license (see `LICENSE`).
Third-party code keeps its own license. This project contains no ROM data:
you need your own copy of the game.
