# Enhanced Flight & Mobility Mods for Crimson Desert

A collection of ASI mods for Crimson Desert that overhaul aerial mobility and combat. Each mod is independent and fully compatible with the others.

---

## Mods

| Mod | Description |
|-----|-------------|
| [Enhanced Flight](docs/enhanced-flight.md) | Turns gliding into true flight — ascend, descend, horizontal boost, aerial roll burst |
| [Enhanced Force Palm](docs/enhanced-force-palm.md) | Double-tap jump to launch vertically with Force Palm |
| [Super Axiom Force](docs/super-axiom-force.md) | Extended range, straight pull, and instant activation for Axiom Force |

Use all three together for complete aerial and combat mobility control.

---

## Requirements

- **Crimson Desert** (Steam)
- **ASI Loader** — [CDUMM](https://www.nexusmods.com/crimsondesert) (recommended) or [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- Xbox or PS5 DualSense controller, or keyboard

---

## Quick Installation

1. Download the `.asi` and `.ini` files for the mods you want from [Releases](https://github.com/Ronibuilds/CrimsonDesertMods/releases).
2. Place them in your Crimson Desert `bin64` folder:
   ```
   Crimson Desert\bin64\EnhancedFlight.asi
   Crimson Desert\bin64\EnhancedFlight.ini
   ```
3. Launch via CDUMM, or ensure Ultimate ASI Loader (`version.dll`) is in `bin64`.
4. Start the game — mods initialize ~15 seconds after launch.

Each mod has its own `.ini` for live configuration. Changes apply within ~1 second with no restart needed.

> **Tip:** Pair with an infinite stamina mod for uninterrupted flight.

---

## Mod Compatibility

- **Enhanced Flight** and **Enhanced Force Palm** coordinate via a shared memory interface (`shared_player_base.h`) — they can run simultaneously with no conflicts
- **Super Axiom Force** is completely independent — no shared state, no hooks in common with the other two
- All mods use pattern scanning and safely disable themselves if signatures break after a game update

---

## Building from Source

The source files are provided so you can compile them in your own C++ project. Set up a DLL project targeting x64, include the relevant `.cpp` and `.h` files, and link against:
- Windows SDK: `xinput.lib`, `hid.lib`, `setupapi.lib`, `psapi.lib`

Rename the output `.dll` to `.asi` and place it in `bin64` alongside the corresponding `.ini`.

---

## Troubleshooting

Each mod writes a log to `bin64` (e.g. `EnhancedFlight.log`). Check the log first.

| Symptom | Fix |
|---------|-----|
| Mod not activating | Wait ~15 seconds after game load before testing |
| Controller not detected | PS5 DualSense works natively; Xbox requires XInput drivers |
| "Pattern not found" in log | Game updated and signatures changed — mod update needed |
| Flight jitter or hard stops | Lower boost values or increase `RampDownMs` in the INI |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

Source released for community reference and contribution. See [LICENSE](LICENSE) for details.
