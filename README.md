# MINI ISAAC (TI-83 Premium CE)

Action roguelike inspired by *The Binding of Isaac*, written in C for the
TI-83 Premium CE eZ80 with the CEdev toolchain. Unofficial fan-game: all code
and graphics (original home-made pixel art, 16x16) are original.

## Features

- Random floors, rooms, secret rooms, shop (coins), treasure rooms (keys)
- Bosses (Monstro, Mom), varied enemies and champions (tough recolors)
- 3+ playable unlockable characters: Isaac, Magda, Cain, Lazarus (revives)
- 24 passive items: damage, range, piercing tears, homing, poison...
- Rechargeable active TELE item, devil deal on the first floors
- Floor curses (darkness, haste), silver coins (5), soul hearts
- 3 save slots (appvars), resume an in-progress run
- Built-in self-test: [Yequ] on the title screen

## Controls

- D-pad: move
- 2 / 4 / 6 / 8: shoot in that direction
- [MODE]: floor map, then stats page (HP, DMG, SPD, RATE, items)
- [ENTER]: resume the run from the map/stats

## Build

CEdev toolchain required (convimg + make). Graphics are generated from
`tools/sprites_art.py`:

    cd tools && python -X utf8 make_gfx.py
    cd ../src/gfx && convimg.exe
    make          # -> bin/ISAAC.8xp

Transfer `bin/ISAAC.8xp` to the calculator, then launch ISAAC. Saves
(appvars `ISAACSV1..3`) are archived to flash on every save: they survive
RAM resets and program version changes (v1.0 -> v1.1 -> v1.2 ...). Do not
delete these appvars by hand — that is the only thing that breaks a save.
