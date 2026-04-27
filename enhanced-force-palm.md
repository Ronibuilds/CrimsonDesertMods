# Enhanced Force Palm

**by Bambozu** — v1.1

Supercharges Force Palm into an instant vertical launcher. Double-tap jump while airborne to fire a powerful upward boost that gets you to flight altitude in a single input — no terrain hunting required.

---

## What It Does

The standard pre-flight setup in Crimson Desert is tedious: find height, double jump, Force Palm up three times, then finally deploy wings. This mod collapses all of that into one combo.

**Combo:** Jump → while airborne, quickly double-tap Jump within 400 ms → Force Palm fires a massive upward boost → deploy wings at the apex

Single tap still deploys wings as normal. The boost only fires on a deliberate double-tap, so there are no accidental triggers.

---

## How the Combo Works

1. **Jump** (tap X/Square or Spacebar)
2. **While airborne**, quickly **double-tap Jump again** within 400 ms
3. Force Palm triggers with a powerful upward velocity boost
4. You launch high — deploy wings and take flight

The boost sustains for a configurable window (default 180 ms) to give you a clean, powerful launch. The mod deactivates once you're in flight, so it can't accidentally fire mid-air.

> **Note:** The double jump skill in-game may be required for this to work correctly.

---

## Controls

| Action | Xbox | PS5 | Keyboard |
|--------|------|-----|----------|
| Force Palm Boost (double-tap) | X | Square | Spacebar |

**Controller support:**
- **Xbox:** plug and play (XInput)
- **PS5 DualSense:** native HID via USB or Bluetooth — no DS4Windows needed
- **PS4:** works via DS4Windows (XInput emulation)

---

## Configuration

File: `ForcePalmBoostByBambozu.ini` — place in `Crimson Desert\bin64\`

Changes apply live within ~1 second. No restart needed.

```ini
[ForcePalm]
Enabled=true

; Upward velocity applied during the boost window
; Default: 28.0 — increase for higher launches (try 35.0–40.0)
BoostValue=28.0

; How long (ms) the boost velocity is sustained after the second press
; Default: 180ms — increase if combo timing feels tight (try 250)
WindowMs=180

; Controller button code — Xbox: 16384 = X, PS5: 16384 = Square
Button=16384

; Keyboard virtual key code — 0x20 = Spacebar
Key=0x20
```

### Settings explained

| Setting | Description |
|---------|-------------|
| `BoostValue` | Upward velocity applied (higher = you go higher) |
| `WindowMs` | Duration the boost is active after the second press |
| `Button` | XInput button code |
| `Key` | Windows virtual key code (hex); set to `0` to disable keyboard |

---

## Tuning Tips

- **Boost too weak?** Increase `BoostValue` — try 35.0 or 40.0
- **Boost too strong / launching into orbit?** Decrease `BoostValue` — try 20.0 or 24.0
- **Missing the combo?** Increase `WindowMs` — try 250 or 300
- **Want tighter timing?** Decrease `WindowMs` — try 120 or 150

---

## Installation

### Option A — CDUMM (recommended)

1. Place in `Crimson Desert\bin64\`:
   - `ForcePalmBoostByBambozu.asi`
   - `ForcePalmBoostByBambozu.ini`
2. Refresh CDUMM.

### Option B — Ultimate ASI Loader

1. Place in `Crimson Desert\bin64\`:
   - `ForcePalmBoostByBambozu.asi`
   - `ForcePalmBoostByBambozu.ini`
   - `version.dll` (Ultimate ASI Loader)
2. Launch the game.

---

## Recommended Setup

Use alongside **Enhanced Flight** for the complete experience:

1. Enhanced Force Palm launches you to flight altitude (this mod)
2. Enhanced Flight gives you full directional control once airborne
3. Together: instant vertical launch + complete flight control

Both mods coordinate through `shared_player_base.h` — they work seamlessly together with no conflicts.

---

## Compatibility

- Works with Enhanced Flight (coordinates via `shared_player_base.h`)
- Works with other ASI and JSON mods
- Fully reversible: remove the `.asi` and `.ini` to restore vanilla behavior
- Pattern scanning safely disables the mod if signatures change after a game update

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Boost not triggering | Check `ForcePalmBoostByBambozu.log`; ensure Force Palm is unlocked in-game |
| Combo timing off | Increase `WindowMs` (try 250 ms) |
| Accidental triggers | `WindowMs` too high or double-tap window too wide — reduce to 150 ms |
| Boost too weak | Increase `BoostValue` (try 35.0–40.0) |
| Nothing happens | Wait ~15 seconds after game load; double jump skill may be required |

---

## Changelog

### v1.1
- Changed: Now requires double-tap (not single press) to fire — prevents accidental triggers when deploying wings
- Changed: Default binding updated to X/Square and Spacebar
- Added: Full DualSense button mapping — all PS5 buttons bindable via INI
- Improved: Shared memory coordination with Enhanced Flight via `shared_player_base.h` — seamless alongside Enhanced Flight

### v1.0
- Initial release: Force Palm vertical launch boost with configurable velocity and window
