# Sky Circuit

Local 4-player couch multiplayer board game for PlayStation 3 (firmware 4.92 + HEN).

Built with **PSL1GHT** and **Tiny3D**.

## Features

- Up to 4 players (any mix of humans + CPUs)
- 3 CPU difficulty levels (Easy / Normal / Hard)
- 2 distinct boards: **Visual Circuit** (looping track) and **Geo Transit** (branching map)
- Selectable match length: 10 / 15 / 20 / 30 rounds
- Stars bought with coins; star relocates every full round
- Shops, positive/negative coin spaces, event spaces, hidden blocks
- Dice roll with expressive token bounce on high rolls
- RNG-based contest substitute (no full minigames in this version)
- Automatic dual logging to `/dev_hdd0/tmp/skycircuit.log` **and** first available USB (`/dev_usb000` … `/dev_usb002`) for crash / hang / black-screen diagnosis

## Build (GitHub Actions)

Push to `main` or run the workflow manually. The pipeline:

1. Restores (or builds & caches) the full PS3 toolchain + PSL1GHT
2. Builds Tiny3D
3. Compiles the ELF / SELF / PKG

Artifact: `sky-circuit-ps3` containing the `.pkg`.

## Local build

Requires a working `ps3dev` / `PSL1GHT` environment and Tiny3D installed into `$PORTLIBS`.

```bash
export PS3DEV=... PSL1GHT=...
make
```

## Controls (DualShock 3)

| Button   | Action                          |
|----------|---------------------------------|
| Cross    | Confirm / Roll dice             |
| Circle   | Cycle options (players / board) |
| Start    | (reserved)                      |

## Logging

On every boot the logger opens both:

- `/dev_hdd0/tmp/skycircuit.log`
- `/dev_usb000/skycircuit.log` (falls back to 001 / 002)

Every major state transition, dice roll, land, shop action, star move and simulated contest is written with timestamp + function + line. Useful when the console freezes or shows a black screen.

## License

Free homebrew – no warranty. Enjoy the contest!
