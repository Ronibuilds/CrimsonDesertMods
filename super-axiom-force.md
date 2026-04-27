# Super Axiom Force

**by Bambozu** — v1.2

Removes the short-range limitation of the Axiom Force ability. Grab enemies, objects, and buildings from across a courtyard, pull them straight toward you, and activate instantly — no more wasted animation time.

---

## What It Does

Vanilla Axiom Force is a short-range skill. This mod writes new values directly to the game's data region to expand its targeting reach and improve its behavior — no hooks, no code patching for the range feature.

### Extended Range
Greatly increases how far the targeting reticle can reach and how far away a target can be when the grab connects. Both are controlled by a single `Range` setting.

**Default:** 500 (vanilla is roughly 20)

> Note: There appears to be an in-engine cap somewhere around 95 units of effective reach. Values beyond that still improve feel but don't scale proportionally.

### Pull Speed (PullNorm)
Controls how fast grabbed targets travel toward you after the grab connects. Keep it roughly 2× `Range` for a natural feel.

**Default:** 200

### Straight Pull *(v2.0+)*
Patches the aerial pull trajectory to travel in a direct line instead of the default curved arc. Useful for precise combat positioning and traversal.

Enable via `StraightPull=true` in the INI.

### Instant Axiom *(v2.0+)*
Removes the activation delay by patching the conditional jump that checks ability duration. Both the Reel and Propell variants are patched for truly instant activation.

Enable via `InstantAxiom=true` in the INI.

---

## Configuration

File: `SuperAxiomForce.ini` — place in `Crimson Desert\bin64\`

Changes apply live within ~1 second. No restart needed.

```ini
[AxiomForce]

; Enable or disable the mod entirely
Enabled=true

; Targeting and grab range
; Vanilla is ~20. Effective cap is around 95, but higher values still feel better.
Range=500

; Pull speed after grab connects
; Recommended: roughly 2× Range for natural-feeling pulls
PullNorm=200

; Patch aerial pull to travel straight instead of curved (true/false)
StraightPull=true

; Remove activation delay for instant use (true/false)
InstantAxiom=true
```

### Settings explained

| Setting | Description |
|---------|-------------|
| `Range` | How far the targeting reticle reaches and how far away targets can be grabbed |
| `PullNorm` | Velocity of pulled targets after grab connects |
| `StraightPull` | `true` patches a CALL to NOPs for straight-line aerial pulls |
| `InstantAxiom` | `true` patches conditional jumps to skip duration checks |

Setting `Enabled=false` restores the vanilla range immediately.

---

## Installation

1. Place in `Crimson Desert\bin64\`:
   - `SuperAxiomForce.asi`
   - `SuperAxiomForce.ini`
2. Requires an ASI loader — **CDUMM** or **Ultimate ASI Loader** both work.
3. The mod activates ~15 seconds after game launch.

---

## Compatibility

- **No hooks on the range feature** — writes directly to a data address, cannot conflict with any other ASI mod
- `StraightPull` and `InstantAxiom` use simple byte patches; the mod detects if those bytes are already modified and won't double-patch
- Compatible with Enhanced Flight and Enhanced Force Palm
- Should be compatible with all other mods — no other known mods touch Axiom Force

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Range not changing | Check `SuperAxiomForce.log` in `bin64`; ensure the mod loaded after ~15 seconds |
| Straight pull not working | Game update may have shifted the patch offset — check the log for "pattern not found" |
| Instant Axiom not working | Same as above; check log |
| Want to restore vanilla | Set `Enabled=false` in the INI — applies within 1 second |

---

## Changelog

### v1.2
- Added: `PullNorm` setting — controls pullback speed of regular Axiom Force

### v2.0 (feature update)
- Added: `StraightPull` — patches aerial pull to travel in a straight line
- Added: `InstantAxiom` — removes activation delay via conditional jump patches

### v1.0
- Initial release: configurable `Range` with live INI reload
