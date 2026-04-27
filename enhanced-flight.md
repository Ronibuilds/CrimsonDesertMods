# Enhanced Flight

**by Bambozu** — v3.0

Turns gliding into true flight. Ascend, descend, boost horizontally, and launch with aerial roll bursts — all with smooth acceleration and full controller/keyboard support.

---

## What It Does

Enhanced Flight hooks into Crimson Desert's physics integration to inject velocity during wings-deployed flight. It activates only when you're genuinely flying — it won't trigger during jumps, combat hops, or backsteps.

### Ascend (hold)
Ramps vertical velocity up to a configurable value so you climb instead of sinking. Smoothly accelerates and decelerates.

**Default:** RB (Xbox) / R1 (PS5) / Caps Lock (PC)

### Descend (hold)
Ramps vertical velocity downward for controlled altitude loss.

**Default:** RT (Xbox) / R2 (PS5) / Ctrl (PC)

### Horizontal Boost (hold or toggle)
Multiplies horizontal movement by a configurable factor. Ramps up smoothly and coasts down naturally on release.

**Default:** A (Xbox) / Cross (PS5) / Left Shift (PC)

### Aerial Roll Boost (press)
A burst of speed that ramps up, holds at max for a configurable duration, then ramps back down. Works great as a combo finisher in Focus mode.

> **Focused Aerial Roll combo:** Enter Focus mode → use Focused Aerial Roll (B/Circle) → immediately press B/Circle again for a massive burst with the full animation. Default `BoostValue=3.0` is tuned for this — raise to 6–10 if you're not using the combo.

**Default:** B (Xbox) / Circle (PS5) / Left Alt (PC)

---

## Controls

| Action | Xbox | PS5 | Keyboard |
|--------|------|-----|----------|
| Ascend (hold) | RB | R1 | Caps Lock |
| Descend (hold) | RT | R2 | Ctrl |
| Horizontal Boost | A | Cross | Left Shift |
| Aerial Roll Boost | B | Circle | Left Alt |

**Controller support:**
- **Xbox:** plug and play (XInput, all 4 slots scanned)
- **PS5 DualSense:** native HID via USB or Bluetooth — no DS4Windows needed
- **PS4:** works via DS4Windows (XInput emulation)
- Triggers (LT/RT) are analog — bind using `0x0800` (LT) or `0x1000` (RT)

---

## Configuration

File: `EnhancedFlight.ini` — place in `Crimson Desert\bin64\`

Changes apply live within ~1 second. No restart needed.

```ini
[Ascend]
Enabled=true
BoostValue=12.0
Button=512        ; RB/R1
Key=0x14          ; Caps Lock
RampUpMs=300
RampDownMs=600

[Descend]
Enabled=true
BoostValue=-12.0
Button=0x1000     ; RT/R2
Key=0x11          ; Ctrl
RampUpMs=300
RampDownMs=600

[Horizontal]
Enabled=true
UseToggle=false
BoostValue=4.0    ; Max speed multiplier
Button=4096       ; A/Cross
Key=0xA0          ; Left Shift
RampUpMs=1000
RampDownMs=2500

[AerialRoll]
Enabled=true
BoostValue=3.0    ; Raise to 6–10 if not using Focused Aerial Roll
Duration=3.0      ; Seconds at max boost
Button=8192       ; B/Circle
Key=0xA4          ; Left Alt
RampUpMs=400
RampDownMs=2500
```

### Key settings explained

| Setting | Description |
|---------|-------------|
| `BoostValue` | Target speed (multiplier for Horizontal/AerialRoll, m/s for Ascend/Descend) |
| `RampUpMs` | Milliseconds to accelerate from baseline to `BoostValue` |
| `RampDownMs` | Milliseconds to decelerate back to baseline on release |
| `UseToggle` | `true` = toggle on/off; `false` = hold (Horizontal only) |
| `Button` | XInput button code (see table below) |
| `Key` | Windows virtual key code (hex) |

### Button codes

| Button | Code |
|--------|------|
| A / Cross | 4096 |
| B / Circle | 8192 |
| X / Square | 16384 |
| Y / Triangle | 32768 |
| RB / R1 | 512 |
| LB / L1 | 256 |
| RT / R2 (trigger) | 0x1000 |
| LT / L2 (trigger) | 0x0800 |

### Variant INIs

Pre-configured INI files are included in `releases/v3.0/`:

- `AerialOnly` — Aerial Roll Boost only
- `VerticalOnly` — Ascend/Descend only
- `HorizontalOnly` — Horizontal Boost only
- `AllEnabled` — everything on (default)

Copy your preferred variant to `bin64` as `EnhancedFlight.ini`.

---

## Tuning Tips

- **Hard stops mid-flight?** Lower `AerialRoll.BoostValue` and/or `Duration`. The engine has a speed cap that causes abrupt stops at very high values.
- **Jittery flight?** Increase `RampDownMs` (try 2000–2500 ms).
- **Too sluggish?** Decrease `RampUpMs` (try 200–400 ms).
- **`BoostValue` = how fast. `RampUpMs`/`RampDownMs` = how quickly you get there.**

---

## Installation

### Option A — CDUMM (recommended)

1. Place in `Crimson Desert\bin64\`:
   - `EnhancedFlight.asi`
   - `EnhancedFlight.ini`
2. Refresh CDUMM — it detects `.asi` plugins directly.

### Option B — Ultimate ASI Loader

1. Place in `Crimson Desert\bin64\`:
   - `EnhancedFlight.asi`
   - `EnhancedFlight.ini`
   - `version.dll` (Ultimate ASI Loader)
2. Launch the game.

### Recommended

Use alongside an **infinite stamina mod** — without it, flight is still limited by stamina drain.

---

## Compatibility

- Works with other ASI mods and JSON mods
- Fully reversible: remove the `.asi` and `.ini` to restore vanilla behavior
- Coordinates with Enhanced Force Palm via `shared_player_base.h` — both can run simultaneously with no conflicts
- Pattern scanning safely disables the mod if game signatures change after an update

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Nothing happens | Check `EnhancedFlight.log` in `bin64`; wait ~15 seconds after game load |
| Controller not detected | Update XInput drivers; PS5 DualSense connects natively |
| RT/LT not working | Use `0x1000` for RT or `0x0800` for LT in the INI |
| INI changes not applying | Wait ~1 second after saving |
| Flight triggers on the ground | Check log for false-positive detection; adjust `g_flightTimeThreshold` |

---

## Changelog

### v3.0
- Fixed: Fast-fall dive input bug — boosts now correctly gated during dives
- Changed: INI cleaned up; only active features present
- Added: Variant INI presets (AerialOnly, VerticalOnly, HorizontalOnly, AllEnabled)

### v2.5
- Improved: Smoother behavior under stricter engine asset streaming after recent patches
- Changed: Default speed values lowered for reliability (Horizontal 4×, Ascend/Descend 12, Aerial Roll 4.5×)
- Added: Smooth ramp-up/ramp-down for Ascend/Descend
- Added: Focused Aerial Roll combo support

### v2.4
- Fixed: Horizontal boost ramp cycling (now holds at max while button is held)
- Improved: Full compatibility with Enhanced Force Palm — no hook conflicts

### v2.3
- Added: Shared memory coordination with Enhanced Force Palm — both mods can run simultaneously with no conflicts

### v2.2
- Added: Smooth acceleration/deceleration for all boosts
- Added: Configurable `RampUpMs`/`RampDownMs` per mechanic
- Added: Native LT support for Ascend/Descend binding
- Added: Multi-slot Xbox controller detection (all 4 XInput slots)
- Fixed: RT/LT trigger binding
- Fixed: Horizontal boost ramp-down

### v2.1
- Added: Native PS5 DualSense support (USB and Bluetooth)
- Improved: Controller hot-plugging and auto-detection

### v2.0
- Added: Aerial Roll Boost (recreates pre-1.02.00 Focus mode boost tech)
- Added: Dedicated Descend boost
- Changed: Horizontal defaults to HOLD mode
- Improved: Flight detection — far fewer false positives on ground/combat
- Fixed: Ground movement receiving speed boosts

---

## Credits